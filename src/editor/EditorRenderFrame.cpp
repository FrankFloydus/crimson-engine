#include "EditorRenderFrame.h"

#include "assets/AssetLibrary.h"
#include "assets/ModelAnimation.h"
#include "crimson-renderer/ShadowReceiverBounds.h"

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

void appendModel(PreparedRenderView& view,ViewPins& pins,const ExtractedModel& model,
                 std::shared_ptr<const PreparedEditorModel> prepared,bool selected,EditorResources& resources) {
    view.instances.push_back({model.id,{prepared->asset,model.world},&prepared->textures,prepared->revision,selected,true,&prepared->materials});
    view.instances.back().castShadows=model.castShadows;view.instances.back().receiveShadows=model.receiveShadows;
    view.instances.back().lightingScopes=model.lightingScopes;
    view.instances.back().lightingCategory=model.lightingCategory;
    view.instances.back().pose=resources.pose(model,prepared->asset);
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
    using Clock=std::chrono::steady_clock;
    if(stats)*stats={};
    resources.setMaterialContext(input.materialContext);
    auto frame=std::make_shared<PreparedRenderFrame>();
    frame->appearance=input.appearance;frame->gridVisible=input.gridVisible;
    frame->views.reserve(2);
    auto worldPins=std::make_shared<EditorRenderWorldPins>();worldPins->particles=std::move(input.particlePins);
    Aabb focusBounds;
    for(const auto& model:input.extracted.models)
        if(model.castShadows && std::find(input.shadowFocusEntities.begin(),input.shadowFocusEntities.end(),model.id)!=input.shadowFocusEntities.end())
            if(const auto asset=assets.findModel(model.assetId))math::include(focusBounds,math::transformBounds(resources.modelBounds(model,asset),model.world));
    const auto priorityOrigin=focusBounds.valid?(focusBounds.minimum+focusBounds.maximum)*.5:input.sceneCamera.target;
    // Full-world caster preparation precedes receiver culling. Both cameras use
    // one caster snapshot and the same interpolation/material clock values.
    std::vector<ShadowCamera> cameras{{input.sceneCamera,input.sceneAspect}};
    const bool gameVisible=input.gameVisible && input.gameWidth && input.gameHeight && input.gameCamera;
    if(gameVisible)cameras.push_back({*input.gameCamera,static_cast<float>(input.gameWidth)/input.gameHeight,true,focusBounds});
    Aabb particleBounds;
    if(gameVisible && std::any_of(input.extracted.directional.begin(),input.extracted.directional.end(),
        [](const auto& light){return light.enabled && light.castShadows;}))
        particleBounds=particleShadowReceiverBounds(input.particles,input.particleMaterials);
    const auto shadowStarted=stats?Clock::now():Clock::time_point{};
    worldPins->shadows=resources.prepareShadows(input.extracted,input.shadowResolution,ResourcePriority::Visible,cameras,input.shadowFocusEntities,priorityOrigin,
        particleBounds.valid?std::span(&particleBounds,1):std::span<const Aabb>{});
    if(stats) {
        stats->shadowPreparationMs=std::chrono::duration<double,std::milli>(Clock::now()-shadowStarted).count();
        stats->ready=worldPins->shadows->ready;
        stats->preparedCasters=worldPins->shadows->instances.size();stats->refined=worldPins->shadows->refined;
        for(const auto& model:worldPins->shadows->models)includePreparedModel(stats,model);
    }
    const auto receiverStarted=stats?Clock::now():Clock::time_point{};
    std::uint64_t signature=14695981039346656037ull;
    for(const auto& material:worldPins->particles)signature=(signature^material->signature)*1099511628211ull;

    PreparedRenderView scene;
    scene.identity={RenderViewPurpose::Scene,0};scene.settings=input.settings;scene.settings.camera=input.sceneCamera;
    scene.width=input.sceneWidth;scene.height=input.sceneHeight;
    auto scenePins=std::make_shared<ViewPins>();
    for(const auto& model:input.extracted.models) {
        const bool selected=std::find(input.selection.begin(),input.selection.end(),model.id)!=input.selection.end();
        if(!selected)if(const auto metadata=assets.findModel(model.assetId))
            if(!visibleBounds(resources.modelBounds(model,metadata),model.world,input.sceneCamera,input.sceneAspect))continue;
        auto prepared=resources.prepare(model.assetId,model.world,input.sceneCamera,input.sceneHeight,selected,
            ResourcePriority::Visible,input.sceneAspect,model.materialId,0,&model);
        includePreparedModel(stats,prepared);
        if(prepared) {
            signature=(signature^prepared->signature)*1099511628211ull;
            if(const auto pose=resources.pose(model,prepared->asset))signature=(signature^pose->signature)*1099511628211ull;
            appendModel(scene,*scenePins,model,std::move(prepared),selected,resources);
        }
    }
    for(const auto& id:input.placementIds) {
        auto prepared=resources.prepare(id,math::translation(input.placementPosition),input.sceneCamera,
            input.sceneHeight,false,ResourcePriority::Placement,input.sceneAspect);
        includePreparedModel(stats,prepared);if(!prepared)continue;
        scene.instances.push_back({0,{prepared->asset,math::translation(input.placementPosition)},&prepared->textures,prepared->revision,false,false,&prepared->materials});
        scene.instances.back().castShadows=false;scene.instances.back().receiveShadows=false;scenePins->models.push_back(std::move(prepared));
    }
    scene.pinned=std::move(scenePins);
    if(input.sceneVisible && input.sceneWidth && input.sceneHeight)frame->views.push_back(std::move(scene));
    frame->resourceSignature=signature;

    if(gameVisible) {
        PreparedRenderView game;
        game.identity={RenderViewPurpose::Game,0};game.settings=input.settings;game.settings.camera=*input.gameCamera;
        game.width=input.gameWidth;game.height=input.gameHeight;
        const auto aspect=static_cast<float>(input.gameWidth)/input.gameHeight;
        auto gamePins=std::make_shared<ViewPins>();
        for(const auto& model:input.extracted.models) {
            if(const auto metadata=assets.findModel(model.assetId))
                if(!visibleBounds(resources.modelBounds(model,metadata),model.world,*input.gameCamera,aspect))continue;
            auto prepared=resources.prepare(model.assetId,model.world,*input.gameCamera,input.gameHeight,false,
                ResourcePriority::Visible,aspect,model.materialId,0,&model);
            includePreparedModel(stats,prepared);
            if(prepared)appendModel(game,*gamePins,model,std::move(prepared),false,resources);
        }
        game.pinned=std::move(gamePins);frame->views.push_back(std::move(game));
    }
    if(stats)stats->receiverPreparationMs=std::chrono::duration<double,std::milli>(Clock::now()-receiverStarted).count();
    frame->world=freezeEditorRenderWorld(input.extracted,std::move(input.particles),std::move(input.particleMaterials),
        std::move(worldPins),input.worldId,input.shadowResolution,input.materialContext,std::move(input.shadowFocusEntities),focusBounds,priorityOrigin);
    return frame;
}
} // namespace crimson
