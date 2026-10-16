#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/RenderTypes.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {
using namespace crimson;
using Kind=RenderPassKind;
using Resource=RenderResource;
using Access=RenderResourceAccess;
using Error=RenderPassPlanError;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
RenderPass& pass(RenderPassPlan& plan,Kind kind) {
    for(std::size_t i=0;i<plan.count;++i)if(plan.entries[i].kind==kind)return plan.entries[i];
    throw std::runtime_error("Test requested a missing pass.");
}
void erase(RenderPassPlan& plan,Kind kind) {
    for(std::size_t i=0;i<plan.count;++i)if(plan.entries[i].kind==kind) {
        std::move(plan.entries.begin()+i+1,plan.entries.begin()+plan.count,plan.entries.begin()+i);--plan.count;return;
    }
    throw std::runtime_error("Test tried to erase a missing pass.");
}
void error(const RenderPassPlan& plan,Error expected,const char* message) { check(validateRenderPassPlan(plan).error==expected,message); }

void profiles() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        for(bool glow:{false,true})for(bool grid:{false,true})for(bool ambient:{false,true})for(bool collisions:{false,true})for(bool occluders:{false,true}) {
            RenderPassPlanConfig config;config.view={purpose,871};config.prepareEnvironment=true;
            config.worldShadows=purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::Game || purpose==RenderViewPurpose::Thumbnail;
            config.ambientVisibility=ambient&&config.worldShadows;
            config.glow=glow && purpose!=RenderViewPurpose::Thumbnail;
            config.grid=grid && (purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::ParticlePreview);
            config.picking=purpose==RenderViewPurpose::Scene;config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
            config.collisionOverlays=collisions&&(purpose==RenderViewPurpose::Scene||purpose==RenderViewPurpose::Game);
            config.occluderLayer=occluders&&(purpose==RenderViewPurpose::Scene||purpose==RenderViewPurpose::Game);
            const auto plan=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(plan)),"A supported view profile has an invalid plan.");
            check(plan.has(Kind::SceneColor) && plan.has(Kind::ModelDeformation) && plan.has(Kind::LightAssignment) && plan.has(Kind::ToneMap) && plan.has(Kind::Encode),"View lost a mandatory rendering stage.");
            check(plan.has(Kind::GlowExtract)==config.glow && plan.has(Kind::GlowHorizontal)==config.glow && plan.has(Kind::GlowVertical)==config.glow,"Glow feature did not add exactly the complete chain.");
            check(plan.has(Kind::Grid)==config.grid && plan.has(Kind::Picking)==config.picking && plan.has(Kind::PickReadback)==config.picking,"Grid or picking did not follow the view profile.");
            check(plan.has(Kind::ThumbnailReadback)==config.thumbnailReadback,"Thumbnail readback escaped its isolated profile.");
            check(plan.has(Kind::CollisionOverlays)==config.collisionOverlays,"Collision overlays escaped their active view profile.");
            check(plan.has(Kind::OccluderLayer)==config.occluderLayer,"Occluder layer escaped its active view profile.");
            check(plan.has(Kind::OccluderDepthPublish)==config.occluderLayer,"Packed occluder depth escaped its active view profile.");
            check(plan.has(Kind::DepthPrepass)==config.ambientVisibility&&plan.has(Kind::AmbientVisibility)==config.ambientVisibility&&
                plan.has(Kind::AmbientPublish)==config.ambientVisibility,"Ambient feature did not add exactly its complete chain.");
            const auto& display=plan.bindings[static_cast<std::size_t>(Resource::Display)];
            check(display.owner==RenderResourceOwner::ViewTargets && display.view==config.view && !display.initiallyAvailable,"View output is not produced by its own target owner.");
            const auto& shadow=plan.bindings[static_cast<std::size_t>(Resource::ShadowAtlas)];
            check(shadow.initiallyAvailable,"Persistent cached shadows require a false transient producer.");
            check(shadow.owner==(purpose==RenderViewPurpose::Thumbnail?RenderResourceOwner::ThumbnailWorldShadows:RenderResourceOwner::ActiveWorldShadows),"Thumbnail shadows share the active world's owner.");
            for(std::size_t i=1;i<plan.count;++i)check(plan.entries[i-1].physicalView<=plan.entries[i].physicalView,"View plan does not follow GPU execution order.");
        }
    }
    RenderPassPlanConfig scene;scene.view.purpose=RenderViewPurpose::Scene;scene.worldShadows=true;scene.glow=true;scene.grid=true;scene.picking=true;
    const auto scenePlan=buildRenderPassPlan(scene);
    check(scenePlan.find(Kind::PointShadowUpdates)->physicalView==1 && scenePlan.find(Kind::PointShadowUpdates)->lastPhysicalView()==96,"Active world point shadow range changed.");
    check(scenePlan.find(Kind::DirectionalShadowUpdates)->physicalView==97 && scenePlan.find(Kind::DirectionalShadowUpdates)->lastPhysicalView()==100,"Scene directional range changed.");
    check(scenePlan.find(Kind::SceneColor)->physicalView==108 && scenePlan.find(Kind::GlowExtract)->physicalView==109 && scenePlan.find(Kind::GlowVertical)->physicalView==111,"Scene HDR/Glow order changed.");
    check(scenePlan.find(Kind::ToneMap)->physicalView==112 && scenePlan.find(Kind::Grid)->physicalView==113 && scenePlan.find(Kind::Encode)->physicalView==114,"Scene display composition order changed.");
    check(scenePlan.find(Kind::Picking)->physicalView==122 && scenePlan.find(Kind::PickReadback)->physicalView==123,"Picking is no longer after the encoded Scene view.");

    RenderPassPlanConfig game;game.view.purpose=RenderViewPurpose::Game;game.worldShadows=true;game.glow=true;
    const auto gamePlan=buildRenderPassPlan(game);
    check(gamePlan.find(Kind::DirectionalShadowUpdates)->physicalView==101 && gamePlan.find(Kind::SceneColor)->physicalView==241,"Game shadow or HDR slots changed.");
    check(gamePlan.find(Kind::ToneMap)->physicalView==245 && gamePlan.find(Kind::Encode)->physicalView==246,"Game encoding is no longer before editor tools.");
    RenderPassPlanConfig thumbnail;thumbnail.view={RenderViewPurpose::Thumbnail,24};thumbnail.worldShadows=true;thumbnail.thumbnailReadback=true;
    const auto thumbPlan=buildRenderPassPlan(thumbnail);
    check(thumbPlan.find(Kind::PointShadowUpdates)->physicalView==124 && thumbPlan.find(Kind::PointShadowUpdates)->lastPhysicalView()==219,"Thumbnail point shadow range changed.");
    check(thumbPlan.find(Kind::DirectionalShadowUpdates)->physicalView==220 && thumbPlan.find(Kind::Encode)->physicalView==230 && thumbPlan.find(Kind::ThumbnailReadback)->physicalView==231,"Thumbnail composition/readback range changed.");
    check(scenePlan.find(Kind::CharacterShadowUpdates)->physicalView==105 && gamePlan.find(Kind::CharacterShadowUpdates)->physicalView==105 &&
        thumbPlan.find(Kind::CharacterShadowUpdates)->physicalView==224,"Dedicated character shadows lost their world-owned pre-color slots.");
    check(FirstPlatformView==248&&ToolsView==247&&256-FirstPlatformView==8,"Native platform-window headroom changed.");
}

