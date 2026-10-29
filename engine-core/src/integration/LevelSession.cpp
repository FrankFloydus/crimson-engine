#include "LevelSession.h"
#include "native_geometry_sources.hpp"
#include "scene/scene_document.hpp"
#include "LevelNavigationPreview.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "assets/MaterialGraphReferences.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "crimson-renderer/Renderer.h"
#include "editor/CollisionResources.h"
#include "editor/AssetPreviews.h"
#include "editor/DecalResources.h"
#include "editor/DecalPreview.h"
#include "editor/EditorRenderFrame.h"
#include "editor/SceneParticles.h"
#include "editor/ViewportSelection.h"
#include "editor/ViewportContextMenu.h"
#include "editor/ResourceRuntime.h"
#include "editor/ShaderRuntimePaths.h"
#include "decals/DecalTransform.h"
#include "decals/DecalResolve.h"
#include "resources/FileWatcher.h"
#include "resources/Paths.h"
#include "runtime/GameInput.h"
#include "runtime/PlaySession.h"
#include "runtime/SceneAnimations.h"
#include "runtime/SceneLights.h"
#include <ShaderSourceBaseline.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <unordered_map>
#include <utility>

namespace crimson::integration {
namespace {
std::string pathUtf8(const std::filesystem::path& value) {
    const auto bytes=value.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
}
struct LevelSession::Impl {
    std::filesystem::path root,runtimeRoot;
    NativeSceneProjection native_projection;
    bool native_projected{};
    std::shared_ptr<const NativeSceneRenderBindings> native_bindings;
    std::uint64_t native_binding_scene{},native_binding_pose{},native_binding_source{};
    std::vector<EntityRecord> play_collisions;
    ResourceManager resources;
    AssetLibrary assets;
    SceneDocument document;
    PlaySession play;
    GameInput input;
    CollisionResources collisions;
    DecalResources decals;
    std::shared_ptr<const ModelingWorldSnapshot> modeling_world,play_modeling_world;
    std::shared_ptr<const ExternalMeshSnapshot> native_mesh_sources;
    SceneParticles particles;
    SceneAnimations animations;
    SceneLights lights;
    std::unique_ptr<FileWatcher> watcher;
    LevelNavigationPreview navigation;
    std::string message,environmentMap;
    shared::Renderer* renderer{};
    std::unique_ptr<EditorResources> prepared;
    std::shared_ptr<const PreparedRenderFrame> lastPrepared;
    bool materialReady{},modeling_current{true},scene_lighting{};
    std::unique_ptr<AssetPreviews> previews;
    std::function<void(const std::string&,const DecodedImage&)> previewPublisher;
    std::function<bool(const std::string&)> previewAvailable;
    std::unique_ptr<ResourceRuntime> resourceRuntime;
    std::vector<std::filesystem::path> fileChanges;
    ResourceHandle<PreparedEnvironment> environment;
    struct MaterialPreviewPacket {MaterialAsset material;ModelMaterialTextures bindings;};
    std::shared_ptr<const MaterialPreviewPacket> materialPreview;
    std::string materialPreviewKey;
    std::unique_ptr<MaterialGraphPrograms> node_preview_programs;
    std::shared_ptr<const MaterialPreviewPacket> node_preview;
    std::string node_preview_key;
    DecalPreviewGeometry previewDecalGeometry;
    std::shared_ptr<const PreparedDecalMaterial> previewDecalMaterial;
    std::shared_ptr<const DecalPatchData> previewDecalSource,previewDecalTransformed;
    Mat4 previewDecalTransform;
    std::string previewMaterialId;
    ResourceHandle<PreparedEnvironment> previewEnvironment;
    ResourceHandle<PreparedEnvironment> documentEnvironment;
    std::array<std::uint64_t,3> documentEnvironmentRevisions{};
    DecalPreviewGeometry documentDecalGeometry;
    std::string previewEnvironmentMap;
    std::uint64_t previewEnvironmentRevision{};
    std::uint64_t environmentRevision{};
    double editSeconds{},realSeconds{},previewDelta{};
    const std::chrono::steady_clock::time_point clockStart{std::chrono::steady_clock::now()};
    bool skipTime{true};
    bool playPending{};
    bool vertexPaintEnabled{},vertexPaintHeld{},vertexPaintSubdivisions{};
    std::optional<PlayStartPoint> playStart;
    std::uint64_t playGeneration{},playRevision{};
    struct ViewportRequest {
        std::string asset;
        std::vector<std::string> models;
        AssetKind kind{AssetKind::Model};
        Camera camera;
        Vec2 pixel,size;
        PickRequest pick;
        std::uint64_t document{};
        std::optional<PickResult> hit;
        EntityId replaceEntity{};
        bool selection{},toggle{},append{},submitted{},context{};
    };
    std::optional<ViewportRequest> viewportRequest;
    ViewportContextMenu contextMenu;
    std::optional<LevelViewportContext> viewportContext;
    std::uint64_t pickSerial{};
    std::function<bool(const PickRequest&)> pick_requester;
    EntityId armedDecal{},restartParticle{};

