#include "EditorRenderFrame.h"

#include "assets/AssetLibrary.h"
#include "assets/ModelAnimation.h"
#include "resources/CacheIO.h"
#include <crimson/renderer/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <chrono>

namespace crimson {
namespace {
bool visibleBounds(const Aabb& local,const Mat4& world,const Camera& camera,float aspect) {
    const auto bounds=math::transformBounds(local,world);if(!bounds.valid)return true;
    const auto depth=bgfx::getCaps()->homogeneousDepth?ClipDepth::NegativeOneToOne:ClipDepth::ZeroToOne;
    const auto projection=camera.projectionMatrix(aspect,depth)*camera.viewMatrix();
    std::array<bool,6> outside{true,true,true,true,true,true};
    for(unsigned corner=0;corner<8;++corner) {
        const Vec4 point{corner&1?bounds.maximum[0]:bounds.minimum[0],corner&2?bounds.maximum[1]:bounds.minimum[1],corner&4?bounds.maximum[2]:bounds.minimum[2],1};
        const auto clip=math::transformHomogeneous(projection,point);if(!math::finite(clip))return true;
        outside[0]&=clip[0]<-clip[3];outside[1]&=clip[0]>clip[3];outside[2]&=clip[1]<-clip[3];outside[3]&=clip[1]>clip[3];
        outside[4]&=clip[2]<(depth==ClipDepth::ZeroToOne?0:-clip[3]);outside[5]&=clip[2]>clip[3];
    }
    return std::none_of(outside.begin(),outside.end(),[](bool value){return value;});
}

struct ViewPins {
    std::vector<std::shared_ptr<const PreparedEditorModel>> models;
};

void includeMeshPopulation(PreparedRenderView& view,const PreparedEditorModel& prepared,const RenderModelInstance& instance) {
    using streaming::detail::hash;
    if(!view.meshPopulationRevision)view.meshPopulationRevision=streaming::detail::HashSeed;
    auto& revision=view.meshPopulationRevision;
    hash(revision,prepared.signature);hash(revision,instance.entityId);hash(revision,instance.model.transform);
    hash(revision,instance.resourceRevision);hash(revision,instance.forceFullDetail);hash(revision,instance.pickable);
    hash(revision,instance.receiveDecals);
    hash(revision,instance.shading);hash(revision,instance.blend);hash(revision,instance.castShadows);hash(revision,instance.receiveShadows);
    hash(revision,instance.meshShading);
    hash(revision,instance.lightingScopes.directReceiverMask);hash(revision,instance.lightingScopes.shadowCasterMask);
    hash(revision,instance.lightingCategory);hash(revision,instance.opacity);
    if(instance.pose)hash(revision,instance.pose->signature);
    if(!revision)revision=1;
}

void appendModel(PreparedRenderView& view,ViewPins& pins,const ExtractedModel& model,
                 std::shared_ptr<const PreparedEditorModel> prepared,bool fullDetail,bool receiveDecals,EditorResources& resources) {
    view.instances.push_back({model.id,{prepared->asset,model.world},&prepared->textures,prepared->revision,fullDetail,true,&prepared->materials});
    view.instances.back().receiveDecals=receiveDecals;
    view.instances.back().castShadows=model.castShadows;view.instances.back().receiveShadows=model.receiveShadows;
    view.instances.back().lightingScopes=model.lightingScopes;
    view.instances.back().lightingCategory=model.lightingCategory;
    view.instances.back().actorOwner=model.actorOwner;
    view.instances.back().meshShading=model.meshShading;
    view.instances.back().pose=resources.pose(model,prepared->asset);
    includeMeshPopulation(view,*prepared,view.instances.back());
    pins.models.push_back(std::move(prepared));
}

void includePreparedModel(EditorRenderFramePreparationStats* stats,const std::shared_ptr<const PreparedEditorModel>& prepared) {
    if(!stats)return;
    if(!prepared || !prepared->ready) {stats->ready=false;stats->refined=false;return;}
    stats->refined&=prepared->refined;
    for(const auto& bindings:prepared->textures) {
        stats->ready&=bindings.drawable() && bindings.sourceReady && !bindings.graphPending;
        stats->refined&=bindings.complete;
    }
    stats->refined&=stats->ready;
}
} // namespace

std::shared_ptr<const PreparedRenderFrame> prepareEditorRenderFrame(
    EditorRenderFrameInput input,AssetLibrary& assets,EditorResources& resources,EditorRenderFramePreparationStats* stats) {
    CRIMSON_PROFILE_ZONE("Scene and Game frame preparation");
    using Clock=std::chrono::steady_clock;
    if(stats)*stats={};
    const auto& ssr=input.settings.environment.ssr;
    const AssetLibrary::ReflectionDemandScope reflectionDemand(assets,ssr.enabled&&ssr.strength>0&&
        ((input.sceneVisible&&input.sceneWidth&&input.sceneHeight)||
         (input.gameVisible&&input.gameWidth&&input.gameHeight&&input.gameCamera)));
    // Clocks were reconciled against the complete extraction. Hidden models
    // must not request receiver, caster, picking or material-preview resources.
    std::erase_if(input.extracted.models,[](const auto& model){return !model.visible;});
    resources.setMaterialContext(input.materialContext);
    if(input.materialPreview)for(auto& model:input.extracted.models) {
        const auto& preview=*input.materialPreview;
        if(model.id!=preview.entityId)continue;
        const auto asset=assets.findModel(model.assetId,model.primitive);
        if(!asset || preview.slot>=asset->materials.size() || preview.materialId.empty())break;
        // Keep requesting the authored packet so cancel/exit restores it at
        // once, and a pending shader/texture never hides the original mesh.
        (void)resources.prepare(model.assetId,model.world,input.sceneCamera,input.sceneHeight,true,
            ResourcePriority::Selected,input.sceneAspect,model.materialOverrides,0,&model);
        auto overrides=model.materialOverrides;
        setModelMaterialOverride(overrides,preview.slot,preview.materialId);
        const auto candidate=resources.prepare(model.assetId,model.world,input.sceneCamera,input.sceneHeight,true,
            ResourcePriority::Selected,input.sceneAspect,overrides,0,&model);
        if(materialSlotPreviewReady(candidate))model.materialOverrides=std::move(overrides);
        break;
    }
    auto frame=std::make_shared<PreparedRenderFrame>();
    frame->appearance=input.appearance;frame->gridVisible=input.gridVisible;
    frame->views.reserve(2);
    auto worldPins=std::make_shared<EditorRenderWorldPins>();worldPins->particles=std::move(input.particlePins);
    const auto priorityOrigin=input.shadowPriorityOrigin();
    // Full-world caster preparation precedes receiver culling. Both cameras use
    // one caster snapshot and the same interpolation/material clock values.
    std::vector<ShadowCamera> cameras{{input.sceneCamera,input.sceneAspect}};
    const bool gameVisible=input.gameVisible && input.gameWidth && input.gameHeight && input.gameCamera;
    if(gameVisible)cameras.push_back({*input.gameCamera,static_cast<float>(input.gameWidth)/input.gameHeight});
    const auto shadowStarted=stats?Clock::now():Clock::time_point{};
    worldPins->shadows=resources.prepareShadows(input.extracted,input.shadowResolution,ResourcePriority::Visible,cameras,priorityOrigin);
    if(stats) {
        stats->shadowPreparationMs=std::chrono::duration<double,std::milli>(Clock::now()-shadowStarted).count();
        stats->ready=worldPins->shadows->ready;
        stats->preparedCasters=worldPins->shadows->instances.size();stats->refined=worldPins->shadows->refined;
        for(const auto& model:worldPins->shadows->models)includePreparedModel(stats,model);
    }
    const auto receiverStarted=stats?Clock::now():Clock::time_point{};
    std::uint64_t signature=14695981039346656037ull;
    for(const auto& material:worldPins->particles)signature=(signature^material->signature)*1099511628211ull;
    for(const auto& decal:input.decals) {
        using streaming::detail::hash;
        hash(signature,decal.entityId);hash(signature,decal.stableOrder);hash(signature,decal.layer);
        hash(signature,decal.tint);hash(signature,decal.opacity);hash(signature,decal.edgeFeather);
        if(decal.patch)hash(signature,decal.patch->fingerprint);
        if(decal.material)hash(signature,decal.material->fingerprint);
    }
    const auto decalReceiver=[&](EntityId id){return std::find(input.decalReceivers.begin(),input.decalReceivers.end(),id)!=input.decalReceivers.end();};

    PreparedRenderView scene;
    scene.identity={RenderViewPurpose::Scene,0};scene.settings=input.settings;scene.settings.camera=input.sceneCamera;
    scene.giWorldId=input.giWorldId;scene.giEnvironmentId=input.environmentId;
    scene.width=input.sceneWidth;scene.height=input.sceneHeight;
    scene.collisionOverlays=std::move(input.sceneCollisionOverlays);
    auto scenePins=std::make_shared<ViewPins>();
    for(const auto& model:input.extracted.models) {
        const bool selected=std::find(input.selection.begin(),input.selection.end(),model.id)!=input.selection.end();
        const bool receiveDecals=decalReceiver(model.id),fullDetail=selected||receiveDecals||input.surfacePickActive;
        if(!selected)if(const auto metadata=assets.findModel(model.assetId,model.primitive))
            if(!visibleBounds(resources.modelBounds(model,metadata),model.world,input.sceneCamera,input.sceneAspect))continue;
        auto prepared=resources.prepare(model.assetId,model.world,input.sceneCamera,input.sceneHeight,fullDetail,
            ResourcePriority::Visible,input.sceneAspect,model.materialOverrides,0,&model);
        includePreparedModel(stats,prepared);
        if(prepared) {
            signature=(signature^prepared->signature)*1099511628211ull;
            if(const auto pose=resources.pose(model,prepared->asset))signature=(signature^pose->signature)*1099511628211ull;
            appendModel(scene,*scenePins,model,std::move(prepared),fullDetail,receiveDecals,resources);
        }
    }
    for(const auto& id:input.placementIds) {
        auto prepared=resources.prepare(id,math::translation(input.placementPosition),input.sceneCamera,
            input.sceneHeight,false,ResourcePriority::Placement,input.sceneAspect);
        includePreparedModel(stats,prepared);if(!prepared)continue;
        scene.instances.push_back({0,{prepared->asset,math::translation(input.placementPosition)},&prepared->textures,prepared->revision,false,false,&prepared->materials});
        scene.instances.back().castShadows=false;scene.instances.back().receiveShadows=false;
        includeMeshPopulation(scene,*prepared,scene.instances.back());scenePins->models.push_back(std::move(prepared));
    }
    scene.pinned=std::move(scenePins);
    if(input.sceneVisible && input.sceneWidth && input.sceneHeight)frame->views.push_back(std::move(scene));
    frame->resourceSignature=signature;

    if(gameVisible) {
        PreparedRenderView game;
        game.identity={RenderViewPurpose::Game,0};game.settings=input.settings;game.settings.camera=*input.gameCamera;
        game.giWorldId=input.giWorldId;game.giCameraId=input.gameCameraId;game.giEnvironmentId=input.environmentId;
        game.width=input.gameWidth;game.height=input.gameHeight;
        game.collisionOverlays=std::move(input.gameCollisionOverlays);
        const auto aspect=static_cast<float>(input.gameWidth)/input.gameHeight;
        auto gamePins=std::make_shared<ViewPins>();
        for(const auto& model:input.extracted.models) {
            const bool receiveDecals=decalReceiver(model.id);
            if(const auto metadata=assets.findModel(model.assetId,model.primitive))
                if(!visibleBounds(resources.modelBounds(model,metadata),model.world,*input.gameCamera,aspect))continue;
            auto prepared=resources.prepare(model.assetId,model.world,*input.gameCamera,input.gameHeight,receiveDecals,
                ResourcePriority::Visible,aspect,model.materialOverrides,0,&model);
            includePreparedModel(stats,prepared);
            if(prepared) {
                appendModel(game,*gamePins,model,std::move(prepared),receiveDecals,receiveDecals,resources);
            }
        }
        game.pinned=std::move(gamePins);frame->views.push_back(std::move(game));
    }
    if(stats)stats->receiverPreparationMs=std::chrono::duration<double,std::milli>(Clock::now()-receiverStarted).count();
    frame->world=freezeEditorRenderWorld(input.extracted,std::move(input.particles),std::move(input.particleMaterials),
        std::move(worldPins),input.worldId,input.shadowResolution,input.materialContext,priorityOrigin,std::move(input.decals));
    return frame;
}
} // namespace crimson