void ambientDependencies() {
    const auto uses=[](const RenderPass& entry,Resource resource,Access access) {
        return std::find(entry.uses().begin(),entry.uses().end(),RenderResourceUse{resource,access})!=entry.uses().end();
    };
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::Thumbnail})for(bool shadows:{false,true}) {
        RenderPassPlanConfig config;config.view={purpose,14};config.ambientVisibility=true;config.prepareEnvironment=true;config.worldShadows=shadows;
        config.glow=purpose!=RenderViewPurpose::Thumbnail;config.grid=purpose==RenderViewPurpose::Scene;config.picking=config.grid;
        config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
        config.collisionOverlays=purpose==RenderViewPurpose::Scene||purpose==RenderViewPurpose::Game;
        const auto correct=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(correct)),"Complete ambient chain is invalid.");
        if(purpose==RenderViewPurpose::Scene&&shadows)check(correct.count==RenderPassPlan::MaximumPasses-2,"Scene profile without an occluder layer lost fixed-capacity coverage.");
        const auto& depth=*correct.find(Kind::DepthPrepass);const auto& evaluate=*correct.find(Kind::AmbientVisibility);
        const auto& publish=*correct.find(Kind::AmbientPublish);const auto& color=*correct.find(Kind::SceneColor);
        check(depth.physicalView<evaluate.physicalView&&evaluate.physicalView==publish.physicalView&&publish.physicalView<color.physicalView,
            "Depth/evaluate/publish/color ordering is invalid.");
        check(uses(depth,Resource::ReadableSceneDepth,Access::DepthWrite)&&uses(depth,Resource::ReceiverScopes,Access::Write),
            "Prepass lost depth or receiver-scope output.");
        check(uses(evaluate,Resource::ReadableSceneDepth,Access::Read)&&uses(evaluate,Resource::ReceiverScopes,Access::Read)&&
            uses(evaluate,Resource::ClusteredLighting,Access::Read)&&uses(evaluate,Resource::AmbientScratch,Access::Write)&&
            uses(evaluate,Resource::ShadowAtlas,Access::Read)==shadows,"Ambient evaluation has incorrect sampled inputs/output.");
        check(uses(publish,Resource::AmbientScratch,Access::Read)&&uses(publish,Resource::ClusteredLighting,Access::ReadWrite)&&
            uses(publish,Resource::AmbientVisibility,Access::Write)&&uses(color,Resource::AmbientVisibility,Access::Read),
            "Ambient visibility was not published through the cluster-atlas alias.");
        const auto& assignment=*correct.find(Kind::LightAssignment);
        check(assignment.physicalView==0&&assignment.resourceCount==3,"Ambient feature changed conservative light assignment dependencies.");
        auto missingDepth=correct;erase(missingDepth,Kind::DepthPrepass);error(missingDepth,Error::MissingProducer,"Ambient evaluation read unproduced depth.");
        auto missingEvaluate=correct;erase(missingEvaluate,Kind::AmbientVisibility);error(missingEvaluate,Error::MissingProducer,"Ambient publish read unproduced scratch.");
        auto missingPublish=correct;erase(missingPublish,Kind::AmbientPublish);error(missingPublish,Error::MissingProducer,"Scene sampled unpublished ambient visibility.");
        auto reordered=correct;std::swap(pass(reordered,Kind::AmbientVisibility),pass(reordered,Kind::AmbientPublish));
        error(reordered,Error::MissingProducer,"Same-view ambient dispatches may publish before evaluation.");
        auto attachmentOnly=correct;pass(attachmentOnly,Kind::AmbientVisibility).resources[0].access=Access::DepthRead;
        error(attachmentOnly,Error::InvalidAccess,"Ambient sampled depth was modeled as an attachment test.");
        auto lostAtlas=correct;pass(lostAtlas,Kind::AmbientPublish).resources[1].access=Access::Write;
        error(lostAtlas,Error::InvalidAccess,"Ambient publish may discard the existing cluster atlas.");
        auto disabled=config;disabled.ambientVisibility=false;const auto legacy=buildRenderPassPlan(disabled);
        check(bool(validateRenderPassPlan(legacy))&&!legacy.has(Kind::DepthPrepass)&&!legacy.has(Kind::AmbientVisibility)&&!legacy.has(Kind::AmbientPublish)&&
            !uses(*legacy.find(Kind::SceneColor),Resource::AmbientVisibility,Access::Read),"Disabled ambient changed the legacy logical profile.");
    }
    for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview}) {
        const auto slots=renderViewPassSlots(purpose);
        check(slots.depthPrepass==InvalidRenderView&&slots.ambient==InvalidRenderView,"Preview acquired world ambient slots.");
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.ambientVisibility=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"World ambient work escaped into a preview.");
    }
}