    Impl(std::filesystem::path project,std::filesystem::path runtime)
        :root(std::filesystem::absolute(std::move(project)).lexically_normal()),runtimeRoot(runtime.empty()?root:std::move(runtime)),
        assets(root,&resources),document(root),play(root),collisions(assets,resources),decals(assets,resources),
        watcher(std::make_unique<FileWatcher>(shaderRuntimeWatchedRoots(root,{}))),navigation(resources,root) {
        play.setResourceManager(resources);
        play.setParticleDefinitions([this](const std::string& id){return ParticleDefinitionVersion{assets.findParticle(id),assets.particleRevision(id),assets.particleDiagnostic(id)};});
        play.setCollisionMeshDefinitions([this](const std::string& id){return collisions.query(id);});
        play.setAgentProfiles([this](const std::string& id){auto value=assets.findNavigationAgentProfile(id);return value?value:document.navigationAgentProfile(id);});
        play.setGameplayEffects([this](const std::string& id){
            auto value=assets.findGameplayEffect(id);if(!value)value=document.gameplayEffect(id);
            return EffectDefinitionSnapshot{std::move(value),assets.gameplayEffectRevision(id),assets.gameplayEffectDiagnostic(id)};
        });
        play.setRagdollDefinitions([this](const std::string& id){return assets.findRagdoll(id);},
            [this](const std::string& id){return assets.findModel(id);},[this](const std::string& id){return assets.findAnimation(id);});
        SceneDocument empty(root);if(!document.resetTransientFrom(empty))throw std::runtime_error(document.status());
    }
    ~Impl(){input.cancel();play.stop();if(watcher)watcher->stop();if(resourceRuntime)resourceRuntime->stop();resources.stop();assets.setMaterialGraphPrograms(nullptr);previews.reset();prepared.reset();resourceRuntime.reset();}
    NavigationAgentProfileQuery profiles() {
        return [this](const std::string& id){
            if(play.playing())return play.acceptedAgentProfile(id);
            auto value=assets.findNavigationAgentProfile(id);return value?value:document.navigationAgentProfile(id);
        };
    }
    void resetPresentation() {
        lastPrepared.reset();materialReady=false;
        viewportRequest.reset();contextMenu.cancel();viewportContext.reset();armedDecal=restartParticle=NoEntity;if(renderer)renderer->cancelPicking();
        input.cancel();play.stop();playPending=false;playStart.reset();particles=SceneParticles{};animations.clear();lights.clear();
        decals.clear();collisions.clear();if(prepared)prepared->clear();skipTime=true;editSeconds=0;
    }
    std::vector<EntityRecord> modeling_collisions()const{
        if(!modeling_world)return {};if(!native_projected||modeling_world->native_collisions.empty())return modeling_world->collision_records;
        std::unordered_map<EntityId,std::vector<const NativeModelingCollision*>> groups;
        for(const auto& collision:modeling_world->native_collisions)if(collision.index<modeling_world->collision_records.size())if(const auto id=native_projection.entity_id(collision.entity_uuid))if(const auto entity=document.scene().find(id);entity&&!entity->collider)groups[id].push_back(&collision);
        std::vector<EntityRecord> records;for(const auto& [id,parts]:groups){EntityRecord record;record.id=id;record.collider.emplace();record.collider->shape=ColliderShape::Trimesh;
            const auto& first=modeling_world->collision_records[parts.front()->index].collider->mesh;
            if(parts.size()==1&&parts.front()->entity_from_local==Mat4{})record.collider->mesh=first;
            else{std::vector<Vec3> positions;std::vector<std::uint32_t> indices;for(const auto* part:parts){const auto& mesh=*modeling_world->collision_records[part->index].collider->mesh;const auto offset=static_cast<std::uint32_t>(positions.size());for(const auto& point:mesh.positions)positions.push_back(math::transformPoint(part->entity_from_local,point));for(const auto index:mesh.indices)indices.push_back(offset+index);}record.collider->mesh=makeCollisionMesh(std::move(positions),std::move(indices));}records.push_back(std::move(record));}return records;
    }
    void processViewport(std::uint32_t frame) {
        if(!viewportRequest||!renderer)return;
        auto& pending=*viewportRequest;
        const bool current=!play.playing()&&!document.transactionActive()&&pending.document==document.documentGeneration()&&
            pending.pick.generation==document.scene().generation()&&pending.pick.revision==document.scene().revision();
        if(!current){viewportRequest.reset();renderer->cancelPicking();return;}
        if(!pending.hit)if(auto hit=renderer->pollPick(frame);hit&&hit->serial==pending.pick.serial&&hit->generation==pending.pick.generation&&hit->revision==pending.pick.revision) {
            if(pending.context){contextMenu.resolve(*hit);if(contextMenu.needsPick()){pending.submitted=false;pending.pick=contextMenu.request(++pickSerial);}else if(contextMenu.completed())pending.hit=std::move(*hit);}
            else if(hit->surfacePending){pending.submitted=false;pending.pick.serial=++pickSerial;}
            else pending.hit=std::move(*hit);
        }
        if(!pending.hit)return;
        const auto hit=*pending.hit;
        try {
            if(pending.context) {
                viewportContext=LevelViewportContext{pending.camera,contextMenu.position(),contextMenu.entity(),pending.document,pending.pick.generation,pending.pick.revision};contextMenu.cancel();
            }else if(pending.selection) {
                auto entity=hit.entityId;
                if(!entity)if(const auto ray=pending.camera.screenRay(pending.pixel[0],pending.pixel[1],ViewportRegion{0,0,pending.size[0],pending.size[1]}))
                    entity=pickCollisionShape(document.scene(),*ray,pending.camera.farPlane,[this](const std::string& id){return collisions.query(id);},profiles()).value_or(NoEntity);
                (void)selectViewportHit(document,entity,pending.toggle,pending.append);
            }else if(pending.kind==AssetKind::Material) {
                const auto record=document.scene().find(hit.entityId);if(!record||(!record->model&&!record->nativeGeometry)||hit.materialSlot==UINT32_MAX)throw std::runtime_error("Drop the material on a visible mesh.");
                if(record->nativeGeometry){
                    const auto material=assets.findMaterial(pending.asset);const auto uuid=native_projection.entity_uuid(hit.entityId);
                    const auto slot=modeling_world?native_material_source_slot(*modeling_world,uuid,hit.sourceInstanceId,hit.materialSlot):std::nullopt;
                    const NativeModelingInstance* source=nullptr;if(modeling_world)for(const auto& entry:modeling_world->native_instances)if(entry.entity_uuid==uuid&&entry.source_instance_id==hit.sourceInstanceId){source=&entry;break;}
                    if(!material||!slot||!source||!modeling_world->render||source->index>=modeling_world->render->shadowInstances.size())throw std::runtime_error("The native mesh surface changed; drop the material again.");
                    const auto model=modeling_world->render->shadowInstances[source->index].model.asset;
                    std::string diagnostic;if(!model||!native_material_can_apply(*material,*model,hit.materialSlot,assets,diagnostic))throw std::runtime_error(diagnostic.empty()?"The target native mesh is unavailable.":diagnostic);
                    auto after=*record;setModelMaterialOverride(after.modelMaterials,*slot,AssetRef<MaterialAsset>{pending.asset});
                    if(!document.updateEntity(after.id,after,"Assign material slot"))throw std::runtime_error(document.status());
                }else{
                const auto model=assets.findModel(record->model->id,record->primitive);const auto material=assets.findMaterial(pending.asset);
                if(!model||hit.materialSlot>=model->materials.size()||!material||!modelMaterialCompatible(*material))throw std::runtime_error("The target mesh or Surface material is unavailable.");
                auto assignments=record->modelMaterials;setModelMaterialOverride(assignments,hit.materialSlot,AssetRef<MaterialAsset>{pending.asset});
                (void)assets.resolvedModelMaterials(*model,assignments);
                auto after=*record;after.modelMaterials=std::move(assignments);if(!document.updateEntity(after.id,after,"Assign material slot"))throw std::runtime_error(document.status());
                }
            }else if(pending.kind==AssetKind::Decal) {
                if(!hit.entityId||!hit.surfaceValid)throw std::runtime_error("Drop the decal on a visible mesh surface.");
                const auto placed=decals.resolvePlacement(document.scene(),hit.entityId,hit.drawIndex,hit.primitive,hit.resourceRevision,hit.worldPosition,hit.sourceInstanceId);
                if(placed.failed)throw std::runtime_error(placed.diagnostic);
                if(!placed.ready){message=placed.diagnostic.empty()?"Preparing decal surface…":placed.diagnostic;return;}
                SceneDecal value;value.asset=AssetRef<DecalAsset>{pending.asset};value.seed=placed.seed;
                if(pending.replaceEntity) {
                    auto target=document.scene().find(pending.replaceEntity);const auto oldWorld=document.scene().worldTransform(pending.replaceEntity);
                    if(!target||!target->decal||!oldWorld)throw std::runtime_error("The decal placement target changed.");
                    value=*target->decal;const auto previousSeed=value.seed;value.seed=placed.seed;
                    Vec3 normal=math::transformDirection(*oldWorld,{0,0,1});
                    if(const auto receiver=document.scene().worldTransform(previousSeed.receiver))if(const auto normals=math::normalMatrix(*receiver))normal=math::transformDirection(*normals,previousSeed.localNormal);
                    const auto parent=target->parent?document.scene().worldTransform(target->parent):std::optional{Mat4{}};
                    const auto inverse=parent?math::inverse(*parent):std::nullopt;if(!inverse)throw std::runtime_error("The decal parent transform is singular.");
                    target->local=*inverse*crimson::decals::reanchorDecalFrame(*oldWorld,normal,placed.frame);target->decal=value;
                    if(!document.updateEntity(target->id,*target,"Place decal on surface"))throw std::runtime_error(document.status());
                }else if(!document.createDecal(value,assets.displayName(pending.asset),placed.frame))throw std::runtime_error(document.status());
            }else {
                Vec3 position=pending.camera.target;
                if(hit.surfaceValid)position=hit.worldPosition;
                else if(const auto ray=pending.camera.screenRay(pending.pixel[0],pending.pixel[1],ViewportRegion{0,0,pending.size[0],pending.size[1]}))
                    if(const auto point=math::intersectPlane(*ray,pending.camera.target,-pending.camera.forward()))position=*point;
                if(pending.kind==AssetKind::Model) {
                    if(!pending.models.empty()){std::vector<ModelPlacement> placements;for(const auto& id:pending.models){if(!assets.findModel(id)){if(assets.modelLoading(id)){message="Preparing dropped models…";return;}throw std::runtime_error(assets.modelDiagnostic(id));}placements.push_back({id,assets.displayName(id),math::translation(position)});}document.createModels(placements);}
                    else {
                    if(!assets.findModel(pending.asset)){if(assets.modelLoading(pending.asset)){message="Preparing dropped model…";return;}throw std::runtime_error(assets.modelDiagnostic(pending.asset));}
                    document.createModel(pending.asset,assets.displayName(pending.asset),math::translation(position));
                    }
                }else if(pending.kind==AssetKind::Particle)document.createParticle(pending.asset,assets.displayName(pending.asset),math::translation(position));
            }
            message.clear();
        }catch(const std::exception& error){message=error.what();}
        viewportRequest.reset();renderer->cancelPicking();
    }
    void prepareEnvironment(const SceneEnvironment& value) {
        const auto map=value.environment.source==EnvironmentSource::Hdri?assets.references().resolveEnvironment(value.environment.map):std::string{};
        if(!environment||environmentMap!=map) {
            environmentMap=map;environmentRevision=0;ResourceRequestOptions options;options.variant="ibl";
            if(!map.empty())options.dependencies={resolveAssetPath(root,map)};
            const auto project=root;
            environment=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{map.empty()?StudioEnvironmentId:map},[project,map](std::stop_token token){
                if(token.stop_requested())throw std::runtime_error("Environment preparation canceled.");
                auto value=prepareEnvironmentCpu(project,map);std::size_t bytes=value->data.diffuse.rgba.size()*sizeof(float)+value->data.brdf.size()*sizeof(float);
                for(const auto& mip:value->data.specular)bytes+=mip.rgba.size()*sizeof(float);
                return ResourceLoadResult<PreparedEnvironment>{value,bytes,value->key,{}};
            },std::move(options));
        }
        if(const auto snapshot=environment.snapshot())if(snapshot.revision()!=environmentRevision&&renderer->publishEnvironment(*snapshot))environmentRevision=snapshot.revision();
    }
};
LevelSession::LevelSession(std::filesystem::path root,std::filesystem::path runtime):impl_(std::make_unique<Impl>(std::move(root),std::move(runtime))){}
LevelSession::~LevelSession()=default;
AssetLibrary& LevelSession::assets(){return impl_->assets;}
ResourceManager& LevelSession::resources(){return impl_->resources;}
void LevelSession::setModelingWorld(std::shared_ptr<const ModelingWorldSnapshot> value,bool current){impl_->modeling_world=std::move(value);impl_->modeling_current=current;}
void LevelSession::setSceneLighting(bool enabled){auto& state=*impl_;if(state.scene_lighting==enabled)return;state.scene_lighting=enabled;if(state.native_bindings){auto next=std::make_shared<NativeSceneRenderBindings>(*state.native_bindings);next->scene_lighting=enabled;state.native_bindings=std::move(next);}}
SceneDocument& LevelSession::document(){return impl_->document;}
const SceneDocument& LevelSession::document()const{return impl_->document;}
SceneDocument& LevelSession::editingDocument(){return impl_->play.playing()?impl_->play.document():impl_->document;}
const Scene& LevelSession::presentationScene()const{return impl_->play.playing()?impl_->play.presentationScene():impl_->document.scene();}
GameInput& LevelSession::gameInput(){return impl_->input;}
PlaySession& LevelSession::runtime(){return impl_->play;}
const PlaySession& LevelSession::runtime()const{return impl_->play;}
const std::filesystem::path& LevelSession::projectRoot()const{return impl_->root;}
const std::string& LevelSession::status()const{return impl_->message;}
std::uint64_t LevelSession::revision()const{return impl_->play.playing()?impl_->play.document().scene().revision():impl_->document.scene().revision();}
bool LevelSession::publishNativeScene(const quader::SceneDocument& native,std::string scene_uuid,std::uint64_t source_revision){
    auto& state=*impl_;if(state.document.transactionActive())return false;
    if(state.native_projected&&state.native_projection.scene_uuid()==scene_uuid&&state.native_projection.source_revision()==source_revision)return true;
    try{
        const bool changed_scene=!state.native_projected||state.native_projection.scene_uuid()!=scene_uuid;
        const auto& snapshot=state.native_projection.prepare(native,std::move(scene_uuid),source_revision,state.root);
        // Native QDR authoring has no Engine asset identity. The disposable
        // projection enters through Scene validation, never the disk reload API.
        if(changed_scene)state.resetPresentation();if(!state.document.resetTransientFrom(snapshot))throw std::runtime_error(state.document.status());
        std::vector<EntityId> selected;for(const auto id:native.selected_entities())if(const auto projected=state.native_projection.entity_id(native.uuid(id)))selected.push_back(projected);
        state.document.select(std::move(selected),state.native_projection.entity_id(native.uuid(native.primary_selection())));state.native_projected=true;state.message.clear();return true;
    }catch(const std::exception& error){state.message=error.what();return false;}
}
const NativeSceneProjection& LevelSession::nativeProjection()const{return impl_->native_projection;}
std::shared_ptr<const NativeSceneRenderBindings> LevelSession::nativeRenderBindings(){
    auto& state=*impl_;if(!state.native_projected)return {};const auto& scene=presentationScene();const auto revision=scene.revision(),pose=scene.poseRevision(),source=state.native_projection.source_revision();
    if(state.native_bindings&&state.native_binding_scene==revision&&state.native_binding_pose==pose&&state.native_binding_source==source){state.native_bindings=refresh_native_materials(state.native_bindings,scene,state.assets);return state.native_bindings;}
    auto bindings=std::make_shared<NativeSceneRenderBindings>();bindings->revision=revision^(pose*1099511628211ull)^(source*14695981039346656037ull);bindings->scene_lighting=state.scene_lighting;
    for(const auto& record:state.native_projection.snapshot().records){const auto uuid=state.native_projection.entity_uuid(record.id);const auto authored=state.document.scene().worldTransform(record.id),presented=scene.worldTransform(record.id);if(!uuid.empty()&&authored&&presented)bindings->entities.emplace(uuid,NativeEntityRenderBinding{record.id,*authored,*presented,record.nativeGeometry});}
    decorate_native_materials(*bindings,scene,state.assets,state.native_bindings.get());
    state.native_binding_scene=revision;state.native_binding_pose=pose;state.native_binding_source=source;state.native_bindings=std::move(bindings);return state.native_bindings;
}
std::vector<std::filesystem::path> LevelSession::takeFileChanges(){auto result=std::move(impl_->fileChanges);impl_->fileChanges.clear();return result;}
bool LevelSession::dropAsset(const std::string& id,Vec2 pixel,const Camera& camera,Vec2 size) {
    auto& state=*impl_;if(!state.renderer||playing()||state.document.transactionActive()||!math::finite(pixel)||!math::finite(size)||size[0]<=0||size[1]<=0)return false;
    const auto found=std::find_if(state.assets.catalog().begin(),state.assets.catalog().end(),[&](const auto& value){return value.id==id;});
    if(found==state.assets.catalog().end()||(found->kind!=AssetKind::Material&&found->kind!=AssetKind::Model&&found->kind!=AssetKind::Decal&&found->kind!=AssetKind::Particle))return false;
    cancelViewportRequest();Impl::ViewportRequest pending;pending.asset=id;pending.kind=found->kind;pending.camera=camera;pending.pixel=pixel;pending.size=size;pending.document=state.document.documentGeneration();
    pending.pick={state.document.scene().generation(),state.document.scene().revision(),++state.pickSerial,
        static_cast<std::uint32_t>(std::clamp(pixel[0],0.f,size[0]-1)),static_cast<std::uint32_t>(std::clamp(pixel[1],0.f,size[1]-1)),0,found->kind==AssetKind::Material?PickMode::MaterialSlot:PickMode::Surface};
    state.viewportRequest=std::move(pending);return true;
}
bool LevelSession::selectViewport(Vec2 pixel,const Camera& camera,Vec2 size,bool toggle,bool append) {
    auto& state=*impl_;if(!state.renderer||playing()||state.document.transactionActive()||size[0]<=0||size[1]<=0)return false;
    if(state.armedDecal) {
        const auto id=state.armedDecal;const auto value=state.document.scene().find(id);if(!value||!value->decal)return false;
        if(!dropAsset(value->decal->asset.id,pixel,camera,size))return false;state.viewportRequest->replaceEntity=id;return true;
    }
    cancelViewportRequest();Impl::ViewportRequest pending;pending.selection=true;pending.toggle=toggle;pending.append=append;pending.camera=camera;pending.pixel=pixel;pending.size=size;pending.document=state.document.documentGeneration();
    pending.pick={state.document.scene().generation(),state.document.scene().revision(),++state.pickSerial,static_cast<std::uint32_t>(std::clamp(pixel[0],0.f,size[0]-1)),static_cast<std::uint32_t>(std::clamp(pixel[1],0.f,size[1]-1)),0,PickMode::Selection};
    state.viewportRequest=std::move(pending);return true;
}
bool LevelSession::dropAssets(std::span<const std::string> ids,Vec2 pixel,const Camera& camera,Vec2 size){if(ids.empty())return false;if(!dropAsset(ids.front(),pixel,camera,size))return false;if(impl_->viewportRequest->kind==AssetKind::Model){for(const auto& id:ids){const auto found=std::find_if(impl_->assets.catalog().begin(),impl_->assets.catalog().end(),[&](const auto& value){return value.id==id;});if(found!=impl_->assets.catalog().end()&&found->kind==AssetKind::Model)impl_->viewportRequest->models.push_back(id);}}return true;}
bool LevelSession::requestViewportContext(Vec2 pixel,const Camera& camera,Vec2 size) {
    auto& state=*impl_;if(!state.renderer||playing()||state.document.transactionActive()||!math::finite(pixel)||!math::finite(size)||size[0]<=0||size[1]<=0)return false;
    cancelViewportRequest();Impl::ViewportRequest pending;pending.context=true;pending.camera=camera;pending.pixel=pixel;pending.size=size;pending.document=state.document.documentGeneration();
    state.contextMenu.begin({pending.document,state.document.scene().generation(),state.document.scene().revision(),camera.viewMatrix(),camera.projectionMatrix(size[0]/size[1],ClipDepth::ZeroToOne),size},
        {static_cast<std::uint32_t>(std::clamp(pixel[0],0.f,size[0]-1)),static_cast<std::uint32_t>(std::clamp(pixel[1],0.f,size[1]-1))});
    pending.pick=state.contextMenu.request(++state.pickSerial);state.viewportRequest=std::move(pending);return true;
}
std::optional<LevelViewportContext> LevelSession::takeViewportContext(){auto result=std::exchange(impl_->viewportContext,std::nullopt);if(result&&(result->document!=impl_->document.documentGeneration()||result->generation!=impl_->document.scene().generation()||result->revision!=impl_->document.scene().revision()))result.reset();return result;}
void LevelSession::setViewportPickRequester(std::function<bool(const PickRequest&)> callback){impl_->pick_requester=std::move(callback);}
bool LevelSession::viewportRequestPending()const{return impl_->viewportRequest.has_value();}
void LevelSession::cancelViewportRequest(){impl_->viewportRequest.reset();impl_->contextMenu.cancel();impl_->viewportContext.reset();impl_->armedDecal=NoEntity;if(impl_->renderer)impl_->renderer->cancelPicking();}
void LevelSession::armDecalPlacement(EntityId id){cancelViewportRequest();if(const auto record=impl_->document.scene().find(id);record&&record->decal)impl_->armedDecal=id;}
void LevelSession::replayParticle(EntityId id){impl_->restartParticle=id;}
VertexPaintTargets LevelSession::vertexPaintTargets(const SceneDocument& document,EntityId id){return impl_->prepared?impl_->prepared->vertexPaintTargets(document,id):VertexPaintTargets{};}
VertexPaintSurfaceSnapshot LevelSession::vertexPaintSurfaces(const SceneDocument& document){return impl_->prepared?impl_->prepared->vertexPaintSurfaces(document):VertexPaintSurfaceSnapshot{};}
bool LevelSession::meshPaintSubdivision(const EntityRecord& record,std::uint32_t level,SceneMeshPaint& result,std::string& diagnostic){if(!impl_->prepared){diagnostic="Renderer resources are unavailable.";return false;}return impl_->prepared->meshPaintSubdivision(record,level,result,diagnostic);}
void LevelSession::setVertexPaintState(bool enabled,bool held,bool subdivisions){impl_->vertexPaintEnabled=enabled;impl_->vertexPaintHeld=enabled&&held;impl_->vertexPaintSubdivisions=enabled&&subdivisions;}
std::string& LevelSession::navigationProfile(){return impl_->navigation.profile();}
std::string LevelSession::navigationStatus(const std::string& profile)const{return impl_->navigation.status(profile);}
void LevelSession::setNavigationVisible(bool visible){impl_->navigation.setVisible(visible);}
void LevelSession::setAssetPreviewPublisher(std::function<void(const std::string&,const DecodedImage&)> callback,std::function<bool(const std::string&)> available){impl_->previewPublisher=std::move(callback);impl_->previewAvailable=std::move(available);if(impl_->previews)impl_->previews->setImagePublisher(impl_->previewPublisher,impl_->previewAvailable);}
void LevelSession::pumpAssetPreviews(std::uint32_t frame,std::span<const std::string> visible,std::uint32_t edge){if(impl_->previews&&!impl_->vertexPaintHeld)impl_->previews->pump(frame,visible,edge);}
void LevelSession::focusSelection(Camera& camera,float aspect,bool preserveLens){auto& state=*impl_;const auto& document=editingDocument();const auto bounds=selectionBounds(document.scene(),document.selection().ids,[&](const std::string& id)->std::optional<Aabb>{const auto model=state.assets.findModel(id);return model?std::optional{model->bounds}:std::nullopt;},[&](EntityId id){return playing()?state.play.particles().bounds(id):state.particles.bounds(id);},[&](const std::string& id){return state.collisions.query(id);},state.profiles(),[&](EntityId id)->std::optional<Aabb>{const auto patch=state.decals.patch(id);return patch?std::optional{patch->bounds}:std::nullopt;});if(bounds.valid){const auto near=camera.nearPlane,far=camera.farPlane;camera.frameBounds(bounds.minimum,bounds.maximum,aspect);if(preserveLens){camera.nearPlane=near;camera.farPlane=far;}}}
bool LevelSession::play(){
    auto& state=*impl_;if(state.play.playing())return true;state.input.cancel();state.skipTime=true;state.playStart.reset();state.play_modeling_world=state.modeling_world;state.play_collisions=state.modeling_collisions();
    const bool result=state.modeling_current&&state.play.start(state.document,std::span<const EntityRecord>(state.play_collisions));
    state.message=state.modeling_current?state.play.status():"Preparing modeling world...";state.playPending=!result&&(!state.modeling_current||state.message.starts_with("Preparing mesh collision"));state.playGeneration=state.document.documentGeneration();state.playRevision=state.document.scene().revision();return result;
}
bool LevelSession::playFromHere(const Vec3& position,const Camera& camera){
    auto& state=*impl_;if(state.play.playing())return true;state.input.cancel();state.skipTime=true;state.playStart=PlayStartPoint{position,camera};state.play_modeling_world=state.modeling_world;state.play_collisions=state.modeling_collisions();
    const bool result=state.modeling_current&&state.play.startFromPoint(state.document,position,camera,std::span<const EntityRecord>(state.play_collisions));
    state.message=state.modeling_current?state.play.status():"Preparing modeling world...";state.playPending=!result&&(!state.modeling_current||state.message.starts_with("Preparing mesh collision"));state.playGeneration=state.document.documentGeneration();state.playRevision=state.document.scene().revision();return result;
}
void LevelSession::stop(){impl_->input.cancel();impl_->play.stop();impl_->play_modeling_world.reset();impl_->playPending=false;impl_->playStart.reset();impl_->skipTime=true;impl_->message.clear();}
bool LevelSession::setPaused(bool paused){impl_->input.cancel();impl_->skipTime=true;return impl_->play.setPaused(paused);}
bool LevelSession::step(){impl_->input.cancel();return impl_->play.step(resolveGameCamera(presentationScene()).value_or(Camera{}));}
bool LevelSession::playing()const{return impl_->play.playing();}
bool LevelSession::playPending()const{return impl_->playPending;}
bool LevelSession::paused()const{return impl_->play.paused();}
void LevelSession::cancelInput(){impl_->input.cancel();impl_->skipTime=true;}
void LevelSession::update(double seconds,bool active,std::uint32_t frame) {
    auto& state=*impl_;std::vector<std::filesystem::path> changed;
    if(state.resourceRuntime){changed=state.resourceRuntime->preFrame(frame);const auto status=state.resourceRuntime->status();if(!status.empty())state.message=status;}
    else {
        state.resources.pump();(void)state.resources.takePublications();state.assets.processImports();
        for(const auto& path:state.assets.takeOwnWrites())state.watcher->acknowledge(path);
        for(const auto& path:state.watcher->poll()){
            const auto relative=path.lexically_relative(state.root).generic_string();if(relative.starts_with(".crimson/cache/")||relative.starts_with("assets/cache/"))continue;
            state.resources.invalidatePath(path);state.assets.onFileChanged(path);changed.push_back(path);
        }
    }
    for(const auto& path:changed){
        if(state.previews)state.previews->invalidateFile(path);if(std::find(state.fileChanges.begin(),state.fileChanges.end(),path)==state.fileChanges.end())state.fileChanges.push_back(path);
    }
    if(state.native_projected&&state.viewportRequest&&state.viewportRequest->kind==AssetKind::Decal){
        state.native_mesh_sources=native_geometry_sources(state.modeling_world,nativeRenderBindings(),presentationScene(),state.native_mesh_sources);
        state.decals.setExternalMeshes(state.native_mesh_sources);
    }
    state.processViewport(frame);
    if(state.playPending) {
        if(state.playGeneration!=state.document.documentGeneration()||state.playRevision!=state.document.scene().revision()) {state.playPending=false;state.playStart.reset();state.message="The scene changed while preparing Play. Press Play to retry.";}
        else if(active) {
            bool ready=state.modeling_current;if(!ready)state.message="Preparing modeling world...";
            for(const auto& record:state.document.scene().records())if(record.collider&&record.collider->mode==ColliderMode::Solid&&record.collider->shape==ColliderShape::Trimesh&&(record.collider->mesh||!record.collider->source.empty())) {
                const auto source=colliderMeshResult(*record.collider,[&](const std::string& id){return state.collisions.query(id);});
                if(!source.mesh||!source.current){ready=false;state.message=source.diagnostic;if(!state.message.starts_with("Preparing mesh collision")){state.playPending=false;state.playStart.reset();}break;}
            }
            if(ready){state.play_modeling_world=state.modeling_world;state.play_collisions=state.modeling_collisions();state.playPending=false;const auto start=state.playStart;state.playStart.reset();const bool started=start?state.play.startFromPoint(state.document,start->position,start->view,std::span<const EntityRecord>(state.play_collisions)):state.play.start(state.document,std::span<const EntityRecord>(state.play_collisions));state.message=state.play.status();if(started)state.skipTime=true;}
        }
    }
    const double delta=active&&!state.skipTime&&(!state.vertexPaintHeld||state.play.playing())&&std::isfinite(seconds)?std::clamp(seconds,0.0,0.25):0;
    state.skipTime=!active;state.realSeconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-state.clockStart).count();state.previewDelta=delta;
    if(!active)state.input.cancel();
    if(state.play.playing())state.play.advance(delta,state.input,resolveGameCamera(state.play.presentationScene()).value_or(Camera{}),active);
    else {state.editSeconds+=delta;state.lights.advance(state.document.documentGeneration(),delta);}
    if(!state.play.playing()||state.play.navigationWorld())state.navigation.update(editingDocument(),[&](const std::string& id){return state.collisions.query(id);},state.profiles(),state.play.playing()?state.play.navigationWorld():nullptr);
    state.collisions.collect();state.decals.collect();
}
void LevelSession::attachRenderer(shared::Renderer& renderer) {
    auto& state=*impl_;if(state.renderer==&renderer)return;detachRenderer();state.renderer=&renderer;
    state.prepared=std::make_unique<EditorResources>(state.assets,renderer,state.resources,state.root);
    state.previews=std::make_unique<AssetPreviews>(state.root,state.assets,state.resources,renderer,*state.prepared);state.previews->setImagePublisher(state.previewPublisher,state.previewAvailable);
    ShaderCompileConfig config{state.runtimeRoot/"shader-tools/shaderc.exe",state.runtimeRoot/"shader-tools/shaders",state.runtimeRoot/"shader-tools/include",state.root/".crimson/cache/shaders"};
#if !defined(NDEBUG) && defined(CRIMSON_ENGINE_SHADER_SOURCE_DIR) && defined(CRIMSON_ENGINE_SHADER_INCLUDE_DIR)
    const ShaderRuntimePaths live{CRIMSON_ENGINE_SHADER_SOURCE_DIR,CRIMSON_ENGINE_SHADER_INCLUDE_DIR};std::error_code error;
    const auto selected=selectShaderRuntimePaths(true,live,{config.sourceDirectory,config.includeDirectory},std::filesystem::is_directory(live.sources,error)&&std::filesystem::is_directory(live.includes,error));config.sourceDirectory=selected.sources;config.includeDirectory=selected.includes;
#endif
    state.watcher.reset();state.resourceRuntime=std::make_unique<ResourceRuntime>(state.root,state.resources,state.assets,renderer,std::move(config),EmbeddedShaderSourceBaseline);
}
void LevelSession::detachRenderer(){auto& state=*impl_;state.input.cancel();state.assets.setMaterialGraphPrograms(nullptr);state.lastPrepared.reset();state.materialReady=false;state.previews.reset();state.prepared.reset();clearMaterialPreview();clearDecalDocumentPreview();state.resourceRuntime.reset();state.renderer=nullptr;state.environment={};state.environmentRevision=0;state.documentEnvironment={};state.documentEnvironmentRevisions={};if(!state.watcher)state.watcher=std::make_unique<FileWatcher>(shaderRuntimeWatchedRoots(state.root,{}));}
std::shared_ptr<const PreparedRenderFrame> LevelSession::prepareRender(const LevelViewRequest& request) {
    auto& state=*impl_;if(!state.renderer||!state.prepared)return {};
    auto& renderer=*state.renderer;state.prepared->beginFrame();state.prepared->setMaterialContext({state.play.playing()?state.play.tickCount()*PlaySession::FixedStep:state.editSeconds,state.realSeconds});
    if(state.viewportRequest) {
        auto& pending=*state.viewportRequest;
        if(pending.camera.viewMatrix()!=request.sceneCamera.viewMatrix()||pending.camera.projectionMatrix(pending.size[0]/pending.size[1],ClipDepth::ZeroToOne)!=request.sceneCamera.projectionMatrix(float(std::max(1u,request.sceneWidth))/float(std::max(1u,request.sceneHeight)),ClipDepth::ZeroToOne)||pending.size!=Vec2{float(request.sceneWidth),float(request.sceneHeight)})cancelViewportRequest();
        else if(!pending.submitted){pending.submitted=state.pick_requester?state.pick_requester(pending.pick):renderer.requestPick(pending.pick);if(!pending.submitted){state.message=renderer.pickingStatus();if(!state.native_projected||!state.pick_requester)state.viewportRequest.reset();}}
    }
    const auto& scene=presentationScene();auto extracted=extractScene(scene,state.profiles(),state.play.playing()?std::optional{state.play.lightPresentationSeconds()}:state.lights.sampleTime());
    if(state.native_projected)state.native_projection.apply_native_light_directions(extracted,scene);
    if(state.play.playing())state.play.prepareRagdollPresentation(extracted);
    else state.animations.advance(extracted,state.document.documentGeneration(),state.previewDelta,[&](const std::string& id){return state.assets.findAnimation(id);});
    if(state.native_projected&&std::any_of(extracted.decals.begin(),extracted.decals.end(),[](const auto& value){return value.visible&&value.decal.projection==SceneDecalProjection::Surface;})){
        state.native_mesh_sources=native_geometry_sources(state.play.playing()?state.play_modeling_world:state.modeling_world,nativeRenderBindings(),scene,state.native_mesh_sources);
        state.decals.setExternalMeshes(state.native_mesh_sources);
    }
    state.prepareEnvironment(extracted.environment);auto decalScene=state.decals.prepare(scene,extracted);
    auto& particles=state.play.playing()?state.play.particles():state.particles;
    if(!state.play.playing()||state.restartParticle)particles.reconcile(state.document.documentGeneration(),extracted.particles,[&](const std::string& id){return ParticleDefinitionVersion{state.assets.findParticle(id),state.assets.particleRevision(id),state.assets.particleDiagnostic(id)};},state.play.playing()?0:state.previewDelta,std::exchange(state.restartParticle,NoEntity));
    EditorRenderFrameInput input;input.settings.environment=extracted.environment.environment;input.settings.exposure=extracted.environment.exposure;input.settings.proceduralSun=extracted.proceduralSun;
    input.settings.antiAliasing=request.antiAliasing;input.sceneCamera=request.sceneCamera;input.gameCamera=resolveGameCamera(scene);
    input.sceneWidth=request.sceneWidth;input.sceneHeight=request.sceneHeight;input.gameWidth=request.gameWidth;input.gameHeight=request.gameHeight;
    input.sceneAspect=float(std::max(1u,request.sceneWidth))/float(std::max(1u,request.sceneHeight));input.sceneVisible=request.sceneVisible;input.gameVisible=request.gameVisible;
    input.selection=editingDocument().selection().ids;input.worldId=editingDocument().documentGeneration();input.giWorldId=(input.worldId<<1)|std::uint64_t(playing());
    input.gameCameraId=scene.properties().activeCamera;input.environmentId=scene.properties().activeEnvironment;input.shadowResolution=request.shadowResolution;
    input.materialContext={state.play.playing()?state.play.tickCount()*PlaySession::FixedStep:state.editSeconds,state.realSeconds};input.appearance=request.appearance;input.gridVisible=request.gridVisible;
    input.decals=std::move(decalScene.decals);input.decalReceivers=std::move(decalScene.receivers);input.particleMaterials.push_back({});
    input.surfacePickActive=state.viewportRequest&&state.viewportRequest->pick.mode==PickMode::Surface;
    input.surfacePickActive|=state.vertexPaintEnabled;
    if(state.vertexPaintSubdivisions)input.sceneCollisionOverlays=state.prepared->vertexPaintSubdivisionOverlays(editingDocument(),editingDocument().selection().ids);
    if(!state.play.playing()||state.play.navigationWorld()){auto navigation=state.navigation.overlays();input.sceneCollisionOverlays.insert(input.sceneCollisionOverlays.end(),std::make_move_iterator(navigation.begin()),std::make_move_iterator(navigation.end()));}
    const auto presentation=particles.presentation();
    for(const auto& group:particles.groups())if(const auto material=state.assets.preparedParticleMaterial(group.materialId);material&&material->ready) {
        const auto index=static_cast<std::uint32_t>(input.particleMaterials.size());input.particleMaterials.push_back(material->render);input.particlePins.push_back(material);
        for(auto offset=group.first;offset<group.first+group.count;++offset){const auto& p=presentation[offset];input.particles.push_back({p.position,p.size,p.color,index,p.rotation,p.billboardMode,p.right,p.up});}
    }
    input.extracted=std::move(extracted);const AssetLibrary::ReflectionDemandScope reflections(state.assets,input.settings.environment.ssr.enabled&&input.settings.environment.ssr.strength>0);
    if(!state.native_projected&&request.sceneVisible&&request.sceneWidth&&request.sceneHeight)renderer.resizeViewport(request.sceneWidth,request.sceneHeight);
    if(request.gameVisible&&request.gameWidth&&request.gameHeight)renderer.resizeGameViewport(request.gameWidth,request.gameHeight);
    EditorRenderFramePreparationStats preparation;auto frame=prepareEditorRenderFrame(std::move(input),state.assets,*state.prepared,&preparation);state.prepared->endFrame();
    if(state.play.playing())frame=append_modeling_world(std::move(frame),state.play_modeling_world,!state.native_projected,nativeRenderBindings(),state.native_projected);
    state.previewDelta=0;state.lastPrepared=frame;state.materialReady=preparation.ready;return frame;
}
const std::shared_ptr<const PreparedRenderFrame>& LevelSession::lastPreparedFrame()const{return impl_->lastPrepared;}
bool LevelSession::materialPreparationReady()const{return impl_->materialReady;}
bool LevelSession::recordModelDocumentPreview(LevelModelPreviewKind kind,const std::string& id,const std::shared_ptr<const ModelPose>& pose,
    const Mat4& world,const SceneSettings& settings,std::uint32_t width,std::uint32_t height){
    auto& state=*impl_;if(!state.renderer||!state.prepared||!pose||!width||!height)return false;
    if(!state.documentEnvironment){ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;const auto root=state.root;state.documentEnvironment=state.resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{std::string(StudioEnvironmentId)},[root](std::stop_token token){if(token.stop_requested())throw std::runtime_error("Document preview environment preparation canceled.");auto bake=prepareEnvironmentCpu(root,{});std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);for(const auto& level:bake->data.specular)bytes+=level.rgba.size()*sizeof(float);return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};},std::move(options));}
    const auto environment=state.documentEnvironment.snapshot();if(!environment)return false;auto& revision=state.documentEnvironmentRevisions.at(static_cast<unsigned>(kind));auto& renderer=*state.renderer;
    if(revision!=environment.revision()){const bool published=kind==LevelModelPreviewKind::Ragdoll?renderer.publishRagdollPreviewEnvironment(*environment):renderer.publishAnimationPreviewEnvironment(*environment);if(!published)return false;revision=environment.revision();}
    const auto model=state.prepared->prepare(id,world,settings.camera,height,true,ResourcePriority::Selected,float(width)/height);if(!model||!model->ready)return false;
    struct Pins{std::shared_ptr<const PreparedEditorModel> model;std::shared_ptr<const ModelPose> pose;std::shared_ptr<const PreparedEnvironment> environment;};auto pins=std::make_shared<Pins>(Pins{model,pose,environment.shared()});
    std::array<RenderModelInstance,1> instances{{{0,{model->asset,world},&model->textures,model->revision,false,false,&model->materials}}};instances.front().pose=pose;instances.front().castShadows=false;RenderScene scene{settings,{},nullptr,nullptr,instances,{},{},true,pins};
    if(kind==LevelModelPreviewKind::Ragdoll){renderer.resizeRagdollPreview(width,height);renderer.renderRagdollPreview(scene);}else{renderer.resizeAnimationPreview(width,height);renderer.renderAnimationPreview(scene);}return true;
}
bool LevelSession::recordParticleDocumentPreview(const std::string& id,std::span<const ParticlePresentation> presentation,const SceneSettings& settings,
    double seconds,std::uint32_t width,std::uint32_t height){auto& state=*impl_;if(!state.renderer||!width||!height)return false;const auto material=state.assets.preparedParticleMaterial(id);if(!material||!material->ready)return false;std::vector<RenderParticle> particles;particles.reserve(presentation.size());for(const auto& p:presentation)particles.push_back({p.position,p.size,p.color,0,p.rotation,p.billboardMode,p.right,p.up});state.renderer->resizeParticlePreview(width,height);state.renderer->renderParticlePreview(settings,particles,std::span<const RenderParticleMaterial>{&material->render,1},{seconds,state.realSeconds});return true;}
