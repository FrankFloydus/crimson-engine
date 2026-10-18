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
        for(bool glow:{false,true})for(bool grid:{false,true})for(bool collisions:{false,true})for(bool gi:{false,true}) {
            RenderPassPlanConfig config;config.view={purpose,871};config.prepareEnvironment=true;
            config.worldShadows=purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::Game || purpose==RenderViewPurpose::Thumbnail;

            config.glow=glow && purpose!=RenderViewPurpose::Thumbnail;
            config.grid=grid && (purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::ParticlePreview);
            config.picking=purpose==RenderViewPurpose::Scene;config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
            config.collisionOverlays=collisions&&(purpose==RenderViewPurpose::Scene||purpose==RenderViewPurpose::Game);
            config.ssgi=gi&&(purpose==RenderViewPurpose::Scene||purpose==RenderViewPurpose::Game);
            const auto plan=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(plan)),"A supported view profile has an invalid plan.");
            check(plan.has(Kind::SceneColor) && plan.has(Kind::ModelDeformation) && plan.has(Kind::LightAssignment) && plan.has(Kind::ToneMap) && plan.has(Kind::Encode),"View lost a mandatory rendering stage.");
            check(plan.has(Kind::GlowExtract)==config.glow && plan.has(Kind::GlowHorizontal)==config.glow && plan.has(Kind::GlowVertical)==config.glow,"Glow feature did not add exactly the complete chain.");
            check(plan.has(Kind::Grid)==config.grid && plan.has(Kind::Picking)==config.picking && plan.has(Kind::PickReadback)==config.picking,"Grid or picking did not follow the view profile.");
            check(plan.has(Kind::ThumbnailReadback)==config.thumbnailReadback,"Thumbnail readback escaped its isolated profile.");
            check(plan.has(Kind::CollisionOverlays)==config.collisionOverlays,"Collision overlays escaped their active view profile.");
            check(plan.has(Kind::DepthPrepass)==config.ssgi,"GI depth prepass does not match its enabled state.");
            for(const auto kind:{Kind::SsgiTrace,Kind::SsgiTemporal,Kind::SsgiFilterHorizontal,Kind::SsgiFilterVertical,Kind::SsgiPublish})
                check(plan.has(kind)==config.ssgi,"GI feature did not add exactly its complete chain.");
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
    check(FirstPlatformView==248&&ToolsView==247&&256-FirstPlatformView==8,"Native platform-window headroom changed.");
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