void dependencyAndAccessFailures() {
    RenderPassPlanConfig config;config.glow=true;config.grid=true;config.picking=true;config.worldShadows=true;config.prepareEnvironment=true;
    const auto correct=buildRenderPassPlan(config);
    auto missingLight=correct;erase(missingLight,Kind::LightAssignment);error(missingLight,Error::MissingProducer,"Scene shading accepted missing cluster assignment.");
    auto missingScene=correct;erase(missingScene,Kind::SceneColor);error(missingScene,Error::MissingProducer,"Glow accepted missing scene HDR.");
    auto missingBlur=correct;erase(missingBlur,Kind::GlowHorizontal);error(missingBlur,Error::MissingProducer,"Vertical blur read unproduced horizontal output.");
    auto missingTone=correct;erase(missingTone,Kind::ToneMap);error(missingTone,Error::MissingProducer,"Grid accepted missing tone-mapped composite.");
    auto missingPick=correct;erase(missingPick,Kind::Picking);error(missingPick,Error::MissingProducer,"Picking readback accepted unproduced IDs.");
    auto missingEncode=correct;erase(missingEncode,Kind::Encode);error(missingEncode,Error::MissingPass,"Scene plan accepted a missing final encode.");
    auto missingDeformation=correct;erase(missingDeformation,Kind::ModelDeformation);error(missingDeformation,Error::MissingProducer,"Shadow/color accepted missing GPU deformation output.");
    auto missingShadow=correct;erase(missingShadow,Kind::PointShadowUpdates);error(missingShadow,Error::MissingPass,"Shadow update contract disappeared behind persistent initialization.");
    auto sampledDepth=correct;pass(sampledDepth,Kind::Grid).resources[0].access=Access::Read;error(sampledDepth,Error::InvalidAccess,"Write-only scene depth became a sampled shader input.");
    auto feedback=correct;pass(feedback,Kind::ToneMap).resources[0].access=Access::ReadWrite;error(feedback,Error::InvalidAccess,"Tone mapping accepted HDR feedback.");
    auto writeImported=correct;pass(writeImported,Kind::SceneColor).resources[0].access=Access::Write;error(writeImported,Error::InvalidAccess,"Scene rendering may mutate the immutable prepared frame.");
    auto omittedRead=correct;--pass(omittedRead,Kind::Encode).resourceCount;error(omittedRead,Error::InvalidAccess,"Encode accepted an incomplete resource contract.");
    auto duplicated=correct;auto& encode=pass(duplicated,Kind::Encode);encode.resources[encode.resourceCount++]=encode.resources[0];error(duplicated,Error::DuplicateResourceUse,"Duplicate access may hide a feedback dependency.");
    auto invalidResource=correct;pass(invalidResource,Kind::Encode).resources[0].resource=Resource::Count;error(invalidResource,Error::InvalidResource,"Invalid resource index was accepted.");
}
void collisionComposition() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game})for(bool grid:{false,true}) {
        RenderPassPlanConfig config;config.view.purpose=purpose;config.grid=grid&&purpose==RenderViewPurpose::Scene;config.collisionOverlays=true;
        const auto correct=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(correct)),"Collision composition has an invalid pass order.");
        const auto& overlays=*correct.find(Kind::CollisionOverlays);
        check(overlays.physicalView>=correct.find(Kind::ToneMap)->physicalView && overlays.physicalView<correct.find(Kind::Encode)->physicalView,
            "Collision overlay escaped display-linear composition.");
        check(overlays.resources[1]==RenderResourceUse{Resource::SceneDepth,Access::DepthRead},"Collision overlay lost its shared scene-depth test.");
        auto writesDepth=correct;pass(writesDepth,Kind::CollisionOverlays).resources[1].access=Access::DepthWrite;
        error(writesDepth,Error::InvalidAccess,"Collision overlay may replace the scene depth buffer.");
        auto missingTone=correct;erase(missingTone,Kind::ToneMap);error(missingTone,Error::MissingProducer,"Collision overlay accepted unproduced display-linear color.");
    }
    for(const auto purpose:{RenderViewPurpose::Thumbnail,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview}) {
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.collisionOverlays=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"Collision overlays escaped into asset previews.");
    }
}