bool LevelSession::recordDecalDocumentPreview(const DecalAsset& asset,std::uint64_t previewRevision,MaterialPreviewMesh mesh,
    const SceneSettings& settings,std::uint32_t width,std::uint32_t height){
    auto& state=*impl_;if(!state.renderer||!state.prepared||!width||!height)return false;
    try{
        if(!state.documentEnvironment){
            ResourceRequestOptions options;options.variant="ibl";options.priority=ResourcePriority::Selected;const auto root=state.root;
            state.documentEnvironment=state.resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{std::string(StudioEnvironmentId)},[root](std::stop_token stop){
                if(stop.stop_requested())throw std::runtime_error("Document preview environment preparation canceled.");
                auto bake=prepareEnvironmentCpu(root,{});std::size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);
                for(const auto& mip:bake->data.specular)bytes+=mip.rgba.size()*sizeof(float);
                return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};
            },std::move(options));
        }
        const auto environment=state.documentEnvironment.snapshot();if(!environment)return false;
        auto& renderer=*state.renderer;auto& revision=state.documentEnvironmentRevisions[2];
        if(revision!=environment.revision()){if(!renderer.publishDecalPreviewEnvironment(*environment))return false;revision=environment.revision();}
        const auto& surface=state.documentDecalGeometry.prepare(mesh,state.resources);if(!surface.model)return false;
        const auto material=state.assets.preparedDecalMaterial(asset,previewRevision,std::max(width,height),ResourcePriority::Selected);
        struct Pins{std::shared_ptr<const ModelAsset> model;std::shared_ptr<const PreparedEnvironment> environment;std::vector<ModelMaterialTextures> textures;std::vector<RenderDecal> decals;};
        auto pins=std::make_shared<Pins>();pins->model=surface.model;pins->environment=environment.shared();pins->textures=state.assets.modelBindings(*surface.model,surface.model->materials);
        state.prepared->retainExternalModelForFrame(surface.model->id);
        if(!renderer.prepareModel(*surface.model)||!state.assets.modelBindingsReady(pins->textures))return false;
        if(material&&material->ready&&material->render&&surface.patch){pins->decals.push_back({2,2,0,surface.patch,material->render});pins->decals.back().frame=surface.frame;}
        std::array<RenderModelInstance,1> instances{{{1,{surface.model,Mat4{}},&pins->textures,1,true,false,&surface.model->materials}}};
        instances.front().receiveDecals=true;instances.front().castShadows=false;
        const std::array<DirectionalLight,1> lights{{{{-.4f,-1,-.7f},{1,1,1},2,true}}};
        RenderScene scene{settings,{},nullptr,nullptr,instances,lights,{},true,pins};scene.decals=pins->decals;scene.materialContext={state.editSeconds,state.realSeconds};
        renderer.resizeDecalPreview(width,height);renderer.renderDecalPreview(scene);return true;
    }catch(const std::exception& error){state.message=error.what();return false;}
}
void LevelSession::clearDecalDocumentPreview(){auto& state=*impl_;state.documentDecalGeometry.clear();state.documentEnvironmentRevisions[2]=0;if(state.renderer)state.renderer->releaseDecalPreview();}
bool LevelSession::recordMaterialPreview(const MaterialAsset& material,const SceneSettings& requested,MaterialPreviewMesh mesh,
    const Mat4& object,std::uint32_t width,std::uint32_t height,bool showEnvironment,bool environmentPublishedExternally,
    std::optional<MaterialGraphPreviewTarget> node_preview) {
    auto& state=*impl_;if(!state.renderer||!width||!height)return false;
    auto& renderer=*state.renderer;
    if(state.previewMaterialId!=material.id){clearMaterialPreview();state.previewMaterialId=material.id;}
    if(state.materialPreview&&state.materialPreview->material.id!=material.id){state.materialPreview.reset();state.materialPreviewKey.clear();}
    if(!environmentPublishedExternally) {
    const auto map=requested.environment.source==EnvironmentSource::Hdri?state.assets.references().resolveEnvironment(requested.environment.map):std::string{};
    if(!state.previewEnvironment||state.previewEnvironmentMap!=map) {
        state.previewEnvironmentMap=map;state.previewEnvironmentRevision=0;ResourceRequestOptions options;options.variant="ibl";
        if(!map.empty())options.dependencies={resolveAssetPath(state.root,map)};const auto root=state.root;
        state.previewEnvironment=state.resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{map.empty()?StudioEnvironmentId:map},[root,map](std::stop_token stop){
            if(stop.stop_requested())throw std::runtime_error("Material preview environment canceled.");
            auto value=prepareEnvironmentCpu(root,map);std::size_t bytes=value->data.diffuse.rgba.size()*sizeof(float)+value->data.brdf.size()*sizeof(float);
            for(const auto& mip:value->data.specular)bytes+=mip.rgba.size()*sizeof(float);
            return ResourceLoadResult<PreparedEnvironment>{value,bytes,value->key,{}};
        },std::move(options));
    }
    if(const auto environment=state.previewEnvironment.snapshot())if(environment.revision()!=state.previewEnvironmentRevision&&renderer.publishMaterialPreviewEnvironment(*environment))state.previewEnvironmentRevision=environment.revision();
    }
    const bool environmentReady=environmentPublishedExternally||state.previewEnvironmentRevision!=0;
    const AssetLibrary::ReflectionDemandScope reflections(state.assets,!node_preview&&requested.environment.ssr.enabled&&requested.environment.ssr.strength>0);
    const bool decal=!node_preview&&materialOutputNode(material.graph).output.shading==MaterialShading::Decal;
    if(decal) {
        try {
            const auto prepared=state.assets.preparedDecalMaterial(material,materialGraphRenderHash(material.surface),std::max(width,height),ResourcePriority::Selected);
            if(prepared&&prepared->ready&&prepared->render)state.previewDecalMaterial=prepared->render;
            const auto& surface=state.previewDecalGeometry.prepare(mesh,state.resources);
            if(!surface.model||!environmentReady)return false;
            if(surface.patch&&(state.previewDecalSource!=surface.patch||state.previewDecalTransform!=object)) {
                auto patch=std::make_shared<DecalPatchData>(*surface.patch);patch->bounds={};
                const auto normal=math::normalMatrix(object).value_or(Mat4{});
                for(auto& vertex:patch->vertices){vertex.position=math::transformPoint(object,vertex.position);vertex.normal=math::normalized(math::transformDirection(normal,vertex.normal));math::include(patch->bounds,vertex.position);}
                std::uint64_t fingerprint=surface.patch->fingerprint;
                for(float value:object.values){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);for(unsigned index=0;index<sizeof(value);++index){fingerprint^=bytes[index];fingerprint*=1099511628211ull;}}
                patch->fingerprint=fingerprint;state.previewDecalTransformed=std::move(patch);state.previewDecalSource=surface.patch;state.previewDecalTransform=object;
            }
            struct Pins {std::shared_ptr<const ModelAsset> model;std::vector<ModelMaterialTextures> textures;std::vector<RenderDecal> decals;};
            auto pins=std::make_shared<Pins>();pins->model=surface.model;pins->textures=state.assets.modelBindings(*surface.model,surface.model->materials);
            if(!renderer.prepareModel(*surface.model)||!state.assets.modelBindingsReady(pins->textures))return false;
            if(state.previewDecalMaterial&&state.previewDecalTransformed){pins->decals.push_back({2,2,0,state.previewDecalTransformed,state.previewDecalMaterial});pins->decals.back().frame=object*surface.frame;}
            std::array<RenderModelInstance,1> instances{{{1,{surface.model,object},&pins->textures,1,true,false,&surface.model->materials}}};instances.front().receiveDecals=true;instances.front().castShadows=false;
            const std::array<DirectionalLight,1> lights{{{{-.4f,-1,-.7f},{1,1,1},2,true}}};
            auto settings=requested;settings.environment.background.show=showEnvironment&&settings.environment.enabled;
            RenderScene scene{settings,{},nullptr,nullptr,instances,lights,{},true,pins};scene.decals=pins->decals;scene.materialContext={state.play.playing()?state.play.tickCount()*PlaySession::FixedStep:state.editSeconds,state.realSeconds};
            renderer.resizeMaterialPreview(width,height);renderer.renderMaterialPreview(scene);return true;
        }catch(const std::exception& error){state.message=error.what();return false;}
    }
    if(node_preview)try {
        if(!state.node_preview_programs)state.node_preview_programs=state.assets.createMaterialNodePreviewPrograms();
        if(!state.node_preview_programs)throw std::runtime_error("Node preview shader compilation is unavailable.");
        auto bindings=state.assets.materialNodePreviewBindings(*state.node_preview_programs,material,*node_preview,std::max(width,height));
        if(!bindings.diagnostic.empty())state.message=bindings.diagnostic;
        if(bindings.sourceReady&&bindings.drawable()&&!bindings.graphPending&&bindings.graph) {
            const auto& output=materialOutputNode(material.graph).output;
            const auto key=material.id+":"+std::to_string(node_preview->node)+":"+std::to_string(unsigned(node_preview->pin))+":"+
                std::to_string(materialGraphValueHash(material.graph,output.shading,output.blend,node_preview->node));
            if(!state.node_preview||state.node_preview_key!=key||state.node_preview->bindings.graph->signature!=bindings.graph->signature) {
                state.node_preview=std::make_shared<Impl::MaterialPreviewPacket>(Impl::MaterialPreviewPacket{material,std::move(bindings)});
                state.node_preview_key=key;
            }
        }
        state.node_preview_programs->trim();
    }catch(const std::exception& error){state.message=error.what();}
    else try {
        auto bindings=state.assets.materialPreviewBindings(material,std::max(width,height));
        if(bindings.sourceReady&&bindings.drawable()&&(!bindings.graphPending||!state.materialPreview||!state.materialPreview->bindings.graph)) {
            auto key=material.surface.graph?std::to_string(materialGraphRenderHash(material.surface)):serializeMaterialAsset(material);
            const auto previous=state.materialPreview;
            if(!previous||state.materialPreviewKey!=key||!std::equal(previous->bindings.handles.begin(),previous->bindings.handles.end(),bindings.handles.begin(),[](auto a,auto b){return a.idx==b.idx;})||
                previous->bindings.flags!=bindings.flags||previous->bindings.signatures!=bindings.signatures||previous->bindings.sourceFingerprints!=bindings.sourceFingerprints||
                (previous->bindings.graph?previous->bindings.graph->signature:0)!=(bindings.graph?bindings.graph->signature:0)) {
                state.materialPreview=std::make_shared<Impl::MaterialPreviewPacket>(Impl::MaterialPreviewPacket{material,std::move(bindings)});state.materialPreviewKey=std::move(key);
            }
        }
    }catch(const std::exception& error){state.message=error.what();}
    const auto displayed=node_preview?state.node_preview:state.materialPreview;
    if(!displayed||!environmentReady)return false;
    renderer.resizeMaterialPreview(width,height);
    renderer.renderMaterialPreview(requested,displayed->material,displayed->bindings,mesh,object,showEnvironment,
        displayed,{state.play.playing()?state.play.tickCount()*PlaySession::FixedStep:state.editSeconds,state.realSeconds});
    return true;
}
void LevelSession::clearMaterialPreview(){impl_->materialPreview.reset();impl_->materialPreviewKey.clear();impl_->node_preview.reset();impl_->node_preview_key.clear();impl_->node_preview_programs.reset();impl_->previewEnvironment={};impl_->previewEnvironmentRevision=0;impl_->previewDecalGeometry.clear();impl_->previewDecalMaterial.reset();impl_->previewDecalSource.reset();impl_->previewDecalTransformed.reset();impl_->previewMaterialId.clear();}
} // namespace crimson::integration
