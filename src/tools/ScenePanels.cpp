#include "ScenePanels.h"
#include "SceneInspectorSections.h"
#include "GameplayTagControls.h"
#include "EnemyInspectorControls.h"
#include "PlayerInputInspectorControls.h"
#include "CollisionFilterControls.h"
#include "editor/CollisionLayersDocument.h"
#include "SceneOutlinerColor.h"
#include "CameraAuthoringControls.h"
#include "InspectorTransform.h"
#include "ColliderInspector.h"
#include "Tools.h"
#include "QuaderInspector.h"
#include "LightingControls.h"
#include "scene/SceneDocument.h"
#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include "scene/CameraAuthoring.h"
#include "scene/CameraTemplates.h"
#include "scene/BuiltinModels.h"
#include "assets/AssetLibrary.h"
#include "assets/ModelMaterialSlots.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <exception>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {
namespace {
constexpr const char* EntityPayload = "CRIMSON_SCENE_ENTITIES";
enum class Edit { None, Name, InlineName, Visible,
    PositionX, PositionY, PositionZ, RotationX, RotationY, RotationZ, ScaleX, ScaleY, ScaleZ,
    DirectionEnabled, DirectionColor, DirectionIntensity,DirectionIndirectEnergy,DirectionShadows,DirectionDistance,DirectionDepthBias,DirectionNormalBias,
    PointEnabled, PointColor, PointIntensity, PointRadius, PointNearFalloff, PointSpecular, PointShadows, PointDepthBias, PointNormalBias,
    PointFalloffExponent,PointEdgeSoftness,PointSourceRadius,PointIndirectEnergy,PointAttenuation,
    DirectionReceivers,DirectionCasters,DirectionRevealReceivers,DirectionReveal,DirectionRevealStrength,
    PointReceivers,PointCasters,PointRevealReceivers,PointReveal,PointRevealStrength,
    ModelCastShadows, ModelReceiveShadows,ModelFadeWhenOccluding,
    ModelDirectReceivers,ModelCasterScope,ModelAmbientReceivers,
    ModelAnimationClip,ModelAnimationPlaying,ModelAnimationLoop,ModelAnimationSpeed,ModelAnimationStart,
    Exposure, EnvironmentSource, EnvironmentEnabled, EnvironmentIntensity, EnvironmentImage, EnvironmentRotation,
    EnvironmentSkyTop, EnvironmentSkyHorizon, EnvironmentSkyCurve,
    EnvironmentGroundBottom, EnvironmentGroundHorizon, EnvironmentGroundCurve,
    EnvironmentSun, EnvironmentSunDiameter, EnvironmentSunHaloExtent, EnvironmentSunHaloCurve,
    EnvironmentBackgroundShow, EnvironmentBackgroundOverride, EnvironmentBackgroundColor,
    EnvironmentFogEnabled, EnvironmentFogMode, EnvironmentFogColor, EnvironmentFogLightEnergy, EnvironmentFogDensity,
    EnvironmentFogDepthBegin, EnvironmentFogDepthEnd, EnvironmentFogDepthCurve, EnvironmentFogDepthOpacity,
    EnvironmentFogHeight, EnvironmentFogHeightDensity,
    EnvironmentFogSkyAffect, EnvironmentFogAerialPerspective, EnvironmentFogSunScatter,
    EnvironmentToneMapping, EnvironmentReinhardWhite, EnvironmentAcesWhite, EnvironmentAgxWhite, EnvironmentAgxContrast,
    EnvironmentDebanding, EnvironmentGlowEnabled, EnvironmentGlowStrength, EnvironmentGlowSize, EnvironmentGlowThreshold,
    EnvironmentAmbientLightSource,EnvironmentAmbientLightColor,EnvironmentAmbientLightSky,EnvironmentAmbientLightEnergy,
    EnvironmentAmbientEnabled,EnvironmentAmbientStrength,EnvironmentAmbientFloor,EnvironmentAmbientThreshold,EnvironmentAmbientTransition,EnvironmentAmbientShadows,EnvironmentAmbientEmission,
    EnvironmentGiEnabled,EnvironmentGiStrength,EnvironmentGiDistance,EnvironmentGiThickness,EnvironmentGiQuality,EnvironmentGiBounces,EnvironmentGiTemporal,
    EnvironmentGiRays,EnvironmentGiSteps,EnvironmentGiBias,EnvironmentGiDenoiseRadius,EnvironmentGiDenoiseStrength,EnvironmentGiRadianceClamp,
    EnvironmentSsaoEnabled,EnvironmentSsaoStrength,EnvironmentSsaoRadius,EnvironmentSsaoQuality,
    CharacterRoute, CharacterCollisionVisible, CharacterCollisionColor,
    MotorEnabled,NavigationEnabled,PlayerInputEnabled,PlayerMouseSensitivity,PlayerInvertY,PlayerJumpHeight,
    PlayerGroundAcceleration,PlayerGroundFriction,PlayerAirAcceleration,PlayerAirControl,PlayerStrafeTilt,PlayerTiltResponse,
    EnemyBehaviour,EnemyRadius,EnemyMinWait,EnemyMaxWait,
    VolumeEnabled,VolumeSize,VolumeCellSize,VolumeCellHeight,VolumeSimplification,VolumeProfiles,
    CharacterRigEnabled,
    CharacterRigLast=CharacterRigEnabled+static_cast<unsigned>(ActorLightRigControl::ShadowNormalBias),
    CharacterRevealEnabled,CharacterRevealSpread,
    ColliderShape,ColliderSizeX,ColliderSizeY,ColliderSizeZ,ColliderRadius,ColliderHeight,ColliderSource,ColliderVisible,ColliderColor,
    CameraOffset,CameraRotation,CameraFov,CameraNear,CameraFar };
enum class Panel { None, Outliner, Inspector };
Panel owner(Edit edit) {
    if (edit==Edit::None) return Panel::None;
    if (edit==Edit::InlineName) return Panel::Outliner;
    return Panel::Inspector;
}
bool transformEdit(Edit edit) { return edit>=Edit::PositionX && edit<=Edit::ScaleZ; }
bool environmentEdit(Edit edit) { return edit>=Edit::Exposure && edit<=Edit::EnvironmentSsaoQuality; }
bool rigEdit(Edit edit) { return edit>=Edit::CharacterRigEnabled && edit<=Edit::CharacterRigLast; }
bool lightRoleEdit(Edit edit) { return edit>=Edit::DirectionReceivers && edit<=Edit::PointRevealStrength; }
static_assert(static_cast<unsigned>(Edit::CharacterRigLast)-static_cast<unsigned>(Edit::CharacterRigEnabled)==
    static_cast<unsigned>(ActorLightRigControl::ShadowNormalBias));
struct PanelsState {
    SceneDocument* document{};
    uint64_t documentGeneration{};
    std::vector<EntityId> visible;
    std::vector<EntityId> lastSelection;
    std::vector<EntityId> dragged;
    std::unordered_set<EntityId> expand;
    EntityId anchor{}, nameId{}, renameId{}, pendingClick{}, lastPrimary{};
    bool renameFocus{}, pendingCtrl{}, pendingShift{};
    std::array<char,1025> name{};
    std::array<char,1025> rename{};
    Edit active{Edit::None};
    std::unordered_map<EntityId,InspectorTransformState> transforms;
    EntityId inspectorEntity{},editingEntity{};
    std::optional<SceneComponentTarget> removeComponent;
    std::unordered_map<EntityId,ColliderInspectorState> colliders;
    std::unordered_map<EntityId,GameplayTagPickerState> tags;
    std::string materialDiagnostic;
    SceneOutlinerColorPopupState outlinerColor;
    std::unordered_map<EntityId,CameraAuthoringControlsState> cameraControls;
    std::vector<CameraTemplate> cameraTemplates;
    std::array<char,129> cameraTemplateName{};
    std::string cameraTemplateStatus;
    bool cameraTemplatesLoaded{};
    bool cameraTemplatesAvailable{};
};
void releasePanelColliders(PanelsState& state) {
    for(auto& [id,collider]:state.colliders)releaseColliderInspector(collider);
}
PanelsState& stateFor(SceneDocument& document) {
    static PanelsState state;
    if (state.document!=&document || state.documentGeneration!=document.documentGeneration()) {
        state={}; state.document=&document;state.documentGeneration=document.documentGeneration();
    }
    std::erase_if(state.transforms,[&](const auto& entry){return !document.scene().contains(entry.first);});
    std::erase_if(state.cameraControls,[&](const auto& entry){return !document.scene().contains(entry.first);});
    std::erase_if(state.tags,[&](const auto& entry){return !document.scene().contains(entry.first);});
    for(auto it=state.colliders.begin();it!=state.colliders.end();) {
        if(document.scene().contains(it->first)){++it;continue;}
        releaseColliderInspector(it->second);it=state.colliders.erase(it);
    }
    if (!document.transactionActive()) {
        releasePanelColliders(state);
        // An editor-level cancel/open can end our transaction before this panel
        // runs. Never revive the buffered inline rename after that boundary.
        if (state.active==Edit::InlineName) { state.renameId=NoEntity; state.renameFocus=false; }
        for(auto& [id,transform]:state.transforms)if(transform.editing) {
            const auto current=document.scene().find(id);
            if(current && current->local==transform.baseline) {
                transform.fields=transform.captured;transform.degrees=transform.capturedDegrees;
            }
            transform.editing=false;transform.refresh=true;
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
    if(record.quader)return "Quader";
    if(record.navigationVolume)return "NavigationVolume";
    if(record.enemy)return "Enemy";
    if (record.playerInput) return "Player";
    if (record.actor) return "Actor";
    if (record.controller) return "Character Controller";
    if (record.collider) return record.collider->shape==ColliderShape::Trimesh?"Mesh Collider":"Collider";
    if (record.camera) return "Camera";
    if (record.model) return "Model";
    if (record.particle) return "Particle effect";
    if (record.directional) return "Directional light";
    if (record.point) return "Point light";
    if (record.environment) return "Environment";
    return isSceneGroup(record)?"Group":"Empty";
}
void selectRow(SceneDocument& document, PanelsState& state, EntityId id, bool toggle, bool range) {
    finishScenePanelEdit(document);
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
    const auto records=document.scene().records();
    const bool hasVolume=std::any_of(records.begin(),records.end(),[](const auto& record){return record.navigationVolume && record.navigationVolume->enabled;});
    if(ImGui::MenuItem("NavigationVolume",nullptr,false,!hasVolume))document.createNavigationVolume("NavigationVolume",parent);
    if (ImGui::MenuItem("Player")) document.createPlayer();
    if (ImGui::MenuItem("Enemy")) document.createEnemy();
    if (ImGui::MenuItem("Camera")) document.createCamera();
    if (ImGui::MenuItem("Quader")) document.createQuader();
    ImGui::Separator();
    if (ImGui::MenuItem("Empty / group")) document.createEmpty("Group");
    if (ImGui::MenuItem("Directional light")) document.createDirectionalLight();
    if (ImGui::MenuItem("Point light")) document.createPointLight();
    if (ImGui::MenuItem("Environment")) document.createEnvironment();
}
struct DraggedEntities { const SceneDocument* document{}; uint64_t generation{}; EntityId source{}; };
std::vector<std::string> modelDropIds(const ImGuiPayload& payload,bool batch) {
    if(!payload.Data || payload.DataSize<2)return {};
    const auto* next=static_cast<const char*>(payload.Data);
    const auto* end=next+payload.DataSize;
    std::vector<std::string> result;
    while(next!=end) {
        const auto* zero=std::find(next,end,'\0');
        if(zero==next || zero==end || (!batch && zero!=end-1))return {};
        result.emplace_back(next,zero);next=zero+1;
    }
    return result;
}
void reparentTarget(SceneDocument& document, PanelsState& state, EntityId parent, std::function<void()>& action,
                    AssetLibrary* assets=nullptr,bool actorVisual=false) {
    const auto target=parent?document.scene().find(parent):std::nullopt;
    if(parent && (!target || (actorVisual && !target->actor)))return;
    if (!ImGui::BeginDragDropTarget()) return;
    if (const auto* payload=ImGui::AcceptDragDropPayload(EntityPayload,ImGuiDragDropFlags_AcceptBeforeDelivery)) {
        if (payload->DataSize==sizeof(DraggedEntities)) {
            const auto dragged=*static_cast<const DraggedEntities*>(payload->Data);
            const bool visuals=std::all_of(state.dragged.begin(),state.dragged.end(),[&](const auto id){
                const auto record=document.scene().find(id);return record && !record->actor && (record->model || record->particle);});
            if (dragged.document==&document && dragged.generation==document.scene().generation() && document.scene().contains(dragged.source) && (!actorVisual || visuals)) {
                ImGui::SetTooltip(parent && target ? "Parent to %s" : "Move to scene root",target ? target->name.c_str() : "");
                if (payload->IsDelivery()) {
                    const auto ids=state.dragged;
                    action=[&document,&state,parent,ids,actorVisual,generation=dragged.generation] {
                        if (document.scene().generation()!=generation) return;
                        finishScenePanelEdit(document);
                        document.select(ids,ids.empty() ? NoEntity : ids.back());
                        if(document.reparentSelection(parent)) {
                            if(parent)state.expand.insert(parent);
                            if(actorVisual)document.select({parent},parent);
                        }
                    };
                }
            }
        }
    } else if(assets) {
        bool batch=true;
        const auto* model=ImGui::AcceptDragDropPayload("CC_MODEL_ASSETS",ImGuiDragDropFlags_AcceptBeforeDelivery);
        if(!model){batch=false;model=ImGui::AcceptDragDropPayload("CC_MODEL_ASSET",ImGuiDragDropFlags_AcceptBeforeDelivery);}
        if(model) {
            auto ids=modelDropIds(*model,batch);
            const auto& catalog=assets->catalog();
            std::erase_if(ids,[&](const auto& id){return std::none_of(catalog.begin(),catalog.end(),[&](const auto& entry){return entry.id==id && entry.kind==AssetKind::Model;});});
            if(!ids.empty()) {
                if(const auto modelParent=document.scene().find(parent))ImGui::SetTooltip("Add model to %s",modelParent->name.c_str());
                if(model->IsDelivery())action=[&document,&state,parent,ids=std::move(ids),assets,actorVisual] {
                    if(parent && !document.scene().contains(parent))return;
                    finishScenePanelEdit(document);
                    std::vector<ModelPlacement> placements;
                    for(const auto& id:ids)placements.push_back({id,assets->displayName(id),Mat4{}});
                    if(!document.createModels(placements,parent).empty()) {
                        if(parent)state.expand.insert(parent);
                        if(actorVisual)document.select({parent},parent);
                    }
                    ImGui::SetWindowFocus();
                };
            }
        }
    }
    ImGui::EndDragDropTarget();
}
bool beginEdit(SceneDocument& document, PanelsState& state, Edit edit, const char* label,EntityId target=NoEntity) {
    if(!target)target=state.inspectorEntity?state.inspectorEntity:document.selection().primary;
    if (state.active==edit && state.editingEntity==target && document.transactionActive()) return true;
    if (state.active!=Edit::None && document.transactionActive()) document.commit();
    state.active=Edit::None;
    if (!(transformEdit(edit) ? document.beginTransform(label,{target}) : document.begin(label))) return false;
    state.active=edit;state.editingEntity=target; return true;
}
void finishEdit(SceneDocument& document, PanelsState& state, Edit edit) {
    if (state.active!=edit || (state.inspectorEntity && state.editingEntity!=state.inspectorEntity)) return;
    document.commit(); state.active=Edit::None;
    if (transformEdit(edit)) { auto& transform=state.transforms[state.editingEntity];transform.editing=false;transform.refresh=true; }
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
    auto indirectEnergy=directional ? record->directional->indirectEnergy : record->point->indirectEnergy;
    const bool indirectChanged=drawIndirectEnergyControl(indirectEnergy);
    propertyEdit(document,state,directional ? Edit::DirectionIndirectEnergy : Edit::PointIndirectEnergy,"Change light GI energy",indirectChanged,
        changeEntity(id,[directional,indirectEnergy](auto& entity){if(directional)entity.directional->indirectEnergy=indirectEnergy;else entity.point->indirectEnergy=indirectEnergy;}));
    if(directional) {
        auto light=*record->directional;
        const bool shadows=ImGui::Checkbox("Cast Shadows",&light.castShadows);
        propertyEdit(document,state,Edit::DirectionShadows,"Toggle directional shadows",shadows,
            changeEntity(id,[v=light.castShadows](auto& entity){entity.directional->castShadows=v;}));
        ImGui::BeginDisabled(!light.castShadows);
        const bool distance=ImGui::DragFloat("Shadow Distance (m)",&light.shadowDistance,.5f,.01f,100000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::DirectionDistance,"Change directional shadow distance",distance,
            changeEntity(id,[v=light.shadowDistance](auto& entity){entity.directional->shadowDistance=v;}));
        const bool depth=ImGui::DragFloat("Depth Bias (m)",&light.shadowDepthBias,.001f,0,std::numeric_limits<float>::max(),"%.4f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::DirectionDepthBias,"Change directional shadow depth bias",depth,
            changeEntity(id,[v=light.shadowDepthBias](auto& entity){entity.directional->shadowDepthBias=v;}));
        const bool normal=ImGui::DragFloat("Normal Bias (texels)",&light.shadowNormalBias,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::DirectionNormalBias,"Change directional shadow normal bias",normal,
            changeEntity(id,[v=light.shadowNormalBias](auto& entity){entity.directional->shadowNormalBias=v;}));
        ImGui::EndDisabled();
    } else {
        auto radius=record->point->radius;
        const bool changed=ImGui::DragFloat("Radius (m)",&radius,.1f,.01f,100000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointRadius,"Change point light radius",changed,
            changeEntity(id,[radius](auto& entity){entity.point->radius=radius;}));
        auto attenuation=record->point->attenuation;
        const bool attenuationChanged=ImGui::SliderFloat("Attenuation",&attenuation,-10,10,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointAttenuation,"Change point light attenuation",attenuationChanged,
            changeEntity(id,[attenuation](auto& entity){entity.point->attenuation=attenuation;}));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Distance decay: 0 has no distance decay, 1 is inverse distance, and 2 is inverse square. Range Falloff still fades the range boundary.");
        auto nearFalloff=record->point->nearFalloffRadius;
        const bool nearChanged=ImGui::DragFloat("Near Falloff Radius (m)",&nearFalloff,.05f,0,100000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointNearFalloff,"Change point light near falloff",nearChanged,
            changeEntity(id,[nearFalloff](auto& entity){entity.point->nearFalloffRadius=nearFalloff;}));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Smooth brightness near the light. Zero retains inverse-square falloff.");
        auto specular=record->point->specularStrength;
        const bool specularChanged=ImGui::SliderFloat("Specular Scale",&specular,0,1,"%.2f");
        propertyEdit(document,state,Edit::PointSpecular,"Change point light specular scale",specularChanged,
            changeEntity(id,[specular](auto& entity){entity.point->specularStrength=specular;}));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Scale this light's shiny highlights without changing diffuse illumination.");
        auto exponent=record->point->rangeFalloffExponent;
        const bool exponentChanged=ImGui::SliderFloat("Range Falloff",&exponent,.25f,8,"%.2f");
        propertyEdit(document,state,Edit::PointFalloffExponent,"Change point light range falloff",exponentChanged,
            changeEntity(id,[exponent](auto& entity){entity.point->rangeFalloffExponent=exponent;}));
        auto softness=record->point->edgeSoftness;
        const bool softnessChanged=ImGui::SliderFloat("Edge Softness",&softness,.05f,1,"%.2f");
        propertyEdit(document,state,Edit::PointEdgeSoftness,"Change point light edge softness",softnessChanged,
            changeEntity(id,[softness](auto& entity){entity.point->edgeSoftness=softness;}));
        auto sourceRadius=record->point->sourceRadius;
        const bool sourceRadiusChanged=ImGui::DragFloat("Source Radius (m)",&sourceRadius,.05f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        propertyEdit(document,state,Edit::PointSourceRadius,"Change point light source radius",sourceRadiusChanged,
            changeEntity(id,[sourceRadius](auto& entity){entity.point->sourceRadius=sourceRadius;}));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Larger source sizes soften shadows and broaden specular highlights. Zero retains a punctual source.");
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
    auto roles=directional?record->directional->roles:record->point->roles;
    bool activeRoleDrawn=false;
    const auto roleEdit=[&](Edit property,const char* label,bool changed) {
        propertyEdit(document,state,property,label,changed,changeEntity(id,[directional,roles](auto& entity){
            if(directional)entity.directional->roles=roles;else entity.point->roles=roles;
        }));
        activeRoleDrawn|=state.active==property;
    };
    if(ImGui::TreeNode("Advanced Lighting")) {
        roleEdit(directional?Edit::DirectionReceivers:Edit::PointReceivers,"Change direct-light receivers",
            drawLightingMaskControl("Affect",roles.receiverMask));
        roleEdit(directional?Edit::DirectionCasters:Edit::PointCasters,"Change light shadow casters",
            drawLightingMaskControl("Shadow Casters",roles.shadowCasterMask));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Meshes allowed to block THIS light, independently of lit receivers. Enable Character for player shadows from this light.");
        roleEdit(directional?Edit::DirectionReveal:Edit::PointReveal,"Toggle light ambient contribution",ImGui::Checkbox("Affect Ambient Darkness",&roles.ambientReveal));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Reveal direct and environment lighting near this source when Ambient Darkness is enabled.");
        ImGui::BeginDisabled(!roles.ambientReveal);
        roleEdit(directional?Edit::DirectionRevealReceivers:Edit::PointRevealReceivers,"Change light ambient receivers",
            drawLightingMaskControl("Ambient Receivers",roles.ambientRevealMask));
        roleEdit(directional?Edit::DirectionRevealStrength:Edit::PointRevealStrength,"Change light ambient strength",
            ImGui::SliderFloat("Ambient Strength",&roles.ambientRevealStrength,0,1,"%.3f"));
        ImGui::EndDisabled();ImGui::TreePop();
    }
    if(lightRoleEdit(state.active) && (!activeRoleDrawn || !ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
    ImGui::PopID();
}
void materialSlotControl(SceneDocument& document,PanelsState& state,const EntityRecord& record,const ModelAsset& model,
    std::size_t slot,AssetLibrary& assets,const Renderer& renderer,std::string* openMaterial) {
    const auto assigned=slot<record.modelMaterials.size()?record.modelMaterials[slot].id:std::string{};
    const auto source=slot<model.materialIds.size()?model.materialIds[slot]:importedMaterialId(model.id,slot);
    const auto materialId=assets.references().resolveMaterial(assigned.empty()?source:assigned);
    const auto material=assets.findMaterial(materialId);
    const auto assign=[&](const std::string& id) {
        auto assignments=record.modelMaterials;
        setModelMaterialOverride(assignments,slot,AssetRef<MaterialAsset>{id});
        if(!id.empty()) {
            const auto candidate=assets.findMaterial(id);
            if(!candidate || !modelMaterialCompatible(*candidate)){state.materialDiagnostic="Choose a valid material.";return;}
            try {(void)assets.resolvedModelMaterials(model,assignments);}
            catch(const std::exception& error){state.materialDiagnostic=error.what();return;}
        }
        if(document.transactionActive() && !finishScenePanelEdit(document))return;
        if(document.edit("Assign material slot "+std::to_string(slot+1),changeEntity(record.id,[slot,id](auto& entity){
            setModelMaterialOverride(entity.modelMaterials,slot,AssetRef<MaterialAsset>{id});
        })))state.materialDiagnostic.clear();
    };
    ImGui::Text("Slot %zu: %s",slot+1,model.materials[slot].name.c_str());
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float openWidth=ImGui::CalcTextSize("Open").x+2*ImGui::GetStyle().FramePadding.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-2*height-openWidth-3*gap);
    ImGui::BeginGroup();bool picker=ImGui::InvisibleButton("Material",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    const auto thumbnail=renderer.thumbnailTexture(materialId);
    if(bgfx::isValid(thumbnail))draw->AddImage(ImGuiBgfx::textureId(thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});
    else if(material) {const auto& c=material->surface.baseColor;draw->AddRectFilled({minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3},ImGui::ColorConvertFloat4ToU32({c[0],c[1],c[2],1}));}
    const auto label=material?assets.displayName(materialId):"Missing: "+assets.displayName(assigned.empty()?source:assigned);
    draw->PushClipRect({minimum.x+height,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({minimum.x+height,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),label.c_str());draw->PopClipRect();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("%s",materialId.c_str());
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##Material",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(assigned.empty());if(ImGui::Button("x##Material",{height,height}))assign({});ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Restore source material");
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(!openMaterial || !material);
    if(ImGui::Button("Open",{openWidth,height}) && openMaterial)*openMaterial=materialId;
    ImGui::EndDisabled();
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
    if(picker){filter.Clear();ImGui::OpenPopup("Select material");}
    if(ImGui::BeginPopup("Select material")) {
        filter.Draw("Search");ImGui::BeginChild("Material choices",{320,ImGui::GetFontSize()*12});
        if(ImGui::Selectable("Source material",assigned.empty())){assign({});ImGui::CloseCurrentPopup();}
        for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Material && filter.PassFilter(assets.displayName(entry.id).c_str())) {
            const auto candidate=assets.findMaterial(entry.id);if(!candidate || !modelMaterialCompatible(*candidate))continue;
            ImGui::PushID(entry.id.c_str());if(ImGui::Selectable(assets.displayName(entry.id).c_str(),entry.id==materialId)){assign(entry.id);ImGui::CloseCurrentPopup();}ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
}
void modelAnimationControls(SceneDocument& document,PanelsState& state,const EntityRecord& record,const AssetLibrary& assets) {
    const auto model=assets.findModel(record.model->id);
    const bool available=model&&(!model->animations.empty()||!model->skins.empty()||
        std::any_of(model->primitives.begin(),model->primitives.end(),[](const auto& primitive){return !primitive.morphTargets.empty();}));
    bool activeDrawn=false;
    if(available&&ImGui::TreeNode("Animation")) {
        auto animation=record.modelAnimation;
        const auto field=[&](Edit property,const char* label,bool changed) {
            propertyEdit(document,state,property,label,changed,
                changeEntity(record.id,[animation](auto& entity){entity.modelAnimation=animation;}));
            activeDrawn|=state.active==property;
        };
        const auto current=animation.clip;
        const auto clipName=current<0?std::string("Bind Pose"):std::size_t(current)<model->animations.size()?
            (model->animations[current].name.empty()?"Clip "+std::to_string(std::int64_t(current)+1):model->animations[current].name):
            "Missing clip "+std::to_string(std::int64_t(current)+1);
        bool changed=false;
        if(ImGui::BeginCombo("Clip",clipName.c_str())) {
            if(ImGui::Selectable("Bind Pose",current<0)){changed=current!=-1;animation.clip=-1;}
            for(std::size_t index=0;index<model->animations.size();++index) {
                ImGui::PushID(static_cast<int>(index));
                const auto& clip=model->animations[index];const auto name=clip.name.empty()?"Clip "+std::to_string(index+1):clip.name;
                if(ImGui::Selectable(name.c_str(),current==static_cast<std::int32_t>(index))) {
                    changed=current!=static_cast<std::int32_t>(index);animation.clip=static_cast<std::int32_t>(index);
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        field(Edit::ModelAnimationClip,"Choose model animation",changed);
        field(Edit::ModelAnimationPlaying,"Toggle model animation playback",ImGui::Checkbox("Playing",&animation.playing));
        field(Edit::ModelAnimationLoop,"Toggle model animation loop",ImGui::Checkbox("Loop",&animation.loop));
        field(Edit::ModelAnimationSpeed,"Change model animation speed",ImGui::DragFloat("Speed",&animation.speed,.01f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        field(Edit::ModelAnimationStart,"Change model animation start",ImGui::DragFloat("Start Time (s)",&animation.startTime,.05f,0,1e9f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Changing Clip or Start Time restarts playback. Playing and Speed retain the current time.");
        ImGui::TreePop();
    }
    if(state.active>=Edit::ModelAnimationClip&&state.active<=Edit::ModelAnimationStart&&
        (!activeDrawn||!ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
}
void modelInformation(SceneDocument& document,PanelsState& state,const EntityRecord& record,AssetLibrary& assets,
    const Renderer& renderer,std::string* openMaterial) {
    const auto model=assets.findModel(record.model->id);
    if (!model) {
        ImGui::TextWrapped("Model unavailable: %s",record.model->id.c_str());
        return;
    }
    for (const auto& warning:model->warnings) ImGui::TextWrapped("%s",warning.c_str());
    if (ImGui::TreeNodeEx("Materials",ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto index:usedModelMaterialSlots(*model)) {
            ImGui::PushID(static_cast<int>(index));
            materialSlotControl(document,state,record,*model,index,assets,renderer,openMaterial);
            ImGui::PopID();
        }
        if(!state.materialDiagnostic.empty())ImGui::TextWrapped("%s",state.materialDiagnostic.c_str());
        const auto diagnostic=assets.modelMaterialsDiagnostic(*model,record.modelMaterials);
        if(!diagnostic.empty())ImGui::TextWrapped("%s",diagnostic.c_str());
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
    if(ImGui::CollapsingHeader("Ambient Light",ImGuiTreeNodeFlags_DefaultOpen)) {
        drawAmbientLightControls(value.ambientLight,[&](AmbientLightControl control,bool changed) {
            constexpr std::array<Edit,4> properties{Edit::EnvironmentAmbientLightSource,Edit::EnvironmentAmbientLightColor,
                Edit::EnvironmentAmbientLightSky,Edit::EnvironmentAmbientLightEnergy};
            field(properties[static_cast<unsigned>(control)],"Change ambient light",changed);
        });
    }
    if(ImGui::CollapsingHeader("Global Illumination")) {
        ImGui::PushID("GI");auto& gi=value.ssgi;
        field(Edit::EnvironmentGiEnabled,"Toggle global illumination",ImGui::Checkbox("Enabled",&gi.enabled));
        ImGui::BeginDisabled(!gi.enabled);
        field(Edit::EnvironmentGiStrength,"Change global illumination strength",
            ImGui::SliderFloat("Strength",&gi.strength,0,4,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        field(Edit::EnvironmentGiBounces,"Change global illumination bounces",
            ImGui::SliderInt("Bounces",&gi.bounces,1,4,"%d",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Additional diffuse bounces increase GPU work.");
        field(Edit::EnvironmentGiDistance,"Change global illumination distance",
            ImGui::DragFloat("Max Distance (m)",&gi.maxDistance,.05f,.1f,20,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Maximum search distance per bounce. Offscreen surfaces remain unavailable.");
        int quality=static_cast<int>(gi.quality);const char* qualities[]{"Low","Medium","High","Custom"};
        const bool qualityChanged=ImGui::Combo("Quality",&quality,qualities,4);
        if(qualityChanged)gi.quality=static_cast<SsgiQuality>(quality);
        field(Edit::EnvironmentGiQuality,"Change screen-space GI quality",qualityChanged);
        if(ImGui::TreeNode("Advanced")) {
            auto budget=ssgiQualityBudget(gi);int rays=static_cast<int>(budget.rays);
            const bool raysChanged=ImGui::SliderInt("Samples Per Pixel",&rays,1,16,"%d",ImGuiSliderFlags_AlwaysClamp);
            if(raysChanged){gi.rays=rays;gi.steps=static_cast<int>(budget.steps);gi.quality=SsgiQuality::Custom;}
            field(Edit::EnvironmentGiRays,"Change global illumination samples",raysChanged);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Rays traced per half-resolution pixel. Editing selects Custom quality.");
            budget=ssgiQualityBudget(gi);int steps=static_cast<int>(budget.steps);
            const bool stepsChanged=ImGui::SliderInt("Ray Steps",&steps,8,64,"%d",ImGuiSliderFlags_AlwaysClamp);
            if(stepsChanged){gi.steps=steps;gi.rays=static_cast<int>(budget.rays);gi.quality=SsgiQuality::Custom;}
            field(Edit::EnvironmentGiSteps,"Change global illumination ray steps",stepsChanged);
            field(Edit::EnvironmentGiBias,"Change global illumination ray bias",
                ImGui::DragFloat("Ray Bias (m)",&gi.rayBias,.001f,.001f,.5f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Offsets ray origins from the receiver to reduce self-intersection.");
            field(Edit::EnvironmentGiThickness,"Change global illumination thickness",
                ImGui::DragFloat("Thickness (m)",&gi.thickness,.005f,.01f,2,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Depth hit tolerance. Higher values can leak through thin walls.");
            field(Edit::EnvironmentGiTemporal,"Change global illumination temporal stability",
                ImGui::SliderFloat("Temporal Stability",&gi.temporalStability,0,.98f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("History reuse. Higher values reduce noise and respond more slowly to changes.");
            field(Edit::EnvironmentGiDenoiseRadius,"Change global illumination denoise radius",
                ImGui::SliderInt("Denoise Radius (px)",&gi.denoiseRadius,0,4,"%d",ImGuiSliderFlags_AlwaysClamp));
            field(Edit::EnvironmentGiDenoiseStrength,"Change global illumination denoise strength",
                ImGui::SliderFloat("Denoise Strength",&gi.denoiseStrength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Spatial noise filtering. Zero preserves unfiltered samples.");
            field(Edit::EnvironmentGiRadianceClamp,"Change global illumination radiance clamp",
                ImGui::DragFloat("Radiance Clamp",&gi.radianceClamp,.1f,0,100,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Limits bright HDR donor samples to reduce fireflies. Zero disables the clamp.");
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
        if(gi.enabled && document.scene().properties().activeEnvironment==record.id && !renderer.ssgiStatus().empty())
            ImGui::TextWrapped("%s",renderer.ssgiStatus().c_str());
        ImGui::PopID();
    }
    if(ImGui::CollapsingHeader("Screen Space Ambient Occlusion")) {
        ImGui::PushID("SSAO");auto& ao=value.ssao;
        field(Edit::EnvironmentSsaoEnabled,"Toggle screen-space ambient occlusion",ImGui::Checkbox("Enabled",&ao.enabled));
        ImGui::BeginDisabled(!ao.enabled);
        field(Edit::EnvironmentSsaoStrength,"Change screen-space ambient occlusion strength",
            ImGui::SliderFloat("Strength",&ao.strength,0,2,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        field(Edit::EnvironmentSsaoRadius,"Change screen-space ambient occlusion radius",
            ImGui::DragFloat("Radius (m)",&ao.radius,.01f,.05f,5,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("World-space radius used to find nearby screen-space occluders.");
        int quality=static_cast<int>(ao.quality);const char* qualities[]{"Low","Medium","High"};
        const bool qualityChanged=ImGui::Combo("Quality",&quality,qualities,3);
        if(qualityChanged)ao.quality=static_cast<SsaoQuality>(quality);
        field(Edit::EnvironmentSsaoQuality,"Change screen-space ambient occlusion quality",qualityChanged);
        ImGui::EndDisabled();
        if(ao.enabled && document.scene().properties().activeEnvironment==record.id && !renderer.ssaoStatus().empty())
            ImGui::TextWrapped("%s",renderer.ssaoStatus().c_str());
        ImGui::PopID();
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
        field(Edit::EnvironmentFogSkyAffect,"Change fog sky affect",
            ImGui::SliderFloat("Sky Affect",&fog.skyAffect,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("How much fog covers the sky and empty background.");
        field(Edit::EnvironmentFogAerialPerspective,"Change fog aerial perspective",
            ImGui::SliderFloat("Aerial Perspective",&fog.aerialPerspective,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Blends distant fog toward the sky color.");
        field(Edit::EnvironmentFogSunScatter,"Change fog sun scatter",
            ImGui::SliderFloat("Sun Scatter",&fog.sunScatter,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Scatters directional light into fog when looking toward its source.");
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
        drawToneMappingParameterControls(value.toneMapping,value.toneMappingParameters,[&](ToneMappingParameterControl control,bool changed) {
            constexpr std::array<Edit,4> properties{Edit::EnvironmentReinhardWhite,Edit::EnvironmentAcesWhite,Edit::EnvironmentAgxWhite,Edit::EnvironmentAgxContrast};
            constexpr std::array<const char*,4> labels{"Change Reinhard white point","Change ACES white point","Change AgX white point","Change AgX contrast"};
            const auto index=static_cast<std::size_t>(control);field(properties[index],labels[index],changed);
        });
        field(Edit::EnvironmentDebanding,"Toggle debanding",ImGui::Checkbox("Debanding",&value.debanding));
        drawGlowControls(value.glow,[&](GlowControl control,bool changed) {
            constexpr std::array<Edit,4> properties{Edit::EnvironmentGlowEnabled,Edit::EnvironmentGlowStrength,Edit::EnvironmentGlowSize,Edit::EnvironmentGlowThreshold};
            constexpr std::array<const char*,4> labels{"Toggle glow","Change glow strength","Change glow size","Change glow threshold"};
            const auto index=static_cast<std::size_t>(control);field(properties[index],labels[index],changed);
        });
        if(!renderer.glowStatus().empty())ImGui::TextWrapped("%s",renderer.glowStatus().c_str());
    }
    if(ImGui::CollapsingHeader("Ambient Darkness")) {
        ImGui::PushID("Ambient darkness");
        drawAmbientVisibilityControls(value.ambientVisibility,[&](AmbientVisibilityControl control,bool changed) {
            constexpr std::array<Edit,7> properties{Edit::EnvironmentAmbientEnabled,Edit::EnvironmentAmbientStrength,
                Edit::EnvironmentAmbientFloor,Edit::EnvironmentAmbientThreshold,Edit::EnvironmentAmbientTransition,Edit::EnvironmentAmbientShadows,Edit::EnvironmentAmbientEmission};
            field(properties[static_cast<unsigned>(control)],"Change ambient darkness",changed);
        });
        ImGui::PopID();
    }
    ImGui::PopID();
    // Collapsing a section or switching sources removes its active widget.
    if(environmentEdit(state.active) && (!activeFieldDrawn || !ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
}
bool componentHeader(PanelsState& state,const EntityRecord& record,SceneComponentKind component,ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_DefaultOpen) {
    const bool open=ImGui::CollapsingHeader(sceneComponentName(component),flags);
    if(editableSceneComponent(component) && ImGui::BeginPopupContextItem()) {
        if(ImGui::MenuItem("Remove"))state.removeComponent=SceneComponentTarget{record.id,component};
        if(component==SceneComponentKind::Camera && record.cameraFollow && ImGui::MenuItem("Remove Follow"))
            state.removeComponent=SceneComponentTarget{record.id,SceneComponentKind::CameraFollow};
        ImGui::EndPopup();
    }
    return open;
}
bool cameraEdit(Edit edit) {return edit>=Edit::CameraOffset && edit<=Edit::CameraFar;}
void drawCameraSection(SceneDocument& document,PanelsState& state,EntityId cameraId,bool actorShortcut) {
    const auto camera=document.scene().find(cameraId);
    if(!camera || !camera->camera)return;
    ImGui::PushID(static_cast<int>(cameraId>>32));ImGui::PushID(static_cast<int>(cameraId));
    if(!componentHeader(state,*camera,SceneComponentKind::Camera)) {
        if(cameraEdit(state.active))finishEdit(document,state,state.active);
        ImGui::PopID();ImGui::PopID();return;
    }
    if(document.scene().properties().activeCamera==cameraId)ImGui::TextUnformatted("Active Camera");
    else if(ImGui::Button("Set Active Camera")) {finishScenePanelEdit(document);document.setActiveCamera(cameraId);}
    const auto captured=captureCameraTemplate(document.scene(),cameraId,camera->cameraFollow?camera->cameraFollow->target:NoEntity,"");
    if(!captured){ImGui::PopID();ImGui::PopID();return;}
    auto value=*captured;
    if(!state.cameraTemplatesLoaded) {
        state.cameraTemplatesLoaded=true;
        try{state.cameraTemplates=loadCameraTemplates(document.projectRoot());state.cameraTemplatesAvailable=true;}
        catch(const std::exception& error){state.cameraTemplateStatus=error.what();}
    }
    const auto builtIn=defaultPlayerCameraTemplate();
    const auto matches=[&](const CameraTemplate& candidate) {
        return candidate.offset==value.offset && candidate.worldBasis==value.worldBasis && candidate.lens==value.lens;
    };
    const char* currentTemplate=matches(builtIn)?builtIn.name.c_str():"Custom";
    for(const auto& candidate:state.cameraTemplates)if(matches(candidate)){currentTemplate=candidate.name.c_str();break;}
    if(ImGui::BeginCombo("Template",currentTemplate)) {
        const auto item=[&](const CameraTemplate& candidate,int identity) {
            ImGui::PushID(identity);
            if(ImGui::Selectable(candidate.name.c_str(),matches(candidate))) {
                finishScenePanelEdit(document);
                if(updateCameraAuthoring(document,cameraId,candidate,"Apply camera template")) {
                    value=candidate;if(state.cameraTemplatesAvailable)state.cameraTemplateStatus.clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        };
        item(builtIn,-1);
        for(std::size_t index=0;index<state.cameraTemplates.size();++index)item(state.cameraTemplates[index],static_cast<int>(index));
        ImGui::EndCombo();
    }
    ImGui::BeginDisabled(!state.cameraTemplatesAvailable);
    if(ImGui::Button("Save Template")) {
        finishScenePanelEdit(document);state.cameraTemplateName.fill(0);state.cameraTemplateStatus.clear();ImGui::OpenPopup("Save Camera Template");
    }
    ImGui::EndDisabled();
    if(ImGui::BeginPopup("Save Camera Template")) {
        if(ImGui::IsWindowAppearing())ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",state.cameraTemplateName.data(),state.cameraTemplateName.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        const bool save=ImGui::Button("Save");
        if(save || enter) {
            if(auto saved=captureCameraTemplate(document.scene(),cameraId,camera->cameraFollow?camera->cameraFollow->target:NoEntity,state.cameraTemplateName.data())) {
                try{saveCameraTemplate(document.projectRoot(),state.cameraTemplates,std::move(*saved));state.cameraTemplateStatus.clear();ImGui::CloseCurrentPopup();}
                catch(const std::exception& error){state.cameraTemplateStatus=error.what();}
            }
        }
        ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();
        if(!state.cameraTemplateStatus.empty())ImGui::TextWrapped("%s",state.cameraTemplateStatus.c_str());
        ImGui::EndPopup();
    }
    if(!state.cameraTemplateStatus.empty())ImGui::TextWrapped("%s",state.cameraTemplateStatus.c_str());
    drawCameraAuthoringControls(state.cameraControls[cameraId],cameraId,value,[&](CameraAuthoringControl control,bool changed) {
        constexpr std::array properties{Edit::CameraOffset,Edit::CameraRotation,Edit::CameraFov,Edit::CameraNear,Edit::CameraFar};
        constexpr std::array labels{"Change camera offset","Change camera rotation","Change camera FOV","Change camera clipping","Change camera clipping"};
        const auto index=static_cast<std::size_t>(control);const auto property=properties[index];
        if((ImGui::IsItemActivated() || changed) && beginEdit(document,state,property,labels[index]) && changed)
            updateCameraAuthoring(document,cameraId,value,labels[index]);
        if(ImGui::IsItemDeactivated() || (changed && !ImGui::IsItemActive()))finishEdit(document,state,property);
    });
    if(!actorShortcut) {
        ImGui::Separator();
        const auto current=document.scene().find(cameraId);
        const auto target=current && current->cameraFollow?document.scene().find(current->cameraFollow->target):std::nullopt;
        const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
        const auto width=std::max(height,ImGui::GetContentRegionAvail().x-height-gap);
        ImGui::TextUnformatted("Follow Target");
        if(ImGui::Button(target?target->name.c_str():"None (Entity)",{width,height}))ImGui::OpenPopup("Choose follow target");
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload=ImGui::AcceptDragDropPayload(EntityPayload))if(payload->DataSize==sizeof(DraggedEntities)) {
                const auto dragged=*static_cast<const DraggedEntities*>(payload->Data);
                if(dragged.document==&document && dragged.generation==document.scene().generation()) {
                    finishScenePanelEdit(document);document.setCameraFollow(cameraId,dragged.source);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0,gap);if(ImGui::Button("X##camera-target",{height,height})) {finishScenePanelEdit(document);document.setCameraFollow(cameraId,NoEntity);}
        if(ImGui::BeginPopup("Choose follow target")) {
            static ImGuiTextFilter filter;filter.Draw("Search");
            for(const auto& candidate:document.scene().records())if(candidate.id!=cameraId && filter.PassFilter(candidate.name.c_str())) {
                ImGui::PushID(static_cast<int>(candidate.id>>32));ImGui::PushID(static_cast<int>(candidate.id));
                if(ImGui::Selectable(candidate.name.c_str(),target && target->id==candidate.id)) {
                    finishScenePanelEdit(document);
                    if(document.setCameraFollow(cameraId,candidate.id))ImGui::CloseCurrentPopup();
                }
                ImGui::PopID();ImGui::PopID();
            }
            ImGui::EndPopup();
        }
    }
    if(cameraEdit(state.active) && !ImGui::IsAnyItemActive())finishEdit(document,state,state.active);
    ImGui::PopID();ImGui::PopID();
}
std::shared_ptr<const NavigationAgentProfile> inspectorProfile(SceneDocument& document,AssetLibrary& assets,const std::string& id) {
    try {
        if(auto managed=assets.findNavigationAgentProfile(id))return managed;
        return document.navigationAgentProfile(id);
    } catch(...) {return {}; }
}
bool profileAssetField(SceneDocument& document,AssetLibrary& assets,const char* label,const std::string& current,
    std::string* openProfile,const std::function<void(const std::string&)>& assign,bool allowDefault=true) {
    ImGui::PushID(label);ImGui::TextUnformatted(label);
    const auto profile=inspectorProfile(document,assets,current);
    const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const auto openWidth=ImGui::CalcTextSize("Open").x+2*ImGui::GetStyle().FramePadding.x;
    const auto width=std::max(height,ImGui::GetContentRegionAvail().x-openWidth-height-2*gap);
    const auto name=profile?profile->name:current.empty()?std::string("Default Agent"):std::string("Missing Profile");
    bool changed=false;
    if(ImGui::Button((name+"##profile").c_str(),{width,height}))ImGui::OpenPopup("Choose Profile");
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_NAVIGATION_AGENT_PROFILE")) {
            if(payload->DataSize>1 && static_cast<const char*>(payload->Data)[payload->DataSize-1]=='\0') {
                const std::string candidate(static_cast<const char*>(payload->Data),payload->DataSize-1);
                if(inspectorProfile(document,assets,candidate)){assign(candidate);changed=true;}
            }
        }
        ImGui::EndDragDropTarget();
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("%s",current.c_str());
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(!openProfile || !profile);
    if(ImGui::Button("Open",{openWidth,height}) && openProfile)*openProfile=current;
    ImGui::EndDisabled();ImGui::SameLine(0,gap);
    ImGui::BeginDisabled(!allowDefault || current==DefaultNavigationAgentProfileId);
    if(ImGui::Button("X",{height,height})){assign(DefaultNavigationAgentProfileId);changed=true;}
    ImGui::EndDisabled();
    if(ImGui::BeginPopup("Choose Profile")) {
        static ImGuiTextFilter filter;filter.Draw("Search");
        for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::NavigationAgentProfile && filter.PassFilter(asset.name.c_str())) {
            if(ImGui::Selectable(asset.name.c_str(),asset.id==current)){assign(asset.id);changed=true;ImGui::CloseCurrentPopup();}
        }
        ImGui::EndPopup();
    }
    if(!document.navigationAgentProfile(current)) {
        const auto diagnostic=assets.navigationAgentProfileDiagnostic(current);
        if(!diagnostic.empty())ImGui::TextWrapped("%s",diagnostic.c_str());
    }
    ImGui::PopID();return changed;
}
void controllerProfileControls(SceneDocument& document,const EntityRecord& record,AssetLibrary& assets,const NavigationInspectorContext& context) {
    profileAssetField(document,assets,"Agent Profile",record.controller->profile.id,context.openProfile,[&](const std::string& id) {
        finishScenePanelEdit(document);document.edit("Assign agent profile",changeEntity(record.id,[&](auto& entity){entity.controller->profile=AssetRef<NavigationAgentProfile>{id};}));
    });
    if(const auto profile=inspectorProfile(document,assets,record.controller->profile.id)) {
        const auto& value=profile->traversal;
        ImGui::Text("Capsule Height: %.3f m",value.height);ImGui::Text("Capsule Radius: %.3f m",value.radius);
        ImGui::Text("Maximum Slope: %.1f degrees",value.maxSlope);ImGui::Text("Maximum Step: %.3f m",value.maxStep);
    } else ImGui::TextWrapped("Profile unavailable: %s",record.controller->profile.id.c_str());
    if(record.navigation && record.navigation->enabled) {
        const auto records=document.scene().records();const auto volume=std::find_if(records.begin(),records.end(),[](const auto& candidate){return candidate.navigationVolume && candidate.navigationVolume->enabled;});
        if(volume==records.end())ImGui::TextUnformatted("Add a NavigationVolume to prepare navigation.");
        else if(std::none_of(volume->navigationVolume->profiles.begin(),volume->navigationVolume->profiles.end(),[&](const auto& ref){return ref==record.controller->profile;}))
            ImGui::TextUnformatted("Add this Agent Profile to NavigationVolume's Supported Agent Profiles.");
    }
}
void navigationVolumeControls(SceneDocument& document,PanelsState& state,const EntityRecord& record,AssetLibrary& assets,const NavigationInspectorContext& context) {
    if(!componentHeader(state,record,SceneComponentKind::NavigationVolume)) {
        if(state.active>=Edit::VolumeEnabled && state.active<=Edit::VolumeProfiles)finishEdit(document,state,state.active);return;
    }
    auto volume=*record.navigationVolume;
    const bool enabled=ImGui::Checkbox("Enabled##navigation-volume",&volume.enabled);
    propertyEdit(document,state,Edit::VolumeEnabled,"Toggle navigation volume",enabled,changeEntity(record.id,[volume](auto& entity){entity.navigationVolume->enabled=volume.enabled;}));
    const bool dimensions=ImGui::DragFloat3("Dimensions (m)",volume.size.data(),.1f,.001f,100000.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    propertyEdit(document,state,Edit::VolumeSize,"Resize navigation volume",dimensions,changeEntity(record.id,[volume](auto& entity){entity.navigationVolume->size=volume.size;}));
    if(ImGui::TreeNodeEx("Baking",ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto bakeField=[&](const char* label,float NavigationBakeSettings::* member,Edit edit,float speed,float minimum,float maximum) {
            const bool changed=ImGui::DragFloat(label,&(volume.bake.*member),speed,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);
            propertyEdit(document,state,edit,"Change navigation baking",changed,changeEntity(record.id,[volume](auto& entity){entity.navigationVolume->bake=volume.bake;}));
        };
        bakeField("Cell Size (m)",&NavigationBakeSettings::cellSize,Edit::VolumeCellSize,.01f,.01f,10.f);
        bakeField("Cell Height (m)",&NavigationBakeSettings::cellHeight,Edit::VolumeCellHeight,.005f,.005f,10.f);
        bakeField("Contour Simplification",&NavigationBakeSettings::maxSimplificationError,Edit::VolumeSimplification,.05f,0.f,10.f);
        ImGui::TreePop();
    } else if(state.active>=Edit::VolumeCellSize && state.active<=Edit::VolumeSimplification)finishEdit(document,state,state.active);
    if(ImGui::TreeNodeEx("Supported Agent Profiles",ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto assignProfiles=[&](std::vector<AssetRef<NavigationAgentProfile>> values) {
            finishScenePanelEdit(document);document.edit("Change supported agent profiles",changeEntity(record.id,[values=std::move(values)](auto& entity){entity.navigationVolume->profiles=values;}));
        };
        for(std::size_t i=0;i<volume.profiles.size();++i) {
            ImGui::PushID(static_cast<int>(i));
            profileAssetField(document,assets,"Profile",volume.profiles[i].id,context.openProfile,[&](const auto& id) {
                auto values=volume.profiles;
                if(std::none_of(values.begin(),values.end(),[&](const auto& value){return value.id==id;})){values[i]=AssetRef<NavigationAgentProfile>{id};assignProfiles(std::move(values));}
            });
            if(ImGui::SmallButton("Remove")){auto values=volume.profiles;values.erase(values.begin()+static_cast<std::ptrdiff_t>(i));assignProfiles(std::move(values));}
            ImGui::PopID();
        }
        if(ImGui::Button("Add Profile"))ImGui::OpenPopup("Add supported profile");
        if(ImGui::BeginPopup("Add supported profile")) {
            for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::NavigationAgentProfile &&
                std::none_of(volume.profiles.begin(),volume.profiles.end(),[&](const auto& value){return value.id==asset.id;}))
                if(ImGui::Selectable(asset.name.c_str())){auto values=volume.profiles;values.emplace_back(asset.id);assignProfiles(std::move(values));ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        ImGui::TreePop();
    }
    if(context.previewProfile) {
        const auto current=inspectorProfile(document,assets,*context.previewProfile);
        if(ImGui::BeginCombo("Preview Agent Type",current?current->name.c_str():"None")) {
            for(const auto& reference:volume.profiles) {
                const auto profile=inspectorProfile(document,assets,reference.id);
                if(ImGui::Selectable(profile?profile->name.c_str():reference.id.c_str(),reference.id==*context.previewProfile))*context.previewProfile=reference.id;
            }
            ImGui::EndCombo();
        }
        if(context.buildStatus)ImGui::TextWrapped("%s",context.buildStatus(*context.previewProfile).c_str());
    }
}
void startingEffectControls(SceneDocument& document,AssetLibrary& assets,const EntityRecord& record,std::string* openEffect) {
    const auto& starting=record.effects->starting;
    const auto assign=[&](std::vector<AssetRef<GameplayEffectAsset>> values,const char* label) {
        finishScenePanelEdit(document);
        document.edit(label,changeEntity(record.id,[values=std::move(values)](auto& entity){entity.effects->starting=values;}));
    };
    ImGui::TextUnformatted("Starting Effects");
    for(std::size_t index=0;index<starting.size();++index) {
        ImGui::PushID(static_cast<int>(index));
        const auto& id=starting[index].id;
        const auto staged=document.gameplayEffect(id);
        const auto managed=assets.findGameplayEffect(id);
        const auto definition=managed?managed:staged;
        const auto name=managed?assets.displayName(id):staged?staged->name:std::string("Missing Effect");
        const auto height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
        const auto openWidth=ImGui::CalcTextSize("Open").x+ImGui::GetStyle().FramePadding.x*2;
        const auto fieldWidth=std::max(height,ImGui::GetContentRegionAvail().x-openWidth-height*3-gap*4);
        if(ImGui::Button((name+"##effect").c_str(),{fieldWidth,height}))ImGui::OpenPopup("Choose Effect");
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("%s",id.c_str());
        if(ImGui::BeginDragDropTarget()) {
            if(const auto* payload=ImGui::AcceptDragDropPayload("CC_GAMEPLAY_EFFECT")) {
                if(payload->DataSize>1&&static_cast<const char*>(payload->Data)[payload->DataSize-1]=='\0') {
                    const std::string candidate(static_cast<const char*>(payload->Data),payload->DataSize-1);
                    if(assets.findGameplayEffect(candidate)||document.gameplayEffect(candidate)) {
                        auto values=starting;values[index]=AssetRef<GameplayEffectAsset>{candidate};assign(std::move(values),"Assign starting effect");
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine(0,gap);ImGui::BeginDisabled(!openEffect||!definition);
        if(ImGui::Button("Open",{openWidth,height})&&openEffect)*openEffect=id;
        ImGui::EndDisabled();ImGui::SameLine(0,gap);
        if(ImGui::Button("X",{height,height})) {auto values=starting;values.erase(values.begin()+static_cast<std::ptrdiff_t>(index));assign(std::move(values),"Remove starting effect");}
        ImGui::SameLine(0,gap);ImGui::BeginDisabled(index==0);
        if(ImGui::Button("^",{height,height})) {auto values=starting;std::swap(values[index],values[index-1]);assign(std::move(values),"Reorder starting effects");}
        ImGui::EndDisabled();ImGui::SameLine(0,gap);ImGui::BeginDisabled(index+1==starting.size());
        if(ImGui::Button("v",{height,height})) {auto values=starting;std::swap(values[index],values[index+1]);assign(std::move(values),"Reorder starting effects");}
        ImGui::EndDisabled();
        if(ImGui::BeginPopup("Choose Effect")) {
            static ImGuiTextFilter filter;filter.Draw("Search");
            for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::GameplayEffect&&filter.PassFilter(asset.name.c_str()))
                if(ImGui::Selectable(asset.name.c_str(),asset.id==id)) {auto values=starting;values[index]=AssetRef<GameplayEffectAsset>{asset.id};assign(std::move(values),"Assign starting effect");ImGui::CloseCurrentPopup();}
            ImGui::EndPopup();
        }
        if(!definition) {
            const auto diagnostic=assets.gameplayEffectDiagnostic(id);
            ImGui::TextWrapped("%s",diagnostic.empty()?("Effect unavailable: "+id).c_str():diagnostic.c_str());
        }
        ImGui::PopID();
    }
    if(ImGui::Button("Add Effect"))ImGui::OpenPopup("Add Starting Effect");
    if(ImGui::BeginPopup("Add Starting Effect")) {
        static ImGuiTextFilter filter;filter.Draw("Search");
        for(const auto& asset:assets.catalog())if(asset.kind==AssetKind::GameplayEffect&&filter.PassFilter(asset.name.c_str()))
            if(ImGui::Selectable(asset.name.c_str())){auto values=starting;values.emplace_back(asset.id);assign(std::move(values),"Add starting effect");ImGui::CloseCurrentPopup();}
        ImGui::EndPopup();
    }
}
void gameplayControls(SceneDocument& document,PanelsState& state,const EntityRecord& record,SceneComponentKind section,AssetLibrary& assets,const NavigationInspectorContext& navigation,const CollisionLayerNames& collisionNames,const GameplayInspectorContext& gameplay) {
    if(record.effects && section==SceneComponentKind::Effects && componentHeader(state,record,section))
        startingEffectControls(document,assets,record,gameplay.openEffect);
    if(record.tags && section==SceneComponentKind::Tags && componentHeader(state,record,section)) {
        auto value=*record.tags;
        if(drawGameplayTagContainerControl("Tags",value,state.tags[record.id])) {
            finishScenePanelEdit(document);
            document.edit("Change entity tags",changeEntity(record.id,[value=std::move(value)](auto& entity){entity.tags=value;}));
        }
    }
    if(record.enemy && section==SceneComponentKind::Enemy) {
        static_assert(static_cast<unsigned>(Edit::EnemyMaxWait)-static_cast<unsigned>(Edit::EnemyBehaviour)==static_cast<unsigned>(EnemyInspectorControl::MaxWait));
        bool activeDrawn=false;
        if(componentHeader(state,record,SceneComponentKind::Enemy)) {
            auto value=*record.enemy;
            drawEnemyInspectorControls(value,[&](EnemyInspectorControl control,const char* label,bool changed) {
                const auto property=static_cast<Edit>(static_cast<unsigned>(Edit::EnemyBehaviour)+static_cast<unsigned>(control));
                propertyEdit(document,state,property,label,changed,changeEntity(record.id,[value](auto& entity){entity.enemy=value;}));
                activeDrawn|=state.active==property;
            });
        }
        if(!activeDrawn && state.active>=Edit::EnemyBehaviour && state.active<=Edit::EnemyMaxWait)finishEdit(document,state,state.active);
    }
    if(record.playerInput && section==SceneComponentKind::PlayerInput) {
        static_assert(static_cast<unsigned>(Edit::PlayerTiltResponse)-static_cast<unsigned>(Edit::PlayerInputEnabled)==
            static_cast<unsigned>(PlayerInputInspectorControl::TiltResponse));
        bool activeDrawn=false;
        if(componentHeader(state,record,SceneComponentKind::PlayerInput)) {
            auto value=*record.playerInput;
            drawPlayerInputInspectorControls(value,[&](PlayerInputInspectorControl control,const char* label,bool changed) {
                const auto property=static_cast<Edit>(static_cast<unsigned>(Edit::PlayerInputEnabled)+static_cast<unsigned>(control));
                propertyEdit(document,state,property,label,changed,changeEntity(record.id,[value](auto& entity){entity.playerInput=value;}));
                activeDrawn|=state.active==property;
            });
            if(document.scene().properties().playerCharacter==record.id)ImGui::TextUnformatted("Controlled Player");
            else if(ImGui::Button("Make Player")){finishScenePanelEdit(document);document.setPlayerCharacter(record.id);}
        }
        if(!activeDrawn&&state.active>=Edit::PlayerInputEnabled&&state.active<=Edit::PlayerTiltResponse)finishEdit(document,state,state.active);
    }
    if(record.motor && section==SceneComponentKind::Motor) {
        auto value=*record.motor;
        if(componentHeader(state,record,SceneComponentKind::Motor)) {
            const bool enabled=ImGui::Checkbox("Enabled##motor",&value.enabled);
            propertyEdit(document,state,Edit::MotorEnabled,"Toggle motor",enabled,
                changeEntity(record.id,[value](auto& entity){entity.motor=value;}));
        } else if(state.active==Edit::MotorEnabled) {
            finishEdit(document,state,state.active);
        }
    }
    if(record.navigation && section==SceneComponentKind::Navigation) {
        auto value=*record.navigation;
        if(componentHeader(state,record,SceneComponentKind::Navigation)) {
            const bool enabled=ImGui::Checkbox("Enabled##navigation",&value.enabled);
            propertyEdit(document,state,Edit::NavigationEnabled,"Toggle navigation agent",enabled,
                changeEntity(record.id,[value](auto& entity){entity.navigation=value;}));
            if(record.controller)controllerProfileControls(document,record,assets,navigation);
            const bool route=ImGui::Checkbox("Show Route",&value.showRoute);
            propertyEdit(document,state,Edit::CharacterRoute,"Toggle character route",route,
                changeEntity(record.id,[v=value.showRoute](auto& entity){entity.navigation->showRoute=v;}));
        } else if(state.active==Edit::CharacterRoute || state.active==Edit::NavigationEnabled) {
            finishEdit(document,state,state.active);
        }
    }
    if(record.controller && section==SceneComponentKind::Controller) {
        auto value=*record.controller;
        if(componentHeader(state,record,SceneComponentKind::Controller)) {
            if(!record.navigation)controllerProfileControls(document,record,assets,navigation);
            if(drawCollisionMaskControl("Collision Layers",value.collision.layers,collisionNames,~WorldCollisionLayer,
                                        "%s is reserved for shared level collision.")) {
                finishScenePanelEdit(document);document.edit("Change character collision layers",changeEntity(record.id,[value](auto& entity){entity.controller->collision.layers=value.collision.layers;}));
            }
            if(drawCollisionMaskControl("Collision Mask",value.collision.mask,collisionNames,~WorldCollisionLayer,
                                        "Character Controllers always collide with %s shared level collision.")) {
                finishScenePanelEdit(document);document.edit("Change character collision mask",changeEntity(record.id,[value](auto& entity){entity.controller->collision.mask=value.collision.mask;}));
            }
            const bool visible=ImGui::Checkbox("Show in Game",&value.showCollisionInGame);
            propertyEdit(document,state,Edit::CharacterCollisionVisible,"Toggle character collision display",visible,
                changeEntity(record.id,[v=value.showCollisionInGame](auto& entity){entity.controller->showCollisionInGame=v;}));
            const bool color=ImGui::ColorEdit4("Color",value.collisionColor.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_AlphaBar);
            propertyEdit(document,state,Edit::CharacterCollisionColor,"Change character collision color",color,
                changeEntity(record.id,[v=value.collisionColor](auto& entity){entity.controller->collisionColor=v;}));
        } else if(state.active==Edit::CharacterCollisionVisible || state.active==Edit::CharacterCollisionColor) {
            finishEdit(document,state,state.active);
        }
    }
    if(record.lightRig && section==SceneComponentKind::LightRig) {
        auto value=*record.lightRig;bool activeRigDrawn=false;
        if(componentHeader(state,record,SceneComponentKind::LightRig))
            drawActorLightRigControls(value,[&](ActorLightRigControl control,const char* label,bool changed) {
                const auto property=static_cast<Edit>(static_cast<unsigned>(Edit::CharacterRigEnabled)+static_cast<unsigned>(control));
                propertyEdit(document,state,property,label,changed,
                    changeEntity(record.id,[rig=value](auto& entity){entity.lightRig=rig;}));
                activeRigDrawn|=state.active==property;
            },false);
        if(rigEdit(state.active) && (!activeRigDrawn || !ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
    }
    if(record.darknessReveal && section==SceneComponentKind::DarknessReveal) {
        auto value=*record.darknessReveal;
        if(componentHeader(state,record,SceneComponentKind::DarknessReveal,0)) {
            const bool enabled=ImGui::Checkbox("Enabled##character-reveal",&value.enabled);
            propertyEdit(document,state,Edit::CharacterRevealEnabled,"Toggle character darkness reveal",enabled,
                changeEntity(record.id,[value](auto& entity){entity.darknessReveal=value;}));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Restore environmental lighting near this entity. Emits no light.");
            const bool spread=ImGui::DragFloat("Spread (m)",&value.spread,.1f,.01f,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
            propertyEdit(document,state,Edit::CharacterRevealSpread,"Change character lighting spread",spread,
                changeEntity(record.id,[value](auto& entity){entity.darknessReveal=value;}));
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Distance where Darkness Reveal falls to 50%. Independent of the Local World Light's radius.");
        } else if(state.active==Edit::CharacterRevealEnabled||state.active==Edit::CharacterRevealSpread) {
            finishEdit(document,state,state.active);
        }
    }

}
}

namespace {
bool cancelPanelEdit(SceneDocument& document, PanelsState& state, Panel panel, bool allowEdits) {
    if (owner(state.active)!=panel || (allowEdits && !ImGui::IsKeyPressed(ImGuiKey_Escape,false) && !ImGui::GetIO().AppFocusLost)) return false;
    releasePanelColliders(state);
    if(cameraEdit(state.active)){ImGui::ClearActiveID();state.cameraControls={};}
    document.cancel(); state.active=Edit::None; state.nameId=NoEntity;
    for(auto& [id,transform]:state.transforms)if(transform.editing) {
        transform.fields=transform.captured;transform.degrees=transform.capturedDegrees;
        transform.editing=false;transform.refresh=true;
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
        const auto cached=state.transforms.find(state.editingEntity);
        if(cached==state.transforms.end() || !finishInspectorTransform(document,cached->second))return false;
    } else {
        releasePanelColliders(state);
        if (!document.commit()) return false;
    }
    state.active=Edit::None; state.nameId=NoEntity;
    if (transform) { auto& cached=state.transforms[state.editingEntity];cached.editing=false;cached.refresh=true; }
    state.renameId=NoEntity; state.renameFocus=false;
    return true;
}
void drawSceneOutliner(SceneDocument& document,AssetLibrary& assets,bool allowEdits) {
    auto& state=stateFor(document);
    const bool canceled=cancelPanelEdit(document,state,Panel::Outliner,allowEdits);
    if((state.lastSelection!=document.selection().ids || state.lastPrimary!=document.selection().primary) && !state.pendingClick)
        state.anchor=document.selection().primary;
    const bool blocked=!allowEdits || canceled || (document.transactionActive() && owner(state.active)!=Panel::Inspector);
    if(!ImGui::Begin("Outliner")) {
        if(state.active==Edit::InlineName){document.cancel();state.active=Edit::None;state.renameId=NoEntity;}
        ImGui::End();return;
    }
    if(!blocked && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput &&
       !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && ImGui::IsKeyPressed(ImGuiKey_F2,false))
        startRename(document,state,document.selection().primary);
    const auto records=document.scene().records();
    std::unordered_map<EntityId,const EntityRecord*> byId;
    for(const auto& record:records)byId.emplace(record.id,&record);
    std::unordered_map<EntityId,std::vector<EntityId>> children;
    for(const auto& row:sceneOutlinerRows(document.scene()))children[row.parent].push_back(row.entity);
    struct Work {EntityId id{};bool close{};};
    std::vector<Work> pending;
    for(auto it=children[NoEntity].rbegin();it!=children[NoEntity].rend();++it)pending.push_back({*it});
    std::vector<EntityId> visible;
    std::function<void()> action;
    while(!pending.empty()) {
        const auto item=pending.back();pending.pop_back();
        if(item.close){ImGui::TreePop();continue;}
        const auto& record=*byId.at(item.id);
        visible.push_back(record.id);
        const auto id=std::to_string(record.id);
        const bool renaming=state.renameId==record.id;
        const bool ownsRename=renaming && (state.active==Edit::InlineName || !document.transactionActive());
        ImGui::BeginDisabled(!allowEdits || canceled || (blocked && !ownsRename));
        const bool entitySelected=selected(document,record.id);
        const bool leaf=children[record.id].empty();
        auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_OpenOnDoubleClick|
            ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_FramePadding;
        if(entitySelected)flags|=ImGuiTreeNodeFlags_Selected;
        if(leaf)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if(renaming)flags|=ImGuiTreeNodeFlags_AllowOverlap;
        if(state.expand.erase(record.id))ImGui::SetNextItemOpen(true);
        bool open{};
        {
            SceneOutlinerRowColorScope appearance(sceneOutlinerColor(record));
            open=ImGui::TreeNodeEx(id.c_str(),flags,"%s",renaming?"":record.name.c_str());appearance.finish(entitySelected);
        }
        const auto rowMin=ImGui::GetItemRectMin(),rowMax=ImGui::GetItemRectMax();
        const bool hovered=ImGui::IsItemHovered();
        if(hovered && !renaming)ImGui::SetTooltip("%s",kind(record));
        if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() && !blocked && !renaming) {
            finishScenePanelEdit(document);
            const auto& io=ImGui::GetIO();
            if(selected(document,record.id) && !io.KeyShift) {
                state.pendingClick=record.id;state.pendingCtrl=io.KeyCtrl;state.pendingShift=io.KeyShift;
            } else selectRow(document,state,record.id,io.KeyCtrl,io.KeyShift);
        }
        if(!blocked && !renaming && ImGui::BeginDragDropSource()) {
            finishScenePanelEdit(document);state.pendingClick=NoEntity;
            if(!selected(document,record.id))document.select({record.id},record.id);
            const auto* existing=ImGui::GetDragDropPayload();
            if(!existing || !existing->IsDataType(EntityPayload))state.dragged=document.selection().ids;
            const DraggedEntities payload{&document,document.scene().generation(),record.id};
            ImGui::SetDragDropPayload(EntityPayload,&payload,sizeof(payload),ImGuiCond_Once);
            ImGui::Text("%zu selected entities",state.dragged.size());ImGui::EndDragDropSource();
        }
        if(!blocked && !renaming)reparentTarget(document,state,record.id,action,&assets);
        if(!renaming && ImGui::BeginPopupContextItem()) {
            if(!blocked) {
                finishScenePanelEdit(document);
                if(!selected(document,record.id))document.select({record.id},record.id);
                else document.select(document.selection().ids,record.id);
            }
            if(ImGui::MenuItem("Rename","F2"))action=[&document,&state,id=record.id]{startRename(document,state,id);};
            if(ImGui::MenuItem("Duplicate","Ctrl+D"))action=[&document]{document.duplicateSelection();};
            if(ImGui::MenuItem("Delete","Delete"))action=[&document]{document.eraseSelection();};
            if(ImGui::MenuItem("Set Color")) {
                std::vector<SceneOutlinerColorTarget> targets;
                for(const auto entity:document.selection().ids)targets.push_back({entity});
                action=[&document,&state,targets=std::move(targets)]{finishScenePanelEdit(document);requestSceneOutlinerColor(document,state.outlinerColor,targets);};
            }
            ImGui::Separator();
            if(ImGui::MenuItem("Group selection"))action=[&document]{document.groupSelection();};
            bool canUngroup=false;
            for(const auto root:document.scene().topmost(document.selection().ids))
                if(const auto group=document.scene().find(root);group && isSceneGroup(*group) && !document.scene().children(root).empty())canUngroup=true;
            if(ImGui::MenuItem("Ungroup",nullptr,false,canUngroup))action=[&document]{document.ungroupSelection();};
            if(ImGui::MenuItem("Unparent",nullptr,false,record.parent!=NoEntity))action=[&document]{document.reparentSelection(NoEntity);};
            if(record.environment && ImGui::MenuItem("Make active environment",nullptr,false,document.scene().properties().activeEnvironment!=record.id))
                action=[&document,id=record.id]{document.setActiveEnvironment(id);};
            ImGui::Separator();
            if(ImGui::BeginMenu("Add")){addMenu(document,record.id);ImGui::EndMenu();}
            ImGui::EndPopup();
        }
        if(!blocked && state.pendingClick==record.id && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if(hovered && ImGui::GetDragDropPayload()==nullptr)selectRow(document,state,record.id,state.pendingCtrl,state.pendingShift);
            state.pendingClick=NoEntity;
        }
        if(renaming)inlineRename(document,state,record.id,{rowMin.x+ImGui::GetTreeNodeToLabelSpacing(),rowMin.y},rowMax.x-rowMin.x-ImGui::GetTreeNodeToLabelSpacing());
        ImGui::EndDisabled();
        if(open && !leaf) {
            pending.push_back({record.id,true});
            for(auto it=children[record.id].rbegin();it!=children[record.id].rend();++it)pending.push_back({*it});
        }
    }
    state.visible=std::move(visible);
    ImGui::BeginDisabled(blocked);
    const auto remaining=ImGui::GetContentRegionAvail();
    if(ImGui::InvisibleButton("##outliner_background",{std::max(remaining.x,1.f),std::max(remaining.y,1.f)})) {
        finishScenePanelEdit(document);document.select({});state.anchor=NoEntity;
    }
    if(!blocked)reparentTarget(document,state,NoEntity,action,&assets);
    if(ImGui::BeginPopupContextItem("Outliner background")) {
        if(ImGui::BeginMenu("Add")){finishScenePanelEdit(document);addMenu(document);ImGui::EndMenu();}
        ImGui::EndPopup();
    }
    ImGui::EndDisabled();
    if(action && !blocked)action();
    drawSceneOutlinerColorPopup(document,state.outlinerColor);
    if(ImGui::IsMouseReleased(ImGuiMouseButton_Left))state.pendingClick=NoEntity;
    state.lastSelection=document.selection().ids;state.lastPrimary=document.selection().primary;
    ImGui::End();
}

void drawSceneInspector(SceneDocument& document, AssetLibrary& assets, Renderer& renderer, bool allowEdits,
                        std::uint64_t* playParticle,std::string* openParticle,QuaderBridge* quader,std::string* openMaterial,const NavigationInspectorContext& navigation,const std::array<std::string,32>* collisionLayerNames,const GameplayInspectorContext& gameplay) {
    auto& state=stateFor(document);
    const auto& collisionNames=collisionLayerNames?*collisionLayerNames:defaultCollisionLayerNames();
    const bool canceled=cancelPanelEdit(document,state,Panel::Inspector,allowEdits);
    if(!ImGui::Begin("Inspector")) {
        releasePanelColliders(state);
        if(owner(state.active)==Panel::Inspector){document.cancel();state.active=Edit::None;state.nameId=NoEntity;}
        ImGui::End();return;
    }
    const bool blocked=!allowEdits || canceled || (document.transactionActive() && owner(state.active)!=Panel::Inspector);
    ImGui::BeginDisabled(blocked);
    std::function<void()> action;
    const auto drawSections=[&](const EntityRecord& record,bool ownedCamera=false) {
        state.inspectorEntity=record.id;
        ImGui::PushID(static_cast<int>(record.id>>32));ImGui::PushID(static_cast<int>(record.id));
        for(const auto section:sceneInspectorSections(record)) {
            if(section==SceneComponentKind::Transform && !ownedCamera) {
                const InspectorTransformTransactions transactions{
                    [&](unsigned field,const char* label){return beginEdit(document,state,static_cast<Edit>(static_cast<unsigned>(Edit::PositionX)+field),label,record.id);},
                    [&](unsigned field){finishEdit(document,state,static_cast<Edit>(static_cast<unsigned>(Edit::PositionX)+field));}};
                drawInspectorTransform(document,state.transforms[record.id],record,transactions);
            }
            gameplayControls(document,state,record,section,assets,navigation,collisionNames,gameplay);
            if(section==SceneComponentKind::NavigationVolume && record.navigationVolume)navigationVolumeControls(document,state,record,assets,navigation);
            if(section==SceneComponentKind::Camera)drawCameraSection(document,state,record.id,ownedCamera);
            if(section==SceneComponentKind::Quader && record.quader && quader && componentHeader(state,record,SceneComponentKind::Quader))drawQuaderInspector(document,record,*quader);
            if(section==SceneComponentKind::Collider && record.collider) {
                if(componentHeader(state,record,SceneComponentKind::Collider)) {
                    auto value=*record.collider;
                    const auto originalMode=value.mode;
                    if(ImGui::BeginCombo("Mode",value.mode==ColliderMode::Solid?"Solid":"Trigger")) {
                        if(ImGui::Selectable("Solid",value.mode==ColliderMode::Solid))value.mode=ColliderMode::Solid;
                        ImGui::BeginDisabled(value.shape==ColliderShape::Trimesh || record.controller.has_value());
                        if(ImGui::Selectable("Trigger",value.mode==ColliderMode::Trigger))value.mode=ColliderMode::Trigger;
                        ImGui::EndDisabled();
                        if((value.shape==ColliderShape::Trimesh || record.controller) && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))
                            ImGui::SetTooltip("Triggers require a primitive Collider on a separate entity.");
                        ImGui::EndCombo();
                    }
                    if(value.mode!=originalMode) {
                        value.collision=collisionFilterForMode(value.collision,value.mode);
                        finishScenePanelEdit(document);document.edit("Change collider mode",changeEntity(record.id,[value](auto& entity){entity.collider->mode=value.mode;entity.collider->collision=value.collision;}));
                    }
                    if(drawCollisionMaskControl("Collision Layers",value.collision.layers,collisionNames,~WorldCollisionLayer,
                                                value.mode==ColliderMode::Solid?
                                                "Solid Colliders always belong to %s shared level collision.":
                                                "%s is reserved for shared level collision.")) {
                        finishScenePanelEdit(document);document.edit("Change collider collision layers",changeEntity(record.id,[value](auto& entity){entity.collider->collision.layers=value.collision.layers;}));
                    }
                    if(drawCollisionMaskControl("Collision Mask",value.collision.mask,collisionNames,value.mode==ColliderMode::Solid?0u:AllCollisionLayers,
                                                "Solid Colliders use every layer for shared level collision.")) {
                        finishScenePanelEdit(document);document.edit("Change collider collision mask",changeEntity(record.id,[value](auto& entity){entity.collider->collision.mask=value.collision.mask;}));
                    }
                    static_assert(static_cast<unsigned>(Edit::ColliderColor)-static_cast<unsigned>(Edit::ColliderShape)==static_cast<unsigned>(ColliderControl::Color));
                    const auto property=[](ColliderControl control){return static_cast<Edit>(static_cast<unsigned>(Edit::ColliderShape)+static_cast<unsigned>(control));};
                    const ColliderInspectorTransactions transactions{
                        [&](ColliderControl control,const char* label){return beginEdit(document,state,property(control),label);},
                        [&](ColliderControl control){finishEdit(document,state,property(control));}};
                    auto current=record;current.collider=value;
                    drawColliderInspector(document,state.colliders[record.id],current,{assets.catalog(),[&assets](const std::string& id){return assets.displayName(id);}},transactions);
                } else if(state.active>=Edit::ColliderShape&&state.active<=Edit::ColliderColor) {
                    releaseColliderInspector(state.colliders[record.id]);finishEdit(document,state,state.active);
                }
            }
            if(section==SceneComponentKind::Directional && record.directional) {
                if(componentHeader(state,record,SceneComponentKind::Directional))lightControls(document,state,record.id,true);
                else if((state.active>=Edit::DirectionEnabled && state.active<=Edit::DirectionNormalBias) ||
                        (state.active>=Edit::DirectionReceivers && state.active<=Edit::DirectionRevealStrength))finishEdit(document,state,state.active);
            }
            if(section==SceneComponentKind::Point && record.point) {
                if(componentHeader(state,record,SceneComponentKind::Point))lightControls(document,state,record.id,false);
                else if((state.active>=Edit::PointEnabled && state.active<=Edit::PointAttenuation) ||
                        (state.active>=Edit::PointReceivers && state.active<=Edit::PointRevealStrength))finishEdit(document,state,state.active);
            }
            if (section==SceneComponentKind::Model && record.model && componentHeader(state,record,SceneComponentKind::Model)) {
                auto castShadows=record.castShadows,receiveShadows=record.receiveShadows;
                const bool castChanged=ImGui::Checkbox("Cast Shadows",&castShadows);
                propertyEdit(document,state,Edit::ModelCastShadows,"Toggle model shadow casting",castChanged,
                    changeEntity(record.id,[castShadows](auto& entity){entity.castShadows=castShadows;}));
                const bool receiveChanged=ImGui::Checkbox("Receive Shadows",&receiveShadows);
                propertyEdit(document,state,Edit::ModelReceiveShadows,"Toggle model shadow receiving",receiveChanged,
                    changeEntity(record.id,[receiveShadows](auto& entity){entity.receiveShadows=receiveShadows;}));
                auto fadeWhenOccluding=record.fadeWhenOccluding;
                const bool fadeChanged=ImGui::Checkbox("Fade when blocking player",&fadeWhenOccluding);
                propertyEdit(document,state,Edit::ModelFadeWhenOccluding,"Toggle player occlusion fading",fadeChanged,
                    changeEntity(record.id,[fadeWhenOccluding](auto& entity){entity.fadeWhenOccluding=fadeWhenOccluding;}));
                auto scopes=record.lightingScopes;
                bool activeScopeDrawn=false;
                const auto scopeEdit=[&](Edit property,const char* label,bool changed) {
                    propertyEdit(document,state,property,label,changed,changeEntity(record.id,[scopes](auto& entity){entity.lightingScopes=scopes;}));
                    activeScopeDrawn|=state.active==property;
                };
                if(ImGui::TreeNode("Advanced Lighting")) {
                    scopeEdit(Edit::ModelDirectReceivers,"Change model direct-light scope",drawLightingMaskControl("Direct Lighting",scopes.directReceiverMask));
                    scopeEdit(Edit::ModelCasterScope,"Change model shadow-caster scope",drawLightingMaskControl("Shadow Casting",scopes.shadowCasterMask));
                    scopeEdit(Edit::ModelAmbientReceivers,"Change model ambient scope",drawLightingMaskControl("Ambient Darkness",scopes.ambientReceiverMask));
                    ImGui::TreePop();
                }
                if(state.active>=Edit::ModelDirectReceivers && state.active<=Edit::ModelAmbientReceivers &&
                   (!activeScopeDrawn || !ImGui::IsAnyItemActive()))finishEdit(document,state,state.active);
                modelAnimationControls(document,state,record,assets);
                modelInformation(document,state,record,assets,renderer,openMaterial);
            } else if(section==SceneComponentKind::Model && record.model && state.active>=Edit::ModelCastShadows && state.active<=Edit::ModelAnimationStart)finishEdit(document,state,state.active);
            if(section==SceneComponentKind::Particle && record.particle && componentHeader(state,record,SceneComponentKind::Particle)) {
                auto assigned=record.particle->id;
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
                if(changed) { auto next=record;next.particle=AssetRef<ParticleEffectAsset>{assigned};document.updateEntity(record.id,next,"Assign particle effect"); }
                ImGui::BeginDisabled(!effect);
                if(ImGui::Button("Play Effect") && playParticle) *playParticle=record.id;
                ImGui::SameLine();if(ImGui::Button("Edit Effect") && openParticle) *openParticle=resolved;
                ImGui::EndDisabled();
                const auto diagnostic=assets.particleDiagnostic(assigned);
                if(!diagnostic.empty()) ImGui::TextWrapped("%s",diagnostic.c_str());
            }
            if(section==SceneComponentKind::Environment && record.environment) {
                if(componentHeader(state,record,SceneComponentKind::Environment))
                    sceneEnvironmentControls(document,state,record,assets,renderer);
                else if(environmentEdit(state.active))finishEdit(document,state,state.active);
            }
        }
        ImGui::PopID();ImGui::PopID();
    };
    if(const auto record=document.scene().find(document.selection().primary)) {
        state.inspectorEntity=record->id;
        drawName(document,state,*record);
        auto visible=record->visible;
        const bool visibilityChanged=ImGui::Checkbox("Visible",&visible);
        propertyEdit(document,state,Edit::Visible,"Toggle entity visibility",visibilityChanged,
            changeEntity(record->id,[visible](auto& entity){entity.visible=visible;}));
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Show this entity and its descendants.");
        drawSections(*record);
        const auto parts=sceneEntityParts(document.scene(),record->id);
        for(const auto partId:parts)if(partId!=record->id)
            if(const auto part=document.scene().find(partId);part && part->camera)drawSections(*part,true);
        if(record->actor) {
            const bool visualsOpen=ImGui::CollapsingHeader("Visuals",ImGuiTreeNodeFlags_DefaultOpen);
            if(ImGui::BeginPopupContextItem()) {
                if(ImGui::MenuItem("Remove Visual Ownership"))state.removeComponent=SceneComponentTarget{record->id,SceneComponentKind::Actor};
                ImGui::EndPopup();
            }
            if(visualsOpen) {
                ImGui::PushID("Actor visuals");
                if(ImGui::Button("Add Model..."))ImGui::OpenPopup("Choose visual model");
                if(!blocked)reparentTarget(document,state,record->id,action,&assets,true);
                if(ImGui::BeginPopup("Choose visual model")) {
                    static ImGuiTextFilter filter;filter.Draw("Search");
                    for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Model && filter.PassFilter(assets.displayName(entry.id).c_str())) {
                        ImGui::PushID(entry.id.c_str());
                        if(ImGui::Selectable(assets.displayName(entry.id).c_str())) {
                            const auto asset=entry.id,name=assets.displayName(entry.id);const auto owner=record->id;
                            action=[&document,asset,name,owner]{finishScenePanelEdit(document);document.createModels({ModelPlacement{asset,name,Mat4{}}},owner);document.select({owner},owner);};
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
                for(const auto partId:parts)if(partId!=record->id)if(const auto part=document.scene().find(partId);part && !part->camera) {
                    ImGui::PushID(static_cast<int>(partId>>32));ImGui::PushID(static_cast<int>(partId));
                    const bool open=ImGui::CollapsingHeader(part->name.c_str(),ImGuiTreeNodeFlags_DefaultOpen);
                    if(ImGui::BeginPopupContextItem()) {
                        if(ImGui::MenuItem("Remove")) {
                            const auto owner=record->id;
                            action=[&document,partId,owner]{finishScenePanelEdit(document);document.select({partId},partId);document.eraseSelection();document.select({owner},owner);};
                        }
                        ImGui::EndPopup();
                    }
                    if(open) {
                        state.inspectorEntity=partId;
                        auto partVisible=part->visible;
                        const bool partVisibilityChanged=ImGui::Checkbox("Visible",&partVisible);
                        propertyEdit(document,state,Edit::Visible,"Toggle visual visibility",partVisibilityChanged,
                            changeEntity(partId,[partVisible](auto& entity){entity.visible=partVisible;}));
                        drawSections(*part);
                    } else if(state.editingEntity==partId && document.transactionActive())finishScenePanelEdit(document);
                    ImGui::PopID();ImGui::PopID();
                }
            } else if(document.transactionActive() && std::any_of(parts.begin(),parts.end(),[&](const auto id) {
                const auto part=document.scene().find(id);
                return id!=record->id && id==state.editingEntity && part && !part->camera;
            }))finishScenePanelEdit(document);
        }
    } else ImGui::TextUnformatted("No selection");
    state.inspectorEntity=NoEntity;
    ImGui::EndDisabled();
    if(state.removeComponent && !blocked) {
        const auto target=*state.removeComponent;state.removeComponent.reset();
        finishScenePanelEdit(document);removeSceneComponent(document,target.entity,target.component);
    }
    if(action && !blocked)action();
    ImGui::End();
}
} // namespace crimson