void occluderDependencies() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game})for(bool ambient:{false,true})for(bool shadows:{false,true}) {
        RenderPassPlanConfig config;config.view.purpose=purpose;config.ambientVisibility=ambient;config.worldShadows=shadows;
        config.occluderLayer=true;config.glow=true;config.grid=purpose==RenderViewPurpose::Scene;
        config.picking=config.grid;config.collisionOverlays=true;config.prepareEnvironment=true;
        const auto correct=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(correct)),"Nearest occluder layer has invalid dependencies.");
        const auto& layer=*correct.find(Kind::OccluderLayer);
        const auto& depthPublish=*correct.find(Kind::OccluderDepthPublish);
        check(layer.physicalView==renderViewPassSlots(purpose).ambient && layer.physicalView<correct.find(Kind::SceneColor)->physicalView,
            "Occluder layer must use its camera's pre-color ambient slot without consuming native window slots.");
        check(depthPublish.physicalView==correct.find(Kind::SceneColor)->physicalView && layer.physicalView<depthPublish.physicalView,
            "Packed depth must publish after detached layer depth and before graphics in the existing color slot.");
        if(purpose==RenderViewPurpose::Scene&&ambient&&shadows)
            check(correct.count==RenderPassPlan::MaximumPasses,"Maximum occluder profile no longer fits the pass capacity.");
        if(ambient) {
            const auto& publish=*correct.find(Kind::AmbientPublish);
            check(publish.physicalView==layer.physicalView,"The pinned compute-before-draw slot contract changed.");
            auto reversed=correct;
            for(std::size_t i=0;i<reversed.count;++i)if(reversed.entries[i].kind==Kind::OccluderLayer) {
                std::swap(reversed.entries[i],reversed.entries[i-1]);break;
            }
            error(reversed,Error::PhysicalOrder,"Ambient publication accepted a preceding occluder draw in its logical view.");
        }
        auto missingLayer=correct;erase(missingLayer,Kind::OccluderLayer);
        error(missingLayer,Error::MissingProducer,"Scene composition read an unproduced occluder layer.");
        auto missingPublish=correct;erase(missingPublish,Kind::OccluderDepthPublish);
        error(missingPublish,Error::MissingProducer,"Transparent phases sampled an unpublished packed depth region.");
        auto reversedPublish=correct;std::swap(pass(reversedPublish,Kind::OccluderDepthPublish),pass(reversedPublish,Kind::SceneColor));
        error(reversedPublish,Error::MissingProducer,"Scene graphics ran before same-view packed depth compute.");
        auto destructivePublish=correct;pass(destructivePublish,Kind::OccluderDepthPublish).resources[1].access=Access::Write;
        error(destructivePublish,Error::InvalidAccess,"Packed depth publication may overwrite the BRDF/light/ambient atlas contents.");
        auto feedback=correct;auto& color=pass(feedback,Kind::SceneColor);
        for(auto& use:color.resources)if(use.resource==Resource::OccluderDepth)use.access=Access::DepthWrite;
        error(feedback,Error::InvalidAccess,"Occluder composition may overwrite the layer depth it samples.");
        auto falseLayer=correct;falseLayer.bindings[static_cast<std::size_t>(Resource::OccluderHdr)].initiallyAvailable=true;
        error(falseLayer,Error::InvalidExternalState,"Retained layer allocation was treated as current-frame content.");
    }
    for(const auto purpose:{RenderViewPurpose::Thumbnail,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview}) {
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.occluderLayer=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"Occluder layer escaped into an isolated preview.");
    }
}

