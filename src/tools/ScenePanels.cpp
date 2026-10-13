#include "ScenePanels.h"
#include "Tools.h"
#include "scene/SceneDocument.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Renderer.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {
namespace {
constexpr const char* EntityPayload = "CRIMSON_SCENE_ENTITIES";
enum class Edit { None, Name, InlineName,
    Matrix00, Matrix01, Matrix02, Matrix03, Matrix10, Matrix11, Matrix12, Matrix13, Matrix20, Matrix21, Matrix22, Matrix23,
    DirectionEnabled, DirectionColor, DirectionIntensity,
    PointEnabled, PointColor, PointIntensity, Exposure, Environment };
enum class Panel { None, Outliner, Inspector, Settings };
Panel owner(Edit edit) {
    if (edit==Edit::None) return Panel::None;
    if (edit==Edit::InlineName) return Panel::Outliner;
    if (edit==Edit::Name || (edit>=Edit::Matrix00 && edit<=Edit::Matrix23)) return Panel::Inspector;
    return Panel::Settings;
}
struct PanelsState {
    SceneDocument* document{};
    std::vector<EntityId> visible;
    std::vector<EntityId> lastSelection;
    std::vector<EntityId> dragged;
    std::unordered_set<EntityId> expand;
    EntityId anchor{}, nameId{}, renameId{}, pendingClick{}, lastPrimary{};
    bool renameFocus{}, pendingCtrl{}, pendingShift{};
    std::array<char,1025> name{};
    std::array<char,1025> rename{};
    Edit active{Edit::None};
};
PanelsState& stateFor(SceneDocument& document) {
    static PanelsState state;
    if (state.document!=&document) { state={}; state.document=&document; }
    if (!document.transactionActive()) {
        // An editor-level cancel/open can end our transaction before this panel
        // runs. Never revive the buffered inline rename after that boundary.
        if (state.active==Edit::InlineName) { state.renameId=NoEntity; state.renameFocus=false; }
        state.active=Edit::None;
    }
    if (state.anchor && !document.scene().contains(state.anchor)) state.anchor=NoEntity;
    if (state.renameId && !document.scene().contains(state.renameId)) { state.renameId=NoEntity; state.renameFocus=false; }
    return state;
}
bool selected(const SceneDocument& document, EntityId id) {
    const auto& ids=document.selection().ids;
    return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
const char* kind(const EntityRecord& record) {
    if (record.model) return "Model";
    if (record.directional) return "Directional light";
    if (record.point) return "Point light";
    return "Empty";
}
void selectRow(SceneDocument& document, PanelsState& state, EntityId id, bool toggle, bool range) {
    auto ids=document.selection().ids;
    if (range && state.anchor) {
        const auto first=std::find(state.visible.begin(),state.visible.end(),state.anchor);
        const auto last=std::find(state.visible.begin(),state.visible.end(),id);
        if (first!=state.visible.end() && last!=state.visible.end()) {
            if (!toggle) ids.clear();
            const auto low=std::min(first,last), high=std::max(first,last);
            for (auto it=low;it<=high;++it) if (std::find(ids.begin(),ids.end(),*it)==ids.end()) ids.push_back(*it);
            document.select(std::move(ids),id); return;
        }
    }
    if (toggle) {
        const auto found=std::find(ids.begin(),ids.end(),id);
        if (found==ids.end()) ids.push_back(id); else ids.erase(found);
        document.select(std::move(ids),id);
    } else document.select({id},id);
    state.anchor=id;
}
void addMenu(SceneDocument& document) {
    if (ImGui::MenuItem("Empty / group")) document.createEmpty("Group");
    if (ImGui::MenuItem("Directional light")) document.createDirectionalLight();
    if (ImGui::MenuItem("Point light")) document.createPointLight();
}
struct DraggedEntities { const SceneDocument* document{}; uint64_t generation{}; EntityId source{}; };
void reparentTarget(SceneDocument& document, PanelsState& state, EntityId parent, std::function<void()>& action) {
    if (!ImGui::BeginDragDropTarget()) return;
    if (const auto* payload=ImGui::AcceptDragDropPayload(EntityPayload,ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        if (payload->DataSize==sizeof(DraggedEntities)) {
            const auto dragged=*static_cast<const DraggedEntities*>(payload->Data);
            if (dragged.document==&document && dragged.generation==document.scene().generation() && document.scene().contains(dragged.source)) {
                const auto target=document.scene().find(parent);
                ImGui::SetTooltip(parent && target ? "Parent to %s" : "Move to scene root",target ? target->name.c_str() : "");
                if (payload->IsDelivery()) {
                    const auto ids=state.dragged;
                    action=[&document,&state,parent,ids,generation=dragged.generation] {
                        if (document.scene().generation()!=generation) return;
                        document.select(ids,ids.empty() ? NoEntity : ids.back());
                        if (document.reparentSelection(parent) && parent) state.expand.insert(parent);
                    };
                }
            }
        }
    }
    ImGui::EndDragDropTarget();
}
bool beginEdit(SceneDocument& document, PanelsState& state, Edit edit, const char* label) {
    if (state.active==edit && document.transactionActive()) return true;
    if (state.active!=Edit::None && document.transactionActive()) document.commit();
    state.active=Edit::None;
    if (!document.begin(label)) return false;
    state.active=edit; return true;
}
void finishEdit(SceneDocument& document, PanelsState& state, Edit edit) {
    if (state.active!=edit) return;
    document.commit(); state.active=Edit::None;
}
void propertyEdit(SceneDocument& document, PanelsState& state, Edit edit, const char* label,
                  bool changed, const SceneDocument::Mutation& mutation) {
    if ((ImGui::IsItemActivated() || changed) && beginEdit(document,state,edit,label) && changed) document.update(mutation);
    if (ImGui::IsItemDeactivated()) finishEdit(document,state,edit);
    // Checkboxes may update without retaining an active ID until the next frame.
    if (changed && !ImGui::IsItemActive()) finishEdit(document,state,edit);
}
template<class Mutator> SceneDocument::Mutation changeEntity(EntityId id, Mutator mutate) {
    return [id,mutate](auto& records,auto&) {
        const auto found=std::find_if(records.begin(),records.end(),[id](const auto& record){return record.id==id;});
        if (found!=records.end()) mutate(*found);
    };
}
void copyName(PanelsState& state, const EntityRecord& record) {
    state.name.fill(0);
    std::copy_n(record.name.data(),std::min(record.name.size(),state.name.size()-1),state.name.data());
    state.nameId=record.id;
}
void drawName(SceneDocument& document, PanelsState& state, const EntityRecord& record) {
    if (state.nameId!=record.id || state.active!=Edit::Name) copyName(state,record);
    const bool entered=ImGui::InputText("Name",state.name.data(),state.name.size(),ImGuiInputTextFlags_EnterReturnsTrue);
    if (ImGui::IsItemActivated()) beginEdit(document,state,Edit::Name,"Rename entity");
    if (state.active==Edit::Name && ImGui::IsItemEdited() && state.name[0]) {
        const std::string name=state.name.data();
        document.update(changeEntity(record.id,[name](auto& entity){entity.name=name;}));
    }
    if (state.active==Edit::Name && (entered || ImGui::IsItemDeactivated())) {
        const std::string name=state.name.data();
        if (!name.empty()) document.update(changeEntity(record.id,[name](auto& entity){entity.name=name;}));
        finishEdit(document,state,Edit::Name);
    }
}
void lightControls(SceneDocument& document, PanelsState& state, EntityId id, bool directional) {
    const auto record=document.scene().find(id);
    if (!record) return;
    ImGui::PushID(directional ? "directional" : "point");
    const auto enabled=directional ? record->directional->enabled : record->point->enabled;
    auto color=directional ? record->directional->color : record->point->color;
    auto intensity=directional ? record->directional->intensity : record->point->intensity;
    bool value=enabled;
    const bool enabledChanged=ImGui::Checkbox("Enabled",&value);
    propertyEdit(document,state,directional ? Edit::DirectionEnabled : Edit::PointEnabled,"Enable light",enabledChanged,
        changeEntity(id,[directional,value](auto& entity){if (directional) entity.directional->enabled=value; else entity.point->enabled=value;}));
    const bool colorChanged=ImGui::ColorEdit3("Color",color.data(),ImGuiColorEditFlags_Float);
    propertyEdit(document,state,directional ? Edit::DirectionColor : Edit::PointColor,"Change light color",colorChanged,
        changeEntity(id,[directional,color](auto& entity){if (directional) entity.directional->color=color; else entity.point->color=color;}));
    const bool intensityChanged=ImGui::DragFloat("Intensity",&intensity,directional ? .02f : .1f,0,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    propertyEdit(document,state,directional ? Edit::DirectionIntensity : Edit::PointIntensity,"Change light intensity",intensityChanged,
        changeEntity(id,[directional,intensity](auto& entity){if (directional) entity.directional->intensity=intensity; else entity.point->intensity=intensity;}));
    if (directional) ImGui::TextWrapped("Rotate the entity to aim the light along its local -Z axis.");
    else ImGui::TextWrapped("Position controls the light location. Intensity uses inverse-square attenuation.");
    ImGui::PopID();
}
void modelInformation(const EntityRecord& record, const AssetLibrary& assets) {
    const auto model=assets.findModel(*record.model);
    if (!model) {
        ImGui::TextWrapped("Model unavailable: %s",record.model->c_str());
        ImGui::TextWrapped("The asset reference is preserved when this scene is saved."); return;
    }
    ImGui::Text("%s (%s)",model->name.c_str(),model->format.c_str());
    ImGui::Text("%zu meshes, %zu primitives, %zu materials",model->meshes.size(),model->primitives.size(),model->materials.size());
    ImGui::Text("%zu nodes, %zu draws",model->nodes.size(),model->draws.size());
    for (const auto& warning:model->warnings) ImGui::TextWrapped("%s",warning.c_str());
    if (ImGui::TreeNode("Imported materials (read-only)")) {
        for (std::size_t index=0;index<model->materials.size();++index) {
            const auto& material=model->materials[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::TreeNode("material","%s",material.name.c_str())) {
                ImGui::Text("Base color: %.3f, %.3f, %.3f, %.3f",material.baseColor[0],material.baseColor[1],material.baseColor[2],material.baseColor[3]);
                ImGui::Text("Metallic: %.3f   Roughness: %.3f",material.metallic,material.roughness);
                ImGui::Text("Normal scale: %.3f   Occlusion: %.3f",material.normalScale,material.occlusionStrength);
                ImGui::Text("Emissive: %.3f, %.3f, %.3f",material.emissive[0],material.emissive[1],material.emissive[2]);
                ImGui::Text("%s%s",material.alphaMask ? "Alpha mask" : "Opaque",material.doubleSided ? ", double-sided" : "");
                const std::array<std::pair<const char*,const ModelTexture*>,5> slots{{
                    {"Base color",&material.baseColorTexture},{"Metallic / roughness",&material.metallicRoughnessTexture},
                    {"Normal",&material.normalTexture},{"Occlusion",&material.occlusionTexture},{"Emissive",&material.emissiveTexture}}};
                for (const auto& [label,texture]:slots) if (!texture->image.empty())
                    ImGui::TextWrapped("%s (UV%u): %s",label,texture->texCoord,texture->image.c_str());
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}
}

namespace {
bool cancelPanelEdit(SceneDocument& document, PanelsState& state, Panel panel, bool allowEdits) {
    if (owner(state.active)!=panel || (allowEdits && !ImGui::IsKeyPressed(ImGuiKey_Escape,false) && !ImGui::GetIO().AppFocusLost)) return false;
    document.cancel(); state.active=Edit::None; state.nameId=NoEntity;
    state.renameId=NoEntity; state.renameFocus=false; state.pendingClick=NoEntity;
    return true;
}
void startRename(SceneDocument& document, PanelsState& state, EntityId id) {
    const auto record=document.scene().find(id);
    if (!record || document.transactionActive()) return;
    if (!selected(document,id)) document.select({id},id);
    else document.select(document.selection().ids,id);
    state.rename.fill(0);
    std::copy_n(record->name.data(),std::min(record->name.size(),state.rename.size()-1),state.rename.data());
    state.renameId=id; state.renameFocus=true; state.pendingClick=NoEntity;
    for (auto parent=record->parent;parent;) {
        state.expand.insert(parent);
        const auto ancestor=document.scene().find(parent);
        parent=ancestor ? ancestor->parent : NoEntity;
    }
}
void inlineRename(SceneDocument& document, PanelsState& state, EntityId id, ImVec2 position, float width) {
    const auto next=ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(position);
    ImGui::SetNextItemWidth(std::max(width,30.0f));
    ImGui::PushID(static_cast<int>(id>>32)); ImGui::PushID(static_cast<int>(id&0xffffffffu));
    if (state.renameFocus) { ImGui::SetKeyboardFocusHere(); state.renameFocus=false; }
    const bool enter=ImGui::InputText("##rename",state.rename.data(),state.rename.size(),
        ImGuiInputTextFlags_EnterReturnsTrue|ImGuiInputTextFlags_AutoSelectAll);
    if (ImGui::IsItemActivated()) beginEdit(document,state,Edit::InlineName,"Rename entity");
    if (state.active==Edit::InlineName && ImGui::IsItemEdited() && state.rename[0]) {
        const std::string name=state.rename.data();
        document.update(changeEntity(id,[name](auto& entity){entity.name=name;}));
    }
    if (state.active==Edit::InlineName && (enter || ImGui::IsItemDeactivated())) {
        const std::string name=state.rename.data();
        if (!name.empty()) document.update(changeEntity(id,[name](auto& entity){entity.name=name;}));
        finishEdit(document,state,Edit::InlineName); state.renameId=NoEntity;
    }
    ImGui::PopID(); ImGui::PopID();
    ImGui::SetCursorScreenPos(next);
}
void matrixControls(SceneDocument& document, PanelsState& state, const EntityRecord& record) {
    ImGui::SeparatorText("Local transform");
    if (!ImGui::BeginTable("Affine matrix",4,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_BordersInnerV)) return;
    ImGui::TableSetupColumn("X"); ImGui::TableSetupColumn("Y"); ImGui::TableSetupColumn("Z"); ImGui::TableSetupColumn("Position");
    ImGui::TableHeadersRow();
    for (std::size_t row=0;row<4;++row) {
        ImGui::TableNextRow();
        for (std::size_t column=0;column<4;++column) {
            ImGui::TableSetColumnIndex(static_cast<int>(column));
            ImGui::PushID(static_cast<int>(row*4+column));
            if (row==3) ImGui::TextDisabled(column==3 ? "1" : "0");
            else {
                auto value=record.local.at(row,column);
                ImGui::SetNextItemWidth(-1);
                const bool changed=ImGui::DragFloat("##coefficient",&value,.01f,0,0,"%.4g");
                const auto edit=static_cast<Edit>(static_cast<int>(Edit::Matrix00)+static_cast<int>(row*4+column));
                propertyEdit(document,state,edit,"Edit local transform",changed,
                    changeEntity(record.id,[row,column,value](auto& entity){entity.local.at(row,column)=value;}));
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Local matrix row %zu, column %zu. Ctrl+click to type.",row+1,column+1);
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
    ImGui::TextDisabled("Affine row: 0, 0, 0, 1");
    if (math::handedness(record.local)==0)
        ImGui::TextWrapped("This transform is singular. Adjust the matrix to restore an invertible transform.");
    if (!document.status().empty()) ImGui::TextWrapped("%s",document.status().c_str());
}
} // namespace

void drawSceneOutliner(SceneDocument& document, bool allowEdits) {
    auto& state=stateFor(document);
    const bool canceled=cancelPanelEdit(document,state,Panel::Outliner,allowEdits);
    if ((state.lastSelection!=document.selection().ids || state.lastPrimary!=document.selection().primary) && !state.pendingClick)
        state.anchor=document.selection().primary;
    const bool blocked=!allowEdits || canceled || document.transactionActive();
    if (!ImGui::Begin("Outliner")) {
        if (state.active==Edit::InlineName) { document.cancel(); state.active=Edit::None; state.renameId=NoEntity; }
        ImGui::End(); return;
    }
    if (!blocked && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_F2,false))
        startRename(document,state,document.selection().primary);

    const auto records=document.scene().records();
    std::unordered_map<EntityId,const EntityRecord*> byId;
    std::unordered_map<EntityId,std::vector<EntityId>> children;
    for (const auto& record:records) { byId[record.id]=&record; children[record.parent].push_back(record.id); }
    for (auto& [parent,ids]:children) {
        static_cast<void>(parent);
        std::sort(ids.begin(),ids.end(),[&](auto a,auto b){const auto& x=*byId[a]; const auto& y=*byId[b]; return x.order!=y.order ? x.order<y.order : a<b;});
    }
    struct Work { EntityId id; bool close; };
    std::vector<Work> pending;
    for (auto it=children[NoEntity].rbegin();it!=children[NoEntity].rend();++it) pending.push_back({*it,false});
    std::vector<EntityId> visible;
    std::function<void()> action;
    while (!pending.empty()) {
        const auto item=pending.back(); pending.pop_back();
        if (item.close) { ImGui::TreePop(); continue; }
        const auto& record=*byId[item.id];
        visible.push_back(record.id);
        const auto id=std::to_string(record.id);
        const bool renaming=state.renameId==record.id;
        const bool ownsRename=renaming && (state.active==Edit::InlineName || !document.transactionActive());
        ImGui::BeginDisabled(!allowEdits || canceled || (blocked && !ownsRename));
        auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|
            ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_FramePadding;
        if (selected(document,record.id)) flags|=ImGuiTreeNodeFlags_Selected;
        const bool leaf=children[record.id].empty();
        if (leaf) flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (renaming) flags|=ImGuiTreeNodeFlags_AllowOverlap;
        if (state.expand.erase(record.id)) ImGui::SetNextItemOpen(true);
        const bool open=ImGui::TreeNodeEx(id.c_str(),flags,"%s",renaming ? "" : record.name.c_str());
        const auto rowMin=ImGui::GetItemRectMin(), rowMax=ImGui::GetItemRectMax();
        const bool hovered=ImGui::IsItemHovered();
        if (hovered && !renaming) ImGui::SetTooltip("%s",kind(record));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !blocked && !renaming) {
            // Keep an already-selected group intact until we know whether the
            // press becomes a drag, matching Quader's retained tree behavior.
            const auto& io=ImGui::GetIO();
            if (selected(document,record.id) && !io.KeyShift) {
                state.pendingClick=record.id; state.pendingCtrl=io.KeyCtrl; state.pendingShift=io.KeyShift;
            } else selectRow(document,state,record.id,io.KeyCtrl,io.KeyShift);
        }
        if (!blocked && !renaming && ImGui::BeginDragDropSource()) {
            state.pendingClick=NoEntity;
            if (!selected(document,record.id)) document.select({record.id},record.id);
            const auto* existing=ImGui::GetDragDropPayload();
            if (!existing || !existing->IsDataType(EntityPayload)) state.dragged=document.selection().ids;
            const DraggedEntities payload{&document,document.scene().generation(),record.id};
            ImGui::SetDragDropPayload(EntityPayload,&payload,sizeof(payload),ImGuiCond_Once);
            ImGui::Text("%zu selected entities",state.dragged.size());
            ImGui::EndDragDropSource();
        }
        if (!blocked && !renaming) reparentTarget(document,state,record.id,action);
        if (!renaming && ImGui::BeginPopupContextItem()) {
            if (!blocked) {
                if (!selected(document,record.id)) document.select({record.id},record.id);
                else document.select(document.selection().ids,record.id);
            }
            if (ImGui::MenuItem("Rename","F2")) action=[&document,&state,id=record.id]{startRename(document,state,id);};
            if (ImGui::MenuItem("Duplicate","Ctrl+D")) action=[&document]{document.duplicateSelection();};
            if (ImGui::MenuItem("Delete","Delete")) action=[&document]{document.eraseSelection();};
            ImGui::Separator();
            if (ImGui::MenuItem("Group selection")) action=[&document]{document.groupSelection();};
            bool canUngroup=false;
            for (auto root:document.scene().topmost(document.selection().ids)) {
                const auto group=document.scene().find(root);
                if (group && !group->model && !group->directional && !group->point && !document.scene().children(root).empty()) canUngroup=true;
            }
            if (ImGui::MenuItem("Ungroup",nullptr,false,canUngroup)) action=[&document]{document.ungroupSelection();};
            if (ImGui::MenuItem("Unparent",nullptr,false,record.parent!=NoEntity)) action=[&document]{document.reparentSelection(NoEntity);};
            ImGui::Separator();
            if (ImGui::BeginMenu("Add")) { addMenu(document); ImGui::EndMenu(); }
            ImGui::EndPopup();
        }
        if (!blocked && state.pendingClick==record.id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (hovered && ImGui::GetDragDropPayload()==nullptr)
                selectRow(document,state,record.id,state.pendingCtrl,state.pendingShift);
            state.pendingClick=NoEntity;
        }
        if (renaming) inlineRename(document,state,record.id,
            ImVec2(rowMin.x+ImGui::GetTreeNodeToLabelSpacing(),rowMin.y),rowMax.x-rowMin.x-ImGui::GetTreeNodeToLabelSpacing());
        ImGui::EndDisabled();
        if (open && !leaf) {
            pending.push_back({record.id,true});
            const auto& descendants=children[record.id];
            for (auto it=descendants.rbegin();it!=descendants.rend();++it) pending.push_back({*it,false});
        }
    }
    state.visible=std::move(visible);
    ImGui::BeginDisabled(blocked);
    // Empty space is a target, never an authored row or a visible common root.
    const auto remaining=ImGui::GetContentRegionAvail();
    if (ImGui::InvisibleButton("##outliner_background",ImVec2(std::max(remaining.x,1.0f),std::max(remaining.y,1.0f)))) {
        document.select({}); state.anchor=NoEntity;
    }
    if (!blocked) reparentTarget(document,state,NoEntity,action);
    if (ImGui::BeginPopupContextItem("Outliner background")) {
        if (ImGui::BeginMenu("Add")) { addMenu(document); ImGui::EndMenu(); }
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if (action && !blocked) action();
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) state.pendingClick=NoEntity;
    state.lastSelection=document.selection().ids;
    state.lastPrimary=document.selection().primary;
    ImGui::End();
}

void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits) {
    static_cast<void>(assets); static_cast<void>(renderer);
    auto& state=stateFor(document);
    const bool canceled=cancelPanelEdit(document,state,Panel::Inspector,allowEdits);
    if (!ImGui::Begin("Inspector")) {
        if (owner(state.active)==Panel::Inspector) { document.cancel(); state.active=Edit::None; state.nameId=NoEntity; }
        ImGui::End(); return;
    }
    const bool blocked=!allowEdits || canceled || (document.transactionActive() && owner(state.active)!=Panel::Inspector);
    ImGui::BeginDisabled(blocked);
    if (const auto record=document.scene().find(document.selection().primary)) {
        if (document.selection().ids.size()>1)
            ImGui::TextWrapped("%zu selected. These fields edit only the primary entity: %s",document.selection().ids.size(),record->name.c_str());
        drawName(document,state,*record);
        matrixControls(document,state,*record);
    } else ImGui::TextUnformatted("No selection");
    ImGui::EndDisabled();
    ImGui::End();
}

void drawSceneSettings(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits) {
    auto& state=stateFor(document);
    const bool canceled=cancelPanelEdit(document,state,Panel::Settings,allowEdits);
    if (!ImGui::Begin("Settings")) {
        if (owner(state.active)==Panel::Settings) { document.commit(); state.active=Edit::None; }
        ImGui::End(); return;
    }
    const bool blocked=!allowEdits || canceled || (document.transactionActive() && owner(state.active)!=Panel::Settings);
    ImGui::BeginDisabled(blocked);
    if (const auto record=document.scene().find(document.selection().primary)) {
        if (record->directional && ImGui::CollapsingHeader("Directional light",ImGuiTreeNodeFlags_DefaultOpen)) lightControls(document,state,record->id,true);
        if (record->point && ImGui::CollapsingHeader("Point light",ImGuiTreeNodeFlags_DefaultOpen)) lightControls(document,state,record->id,false);
        if (record->model && ImGui::CollapsingHeader("Model",ImGuiTreeNodeFlags_DefaultOpen)) modelInformation(*record,assets);
    }
    if (ImGui::CollapsingHeader("Scene lighting",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto properties=document.scene().properties();
        const bool exposureChanged=ImGui::SliderFloat("Exposure",&properties.exposure,.05f,8.0f,"%.2f",ImGuiSliderFlags_Logarithmic);
        propertyEdit(document,state,Edit::Exposure,"Change exposure",exposureChanged,
            [value=properties.exposure](auto&,auto& settings){settings.exposure=value;});
        ImGui::BeginGroup();
        const bool environmentChanged=drawEnvironmentControls(properties.environment,assets,renderer);
        ImGui::EndGroup();
        if (environmentChanged && beginEdit(document,state,Edit::Environment,"Change environment"))
            document.update([environment=properties.environment](auto&,auto& settings){settings.environment=environment;});
        if (state.active==Edit::Environment && !ImGui::IsAnyItemActive()) finishEdit(document,state,Edit::Environment);
    }
    ImGui::EndDisabled();
    ImGui::End();
}
} // namespace crimson
