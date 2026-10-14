#include "ScenePanels.h"
#include "InspectorTransform.h"
#include "Tools.h"
#include "scene/SceneDocument.h"
#include "scene/BuiltinModels.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"

#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {
namespace {
constexpr const char* EntityPayload = "CRIMSON_SCENE_ENTITIES";
enum class Edit { None, Name, InlineName,
    PositionX, PositionY, PositionZ, RotationX, RotationY, RotationZ, ScaleX, ScaleY, ScaleZ,
    DirectionEnabled, DirectionColor, DirectionIntensity,
    PointEnabled, PointColor, PointIntensity, PointRadius, PointShadows, PointDepthBias, PointNormalBias,
    ModelCastShadows, ModelReceiveShadows,
    Exposure, EnvironmentSource, EnvironmentEnabled, EnvironmentIntensity, EnvironmentImage, EnvironmentRotation,
    EnvironmentSkyTop, EnvironmentSkyHorizon, EnvironmentSkyCurve,
    EnvironmentGroundBottom, EnvironmentGroundHorizon, EnvironmentGroundCurve,
    EnvironmentSun, EnvironmentSunDiameter, EnvironmentSunHaloExtent, EnvironmentSunHaloCurve,
    EnvironmentBackgroundShow, EnvironmentBackgroundOverride, EnvironmentBackgroundColor,
    EnvironmentFogEnabled, EnvironmentFogMode, EnvironmentFogColor, EnvironmentFogLightEnergy, EnvironmentFogDensity,
    EnvironmentFogDepthBegin, EnvironmentFogDepthEnd, EnvironmentFogDepthCurve, EnvironmentFogDepthOpacity,
    EnvironmentFogHeight, EnvironmentFogHeightDensity,
    EnvironmentToneMapping, EnvironmentDebanding, EnvironmentGlowEnabled, EnvironmentGlowStrength, EnvironmentGlowSize, EnvironmentGlowThreshold,
    CharacterHeight, CharacterRadius, CharacterSpeed, CharacterRoute, CharacterCollisionVisible, CharacterCollisionColor,
    ColliderSize, ColliderVisible, ColliderColor, CameraFov, CameraNear, CameraFar };
enum class Panel { None, Outliner, Inspector };
Panel owner(Edit edit) {
    if (edit==Edit::None) return Panel::None;
    if (edit==Edit::InlineName) return Panel::Outliner;
    return Panel::Inspector;
}
bool transformEdit(Edit edit) { return edit>=Edit::PositionX && edit<=Edit::ScaleZ; }
bool environmentEdit(Edit edit) { return edit>=Edit::Exposure && edit<=Edit::EnvironmentGlowThreshold; }
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
    InspectorTransformState transform;
    std::string materialDiagnostic;
};
PanelsState& stateFor(SceneDocument& document) {
    static PanelsState state;
    if (state.document!=&document) { state={}; state.document=&document; }
    if (!document.transactionActive()) {
        // An editor-level cancel/open can end our transaction before this panel
        // runs. Never revive the buffered inline rename after that boundary.
        if (state.active==Edit::InlineName) { state.renameId=NoEntity; state.renameFocus=false; }
        if (state.transform.editing) {
            const auto current=document.scene().find(state.transform.entity);
            if (current && current->local==state.transform.baseline) {
                state.transform.fields=state.transform.captured; state.transform.degrees=state.transform.capturedDegrees;
            }
            state.transform.editing=false; state.transform.refresh=true;
        }
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
    if (record.character) return "Character";
    if (record.collider) return "Collider";
    if (record.camera) return "Camera";
    if (record.model) return "Model";
    if (record.particle) return "Particle effect";
    if (record.directional) return "Directional light";
    if (record.point) return "Point light";
    if (record.environment) return "Environment";
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
void addMenu(SceneDocument& document,EntityId parent=NoEntity) {
    if (ImGui::MenuItem("Box")) document.createBox();
    if (ImGui::MenuItem("Collider")) document.createCollider("Collider",parent);
    if (ImGui::MenuItem("Character")) document.createCharacter();
    if (ImGui::MenuItem("Camera")) document.createCamera();
    ImGui::Separator();
    if (ImGui::MenuItem("Empty / group")) document.createEmpty("Group");
    if (ImGui::MenuItem("Directional light")) document.createDirectionalLight();
    if (ImGui::MenuItem("Point light")) document.createPointLight();
    if (ImGui::MenuItem("Environment")) document.createEnvironment();
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
    if (!(transformEdit(edit) ? document.beginTransform(label,{document.selection().primary}) : document.begin(label))) return false;
    state.active=edit; return true;
}
void finishEdit(SceneDocument& document, PanelsState& state, Edit edit) {
    if (state.active!=edit) return;
    document.commit(); state.active=Edit::None;
    if (transformEdit(edit)) { state.transform.editing=false; state.transform.refresh=true; }
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
    if (!directional) {
        auto radius=record->point->radius;
        const bool changed=ImGui::DragFloat("Radius (m)",&radius,.1f,.01f,100000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointRadius,"Change point light radius",changed,
            changeEntity(id,[radius](auto& entity){entity.point->radius=radius;}));
        auto shadows=record->point->castShadows;
        const bool shadowChanged=ImGui::Checkbox("Cast Shadows",&shadows);
        propertyEdit(document,state,Edit::PointShadows,"Toggle point shadows",shadowChanged,
            changeEntity(id,[shadows](auto& entity){entity.point->castShadows=shadows;}));
        ImGui::BeginDisabled(!shadows);
        auto depthBias=record->point->shadowDepthBias;
        const bool depthChanged=ImGui::DragFloat("Depth Bias (m)",&depthBias,.001f,0,std::numeric_limits<float>::max(),"%.4f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointDepthBias,"Change point shadow depth bias",depthChanged,
            changeEntity(id,[depthBias](auto& entity){entity.point->shadowDepthBias=depthBias;}));
        auto normalBias=record->point->shadowNormalBias;
        const bool normalChanged=ImGui::DragFloat("Normal Bias (texels)",&normalBias,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointNormalBias,"Change point shadow normal bias",normalChanged,
            changeEntity(id,[normalBias](auto& entity){entity.point->shadowNormalBias=normalBias;}));
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}
void builtinMaterialControls(SceneDocument& document,PanelsState& state,const EntityRecord& record,AssetLibrary& assets,const Renderer& renderer) {
    const auto assigned=record.modelMaterial.id;
    const auto materialId=assigned.empty()?std::string(DefaultMaterialId):assets.references().resolveMaterial(assigned);
    const auto material=assets.findMaterial(materialId);
    const auto model=assets.findModel(record.model->id);
    const auto assign=[&](const std::string& id) {
        if(!id.empty()) {
            const auto candidate=assets.findMaterial(id);
            if(!candidate || !modelMaterialCompatible(*candidate)){state.materialDiagnostic="Choose a valid material.";return;}
            try {if(model)(void)assets.resolvedModelMaterials(*model,id);}
            catch(const std::exception& error){state.materialDiagnostic=error.what();return;}
        }
        if(document.transactionActive() && !finishScenePanelEdit(document))return;
        if(document.edit("Assign Box material",changeEntity(record.id,[id](auto& entity){entity.modelMaterial=AssetRef<MaterialAsset>{id};})))state.materialDiagnostic.clear();
    };
    ImGui::TextUnformatted("Material");
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-2*height-2*gap);
    ImGui::BeginGroup();bool picker=ImGui::InvisibleButton("Box material",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    const auto thumbnail=renderer.thumbnailTexture(materialId);
    if(bgfx::isValid(thumbnail))draw->AddImage(ImGuiBgfx::textureId(thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});
    else if(material) {const auto& c=material->surface.baseColor;draw->AddRectFilled({minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3},ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],1}));}
    const auto label=assigned.empty()?std::string("Default PBR"):material?assets.displayName(materialId):"Missing: "+assets.displayName(assigned);
    draw->PushClipRect({minimum.x+height,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({minimum.x+height,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());draw->PopClipRect();
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##Box material",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(assigned.empty());if(ImGui::Button("x##Box material",{height,height}))assign({});ImGui::EndDisabled();
    ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_MATERIAL_ASSET")) {
            const auto* bytes=static_cast<const char*>(payload->Data);
            if(bytes && payload->DataSize>1 && bytes[payload->DataSize-1]=='\0' && std::find(bytes,bytes+payload->DataSize-1,'\0')==bytes+payload->DataSize-1)
                assign(std::string(bytes,static_cast<size_t>(payload->DataSize-1)));
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(picker){filter.Clear();ImGui::OpenPopup("Select Box material");}
    if(ImGui::BeginPopup("Select Box material")) {
        filter.Draw("Search");ImGui::BeginChild("Material choices",{320,ImGui::GetFontSize()*12});
        if(ImGui::Selectable("Default PBR",assigned.empty())){assign({});ImGui::CloseCurrentPopup();}
        for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Material && filter.PassFilter(assets.displayName(entry.id).c_str())) {
            const auto candidate=assets.findMaterial(entry.id);if(!candidate || !modelMaterialCompatible(*candidate))continue;
            ImGui::PushID(entry.id.c_str());if(ImGui::Selectable(assets.displayName(entry.id).c_str(),entry.id==materialId)){assign(entry.id);ImGui::CloseCurrentPopup();}ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
    if(!state.materialDiagnostic.empty())ImGui::TextWrapped("%s",state.materialDiagnostic.c_str());
    if(model && !assigned.empty() && !assets.modelMaterialsReady(*model,assigned))ImGui::TextWrapped("%s",assets.modelMaterialsDiagnostic(*model,assigned).c_str());
}
void modelInformation(const EntityRecord& record, const AssetLibrary& assets) {
    const auto model=assets.findModel(record.model->id);
    if (!model) {
        ImGui::TextWrapped("Model unavailable: %s",record.model->id.c_str());
        return;
    }
    for (const auto& warning:model->warnings) ImGui::TextWrapped("%s",warning.c_str());
    if (model->format!="BUILTIN" && ImGui::TreeNode("Imported materials (read-only)")) {
        auto materials=model->materials;
        try { materials=assets.resolvedModelMaterials(*model); }
        catch (const std::exception& error) { ImGui::TextWrapped("Material unavailable: %s",error.what()); }
        for (std::size_t index=0;index<materials.size();++index) {
            const auto& material=materials[index];
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
                for (const auto& [label,texture]:slots) if (!texture->image.empty()) {
                    const auto resolved=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                    const auto name=resolved==GrayTextureId ? std::string("Default gray") : resolved==NeutralNormalTextureId ?
                        std::string("Neutral normal") : assets.displayName(resolved);
                    ImGui::TextWrapped("%s (UV%u): %s",label,texture->texCoord,name.c_str());
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
}
void sceneEnvironmentControls(SceneDocument& document,PanelsState& state,const EntityRecord& record,
                              AssetLibrary& assets,const Renderer& renderer) {
    auto environment=*record.environment;
    auto& value=environment.environment;
    bool activeFieldDrawn=false;
    const auto field=[&](Edit property,const char* label,bool changed) {
        propertyEdit(document,state,property,label,changed,
            changeEntity(record.id,[environment](auto& entity){entity.environment=environment;}));
        activeFieldDrawn|=state.active==property;
    };
    ImGui::PushID("Scene environment");
    if(document.scene().properties().activeEnvironment!=record.id && ImGui::Button("Make Active")) {
        if(!document.transactionActive() || finishScenePanelEdit(document))document.setActiveEnvironment(record.id);
    }
    int source=static_cast<int>(value.source);
    const char* sources[]{"HDRI / IBL","Procedural Sky"};
    const bool sourceChanged=ImGui::Combo("Source",&source,sources,2);
    if(sourceChanged)value.source=static_cast<EnvironmentSource>(source);
    field(Edit::EnvironmentSource,"Change environment source",sourceChanged);
    field(Edit::EnvironmentEnabled,"Toggle environment lighting",ImGui::Checkbox("Lighting",&value.enabled));
    field(Edit::EnvironmentIntensity,"Change environment intensity",
        ImGui::SliderFloat("Intensity",&value.intensity,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    if(value.source==EnvironmentSource::Hdri) {
        if(ImGui::CollapsingHeader("HDRI",ImGuiTreeNodeFlags_DefaultOpen)) {
            field(Edit::EnvironmentImage,"Assign environment image",drawEnvironmentImageField(value,assets));
            field(Edit::EnvironmentRotation,"Rotate environment",
                ImGui::SliderFloat("Rotation",&value.rotation,-180,180,"%.1f deg",ImGuiSliderFlags_AlwaysClamp));
        }
    } else if(ImGui::CollapsingHeader("Procedural Sky",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& sky=value.procedural;
        ImGui::Indent();
        ImGui::PushID("Procedural");
        if(ImGui::CollapsingHeader("Sky",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Sky");
            field(Edit::EnvironmentSkyTop,"Change sky top color",ImGui::ColorEdit3("Top Color",sky.skyTop.data(),ImGuiColorEditFlags_Float));
            field(Edit::EnvironmentSkyHorizon,"Change sky horizon color",ImGui::ColorEdit3("Horizon Color",sky.skyHorizon.data(),ImGuiColorEditFlags_Float));
            field(Edit::EnvironmentSkyCurve,"Change sky curve",
                ImGui::DragFloat("Curve",&sky.skyCurve,.005f,.001f,10,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            ImGui::PopID();
        }
        if(ImGui::CollapsingHeader("Ground",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Ground");
            field(Edit::EnvironmentGroundBottom,"Change ground bottom color",ImGui::ColorEdit3("Bottom Color",sky.groundBottom.data(),ImGuiColorEditFlags_Float));
            field(Edit::EnvironmentGroundHorizon,"Change ground horizon color",ImGui::ColorEdit3("Horizon Color",sky.groundHorizon.data(),ImGuiColorEditFlags_Float));
            field(Edit::EnvironmentGroundCurve,"Change ground curve",
                ImGui::DragFloat("Curve",&sky.groundCurve,.005f,.001f,10,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            ImGui::PopID();
        }
        if(ImGui::CollapsingHeader("Sun",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::PushID("Sun");
            const auto linked=document.scene().find(environment.sun);
            const bool linkedDirectional=linked && linked->directional;
            bool sunChanged=false;
            if(ImGui::BeginCombo("Directional light",linkedDirectional?linked->name.c_str():"None")) {
                if(ImGui::Selectable("None",!linkedDirectional)) {sunChanged=environment.sun!=NoEntity;environment.sun=NoEntity;}
                for(const auto& candidate:document.scene().records())if(candidate.directional) {
                    ImGui::PushID(static_cast<int>(candidate.id>>32));ImGui::PushID(static_cast<int>(candidate.id));
                    if(ImGui::Selectable(candidate.name.c_str(),environment.sun==candidate.id)) {
                        sunChanged=environment.sun!=candidate.id;environment.sun=candidate.id;
                    }
                    ImGui::PopID();ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            field(Edit::EnvironmentSun,"Assign sky sun",sunChanged);
            field(Edit::EnvironmentSunDiameter,"Change sun diameter",
                ImGui::DragFloat("Size (deg)",&sky.sunDiameter,.01f,0,std::nextafter(2*sky.sunHaloExtent,0.f),"%.2f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Angular diameter in degrees.");
            field(Edit::EnvironmentSunHaloExtent,"Change sun halo extent",
                ImGui::DragFloat("Halo Extent",&sky.sunHaloExtent,.1f,std::nextafter(sky.sunDiameter*.5f,180.f),180,"%.2f deg",ImGuiSliderFlags_AlwaysClamp));
            field(Edit::EnvironmentSunHaloCurve,"Change sun halo curve",
                ImGui::DragFloat("Halo Curve",&sky.sunHaloCurve,.005f,.001f,10,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            ImGui::PopID();
        }
        ImGui::PopID();
        ImGui::Unindent();
    }
    if(ImGui::CollapsingHeader("Background",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("Background");
        field(Edit::EnvironmentBackgroundShow,"Toggle environment background",ImGui::Checkbox("Show Background",&value.background.show));
        if(!value.background.show) {
            field(Edit::EnvironmentBackgroundOverride,"Toggle custom background color",ImGui::Checkbox("Custom Color",&value.background.overrideColor));
            if(value.background.overrideColor)
                field(Edit::EnvironmentBackgroundColor,"Change background color",ImGui::ColorEdit3("Color",value.background.color.data(),ImGuiColorEditFlags_Float));
        }
        ImGui::PopID();
    }
    if(ImGui::CollapsingHeader("Fog",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("Fog");
        auto& fog=value.fog;
        field(Edit::EnvironmentFogEnabled,"Toggle fog",ImGui::Checkbox("Enabled",&fog.enabled));
        ImGui::BeginDisabled(!fog.enabled);
        int mode=static_cast<int>(fog.mode);
        const char* modes[]{"Exponential","Depth"};
        const bool modeChanged=ImGui::Combo("Mode",&mode,modes,2);
        if(modeChanged)fog.mode=static_cast<FogMode>(mode);
        field(Edit::EnvironmentFogMode,"Change fog mode",modeChanged);
        field(Edit::EnvironmentFogColor,"Change fog color",ImGui::ColorEdit3("Color",fog.color.data(),ImGuiColorEditFlags_Float));
        field(Edit::EnvironmentFogLightEnergy,"Change fog light energy",
            ImGui::DragFloat("Light Energy",&fog.lightEnergy,.01f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Brightness of the fog color.");
        if(fog.mode==FogMode::Exponential) {
            field(Edit::EnvironmentFogDensity,"Change fog density",
                ImGui::DragFloat("Density",&fog.density,.0001f,0,1000,"%.4f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Inverse meters (m^-1).");
        } else {
            const float beginMaximum=std::nextafter(fog.depthEnd>0?fog.depthEnd:std::numeric_limits<float>::max(),0.f);
            const bool beginChanged=ImGui::DragFloat("Start (m)",&fog.depthBegin,.1f,0,beginMaximum,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(beginChanged)fog.depthBegin=std::clamp(fog.depthBegin,0.f,beginMaximum);
            field(Edit::EnvironmentFogDepthBegin,"Change fog start",beginChanged);
            const bool endChanged=ImGui::DragFloat("End (m)",&fog.depthEnd,.1f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
            if(endChanged && fog.depthEnd>0 && fog.depthEnd<=fog.depthBegin) {
                fog.depthEnd=std::nextafter(fog.depthBegin,std::numeric_limits<float>::max());
                if(fog.depthEnd==fog.depthBegin)fog.depthEnd=0;
            }
            field(Edit::EnvironmentFogDepthEnd,"Change fog end",endChanged);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("0 uses the camera far plane.");
            field(Edit::EnvironmentFogDepthCurve,"Change fog depth curve",
                ImGui::DragFloat("Curve",&fog.depthCurve,.01f,.001f,std::numeric_limits<float>::max(),"%.3f",ImGuiSliderFlags_AlwaysClamp));
            field(Edit::EnvironmentFogDepthOpacity,"Change fog max opacity",
                ImGui::SliderFloat("Max Opacity",&fog.depthOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Maximum opacity of depth fog.");
        }
        field(Edit::EnvironmentFogHeight,"Change fog height",ImGui::DragFloat("Height (m)",&fog.height,.01f,0,0,"%.2f"));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("World-space level. Requires nonzero Height Density.");
        field(Edit::EnvironmentFogHeightDensity,"Change fog height density",ImGui::DragFloat("Height Density",&fog.heightDensity,.0001f,0,0,"%.4f"));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Positive fills below Height; negative above. 0 disables height fog.");
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if(ImGui::CollapsingHeader("Post-processing",ImGuiTreeNodeFlags_DefaultOpen)) {
        field(Edit::Exposure,"Change exposure",
            ImGui::SliderFloat("Exposure",&environment.exposure,.05f,8,"%.2f",ImGuiSliderFlags_Logarithmic));
        field(Edit::EnvironmentToneMapping,"Change tone mapping",drawToneMappingControl(value.toneMapping));
        field(Edit::EnvironmentDebanding,"Toggle debanding",ImGui::Checkbox("Debanding",&value.debanding));
        drawGlowControls(value.glow,[&](GlowControl control,bool changed) {
            constexpr std::array<Edit,4> properties{Edit::EnvironmentGlowEnabled,Edit::EnvironmentGlowStrength,Edit::EnvironmentGlowSize,Edit::EnvironmentGlowThreshold};
            constexpr std::array<const char*,4> labels{"Toggle glow","Change glow strength","Change glow size","Change glow threshold"};
            const auto index=static_cast<std::size_t>(control);field(properties[index],labels[index],changed);
        });
        if(!renderer.glowStatus().empty())ImGui::TextWrapped("%s",renderer.glowStatus().c_str());
    }
    ImGui::PopID();
    // Collapsing a section or switching sources removes its active widget.
    if(environmentEdit(state.active) && (!activeFieldDrawn || !ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
}
void gameplayControls(SceneDocument& document,PanelsState& state,const EntityRecord& record) {
    if(record.character) {
        if(document.scene().properties().playerCharacter!=record.id && ImGui::Button("Make Player"))document.setPlayerCharacter(record.id);
        auto value=*record.character;
        if(ImGui::CollapsingHeader("Movement",ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool speed=ImGui::DragFloat("Speed (m/s)",&value.moveSpeed,.1f,0,1000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            propertyEdit(document,state,Edit::CharacterSpeed,"Change movement speed",speed,
                changeEntity(record.id,[v=value.moveSpeed](auto& entity){entity.character->moveSpeed=v;}));
            const bool route=ImGui::Checkbox("Show Route",&value.showRoute);
            propertyEdit(document,state,Edit::CharacterRoute,"Toggle character route",route,
                changeEntity(record.id,[v=value.showRoute](auto& entity){entity.character->showRoute=v;}));
        } else if(state.active==Edit::CharacterSpeed || state.active==Edit::CharacterRoute) {
            finishEdit(document,state,state.active);
        }
        if(ImGui::CollapsingHeader("Collision",ImGuiTreeNodeFlags_DefaultOpen)) {
            const bool height=ImGui::DragFloat("Capsule Height (m)",&value.height,.02f,2*value.radius,100.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            propertyEdit(document,state,Edit::CharacterHeight,"Change character height",height,
                changeEntity(record.id,[v=value.height](auto& entity){entity.character->height=v;}));
            const bool radius=ImGui::DragFloat("Capsule Radius (m)",&value.radius,.01f,.01f,value.height*.5f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            propertyEdit(document,state,Edit::CharacterRadius,"Change character radius",radius,
                changeEntity(record.id,[v=value.radius](auto& entity){entity.character->radius=v;}));
            const bool visible=ImGui::Checkbox("Show in Game",&value.showCollisionInGame);
            propertyEdit(document,state,Edit::CharacterCollisionVisible,"Toggle character collision display",visible,
                changeEntity(record.id,[v=value.showCollisionInGame](auto& entity){entity.character->showCollisionInGame=v;}));
            const bool color=ImGui::ColorEdit4("Color",value.collisionColor.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_AlphaBar);
            propertyEdit(document,state,Edit::CharacterCollisionColor,"Change character collision color",color,
                changeEntity(record.id,[v=value.collisionColor](auto& entity){entity.character->collisionColor=v;}));
        } else if(state.active==Edit::CharacterHeight || state.active==Edit::CharacterRadius ||
            state.active==Edit::CharacterCollisionVisible || state.active==Edit::CharacterCollisionColor) {
            finishEdit(document,state,state.active);
        }
    }
    if(record.collider && ImGui::CollapsingHeader("Collision",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto value=*record.collider;
        const bool changed=ImGui::DragFloat3("Box Size (m)",value.size.data(),.02f,.001f,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::ColliderSize,"Resize collider",changed,
            changeEntity(record.id,[v=value.size](auto& entity){entity.collider->size=v;}));
        const bool visible=ImGui::Checkbox("Show in Game",&value.showCollisionInGame);
        propertyEdit(document,state,Edit::ColliderVisible,"Toggle collider display",visible,
            changeEntity(record.id,[v=value.showCollisionInGame](auto& entity){entity.collider->showCollisionInGame=v;}));
        const bool color=ImGui::ColorEdit4("Color",value.collisionColor.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_AlphaBar);
        propertyEdit(document,state,Edit::ColliderColor,"Change collider color",color,
            changeEntity(record.id,[v=value.collisionColor](auto& entity){entity.collider->collisionColor=v;}));
    } else if(record.collider && (state.active==Edit::ColliderSize || state.active==Edit::ColliderVisible || state.active==Edit::ColliderColor)) {
        finishEdit(document,state,state.active);
    }
    if(record.camera && ImGui::CollapsingHeader("Camera",ImGuiTreeNodeFlags_DefaultOpen)) {
        if(document.scene().properties().activeCamera==record.id)ImGui::TextUnformatted("Active Camera");
        else if(ImGui::Button("Make Active"))document.setActiveCamera(record.id);
        auto value=*record.camera;
        const bool fov=ImGui::DragFloat("Vertical FOV",&value.verticalFov,.2f,1,179,"%.1f deg",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::CameraFov,"Change camera FOV",fov,
            changeEntity(record.id,[v=value.verticalFov](auto& entity){entity.camera->verticalFov=v;}));
        const bool nearChanged=ImGui::DragFloat("Near (m)",&value.nearPlane,.01f,.001f,value.farPlane-.001f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::CameraNear,"Change camera clipping",nearChanged,
            changeEntity(record.id,[v=value.nearPlane](auto& entity){entity.camera->nearPlane=v;}));
        const bool farChanged=ImGui::DragFloat("Far (m)",&value.farPlane,1,value.nearPlane+.001f,100000,"%.1f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::CameraFar,"Change camera clipping",farChanged,
            changeEntity(record.id,[v=value.farPlane](auto& entity){entity.camera->farPlane=v;}));
        ImGui::TextUnformatted("Follow Target");
        const auto target=document.scene().find(value.followTarget);
        const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
        const auto width=std::max(height,ImGui::GetContentRegionAvail().x-2*height-2*gap);
        if(ImGui::Button(target?target->name.c_str():"None (Entity)",{width,height}))ImGui::OpenPopup("Choose follow target");
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload=ImGui::AcceptDragDropPayload(EntityPayload))if(payload->DataSize==sizeof(DraggedEntities)) {
                const auto dragged=*static_cast<const DraggedEntities*>(payload->Data);
                if(dragged.document==&document && dragged.generation==document.scene().generation())document.setCameraFollow(record.id,dragged.source);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0,gap);if(ImGui::Button("...##camera-target",{height,height}))ImGui::OpenPopup("Choose follow target");
        ImGui::SameLine(0,gap);if(ImGui::Button("X##camera-target",{height,height}))document.setCameraFollow(record.id,NoEntity);
        if(ImGui::BeginPopup("Choose follow target")) {
            static ImGuiTextFilter filter;filter.Draw("Search");
            for(const auto& candidate:document.scene().records())if(candidate.id!=record.id && filter.PassFilter(candidate.name.c_str())) {
                ImGui::PushID(static_cast<int>(candidate.id>>32));ImGui::PushID(static_cast<int>(candidate.id));
                if(ImGui::Selectable(candidate.name.c_str(),candidate.id==value.followTarget)) {
                    if(document.setCameraFollow(record.id,candidate.id))ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }
}
}

namespace {
bool cancelPanelEdit(SceneDocument& document, PanelsState& state, Panel panel, bool allowEdits) {
    if (owner(state.active)!=panel || (allowEdits && !ImGui::IsKeyPressed(ImGuiKey_Escape,false) && !ImGui::GetIO().AppFocusLost)) return false;
    document.cancel(); state.active=Edit::None; state.nameId=NoEntity;
    if (state.transform.editing) {
        state.transform.fields=state.transform.captured; state.transform.degrees=state.transform.capturedDegrees;
        state.transform.editing=false; state.transform.refresh=true;
    }
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

} // namespace

bool finishScenePanelEdit(SceneDocument& document) {
    auto& state=stateFor(document);
    if (state.active==Edit::None || !document.transactionActive()) return false;
    const bool transform=transformEdit(state.active);
    if (transform) {
        if (!finishInspectorTransform(document,state.transform)) return false;
    } else if (!document.commit()) return false;
    state.active=Edit::None; state.nameId=NoEntity;
    if (transform) { state.transform.editing=false; state.transform.refresh=true; }
    state.renameId=NoEntity; state.renameFocus=false;
    return true;
}

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
                if (group && !group->model && !group->particle && !group->directional && !group->point && !group->environment &&
                    !group->character && !group->collider && !group->camera && !document.scene().children(root).empty()) canUngroup=true;
            }
            if (ImGui::MenuItem("Ungroup",nullptr,false,canUngroup)) action=[&document]{document.ungroupSelection();};
            if (ImGui::MenuItem("Unparent",nullptr,false,record.parent!=NoEntity)) action=[&document]{document.reparentSelection(NoEntity);};
            if (record.environment && ImGui::MenuItem("Make active environment",nullptr,false,
                document.scene().properties().activeEnvironment!=record.id))
                action=[&document,id=record.id]{document.setActiveEnvironment(id);};
            ImGui::Separator();
            if (ImGui::BeginMenu("Add")) { addMenu(document,record.id); ImGui::EndMenu(); }
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

void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits,
                        std::uint64_t* playParticle,std::string* openParticle) {
    auto& state=stateFor(document);
    const bool canceled=cancelPanelEdit(document,state,Panel::Inspector,allowEdits);
    if (!ImGui::Begin("Inspector")) {
        if (owner(state.active)==Panel::Inspector) { document.cancel(); state.active=Edit::None; state.nameId=NoEntity; }
        ImGui::End(); return;
    }
    const bool blocked=!allowEdits || canceled || (document.transactionActive() && owner(state.active)!=Panel::Inspector);
    ImGui::BeginDisabled(blocked);
    if (const auto record=document.scene().find(document.selection().primary)) {
        drawName(document,state,*record);
        const InspectorTransformTransactions transformTransactions{
            [&](unsigned field,const char* label) {
                return beginEdit(document,state,static_cast<Edit>(static_cast<unsigned>(Edit::PositionX)+field),label);
            },
            [&](unsigned field) {
                finishEdit(document,state,static_cast<Edit>(static_cast<unsigned>(Edit::PositionX)+field));
            }};
        drawInspectorTransform(document,state.transform,*record,transformTransactions);
        gameplayControls(document,state,*record);
        if (record->directional && ImGui::CollapsingHeader("Directional light",ImGuiTreeNodeFlags_DefaultOpen)) lightControls(document,state,record->id,true);
        if (record->point && ImGui::CollapsingHeader("Point light",ImGuiTreeNodeFlags_DefaultOpen)) lightControls(document,state,record->id,false);
        if (record->model && ImGui::CollapsingHeader("Model",ImGuiTreeNodeFlags_DefaultOpen)) {
            if(isBuiltinModel(record->model->id))builtinMaterialControls(document,state,*record,assets,renderer);
            auto castShadows=record->castShadows,receiveShadows=record->receiveShadows;
            const bool castChanged=ImGui::Checkbox("Cast Shadows",&castShadows);
            propertyEdit(document,state,Edit::ModelCastShadows,"Toggle model shadow casting",castChanged,
                changeEntity(record->id,[castShadows](auto& entity){entity.castShadows=castShadows;}));
            const bool receiveChanged=ImGui::Checkbox("Receive Shadows",&receiveShadows);
            propertyEdit(document,state,Edit::ModelReceiveShadows,"Toggle model shadow receiving",receiveChanged,
                changeEntity(record->id,[receiveShadows](auto& entity){entity.receiveShadows=receiveShadows;}));
            modelInformation(*record,assets);
        }
        if(record->particle && ImGui::CollapsingHeader("Particle effect",ImGuiTreeNodeFlags_DefaultOpen)) {
            auto assigned=record->particle->id;
            const auto resolved=assets.references().resolve(assigned);
            const auto effect=assets.findParticle(assigned);
            bool changed=false;
            const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
            const auto width=std::max(height,ImGui::GetContentRegionAvail().x-2*height-2*gap);
            const auto label=effect?assets.displayName(resolved):assigned.empty()?std::string("None (Particle Effect)"):std::string("Missing: ")+assigned;
            if(ImGui::Button(label.c_str(),{width,height})) { if(openParticle && effect)*openParticle=resolved; }
            if(ImGui::BeginDragDropTarget()) {
                if(const auto* payload=ImGui::AcceptDragDropPayload("CC_PARTICLE_ASSET"))
                    if(payload->Data && payload->DataSize>1 && static_cast<const char*>(payload->Data)[payload->DataSize-1]=='\0') {
                        const std::string id(static_cast<const char*>(payload->Data),static_cast<size_t>(payload->DataSize-1));
                        if(assets.findParticle(id)) { assigned=id;changed=true; }
                    }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(0,gap);
            if(ImGui::Button("...##effect-picker",{height,height})) ImGui::OpenPopup("Select particle effect");
            ImGui::SameLine(0,gap);
            if(ImGui::Button("X##clear-effect",{height,height})) { assigned.clear();changed=true; }
            if(ImGui::BeginPopup("Select particle effect")) {
                static ImGuiTextFilter filter;filter.Draw("Search");
                for(const auto& entry:assets.catalog()) if(entry.kind==AssetKind::Particle && filter.PassFilter(assets.displayName(entry.id).c_str()))
                    if(ImGui::Selectable(assets.displayName(entry.id).c_str(),assigned==entry.id)) { assigned=entry.id;changed=true;ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            if(changed) { auto next=*record;next.particle=AssetRef<ParticleEffectAsset>{assigned};document.updateEntity(record->id,next,"Assign particle effect"); }
            ImGui::BeginDisabled(!effect);
            if(ImGui::Button("Play Effect") && playParticle) *playParticle=record->id;
            ImGui::SameLine();if(ImGui::Button("Edit Effect") && openParticle) *openParticle=resolved;
            ImGui::EndDisabled();
            const auto diagnostic=assets.particleDiagnostic(assigned);
            if(!diagnostic.empty()) ImGui::TextWrapped("%s",diagnostic.c_str());
        }
        if(record->environment) {
            if(ImGui::CollapsingHeader("Environment",ImGuiTreeNodeFlags_DefaultOpen))
                sceneEnvironmentControls(document,state,*record,assets,renderer);
            else if(environmentEdit(state.active))finishEdit(document,state,state.active);
        }
    } else ImGui::TextUnformatted("No selection");
    ImGui::EndDisabled();
    ImGui::End();
}
} // namespace crimson