void ownershipAndOrderFailures() {
    RenderPassPlanConfig config;config.view={RenderViewPurpose::Scene,4};config.worldShadows=true;config.prepareEnvironment=true;
    const auto correct=buildRenderPassPlan(config);
    auto wrongView=correct;wrongView.bindings[static_cast<std::size_t>(Resource::Display)].view={RenderViewPurpose::Game,4};error(wrongView,Error::InvalidOwner,"Scene plan may write the Game target owner.");
    auto wrongInstance=correct;++wrongInstance.bindings[static_cast<std::size_t>(Resource::SceneHdr)].view.instanceId;error(wrongInstance,Error::InvalidOwner,"Two isolated camera instances share a target binding.");
    auto wrongOwner=correct;wrongOwner.bindings[static_cast<std::size_t>(Resource::ShadowAtlas)].owner=RenderResourceOwner::ThumbnailWorldShadows;error(wrongOwner,Error::InvalidOwner,"Active and thumbnail worlds share shadow storage.");
    auto falseProducer=correct;falseProducer.bindings[static_cast<std::size_t>(Resource::SceneHdr)].initiallyAvailable=true;error(falseProducer,Error::InvalidExternalState,"Allocated HDR memory was mistaken for valid current-frame content.");
    auto lostPersistent=correct;lostPersistent.bindings[static_cast<std::size_t>(Resource::EnvironmentLighting)].initiallyAvailable=false;error(lostPersistent,Error::InvalidExternalState,"External coherent IBL data lost persistent initialization.");
    auto reversed=correct;std::swap(reversed.entries[0],reversed.entries[1]);error(reversed,Error::PhysicalOrder,"Environment and light compute dependency may reverse inside view zero.");
    auto numericOrder=correct;std::swap(numericOrder.entries[2],numericOrder.entries[3]);error(numericOrder,Error::PhysicalOrder,"Shadow ranges violated numeric execution order.");
    auto movedEncode=correct;pass(movedEncode,Kind::Encode).physicalView=PickReadView;error(movedEncode,Error::InvalidPhysicalView,"Plan silently remapped the D3D11 backend's view IDs.");
    auto tooManyViews=correct;pass(tooManyViews,Kind::PointShadowUpdates).viewCount=97;error(tooManyViews,Error::InvalidPhysicalView,"Point shadow range overlaps directional views.");
    auto duplicate=correct;duplicate.entries[duplicate.count++]=duplicate.entries[0];error(duplicate,Error::DuplicatePass,"A duplicated environment pass was accepted.");
    auto oversized=correct;oversized.count=static_cast<std::uint8_t>(RenderPassPlan::MaximumPasses+1);error(oversized,Error::CapacityExceeded,"Corrupt pass count escaped validation.");

    for(const auto purpose:{RenderViewPurpose::Game,RenderViewPurpose::MaterialPreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.grid=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"A view gained an unsolicited grid path.");
    }
    RenderPassPlanConfig thumb;thumb.view.purpose=RenderViewPurpose::Thumbnail;thumb.glow=true;
    error(buildRenderPassPlan(thumb),Error::UnsupportedFeature,"Thumbnails gained post-processing-dependent cache content.");
    RenderPassPlanConfig preview;preview.view.purpose=RenderViewPurpose::ParticlePreview;preview.worldShadows=true;
    error(buildRenderPassPlan(preview),Error::UnsupportedFeature,"Preview acquired the active world's shadows.");
    preview.worldShadows=false;preview.picking=true;error(buildRenderPassPlan(preview),Error::UnsupportedFeature,"Picking escaped the Scene view.");
    RenderPassPlanConfig invalid;invalid.view.purpose=static_cast<RenderViewPurpose>(255);
    error(buildRenderPassPlan(invalid),Error::InvalidViewPurpose,"Unknown camera purpose was accepted.");
    RenderPassPlanConfig overfull;overfull.prepareEnvironment=overfull.glow=overfull.grid=overfull.picking=
        overfull.thumbnailReadback=overfull.worldShadows=overfull.ambientVisibility=true;
    error(buildRenderPassPlan(overfull),Error::UnsupportedFeature,"Unsupported over-capacity profile escaped safe construction.");
}
}

int main() {
    try {
        static_assert(std::is_trivially_copyable_v<RenderPassPlan>,"Pass plans must remain fixed-size CPU values.");
        profiles();ambientDependencies();dependencyAndAccessFailures();collisionComposition();occluderDependencies();ownershipAndOrderFailures();
        std::cout<<"Renderer pass-plan checks passed.\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