void ssgiDependencies() {
    constexpr std::array chain{Kind::SsgiTrace,Kind::SsgiTemporal,Kind::SsgiFilterHorizontal,Kind::SsgiFilterVertical,Kind::SsgiPublish};
    const auto uses=[](const RenderPass& entry,Resource resource,Access access) {
        return std::find(entry.uses().begin(),entry.uses().end(),RenderResourceUse{resource,access})!=entry.uses().end();
    };
    const auto position=[](const RenderPassPlan& plan,Kind kind) {
        for(std::size_t i=0;i<plan.count;++i)if(plan.entries[i].kind==kind)return i;
        throw std::runtime_error("GI test requested a missing pass position.");
    };
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game})for(bool shadows:{false,true}) {
        RenderPassPlanConfig config;config.view={purpose,901};config.ssgi=true;
        config.worldShadows=shadows;config.glow=true;config.prepareEnvironment=true;config.collisionOverlays=true;
        config.grid=purpose==RenderViewPurpose::Scene;config.picking=config.grid;
        const auto correct=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(correct)),"A supported GI combination has an invalid plan.");
        const auto& depth=*correct.find(Kind::DepthPrepass);const auto& trace=*correct.find(Kind::SsgiTrace);const auto& temporal=*correct.find(Kind::SsgiTemporal);
        const auto& publish=*correct.find(Kind::SsgiPublish);const auto& color=*correct.find(Kind::SceneColor);
        check(depth.physicalView<trace.physicalView&&publish.physicalView<color.physicalView,"GI escaped the detached-depth/pre-color slots.");
        for(std::size_t i=0;i<chain.size();++i) {
            check(correct.find(chain[i])->physicalView==renderViewPassSlots(purpose).lightingEffects,"GI consumed an extra native-window view slot.");
            if(i)check(position(correct,chain[i])==position(correct,chain[i-1])+1,"Same-view GI compute dispatches are not consecutive.");
        }
        check(uses(trace,Resource::ReadableSceneDepth,Access::Read)&&uses(trace,Resource::ReceiverScopes,Access::Read)&&
            uses(trace,Resource::SsgiSources,Access::Read)&&uses(trace,Resource::SsgiTraceScratch,Access::Write),"GI trace lost current geometry or coherent previous radiance.");
        check(uses(temporal,Resource::SsgiHistory,Access::ReadWrite)&&uses(temporal,Resource::SsgiTraceScratch,Access::Read)&&
            uses(temporal,Resource::SsgiTemporalScratch,Access::Write),"GI history update lost its explicit input/output contract.");
        check(uses(publish,Resource::ClusteredLighting,Access::ReadWrite)&&uses(publish,Resource::SsgiFilterB,Access::Read)&&
            uses(publish,Resource::SsgiIrradiance,Access::Write)&&uses(color,Resource::SsgiIrradiance,Access::Read)&&
            uses(color,Resource::SsgiSources,Access::Write),"GI publication/capture lost its packed-atlas or immutable source contract.");
        for(const auto resource:{Resource::SsgiSources,Resource::SsgiHistory,Resource::SsgiTraceScratch,Resource::SsgiTemporalScratch,Resource::SsgiFilterA,Resource::SsgiFilterB,Resource::SsgiIrradiance}) {
            const auto& binding=correct.bindings[static_cast<std::size_t>(resource)];
            check(binding.owner==RenderResourceOwner::ViewTargets&&binding.view==config.view,"GI resources crossed camera or target ownership.");
            check(binding.initiallyAvailable==(resource==Resource::SsgiSources||resource==Resource::SsgiHistory),"GI treated retained scratch as coherent current-frame data.");
            auto wrongView=correct;wrongView.bindings[static_cast<std::size_t>(resource)].view.purpose=purpose==RenderViewPurpose::Scene?RenderViewPurpose::Game:RenderViewPurpose::Scene;
            error(wrongView,Error::InvalidOwner,"Scene and Game GI histories share an owner.");
            auto wrongInstance=correct;++wrongInstance.bindings[static_cast<std::size_t>(resource)].view.instanceId;
            error(wrongInstance,Error::InvalidOwner,"Two GI camera instances share a history or scratch binding.");
            auto falseState=correct;falseState.bindings[static_cast<std::size_t>(resource)].initiallyAvailable=!binding.initiallyAvailable;
            error(falseState,Error::InvalidExternalState,"GI coherent import/scratch availability was accepted incorrectly.");
        }
        for(const auto kind:{Kind::DepthPrepass,Kind::SsgiTrace,Kind::SsgiTemporal,Kind::SsgiFilterHorizontal,Kind::SsgiFilterVertical,Kind::SsgiPublish}) {
            auto missing=correct;erase(missing,kind);check(!bool(validateRenderPassPlan(missing)),"A missing GI producer was accepted.");
        }
        for(std::size_t i=1;i<chain.size();++i) {
            auto reversed=correct;std::swap(pass(reversed,chain[i-1]),pass(reversed,chain[i]));
            check(!bool(validateRenderPassPlan(reversed)),"A reversed same-view GI compute dependency was accepted.");
        }
        auto attachmentOnly=correct;pass(attachmentOnly,Kind::SsgiTrace).resources[0].access=Access::DepthRead;
        error(attachmentOnly,Error::InvalidAccess,"GI shader reads were mistaken for an attached depth test.");
        auto destructivePublish=correct;
        for(auto& use:pass(destructivePublish,Kind::SsgiPublish).resources)if(use.resource==Resource::ClusteredLighting)use.access=Access::Write;
        error(destructivePublish,Error::InvalidAccess,"GI publication may discard unrelated light/BRDF/ambient atlas contents.");
        auto currentSourceFeedback=correct;
        for(auto& use:pass(currentSourceFeedback,Kind::SsgiTrace).resources)if(use.resource==Resource::SsgiSources)use.access=Access::ReadWrite;
        error(currentSourceFeedback,Error::InvalidAccess,"GI tracing may overwrite its previous-frame source input.");
        auto disabledConfig=config;disabledConfig.ssgi=false;const auto disabled=buildRenderPassPlan(disabledConfig);
        check(bool(validateRenderPassPlan(disabled)),"Disabling GI left an invalid baseline plan.");
        check(correct.count-disabled.count==6,"Disabled GI retained dispatches or an unnecessary depth prepass.");
        check(!disabled.has(Kind::DepthPrepass),"GI retained its readable prepass after disabling.");
        for(const auto kind:chain)check(!disabled.has(kind),"Disabled GI retained a compute dispatch.");
        for(const auto& entry:disabled.passes())for(const auto use:entry.uses())
            check(use.resource<Resource::SsgiSources,"Disabled GI retained a source/history/scratch/irradiance dependency.");
    }
    for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.ssgi=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"GI escaped into an independent preview or a single-frame thumbnail.");
    }
}

void ssaoDependencies() {
    constexpr std::array chain{Kind::SsaoEvaluate,Kind::SsaoPublish};
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game})for(bool ssgi:{false,true}) {
        RenderPassPlanConfig config;config.view={purpose,912};config.ssao=true;config.ssgi=ssgi;
        config.worldShadows=true;config.glow=true;config.prepareEnvironment=true;
        config.collisionOverlays=true;config.grid=purpose==RenderViewPurpose::Scene;config.picking=config.grid;
        const auto correct=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(correct)),"SSAO has an invalid combined pass chain.");
        for(const auto kind:chain) {
            check(correct.find(kind)->physicalView==renderViewPassSlots(purpose).lightingEffects,"SSAO consumed extra native-window views.");
            auto missing=correct;erase(missing,kind);check(!bool(validateRenderPassPlan(missing)),"An SSAO producer is optional while its consumer remains.");
        }
        auto reversed=correct;std::swap(pass(reversed,chain[0]),pass(reversed,chain[1]));
        check(!bool(validateRenderPassPlan(reversed)),"SSAO same-view dispatch dependencies may reverse.");
        check(correct.has(Kind::DepthPrepass),"SSAO lost readable receiver depth.");
        auto disabled=config;disabled.ssao=false;const auto baseline=buildRenderPassPlan(disabled);
        check(bool(validateRenderPassPlan(baseline))&&correct.count-baseline.count==(ssgi?2:3),"Disabled SSAO retained compute or depth work.");
    }
    for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig config;config.view.purpose=purpose;config.ssao=true;
        error(buildRenderPassPlan(config),Error::UnsupportedFeature,"SSAO escaped into an isolated preview.");
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
        overfull.thumbnailReadback=overfull.worldShadows=overfull.ssao=true;
    error(buildRenderPassPlan(overfull),Error::UnsupportedFeature,"Unsupported over-capacity profile escaped safe construction.");
}
}

int main() {
    try {
        static_assert(std::is_trivially_copyable_v<RenderPassPlan>,"Pass plans must remain fixed-size CPU values.");
        profiles();dependencyAndAccessFailures();collisionComposition();ssgiDependencies();ssaoDependencies();ownershipAndOrderFailures();
        std::cout<<"Renderer pass-plan checks passed.\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
