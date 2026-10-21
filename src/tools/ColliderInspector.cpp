#include "ColliderInspector.h"
#include "PropertyReset.h"
#include "scene/SceneDocument.h"
#include "scene/BuiltinModels.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <string>

namespace crimson {
namespace {
constexpr float MinimumDimension=.001f,MaximumDimension=100000.f;
std::string folded(std::string text) {for(auto& c:text)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return text;}
}
SceneCollider colliderForShape(const Scene& scene,EntityId id,const SceneCollider& before,ColliderShape shape) {
    auto result=before;result.shape=shape;
    if(shape==ColliderShape::Capsule) {
        result.radius=std::clamp(result.radius,MinimumDimension,MaximumDimension*.5f);
        result.height=std::clamp(std::max(result.height,result.radius*2),MinimumDimension*2,MaximumDimension);
    }
    if(shape==ColliderShape::Trimesh && result.source.empty() && !result.mesh) {
        for(auto current=scene.find(id);current;current=scene.find(current->parent)) {
            if(current->model && !current->model->empty()){result.source=*current->model;break;}
            if(!current->parent)break;
        }
    }
    return result;
}
void releaseColliderInspector(ColliderInspectorState& state) {
    if(state.editing && ImGui::GetActiveID()==state.activeItem)ImGui::ClearActiveID();
    state.editing=false;
}
void drawColliderInspector(SceneDocument& document,ColliderInspectorState& state,const EntityRecord& record,
    const ColliderInspectorModels& models,const ColliderInspectorTransactions& transactions) {
    if(!record.collider)return;
    if(!document.transactionActive())releaseColliderInspector(state);
    bool canceled=false;
    if(state.editing && (ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::GetIO().AppFocusLost||
        state.entity!=record.id||state.documentGeneration!=document.documentGeneration())) {
        document.cancel();ImGui::ClearActiveID();state.editing=false;canceled=true;
    }
    const bool numeric=state.active>=ColliderControl::SizeX&&state.active<=ColliderControl::Height;
    if(state.editing && numeric && (ImGui::GetActiveID()!=state.activeItem || ImGui::GetIO().AppFocusLost)) {
        transactions.finish(state.active);state.editing=false;
    }
    auto value=*record.collider;
    const SceneCollider defaults;
    const auto edit=[&](ColliderControl control,const char* label,bool changed) {
        if((ImGui::IsItemActivated()||changed) && transactions.begin(control,label)) {
            state.active=control;state.activeItem=ImGui::GetItemID();state.editing=true;
            state.entity=record.id;state.documentGeneration=document.documentGeneration();
            if(changed)document.update([id=record.id,value](auto& records,auto&){
                const auto found=std::find_if(records.begin(),records.end(),[id](const auto& item){return item.id==id;});
                if(found!=records.end()&&found->collider)found->collider=value;
            });
        }
        if(state.editing && state.active==control && (ImGui::IsItemDeactivated() || (changed&&!ImGui::IsItemActive()))) {
            transactions.finish(control);state.editing=false;
        }
    };
    ImGui::PushID("ColliderProperties");
    ImGui::BeginDisabled(canceled);
    int shape=static_cast<int>(value.shape);
    const bool shapeChanged=resettableProperty(shape,static_cast<int>(defaults.shape),[&]{return ImGui::Combo("Shape",&shape,value.mode==ColliderMode::Trigger?"Cube\0Sphere\0Cylinder\0Capsule\0":"Cube\0Sphere\0Cylinder\0Capsule\0Trimesh\0");});
    if(shapeChanged)value=colliderForShape(document.scene(),record.id,value,static_cast<ColliderShape>(shape));
    edit(ColliderControl::Shape,"Change collision shape",shapeChanged);
    if(value.shape==ColliderShape::Box) {
        ImGui::TextUnformatted("Size (m)");
        if(ImGui::BeginTable("ColliderAxes",3,ImGuiTableFlags_SizingStretchSame)) {
            constexpr std::array<const char*,3> labels{"X","Y","Z"};
            const std::array<ImU32,3> colors{IM_COL32(170,0,0,255),IM_COL32(0,170,0,255),IM_COL32(0,0,170,255)};
            for(const auto* label:labels)ImGui::TableSetupColumn(label,ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            for(unsigned axis=0;axis<3;++axis) {
                ImGui::TableSetColumnIndex(static_cast<int>(axis));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(labels[axis]);
                ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);ImGui::PushID(static_cast<int>(axis));ImGui::SetNextItemWidth(-1);
                const bool changed=resettableProperty(value.size[axis],defaults.size[axis],[&]{return ImGui::DragFloat("##size",&value.size[axis],.02f,MinimumDimension,MaximumDimension,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddRectFilled(a,{a.x+std::max(1.f,ImGui::GetFontSize()*.15f),b.y},ImGui::GetColorU32(colors[axis]));
                edit(static_cast<ColliderControl>(static_cast<unsigned>(ColliderControl::SizeX)+axis),"Resize cube collider",changed);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else if(value.shape!=ColliderShape::Trimesh) {
        const bool capsule=value.shape==ColliderShape::Capsule;
        const bool radius=resettableProperty(value.radius,defaults.radius,[&]{return ImGui::DragFloat("Radius (m)",&value.radius,.01f,MinimumDimension,capsule?MaximumDimension*.5f:MaximumDimension,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
        if(radius&&capsule)value.height=std::max(value.height,value.radius*2);
        edit(ColliderControl::Radius,"Resize collision radius",radius);
        if(value.shape!=ColliderShape::Sphere) {
            const bool height=resettableProperty(value.height,defaults.height,[&]{return ImGui::DragFloat("Height (m)",&value.height,.02f,capsule?MinimumDimension*2:MinimumDimension,MaximumDimension,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            if(height&&capsule)value.radius=std::min(value.radius,value.height*.5f);
            edit(ColliderControl::Height,"Resize collision height",height);
            if(capsule&&ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Total height, including both rounded caps.");
        }
    } else if(value.mesh) {
        ImGui::TextUnformatted("Source");ImGui::TextUnformatted("Imported collision");
    } else {
        ImGui::TextUnformatted("Source");
        const auto name=[&](const std::string& id){return isBuiltinModel(id)?std::string("Box"):models.name?models.name(id):id;};
        const auto accepts=[&](const std::string& id){return isBuiltinModel(id)||std::any_of(models.entries.begin(),models.entries.end(),[&](const auto& entry){return entry.kind==AssetKind::Model&&entry.id==id;});};
        bool sourceChanged=false,open=false;
        const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
        const auto width=std::max(height,ImGui::GetContentRegionAvail().x-2*height-2*gap);
        ImGui::BeginGroup();
        const auto label=(value.source.empty()?std::string("None (Model)"):name(value.source.id))+"##collision-source";
        open=ImGui::Button(label.c_str(),{width,height});
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Generated from source geometry. Trimesh has no dimension handles.");
        ImGui::SameLine(0,gap);open|=ImGui::Button("...##source-picker",{height,height});
        ImGui::SameLine(0,gap);ImGui::BeginDisabled(value.source.empty());
        if(ImGui::Button("X##clear-source",{height,height})){value.source={};sourceChanged=true;}
        ImGui::EndDisabled();ImGui::EndGroup();
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload=ImGui::AcceptDragDropPayload("CC_MODEL_ASSET")) {
                const auto* text=static_cast<const char*>(payload->Data);
                if(text&&payload->DataSize>1&&text[payload->DataSize-1]=='\0'&&std::find(text,text+payload->DataSize-1,'\0')==text+payload->DataSize-1) {
                    const std::string id(text,static_cast<size_t>(payload->DataSize-1));
                    if(accepts(id)){value.source=AssetRef<ModelAsset>{id};sourceChanged=true;}
                }
            }
            ImGui::EndDragDropTarget();
        }
        if(open){state.search.fill(0);ImGui::OpenPopup("Collision model");}
        if(ImGui::BeginPopup("Collision model")) {
            if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
            ImGui::InputTextWithHint("##search","Search models...",state.search.data(),state.search.size());
            const auto search=folded(state.search.data());
            const auto choice=[&](const std::string& id) {
                const auto title=name(id);if(folded(title).find(search)==std::string::npos)return;
                ImGui::PushID(id.c_str());
                if(ImGui::Selectable(title.c_str(),value.source.id==id)){value.source=AssetRef<ModelAsset>{id};sourceChanged=true;ImGui::CloseCurrentPopup();}
                ImGui::PopID();
            };
            ImGui::BeginChild("Models",{320,ImGui::GetFontSize()*12});
            choice(BuiltinBoxModelId);
            for(const auto& entry:models.entries)if(entry.kind==AssetKind::Model&&!isBuiltinModel(entry.id))choice(entry.id);
            ImGui::EndChild();ImGui::EndPopup();
        }
        if(sourceChanged) {
            if(transactions.begin(ColliderControl::Source,"Generate trimesh collision")) {
                document.update([id=record.id,source=value.source](auto& records,auto&){
                    const auto found=std::find_if(records.begin(),records.end(),[id](const auto& item){return item.id==id;});
                    if(found!=records.end()&&found->collider){found->collider->source=source;found->collider->mesh.reset();}
                });
                transactions.finish(ColliderControl::Source);state.editing=false;
            }
        }
    }
    const bool visible=resettableProperty(value.showCollisionInGame,defaults.showCollisionInGame,[&]{return ImGui::Checkbox("Show in Game",&value.showCollisionInGame);});
    edit(ColliderControl::Visible,"Toggle collision display",visible);
    const bool color=resettableProperty(value.collisionColor,defaults.collisionColor,[&]{return ImGui::ColorEdit4("Color",value.collisionColor.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_AlphaBar);});
    edit(ColliderControl::Color,"Change collision color",color);
    ImGui::EndDisabled();
    ImGui::PopID();
}
}
