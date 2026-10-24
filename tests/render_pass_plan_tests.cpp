#include "crimson-renderer/RenderPassPlan.h"
#include "crimson-renderer/RenderTypes.h"

#include <bgfx/bgfx.h>
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
using Load=RenderAttachmentLoad;
using Store=RenderAttachmentStore;
constexpr std::size_t index(Resource resource) { return static_cast<std::size_t>(resource); }
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
RenderPass& pass(RenderPassPlan& plan,Kind kind) {
    for(std::size_t i=0;i<plan.count;++i)if(plan.entries[i].kind==kind)return plan.entries[i];
    throw std::runtime_error("Test requested a missing pass.");
}
RenderResourceUse& use(RenderPassPlan& plan,Kind kind,Resource resource) {
    auto& entry=pass(plan,kind);
    for(auto& value:entry.resources)if(value.resource==resource)return value;
    throw std::runtime_error("Test requested a missing resource use.");
}
void erase(RenderPassPlan& plan,Kind kind) {
    for(std::size_t i=0;i<plan.count;++i)if(plan.entries[i].kind==kind) {
        std::move(plan.entries.begin()+i+1,plan.entries.begin()+plan.count,plan.entries.begin()+i);--plan.count;return;
    }
    throw std::runtime_error("Test tried to erase a missing pass.");
}
void error(const RenderPassPlan& plan,Error expected,const char* message) {
    const auto result=validateRenderPassPlan(plan);
    if(result.error!=expected) {
        std::cerr<<message<<" Actual error: "<<renderPassPlanErrorText(result.error)<<" resource "<<unsigned(result.resource)<<" pass "<<unsigned(result.passIndex)<<'\n';
    }
    check(result.error==expected,message);
}
void error(const RenderPassPlan& plan,const RenderResourceTable& table,Error expected,const char* message) {
    const auto result=validateRenderPassPlan(plan,table);
    if(result.error!=expected) {
        std::cerr<<message<<" Actual error: "<<renderPassPlanErrorText(result.error)<<" resource "<<unsigned(result.resource)<<" pass "<<unsigned(result.passIndex)<<'\n';
        throw std::runtime_error(message);
    }
}
RenderPassPlanConfig fullConfig(RenderViewPurpose purpose=RenderViewPurpose::Scene) {
    RenderPassPlanConfig config;config.view={purpose,871};config.prepareEnvironment=true;
    config.glowBaseWidth=config.glowBaseHeight=32;
    config.glow=true;config.grid=purpose==RenderViewPurpose::Scene;config.picking=purpose==RenderViewPurpose::Scene;
    config.worldShadows=true;config.collisionOverlays=true;config.ssgi=true;config.ssao=true;return config;
}
RenderTextureView texture(std::uint32_t id,std::uint32_t width=64,std::uint32_t height=64,std::uint16_t format=1) {
    RenderTextureView result;result.physicalId=id;result.generation=7;result.format=format;result.width=width;result.height=height;return result;
}
RenderResourceTable concrete(const RenderPassPlan& plan) {
    auto table=makeRenderResourceTable(plan);
    std::array<bool,RenderPassPlan::ResourceCount> needed{};
    for(const auto& entry:plan.passes())for(const auto value:entry.uses())needed[index(value.resource)]=true;
    for(std::size_t i=0;i<needed.size();++i)if(needed[i]) {
        const auto resource=static_cast<Resource>(i);
        if(resource==Resource::PreparedScene || resource==Resource::DeformedGeometry ||
           resource==Resource::EnvironmentLighting || resource==Resource::ShadowAtlas)continue;
        auto& binding=table.bindings[i];binding.textureCount=1;
        if(resource==Resource::SsgiSourcesPrevious || resource==Resource::SsgiSourcesCurrent)binding.textureCount=3;
        for(unsigned lane=0;lane<binding.textureCount;++lane)binding.textures[lane]=texture(static_cast<std::uint32_t>(i*4+lane+1));
        if(resource==Resource::SsgiHistoryPrevious || resource==Resource::SsgiHistoryCurrent ||
           resource==Resource::SsgiTraceScratch || resource==Resource::SsgiTemporalScratch ||
           resource==Resource::SsgiFilterA || resource==Resource::SsgiFilterB || resource==Resource::SsgiFilterC) {
            binding.textures[0].layers=2;binding.textures[0].range.layerCount=2;
        }
    }
    if(plan.config.glow) {
        const auto& config=plan.config;
        const auto first_prefilter=Resource::GlowPrefilter0;
        const auto first_down=Resource::GlowDown0;
        const auto first_up=Resource::GlowUp0;
        const unsigned base_width=config.glowBaseWidth,base_height=config.glowBaseHeight;
        unsigned width=64,height=64;
        const auto set_extent=[&](Resource resource,unsigned w,unsigned h) {
            auto& image=table.bindings[index(resource)].textures[0];image.width=w;image.height=h;
        };
        const unsigned prefilters=config.glowPrefilters;
        for(unsigned i=0;i<prefilters;++i) {
            width=std::max(base_width,(width+1)/2);height=std::max(base_height,(height+1)/2);
            set_extent(static_cast<Resource>(unsigned(first_prefilter)+i),width,height);
        }
        width=base_width;height=base_height;
        const unsigned levels=config.glowLevels;
        for(unsigned i=0;i<levels;++i) {
            set_extent(static_cast<Resource>(unsigned(first_down)+i),width,height);
            if(i<std::max(1u,levels-1))set_extent(static_cast<Resource>(unsigned(first_up)+i),width,height);
            width=(width+1)/2;height=(height+1)/2;
        }
    }
    if(plan.config.ssgi) {
        table.bindings[index(Resource::SsgiSourcesCurrent)].textures=table.bindings[index(Resource::SsgiSourcesPrevious)].textures;
        table.bindings[index(Resource::SsgiHistoryCurrent)].textures=table.bindings[index(Resource::SsgiHistoryPrevious)].textures;
        table.bindings[index(Resource::SsgiFilterB)].textures=table.bindings[index(Resource::SsgiTraceScratch)].textures;
        table.bindings[index(Resource::SsgiFilterC)].textures=table.bindings[index(Resource::SsgiFilterA)].textures;
    }
    auto atlas=texture(500,64,plan.config.shadow_contact?128:64,4);
    auto& lighting=table.bindings[index(Resource::ClusteredLighting)];lighting.textures[0]=atlas;
    lighting.textures[0].range.texelCount=1024;
    auto& shadowGeometry=table.bindings[index(Resource::ShadowRayGeometry)];shadowGeometry.textures[0]=atlas;
    shadowGeometry.textures[0].range.firstTexel=1046;shadowGeometry.textures[0].range.texelCount=23;
    if(plan.config.shadow_contact) {
        constexpr auto format=static_cast<std::uint16_t>(bgfx::TextureFormat::RGBA32F);
        for(const auto resource:{Resource::ReceiverPosition,Resource::ReceiverPlane,Resource::ShadowContactScratch})
            table.bindings[index(resource)].textures[0].format=format;
        auto& visibility=table.bindings[index(Resource::ShadowContactVisibility)];visibility.textures[0]=atlas;
        visibility.textures[0].range.firstTexel=1069;visibility.textures[0].range.texelCount=4096;
    }
    if(plan.config.ssgi) {
        auto& gi=table.bindings[index(Resource::SsgiIrradiance)];gi.textures[0]=atlas;
        gi.textures[0].range.firstTexel=1024;gi.textures[0].range.texelCount=17;
    }
    if(plan.config.ssao) {
        auto& ao=table.bindings[index(Resource::SsaoVisibility)];ao.textures[0]=atlas;
        ao.textures[0].range.firstTexel=1041;ao.textures[0].range.texelCount=5;
    }
    if(plan.config.refraction) {
        auto snapshot=texture(6000,64,64,5);snapshot.mipLevels=plan.config.refraction_levels;
        table.bindings[index(Resource::RefractionMatte)].textures[0]=texture(6200,64,64,6);
        for(unsigned level=0;level<plan.config.refraction_levels;++level) {
            auto& mip=table.bindings[index(static_cast<Resource>(unsigned(Resource::RefractionMip0)+level))];
            mip.textures[0]=snapshot;mip.textures[0].range.firstMip=std::uint16_t(level);
            if(level) {
                auto& scratch=table.bindings[index(static_cast<Resource>(unsigned(Resource::RefractionScratch1)+level-1))];
                scratch.textures[0]=texture(6100+level,std::max(1u,64u>>level),std::max(1u,64u>>level),5);
            }
        }
    }
    if(plan.config.ssr)for(bool depth:{false,true}) {
        auto chain=texture(depth?7100:7000,64,64,depth?7:1);chain.mipLevels=plan.config.ssr_levels;
        for(unsigned level=0;level<plan.config.ssr_levels;++level) {
            const auto resource=static_cast<Resource>(unsigned(depth?Resource::SsrDepthMip0:Resource::SsrColorMip0)+level);
            auto& mip=table.bindings[index(resource)];mip.textures[0]=chain;mip.textures[0].range.firstMip=std::uint16_t(level);
            if(level) {
                const auto scratch=static_cast<Resource>(unsigned(depth?Resource::SsrDepthScratch1:Resource::SsrColorScratch1)+level-1);
                table.bindings[index(scratch)].textures[0]=texture((depth?7300:7200)+level,std::max(1u,64u>>level),std::max(1u,64u>>level),chain.format);
            }
        }
    }
    if(plan.config.ssr) {
        table.bindings[index(Resource::SsrRaw)].textures[0]=texture(7400,64,64,1);
        table.bindings[index(Resource::SsrGuide)].textures[0]=texture(7401,64,64,1);
        table.bindings[index(Resource::SsrFilterA)].textures[0]=texture(7402,64,64,1);
        table.bindings[index(Resource::SsrFilterB)].textures[0]=texture(7403,64,64,1);
    }
    return table;
}

void profiles() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail,RenderViewPurpose::RagdollPreview})
        for(bool glow:{false,true})for(bool grid:{false,true})for(bool collisions:{false,true})for(bool gi:{false,true})for(bool ao:{false,true})for(bool smaa:{false,true})for(bool refraction:{false,true})for(bool ssr:{false,true}) {
            const bool world=purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::Game;
            RenderPassPlanConfig config;config.view={purpose,871};config.prepareEnvironment=true;
            config.worldShadows=world || purpose==RenderViewPurpose::Thumbnail;
            config.glow=glow && purpose!=RenderViewPurpose::Thumbnail && purpose!=RenderViewPurpose::RagdollPreview;
            config.grid=grid && (purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::ParticlePreview || purpose==RenderViewPurpose::RagdollPreview);
            config.picking=purpose==RenderViewPurpose::Scene;config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
            config.collisionOverlays=collisions && world;config.ssgi=gi && world;config.ssao=ao && world;
            config.smaa=smaa;
            config.refraction=refraction && purpose!=RenderViewPurpose::RagdollPreview;config.refraction_levels=7;
            config.ssr=ssr && purpose!=RenderViewPurpose::RagdollPreview;config.ssr_levels=7;
            config.glowBaseWidth=config.glowBaseHeight=32;
            const auto plan=buildRenderPassPlan(config);check(bool(validateRenderPassPlan(plan)),"A supported profile has an invalid structural plan.");
            const auto bindingsValidation=validateRenderPassPlan(plan,concrete(plan));
            if(!bindingsValidation)std::cerr<<"Profile purpose="<<unsigned(purpose)<<" glow="<<config.glow<<" grid="<<config.grid
                <<" collisions="<<config.collisionOverlays<<" gi="<<config.ssgi<<" ao="<<config.ssao<<" smaa="<<config.smaa
                <<" refraction="<<config.refraction<<" ssr="<<config.ssr<<": "<<renderPassPlanErrorText(bindingsValidation.error)
                <<" resource="<<unsigned(bindingsValidation.resource)<<" pass="<<unsigned(bindingsValidation.passIndex)<<'\n';
            check(bool(bindingsValidation),"A supported profile has invalid concrete resource bindings.");
            for(const auto kind:{Kind::ModelDeformation,Kind::LightAssignment,Kind::Background,Kind::Opaque,Kind::Transparent,Kind::ToneMap,Kind::Encode})
                check(plan.has(kind),"A view lost a mandatory stage.");
            check(plan.find(Kind::Background)->physicalView<plan.find(Kind::Opaque)->physicalView &&
                plan.find(Kind::Opaque)->physicalView<plan.find(Kind::Transparent)->physicalView,"Color queues share or reverse physical execution views.");
            check(plan.has(Kind::DepthPrepass)==(config.ssgi || config.ssao || (config.ssr && config.refraction)),"Depth prepass does not follow active effects.");
            for(const auto kind:{Kind::SsgiTrace,Kind::SsgiTemporal,Kind::SsgiReconstruct,Kind::SsgiFilterMedium,Kind::SsgiFilterWide,Kind::SsgiPublish})
                check(plan.has(kind)==config.ssgi,"GI does not contain exactly the complete enabled chain.");
            if(const auto* depth=plan.find(Kind::DepthPrepass))
                check(std::any_of(depth->uses().begin(),depth->uses().end(),[](const auto& item){return item.resource==Resource::ObjectMotion;})==config.ssgi,
                    "Object-motion attachment work escaped the GI profile.");
            check(plan.has(Kind::SsaoEvaluate)==config.ssao && plan.has(Kind::SsaoPublish)==config.ssao,"AO does not contain exactly its enabled chain.");
            check(plan.has(Kind::SmaaEdges)==smaa && plan.has(Kind::SmaaWeights)==smaa && plan.has(Kind::SmaaBlend)==smaa,"Spatial AA does not contain exactly its enabled chain.");
            check(plan.has(Kind::Grid)==config.grid && plan.has(Kind::CollisionOverlays)==config.collisionOverlays,"Overlay work escaped its profile.");
            check(plan.has(Kind::Picking)==config.picking && plan.has(Kind::PickReadback)==config.picking && plan.has(Kind::ThumbnailReadback)==config.thumbnailReadback,"Readbacks escaped their profile.");
            check(plan.bindings[index(Resource::ShadowAtlas)].owner==(purpose==RenderViewPurpose::Thumbnail?RenderResourceOwner::ThumbnailWorldShadows:RenderResourceOwner::ActiveWorldShadows),"Thumbnail shadows share the active world owner.");
        }
    const auto scene=buildRenderPassPlan(fullConfig());
    check(scene.find(Kind::PointShadowUpdates)->physicalView==1 && scene.find(Kind::PointShadowUpdates)->lastPhysicalView()==96,"Active point-shadow range changed.");
    check(scene.find(Kind::Background)->physicalView==114 && scene.find(Kind::Opaque)->physicalView==115 && scene.find(Kind::Transparent)->physicalView==128,"Scene split-stage layout changed.");
    check(scene.find(Kind::GlowDown0)->physicalView==136 && scene.find(Kind::ToneMap)->physicalView==144 && scene.find(Kind::Encode)->physicalView==159,"Scene post order changed.");
    const auto game=buildRenderPassPlan(fullConfig(RenderViewPurpose::Game));
    check(game.find(Kind::Background)->physicalView==370 && game.find(Kind::Opaque)->physicalView==371 && game.find(Kind::Transparent)->physicalView==384,"Game split-stage layout changed.");
    check(game.find(Kind::CollisionOverlays)->physicalView==408 && game.find(Kind::Encode)->physicalView==415,"Game overlays no longer follow tone mapping.");
    check(RendererViewCapacity==512 && ExternalFrameViewLimit==248 && ToolsView==496 && FirstPlatformView==504 && PlatformTerminalView==511,"Shared view capacity or external/native-window headroom changed.");
}
void attachmentAndDependencyContracts() {
    const auto correct=buildRenderPassPlan(fullConfig());
    auto value=correct;erase(value,Kind::LightAssignment);error(value,Error::MissingProducer,"Opaque shading accepted missing light assignment.");
    value=correct;erase(value,Kind::Background);error(value,Error::UndefinedLoad,"Opaque attachments loaded without background initialization.");
    value=correct;erase(value,Kind::GlowDown1);error(value,Error::MissingProducer,"Glare pyramid read an unproduced downsample level.");
    value=correct;erase(value,Kind::ToneMap);error(value,Error::UndefinedLoad,"Grid loaded an undefined display-linear composite.");
    value=correct;erase(value,Kind::Picking);error(value,Error::MissingProducer,"Picking readback accepted unproduced IDs.");
    value=correct;erase(value,Kind::ModelDeformation);error(value,Error::MissingProducer,"Shadow/color accepted missing deformed geometry.");
    value=correct;erase(value,Kind::Encode);error(value,Error::MissingPass,"Plan omitted final display encoding.");
    value=correct;use(value,Kind::Opaque,Resource::SceneHdr).load=Load::Clear;error(value,Error::InvalidLoadStore,"Opaque rendering accepted a second HDR clear.");
    value=correct;use(value,Kind::Opaque,Resource::SceneDepth).store=Store::Discard;error(value,Error::InvalidLoadStore,"Opaque rendering discarded depth needed by later stages.");
    value=correct;use(value,Kind::Transparent,Resource::SceneDepth).access=Access::DepthAttachmentWrite;error(value,Error::InvalidAccess,"Transparent rendering may overwrite opaque depth.");
    value=correct;use(value,Kind::Grid,Resource::SceneDepth).access=Access::SampledRead;error(value,Error::InvalidAccess,"Write-only scene depth became a shader input.");
    value=correct;use(value,Kind::Opaque,Resource::PreparedScene).access=Access::Write;error(value,Error::InvalidAccess,"Color may mutate its prepared frame.");
    value=correct;--pass(value,Kind::Encode).resourceCount;error(value,Error::InvalidAccess,"Encode accepted an incomplete contract.");
    value=correct;auto& encode=pass(value,Kind::Encode);encode.resources[encode.resourceCount++]=encode.resources[0];error(value,Error::DuplicateResourceUse,"Duplicate resource use hid a dependency.");
    value=correct;pass(value,Kind::Encode).resources[0].resource=Resource::Count;error(value,Error::InvalidResource,"Invalid resource index was accepted.");
    const auto& transparent=*correct.find(Kind::Transparent);
    check(std::none_of(transparent.uses().begin(),transparent.uses().end(),[](const auto& item){return item.resource==Resource::SsgiSourcesCurrent;}),"Transparent stages can attach opaque GI donor MRTs.");
}
void effectTopology() {
    const auto correct=buildRenderPassPlan(fullConfig());
    auto value=correct;erase(value,Kind::DepthPrepass);error(value,Error::MissingProducer,"Effects read missing depth/scope data.");
    value=correct;erase(value,Kind::SsgiTrace);error(value,Error::MissingProducer,"Temporal reprojection accepted missing trace/metadata.");
    value=correct;erase(value,Kind::SsgiTemporal);error(value,Error::MissingProducer,"GI reconstruction accepted missing temporal radiance/moments.");
    value=correct;erase(value,Kind::SsgiReconstruct);error(value,Error::MissingProducer,"GI filtering accepted missing reconstructed radiance/history.");
    value=correct;erase(value,Kind::SsgiFilterMedium);error(value,Error::MissingProducer,"Wide GI filtering accepted missing intermediate scratch.");
    value=correct;erase(value,Kind::SsgiFilterWide);error(value,Error::MissingProducer,"GI publication accepted missing wide reconstruction.");
    value=correct;erase(value,Kind::SsgiPublish);error(value,Error::MissingProducer,"Opaque shading accepted unpublished indirect lighting.");
    value=correct;erase(value,Kind::SsaoEvaluate);error(value,Error::MissingProducer,"AO publication accepted missing scratch.");
    value=correct;erase(value,Kind::SsaoPublish);error(value,Error::MissingProducer,"Opaque shading accepted unpublished AO.");
    const auto& temporal=*correct.find(Kind::SsgiTemporal);
    check(std::none_of(temporal.uses().begin(),temporal.uses().end(),[](const auto& item){return item.resource==Resource::ReadableSceneDepth || item.resource==Resource::ReceiverScopes || item.resource==Resource::SsgiSourcesPrevious;}),"Temporal pass declares depth/donor inputs it does not bind.");
    for(const auto resource:{Resource::ObjectMotion,Resource::SsgiMomentsPrevious,Resource::SsgiMomentsCurrent,Resource::SsgiTemporalScratch})
        check(std::any_of(temporal.uses().begin(),temporal.uses().end(),[&](const auto& item){return item.resource==resource;}),
            "Temporal reconstruction omitted motion, moments or its distinct scratch output.");
    check(std::none_of(temporal.uses().begin(),temporal.uses().end(),[](const auto& item){return item.resource==Resource::SsgiHistoryCurrent;}),
        "Temporal reconstruction overwrites spatial feedback before consuming it.");
    const auto& reconstruct=*correct.find(Kind::SsgiReconstruct);
    check(std::any_of(reconstruct.uses().begin(),reconstruct.uses().end(),[](const auto& item){
        return item.resource==Resource::SsgiHistoryCurrent && item.access==Access::StorageWrite;
    }),
        "Early spatial reconstruction no longer publishes next-frame feedback.");
    check(std::any_of(reconstruct.uses().begin(),reconstruct.uses().end(),[](const auto& item){return item.resource==Resource::SsgiMomentsCurrent;}),
        "Short-history reconstruction omitted its independent luminance moments.");
    for(const auto kind:{Kind::SsgiReconstruct,Kind::SsgiFilterMedium,Kind::SsgiFilterWide}) {
        const auto& entry=*correct.find(kind);
        check(std::any_of(entry.uses().begin(),entry.uses().end(),[](const auto& item){return item.resource==Resource::SsgiMetadataCurrent;}),"Geometry-guided filter omitted current metadata.");
        check(std::none_of(entry.uses().begin(),entry.uses().end(),[](const auto& item){return item.resource==Resource::ReadableSceneDepth || item.resource==Resource::ReceiverScopes;}),"Filter declares unbound raw depth/scope inputs.");
    }
    for(const auto kind:{Kind::SsgiPublish,Kind::SsaoPublish}) {
        const auto& entry=*correct.find(kind);
        check(std::none_of(entry.uses().begin(),entry.uses().end(),[](const auto& item){return item.resource==Resource::ClusteredLighting;}),"Publication hides disjoint atlas writes behind whole-atlas ReadWrite.");
    }
}
void shadow_contact_contracts() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game})for(bool gi:{false,true})for(bool ao:{false,true}) {
        RenderPassPlanConfig config;config.view={purpose,997};config.worldShadows=true;
        config.shadow_contact=true;config.ssgi=gi;config.ssao=ao;config.refraction=true;
        const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
        check(bool(validateRenderPassPlan(plan,resources)),"Exact opaque shadow visibility has invalid pass/resource dependencies.");
        check(plan.has(Kind::DepthPrepass)&&!plan.has(Kind::RefractionDepth),"Contact visibility did not force the early receiver prepass.");
        const auto* depth=plan.find(Kind::DepthPrepass);const auto* trace=plan.find(Kind::ShadowContactTrace);
        const auto* publish=plan.find(Kind::ShadowContactPublish);
        check(depth->physicalView<trace->physicalView&&trace->physicalView==publish->physicalView&&trace<publish&&
            publish->physicalView<plan.find(Kind::Opaque)->physicalView,"Contact visibility escaped the existing pre-color compute boundary.");
        for(const auto resource:{Resource::ObjectMotion,Resource::ReceiverPosition,Resource::ReceiverPlane})
            check(std::any_of(depth->uses().begin(),depth->uses().end(),[&](const auto& item){return item.resource==resource;}),
                "Fixed receiver attachment indices omitted motion or exact FP32 plane inputs.");
        auto broken=plan;erase(broken,Kind::DepthPrepass);
        error(broken,Error::MissingProducer,"Geometry visibility accepted missing exact receiver inputs.");
        broken=plan;erase(broken,Kind::ShadowContactTrace);
        error(broken,Error::MissingProducer,"Contact publication sampled an unproduced scratch target.");
        broken=plan;erase(broken,Kind::ShadowContactPublish);
        error(broken,Error::MissingProducer,"Opaque lighting accepted an unpublished contact mask.");
        broken=plan;std::swap(pass(broken,Kind::ShadowContactTrace),pass(broken,Kind::ShadowContactPublish));
        error(broken,Error::MissingProducer,"Contact publication ran before tracing in the shared compute view.");
        auto aliased=resources;
        aliased.bindings[index(Resource::ShadowRayGeometry)].textures=aliased.bindings[index(Resource::ShadowContactScratch)].textures;
        error(plan,aliased,Error::FeedbackLoop,"Contact tracing sampled and overwrote the geometry atlas.");
        aliased=resources;aliased.bindings[index(Resource::ShadowContactVisibility)].textures[0].range.firstTexel=1046;
        error(plan,aliased,Error::IncompatibleAlias,"Contact publication overlapped immutable caster geometry.");
        auto missing=resources;missing.bindings[index(Resource::ShadowContactScratch)].textureCount=0;
        error(plan,missing,Error::MissingBinding,"Contact tracing accepted a missing scratch allocation.");
        auto resized=resources;resized.bindings[index(Resource::ReceiverPlane)].textures[0].width/=2;
        error(plan,resized,Error::InvalidTexture,"Contact tracing accepted another receiver plane footprint.");
        resized=resources;--resized.bindings[index(Resource::ShadowContactVisibility)].textures[0].range.texelCount;
        error(plan,resized,Error::InvalidTexture,"Contact publication omitted a primary receiver pixel.");
        resized=resources;resized.bindings[index(Resource::ShadowContactScratch)].textures[0].format=
            static_cast<std::uint16_t>(bgfx::TextureFormat::RGBA16F);
        error(plan,resized,Error::InvalidTexture,"Contact tracing lost exact RGBA32F scratch storage.");
        auto prefix=resources;prefix.bindings[index(Resource::ShadowContactVisibility)].textureCount=0;
        check(bool(validateRenderPassBindings(plan,prefix,Kind::ShadowContactTrace)),
            "Tracing required a future visibility allocation before its publication boundary.");
        config.shadow_contact=false;const auto off=buildRenderPassPlan(config);
        check(!off.has(Kind::ShadowContactTrace)&&!off.has(Kind::ShadowContactPublish),"Disabled contact retained compute work.");
        for(const auto& pass:off.passes())for(const auto use:pass.uses())
            check(use.resource!=Resource::ReceiverPosition&&use.resource!=Resource::ReceiverPlane&&
                use.resource!=Resource::ShadowContactScratch&&use.resource!=Resource::ShadowContactVisibility,
                "Disabled contact retained exact receiver or visibility dependencies.");
    }
    for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig config;config.view.purpose=purpose;config.shadow_contact=true;
        error(buildRenderPassPlan(config),Error::UnsupportedFeature,"Opaque world contact leaked into an unsupported preview profile.");
    }
}
void concreteHazards() {
    const auto plan=buildRenderPassPlan(fullConfig());const auto correct=concrete(plan);
    auto table=correct;table.bindings[index(Resource::SceneHdr)].view.instanceId++;error(plan,table,Error::InvalidOwner,"A view accepted another view's HDR allocation.");
    table=correct;table.bindings[index(Resource::SsgiHistoryPrevious)].owner=RenderResourceOwner::Environment;error(plan,table,Error::InvalidOwner,"History escaped its camera target owner.");
    table=correct;table.bindings[index(Resource::SceneHdr)].initialized=true;error(plan,table,Error::InvalidExternalState,"A current HDR target pretended to be coherent imported content.");
    table=correct;table.bindings[index(Resource::Composite)].textureCount=0;error(plan,table,Error::MissingBinding,"A recording accepted an absent composite allocation.");
    table=correct;table.bindings[index(Resource::PickReadback)].textureCount=0;error(plan,table,Error::MissingBinding,"Picking accepted an absent staging texture destination.");
    table=correct;table.bindings[index(Resource::SsgiSourcesCurrent)].textureCount=1;error(plan,table,Error::InvalidTexture,"Capture MRT bundle omitted two actual physical lanes.");
    table=correct;table.bindings[index(Resource::ObjectMotion)].textureCount=0;error(plan,table,Error::MissingBinding,"GI accepted an absent object-motion allocation.");
    table=correct;table.bindings[index(Resource::SsgiTemporalScratch)].textures[0].layers=1;
    table.bindings[index(Resource::SsgiTemporalScratch)].textures[0].range.layerCount=1;
    error(plan,table,Error::InvalidTexture,"A single-layer temporal texture silently discarded Character irradiance.");
    table=correct;table.bindings[index(Resource::SsgiFilterB)].textures[0].range.layerCount=1;
    error(plan,table,Error::InvalidTexture,"Filtering bound only one of the two irradiance array layers.");
    table=correct;table.bindings[index(Resource::SceneDepth)].textures[0].range.firstMip=1;error(plan,table,Error::InvalidTexture,"A subresource escaped its allocation mip range.");
    table=correct;table.bindings[index(Resource::SceneHdr)].readEnabled=false;error(plan,table,Error::DisabledRead,"An ordinary texture bypassed its read dependency.");
    table=correct;table.bindings[index(Resource::SsgiSourcesCurrent)].textures[0].format++;error(plan,table,Error::IncompatibleAlias,"One physical capture allocation accepted incompatible descriptors.");
    table=correct;table.bindings[index(Resource::SsgiSourcesCurrent)].textures[0].generation++;error(plan,table,Error::StaleVersion,"A generation tag hid the same live physical handle in two logical bindings.");
    table=correct;table.bindings[index(Resource::Composite)].textures=table.bindings[index(Resource::SceneHdr)].textures;error(plan,table,Error::FeedbackLoop,"Tone mapping sampled its own attached output.");
    table=correct;table.bindings[index(Resource::PickColor)].textures=table.bindings[index(Resource::GlowUp0)].textures;error(plan,table,Error::IncompatibleAlias,"Unrelated picking and Glow allocations aliased silently.");
    table=correct;table.bindings[index(Resource::SsaoVisibility)].textures[0].range.firstTexel=1040;error(plan,table,Error::IncompatibleAlias,"GI and AO shared an overlapping packed atlas texel.");
    table=correct;table.bindings[index(Resource::ShadowRayGeometry)].textures[0].range.firstTexel=1045;
    error(plan,table,Error::IncompatibleAlias,"Shadow ray geometry overlapped the independently published AO atlas range.");
    table=correct;table.bindings[index(Resource::ShadowRayGeometry)].readEnabled=false;
    error(plan,table,Error::DisabledRead,"Shadow ray geometry bypassed its declared surface dependency.");
    table=correct;table.bindings[index(Resource::SsgiTemporalScratch)].textures=table.bindings[index(Resource::SsgiHistoryPrevious)].textures;error(plan,table,Error::FeedbackLoop,"Temporal compute sampled and overwrote its spatial feedback allocation.");
    table=correct;table.bindings[index(Resource::SsgiHistoryCurrent)].textures=table.bindings[index(Resource::SsgiTemporalScratch)].textures;error(plan,table,Error::FeedbackLoop,"Early reconstruction sampled and overwrote temporal scratch.");
    table=correct;table.bindings[index(Resource::SsgiMomentsCurrent)].textures=table.bindings[index(Resource::SsgiMomentsPrevious)].textures;error(plan,table,Error::FeedbackLoop,"Temporal compute sampled and overwrote independent raw moments.");
    table=correct;table.bindings[index(Resource::SsgiFilterB)].textures=table.bindings[index(Resource::SsgiFilterA)].textures;error(plan,table,Error::FeedbackLoop,"Multi-scale GI filtering sampled and overwrote the same radiance array.");
    table=correct;table.bindings[index(Resource::SsgiMetadataCurrent)].textures=table.bindings[index(Resource::SsgiMetadataPrevious)].textures;error(plan,table,Error::StaleVersion,"Temporal compute read previous metadata after trace overwrote its physical storage.");
    table=correct;table.bindings[index(Resource::SsgiSourcesPrevious)].initialized=false;error(plan,table,Error::MissingProducer,"An enabled source read accepted uninitialized first-frame capture.");
    table=correct;table.bindings[index(Resource::SsgiMomentsPrevious)].initialized=false;
    error(plan,table,Error::MissingProducer,"Temporal reconstruction accepted undefined imported luminance moments.");
    table=correct;
    for(const auto resource:{Resource::SsgiSourcesPrevious,Resource::SsgiHistoryPrevious,Resource::SsgiMetadataPrevious,Resource::SsgiMomentsPrevious}) {
        table.bindings[index(resource)].initialized=false;table.bindings[index(resource)].readEnabled=false;
    }
    check(bool(validateRenderPassPlan(plan,table)),"Actual first-frame shader guards did not permit undefined previous inputs.");
    // Subresource overlap is evaluated by mip/layer/aspect and linear interval,
    // not a whole-allocation boolean or row-rounded atlas rectangle.
    table=correct;
    auto metadata=texture(900,64,64,5);metadata.mipLevels=2;metadata.layers=2;
    table.bindings[index(Resource::SsgiMetadataCurrent)].textures[0]=metadata;
    metadata.range.firstMip=1;metadata.range.firstLayer=1;
    table.bindings[index(Resource::SsgiMetadataPrevious)].textures[0]=metadata;
    check(bool(validateRenderPassPlan(plan,table)),"Disjoint metadata mip/layer views were treated as whole-texture feedback.");
    table=correct;table.bindings[index(Resource::Display)].textureCount=0;
    check(bool(validateRenderPassBindings(plan,table,Kind::SsgiTrace)),"Before-dispatch validation required an unallocated later display output.");
    check(validateRenderPassBindings(plan,table,Kind::Encode).error==Error::MissingBinding,"Final recording boundary accepted an absent display output.");
}
void glarePyramidContracts() {
    for(unsigned levels=1;levels<=7;++levels) {
        auto config=fullConfig();config.glowLevels=static_cast<std::uint8_t>(levels);
        const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
        check(bool(validateRenderPassPlan(plan,resources)),"An effective Glow level count has invalid bindings.");
        constexpr auto down=Kind::GlowDown0,last=Kind::GlowUp0;
        unsigned down_count=0,up_count=0;
        for(const auto& entry:plan.passes()) {
            if(entry.kind>=down && entry.kind<=Kind::GlowDown6)++down_count;
            if(entry.kind>=Kind::GlowUp5 && entry.kind<=last)++up_count;
        }
        check(down_count==levels && up_count==std::max(1u,levels-1),"Unused Glow levels retained dispatches or a needed scale disappeared.");
        check(plan.find(down)->execution==RenderPassExecution::Compute &&
            plan.find(last)->physicalView==plan.find(down)->physicalView &&
            plan.find(last)->physicalView<plan.find(Kind::ToneMap)->physicalView,"Glow pyramid escaped ordered compute before tone mapping.");
        check(std::any_of(plan.find(down)->uses().begin(),plan.find(down)->uses().end(),[](const auto& item){
            return item.resource==Resource::SceneHdr && item.access==Access::SampledRead;
        }),"Glow did not sample the complete visible HDR scene.");
        auto broken=plan;erase(broken,down);error(broken,Error::MissingProducer,"A Glow chain accepted an absent original-source extraction.");
        if(levels>1) {
            broken=plan;erase(broken,static_cast<Kind>(unsigned(down)+levels-1));
            error(broken,Error::MissingProducer,"Reconstruction read a missing coarse Glow level.");
        }
        auto bad=resources;bad.bindings[index(Resource::GlowUp0)].textures=bad.bindings[index(Resource::GlowDown0)].textures;
        error(plan,bad,Error::FeedbackLoop,"Glow reconstruction sampled its own output allocation.");
        broken=plan;erase(broken,last);error(broken,Error::MissingProducer,"Tone mapping accepted unpublished Glow.");
    }
    for(unsigned prefilters=0;prefilters<=5;++prefilters) {
        auto config=fullConfig();const auto base=static_cast<std::uint16_t>(32u>>prefilters);
        config.glowBaseWidth=config.glowBaseHeight=base;config.glowPrefilters=static_cast<std::uint8_t>(prefilters);
        const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
        check(bool(validateRenderPassPlan(plan,resources)),"A bounded Glow prefilter chain has invalid concrete extents.");
        constexpr auto first=Kind::GlowPrefilter0;
        for(unsigned i=0;i<5;++i)check(plan.has(static_cast<Kind>(unsigned(first)+i))==(i<prefilters),"Glow prefix includes disabled stages or skips a required source reduction.");
        if(prefilters) {
            auto broken=plan;erase(broken,first);error(broken,Error::MissingProducer,"A Glow prefix read an unproduced reduced source.");
            auto bad=resources;--bad.bindings[index(Resource::GlowPrefilter0)].textures[0].width;
            error(plan,bad,Error::InvalidTexture,"An incorrect prefilter extent silently increased the source reduction ratio.");
            bad=resources;++bad.bindings[index(Resource::GlowDown0)].textures[0].height;
            error(plan,bad,Error::InvalidTexture,"A stale artistic base allocation was accepted.");
            auto missing=plan;--missing.config.glowPrefilters;
            error(missing,Error::UnexpectedPass,"An undeclared Glow prefix remained in the plan.");
        }
    }
    auto maximum=fullConfig();maximum.glowPrefilters=5;maximum.glowBaseWidth=maximum.glowBaseHeight=1;maximum.smaa=true;
    const auto maximum_plan=buildRenderPassPlan(maximum);
    check(maximum_plan.count<=RenderPassPlan::MaximumPasses && bool(validateRenderPassPlan(maximum_plan,concrete(maximum_plan))),"Maximum Glow chain exceeds the fixed plan capacity.");
    auto config=fullConfig();config.glowLevels=0;error(buildRenderPassPlan(config),Error::InvalidPass,"Active Glow accepted zero levels.");
    config.glowLevels=8;error(buildRenderPassPlan(config),Error::InvalidPass,"Glow accepted an eighth unsupported scale.");
    config=fullConfig();config.glow=false;const auto off=buildRenderPassPlan(config);
    for(const auto& entry:off.passes())for(const auto& item:entry.uses())
        check(item.resource<Resource::GlowPrefilter0 || item.resource>Resource::GlowUp5,"Disabled Glow retained pyramid dependencies.");
    const auto full=buildRenderPassPlan(fullConfig());
    for(const auto kind:{Kind::Background,Kind::Opaque,Kind::Transparent}) {
        const auto* enabled=full.find(kind);const auto* disabled=off.find(kind);
        check(enabled->resourceCount==disabled->resourceCount && std::equal(enabled->uses().begin(),enabled->uses().end(),disabled->uses().begin()),
            "Enabling full-scene Glow changed scene attachments or GI donor capture.");
    }
}
void spatialAntiAliasingContracts() {
    check(SceneSettings{}.antiAliasing==AntiAliasing::Off && valid_anti_aliasing(AntiAliasing::Off) && valid_anti_aliasing(AntiAliasing::Smaa) &&
        !valid_anti_aliasing(static_cast<AntiAliasing>(255)),"Renderer AA settings lost their explicit Off default or enum validation.");
    auto config=fullConfig();config.smaa=true;
    const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
    check(bool(validateRenderPassPlan(plan,resources)),"SMAA resource bindings failed validation.");
    check(plan.find(Kind::ToneMap)->physicalView<plan.find(Kind::SmaaEdges)->physicalView &&
        plan.find(Kind::SmaaEdges)->physicalView<plan.find(Kind::SmaaWeights)->physicalView &&
        plan.find(Kind::SmaaWeights)->physicalView<plan.find(Kind::SmaaBlend)->physicalView &&
        plan.find(Kind::SmaaBlend)->physicalView<plan.find(Kind::Grid)->physicalView &&
        plan.find(Kind::Grid)->physicalView<plan.find(Kind::Encode)->physicalView,"SMAA does not run after tone mapping and before editor overlays/encoding.");
    for(const auto kind:{Kind::Grid,Kind::CollisionOverlays,Kind::Encode}) {
        const auto& entry=*plan.find(kind);
        check(std::any_of(entry.uses().begin(),entry.uses().end(),[](const auto& item){return item.resource==Resource::SmaaOutput;}),"Display composition reads unfiltered color after SMAA.");
        check(std::none_of(entry.uses().begin(),entry.uses().end(),[](const auto& item){return item.resource==Resource::Composite;}),"An editor overlay bypasses or precedes SMAA.");
    }
    auto broken=plan;erase(broken,Kind::SmaaEdges);error(broken,Error::MissingProducer,"SMAA weights accepted missing edge data.");
    broken=plan;erase(broken,Kind::SmaaWeights);error(broken,Error::MissingProducer,"SMAA blend accepted missing neighborhood weights.");
    broken=plan;erase(broken,Kind::SmaaBlend);error(broken,Error::UndefinedLoad,"Grid loaded AA color without neighborhood blending.");
    broken=plan;use(broken,Kind::SmaaEdges,Resource::SmaaEdges).load=Load::DontCare;error(broken,Error::InvalidLoadStore,"SMAA edge discards left undefined edge pixels.");
    broken=plan;use(broken,Kind::SmaaBlend,Resource::SceneDepth).access=Access::DepthAttachmentWrite;error(broken,Error::InvalidAccess,"SMAA replaced opaque depth needed by editor overlays.");
    auto badResources=resources;badResources.bindings[index(Resource::SmaaArea)].initialized=false;error(plan,badResources,Error::MissingProducer,"SMAA weights sampled an uninitialized reference area LUT.");
    badResources=resources;badResources.bindings[index(Resource::SmaaSearch)].textureCount=0;error(plan,badResources,Error::MissingBinding,"SMAA weights sampled an absent reference search LUT.");
    badResources=resources;badResources.bindings[index(Resource::SmaaOutput)].textures=badResources.bindings[index(Resource::Composite)].textures;error(plan,badResources,Error::FeedbackLoop,"SMAA sampled and attached the same color image.");
    badResources=resources;badResources.bindings[index(Resource::SmaaEdges)].view.instanceId++;error(plan,badResources,Error::InvalidOwner,"Two views shared SMAA scratch ownership.");
    config.smaa=false;const auto off=buildRenderPassPlan(config);
    check(plan.count==off.count+3,"Disabling AA retained extra passes.");
    for(const auto& entry:off.passes())for(const auto& item:entry.uses())
        check(item.resource<Resource::SmaaEdges || item.resource>Resource::SmaaSearch,"Disabled AA retained scratch/LUT dependencies.");
    check(std::any_of(off.find(Kind::Encode)->uses().begin(),off.find(Kind::Encode)->uses().end(),[](const auto& item){return item.resource==Resource::Composite;}),"Off did not retain the ordinary tone-map composition path.");
}
void optionalPresentationContracts() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::Thumbnail})for(bool smaa:{false,true}) {
        auto config=fullConfig(purpose);config.ssgi=config.ssao=false;
        config.glow=false;config.smaa=smaa;config.postProcess=true;
        config.picking=purpose==RenderViewPurpose::Scene;
        config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
        config.collisionOverlays=purpose!=RenderViewPurpose::Thumbnail;
        auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
        check(bool(validateRenderPassPlan(plan,resources)),"Optional presentation effect resources failed validation.");
        const auto& effect=*plan.find(Kind::PostProcess);
        const auto input=smaa?Resource::SmaaOutput:Resource::Composite;
        check(effect.physicalView>plan.find(smaa?Kind::SmaaBlend:Kind::ToneMap)->physicalView &&
            effect.physicalView<plan.find(Kind::Encode)->physicalView,"Presentation effects escaped their post-AA/pre-encoding boundary.");
        check(use(plan,Kind::PostProcess,input).access==Access::SampledRead,
            "Presentation effects bypassed the completed tone-map/AA input.");
        for(const auto kind:{Kind::Grid,Kind::CollisionOverlays,Kind::Encode})if(const auto* entry=plan.find(kind)) {
            check(effect.physicalView<entry->physicalView,"An editor overlay was pixelized or vignetted.");
            check(std::any_of(entry->uses().begin(),entry->uses().end(),[](const auto& item){return item.resource==Resource::PostProcessOutput;}),
                "Presentation composition did not consume the optional effect output.");
        }
        auto broken=plan;erase(broken,Kind::PostProcess);
        error(broken,purpose==RenderViewPurpose::Thumbnail?Error::MissingProducer:Error::UndefinedLoad,
            "Presentation consumers accepted a missing effect output.");
        auto badResources=resources;
        badResources.bindings[index(Resource::PostProcessOutput)].textures=badResources.bindings[index(input)].textures;
        error(plan,badResources,Error::FeedbackLoop,"Presentation effects sampled their own attached output.");
        badResources=resources;badResources.bindings[index(Resource::PostProcessOutput)].view.instanceId++;
        error(plan,badResources,Error::InvalidOwner,"Two views shared optional presentation output.");
        config.postProcess=false;const auto off=buildRenderPassPlan(config);
        check(plan.count==off.count+1 && !off.has(Kind::PostProcess),"Disabled presentation effects retained a raster pass.");
        for(const auto& entry:off.passes())for(const auto& item:entry.uses())
            check(item.resource!=Resource::PostProcessOutput,"Disabled presentation effects retained output dependencies.");
    }
}
void refractionContracts() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail})
        for(bool gi:{false,true})for(bool ao:{false,true}) {
            const bool world=purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::Game;
            RenderPassPlanConfig config;config.view={purpose,721};config.refraction=true;config.refraction_levels=7;
            config.ssgi=gi&&world;config.ssao=ao&&world;config.thumbnailReadback=purpose==RenderViewPurpose::Thumbnail;
            const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
            check(bool(validateRenderPassPlan(plan,resources)),"Refraction failed a view's concrete resource contract.");
            const auto slots=renderViewPassSlots(purpose);
            check(plan.has(Kind::RefractionDepth)==(!config.ssgi&&!config.ssao),"Refraction duplicated an existing readable-depth prepass or omitted its late depth pass.");
            const auto& capture=*plan.find(Kind::RefractionCapture);
            check(capture.execution==RenderPassExecution::Compute && capture.physicalView==slots.refraction_capture &&
                plan.find(Kind::Opaque)->physicalView<capture.physicalView && capture.physicalView<plan.find(Kind::Transparent)->physicalView,
                "Refraction snapshot is not captured strictly between opaque and transparent rendering.");
            if(const auto* depth=plan.find(Kind::RefractionDepth))check(plan.find(Kind::Opaque)->physicalView<depth->physicalView &&
                depth->physicalView<capture.physicalView,"Late readable depth escaped its raster-to-compute boundary.");
            check(std::any_of(capture.uses().begin(),capture.uses().end(),[](const auto& item){return item.resource==Resource::SceneHdr && item.access==Access::SampledRead;}),
                "Refraction capture does not read the original HDR source.");
            const auto& matte_init=*plan.find(Kind::RefractionMatteInit);
            check(matte_init.physicalView==capture.physicalView&&matte_init.execution==RenderPassExecution::Compute&&
                matte_init.resources[0].resource==Resource::RefractionMip0&&matte_init.resources[0].access==Access::SampledRead&&
                matte_init.resources[1].resource==Resource::RefractionMatte&&matte_init.resources[1].access==Access::StorageWrite,
                "Refraction matte throughput is not initialized from captured opaque coverage.");
            auto matte_contract=plan;
            check(use(matte_contract,Kind::Transparent,Resource::RefractionMatte).access==Access::ColorAttachment&&
                use(matte_contract,Kind::Transparent,Resource::RefractionMatte).load==Load::Load&&
                use(matte_contract,Kind::ToneMap,Resource::RefractionMatte).access==Access::SampledRead,
                "Transparent compositing and presentation do not share initialized RGB matte throughput.");
            for(unsigned level=1;level<7;++level) {
                const auto filter=static_cast<Kind>(unsigned(Kind::RefractionFilter1)+(level-1)*2),copy=static_cast<Kind>(unsigned(filter)+1);
                const auto& down=*plan.find(filter);const auto& publish=*plan.find(copy);
                check(down.physicalView==capture.physicalView && publish.physicalView==capture.physicalView &&
                    down.execution==RenderPassExecution::Compute && publish.execution==RenderPassExecution::Compute,
                    "Refraction filters do not run in their ordered compute view.");
                check(down.resources[0].resource==static_cast<Resource>(unsigned(Resource::RefractionMip0)+level-1) &&
                    down.resources[1].resource==static_cast<Resource>(unsigned(Resource::RefractionScratch1)+level-1) &&
                    publish.resources[0].resource==down.resources[1].resource &&
                    publish.resources[1].resource==static_cast<Resource>(unsigned(Resource::RefractionMip0)+level),
                    "Refraction does not filter into independent scratch before publishing a lower mip.");
            }
            auto broken=plan;erase(broken,Kind::RefractionCapture);error(broken,Error::MissingProducer,"Refraction accepted an uncaptured base mip.");
            broken=plan;erase(broken,Kind::RefractionMatteInit);error(broken,Error::UndefinedLoad,"Refraction accepted uninitialized presentation matte throughput.");
            broken=plan;erase(broken,Kind::RefractionCopy3);error(broken,Error::MissingProducer,"Refraction accepted a lower mip without publication.");
            auto bad=resources;bad.bindings[index(Resource::RefractionMip0)].textures=bad.bindings[index(Resource::SceneHdr)].textures;
            bad.bindings[index(Resource::RefractionMip0)].textures[0].mipLevels=config.refraction_levels;
            // Matching source dimensions still cannot make scene-color feedback legal.
            check(!validateRenderPassPlan(plan,bad),"Refraction captured into the attached scene HDR allocation.");
            bad=resources;bad.bindings[index(Resource::RefractionScratch2)].textures[0].width++;
            error(plan,bad,Error::InvalidTexture,"Refraction accepted a stale scratch extent.");
            bad=resources;bad.bindings[index(Resource::RefractionMip2)].textures[0].range.firstMip=1;
            error(plan,bad,Error::InvalidTexture,"Refraction lower mips overlap their previous source range.");
            bad=resources;bad.bindings[index(Resource::RefractionMip2)].textures[0].physicalId++;
            error(plan,bad,Error::InvalidTexture,"Refraction lower mips escaped their single snapshot allocation.");
            bad=resources;bad.bindings[index(Resource::RefractionScratch2)].view.instanceId++;
            error(plan,bad,Error::InvalidOwner,"Two views shared refraction scratch ownership.");
            bad=resources;bad.bindings[index(Resource::RefractionMatte)].textures[0].width++;
            error(plan,bad,Error::InvalidTexture,"Refraction accepted a stale matte throughput extent.");
            bad=resources;bad.bindings[index(Resource::RefractionMatte)].textures=bad.bindings[index(Resource::Composite)].textures;
            error(plan,bad,Error::FeedbackLoop,"Tone mapping sampled its attached output as matte throughput.");
            bad=resources;bad.bindings[index(Resource::RefractionMatte)].view.instanceId++;
            error(plan,bad,Error::InvalidOwner,"Two views shared presentation matte throughput.");
            bad=resources;bad.bindings[index(Resource::ReadableSceneDepth)].textureCount=0;
            error(plan,bad,Error::MissingBinding,"Refraction accepted missing foreground rejection depth.");
            config.refraction=false;const auto off=buildRenderPassPlan(config);
            for(const auto& entry:off.passes())for(const auto& item:entry.uses())check(item.resource<Resource::RefractionMip0 || item.resource>Resource::RefractionMatte,
                "Disabled refraction retained snapshot dependencies.");
            check(!off.has(Kind::RefractionCapture)&&!off.has(Kind::RefractionDepth)&&!off.has(Kind::RefractionMatteInit),"Disabled refraction scheduled capture/depth/matte work.");
        }
    auto maximum=fullConfig();maximum.smaa=true;maximum.glowPrefilters=5;maximum.glowBaseWidth=maximum.glowBaseHeight=1;
    maximum.refraction=true;maximum.refraction_levels=MaximumRefractionMipLevels;
    check(bool(validateRenderPassPlan(buildRenderPassPlan(maximum))),"Maximum lighting, Glow, AA and refraction chains exceed bounded plan capacity.");
    maximum.refraction_levels=0;error(buildRenderPassPlan(maximum),Error::InvalidPass,"Refraction accepted no snapshot mip.");
    maximum.refraction_levels=MaximumRefractionMipLevels+1;error(buildRenderPassPlan(maximum),Error::InvalidPass,"Refraction accepted an unsupported seventeenth mip.");
    RenderPassPlanConfig one;one.refraction=true;const auto one_plan=buildRenderPassPlan(one);
    check(bool(validateRenderPassPlan(one_plan,concrete(one_plan))) && !one_plan.has(Kind::RefractionFilter1),"A single-mip refraction snapshot retained filtering work.");
}
void ssrContracts() {
    for(const auto purpose:{RenderViewPurpose::Scene,RenderViewPurpose::Game,RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig config;config.view={purpose,984};config.ssr=true;config.ssr_levels=7;config.refraction=true;config.refraction_levels=7;
        const auto plan=buildRenderPassPlan(config);const auto resources=concrete(plan);
        check(bool(validateRenderPassPlan(plan,resources)),"SSR failed a view's concrete resource contract.");
        const auto slots=renderViewPassSlots(purpose);
        const std::array<std::uint16_t,5> expectedTrace{116,372,338,162,300};
        const auto first=expectedTrace[static_cast<std::size_t>(purpose)];
        check(RenderViewLayoutVersion==5&&slots.ssr_prepare==first&&slots.ssr_trace==first&&slots.ssr_reconstruct==first+1&&
            slots.ssr_opaque==first+2&&slots.refraction_capture==first+3&&slots.refraction_capture<slots.transparent,
            "SSR reconstruction escaped reserved view gaps or changed the numeric profile layout.");
        check(plan.has(Kind::DepthPrepass)&&!plan.has(Kind::RefractionDepth)&&plan.find(Kind::DepthPrepass)->physicalView<plan.find(Kind::Background)->physicalView,
            "SSR plus glass did not retain one early opaque/masked readable-depth prepass.");
        const auto& prepare=*plan.find(Kind::SsrPrepareDepth);const auto& trace=*plan.find(Kind::SsrTrace);const auto& correction=*plan.find(Kind::SsrOpaque);
        check(prepare.execution==RenderPassExecution::Compute&&prepare.physicalView==slots.ssr_prepare&&trace.execution==RenderPassExecution::Raster&&
            trace.physicalView==slots.ssr_trace&&prepare.physicalView==trace.physicalView&&correction.execution==RenderPassExecution::Raster&&
            correction.physicalView==slots.ssr_opaque&&trace.physicalView<slots.ssr_reconstruct&&slots.ssr_reconstruct<correction.physicalView&&
            plan.find(Kind::Opaque)->physicalView<prepare.physicalView&&correction.physicalView<plan.find(Kind::RefractionCapture)->physicalView,
            "SSR preparation, tracing, reconstruction and correction did not precede glass snapshot capture.");
        auto contract=plan;
        check(use(contract,Kind::SsrPrepareDepth,Resource::SsrColorMip0).access==Access::SampledRead&&prepare.resourceCount==2,
            "SSR linear-depth initialization does not consume its own opaque source MRT.");
        check(use(contract,Kind::Background,Resource::SsrColorMip0).access==Access::ColorAttachment&&
            use(contract,Kind::Background,Resource::SsrColorMip0).load==Load::Clear&&
            use(contract,Kind::Opaque,Resource::SsrColorMip0).load==Load::Load,
            "Unfogged SSR radiance is not captured as an initialized opaque/sky MRT.");
        check(use(contract,Kind::SsrTrace,Resource::SsrRaw).access==Access::ColorAttachment&&use(contract,Kind::SsrTrace,Resource::SsrRaw).load==Load::Clear&&
            use(contract,Kind::SsrTrace,Resource::SsrGuide).access==Access::ColorAttachment&&use(contract,Kind::SsrTrace,Resource::SsrGuide).load==Load::Clear&&
            use(contract,Kind::SsrTrace,Resource::SsrFilterA).access==Access::ColorAttachment,
            "SSR receiver capture did not initialize exact position, normal/roughness and plane inputs.");
        const auto& sharedTrace=*plan.find(Kind::SsrTraceCompute);
        check(sharedTrace.execution==RenderPassExecution::Compute&&sharedTrace.physicalView==slots.ssr_reconstruct&&
            use(contract,Kind::SsrTraceCompute,Resource::SsrRaw).access==Access::SampledRead&&
            use(contract,Kind::SsrTraceCompute,Resource::SsrGuide).access==Access::SampledRead&&
            use(contract,Kind::SsrTraceCompute,Resource::SsrFilterA).access==Access::SampledRead&&
            use(contract,Kind::SsrTraceCompute,Resource::SsrFilterB).access==Access::StorageWrite,
            "Shared SSR intersection lost its exact receiver inputs or detached radiance output.");
        check(use(contract,Kind::SsrTrace,Resource::SceneDepth).access==Access::DepthAttachmentWrite&&use(contract,Kind::SsrTrace,Resource::SceneDepth).load==Load::Load&&
            use(contract,Kind::SsrOpaque,Resource::SceneDepth).access==Access::DepthAttachmentWrite,
            "SSR trace or correction omitted its stencil ownership contract.");
        const std::array reconstruct{Kind::SsrReconstruct1,Kind::SsrReconstruct2,Kind::SsrReconstruct4};
        const std::array inputs{Resource::SsrFilterB,Resource::SsrRaw,Resource::SsrFilterA};
        const std::array outputs{Resource::SsrRaw,Resource::SsrFilterA,Resource::SsrFilterB};
        for(unsigned stage=0;stage<reconstruct.size();++stage) {
            const auto& filter=*plan.find(reconstruct[stage]);
            check(filter.execution==RenderPassExecution::Compute&&filter.physicalView==slots.ssr_reconstruct&&
                use(contract,reconstruct[stage],inputs[stage]).access==Access::SampledRead&&use(contract,reconstruct[stage],outputs[stage]).access==Access::StorageWrite&&
                use(contract,reconstruct[stage],Resource::SsrGuide).access==Access::SampledRead&&use(contract,reconstruct[stage],Resource::SsrDepthMip0).access==Access::SampledRead,
                "SSR receiver reconstruction lost its B-to-raw-to-A-to-B ping-pong or immutable guides.");
        }
        check(use(contract,Kind::SsrOpaque,Resource::SsrFilterB).access==Access::SampledRead,
            "SSR opaque composition did not consume reconstructed positive radiance/confidence.");
        for(bool depth:{false,true})for(unsigned level=1;level<config.ssr_levels;++level) {
            const auto filter=static_cast<Kind>(unsigned(depth?Kind::SsrDepthFilter1:Kind::SsrColorFilter1)+(level-1)*2),copy=static_cast<Kind>(unsigned(filter)+1);
            const auto source=static_cast<Resource>(unsigned(depth?Resource::SsrDepthMip0:Resource::SsrColorMip0)+level-1);
            const auto scratch=static_cast<Resource>(unsigned(depth?Resource::SsrDepthScratch1:Resource::SsrColorScratch1)+level-1);
            check(plan.find(filter)->resources[0].resource==source&&plan.find(filter)->resources[1].resource==scratch&&plan.find(copy)->resources[0].resource==scratch&&
                plan.find(filter)->execution==RenderPassExecution::Compute&&plan.find(copy)->physicalView==prepare.physicalView,
                "SSR reduction samples and writes the same allocation instead of independent scratch.");
        }
        for(unsigned level=0;level<config.ssr_levels;++level)for(bool depth:{false,true}) {
            const auto resource=static_cast<Resource>(unsigned(depth?Resource::SsrDepthMip0:Resource::SsrColorMip0)+level);
            check(use(contract,Kind::SsrTraceCompute,resource).access==Access::SampledRead&&use(contract,Kind::Transparent,resource).access==Access::SampledRead&&
                std::none_of(correction.uses().begin(),correction.uses().end(),[&](const auto& item){return item.resource==resource;}),
                "SSR tracing/transparent receivers lost immutable inputs or opaque composition retraced the source.");
        }
        auto broken=plan;erase(broken,Kind::SsrPrepareDepth);error(broken,Error::MissingProducer,"SSR accepted uninitialized view-depth intervals.");
        broken=plan;erase(broken,Kind::SsrDepthCopy2);error(broken,Error::MissingProducer,"SSR accepted an unpublished depth mip.");
        broken=plan;erase(broken,Kind::SsrColorCopy2);error(broken,Error::MissingProducer,"SSR accepted an unpublished color mip.");
        broken=plan;erase(broken,Kind::SsrTrace);error(broken,Error::MissingProducer,"SSR accepted uninitialized raw radiance and receiver guides.");
        broken=plan;erase(broken,Kind::SsrTraceCompute);error(broken,Error::MissingProducer,"SSR accepted missing shared ray intersection.");
        broken=plan;erase(broken,Kind::SsrReconstruct1);error(broken,Error::MissingPass,"SSR accepted missing first receiver reconstruction.");
        broken=plan;erase(broken,Kind::SsrReconstruct2);error(broken,Error::MissingPass,"SSR accepted an unproduced second reconstruction image.");
        broken=plan;erase(broken,Kind::SsrReconstruct4);error(broken,Error::MissingPass,"SSR accepted missing wide receiver reconstruction.");
        broken=plan;erase(broken,Kind::SsrOpaque);error(broken,Error::MissingPass,"SSR accepted missing opaque correction.");
        broken=plan;std::swap(pass(broken,Kind::SsrTrace),pass(broken,Kind::SsrPrepareDepth));error(broken,Error::PhysicalOrder,"SSR placed receiver capture before its same-view compute preparation.");
        auto bad=resources;bad.bindings[index(Resource::SsrColorScratch2)].textures[0].width++;
        error(plan,bad,Error::InvalidTexture,"SSR accepted a stale color filter extent.");
        bad=resources;bad.bindings[index(Resource::SsrDepthMip0)].textures[0].width++;
        error(plan,bad,Error::InvalidTexture,"SSR accepted a linear-depth source extent differing from its opaque radiance.");
        bad=resources;bad.bindings[index(Resource::SsrDepthMip2)].textures[0].range.firstMip=1;
        error(plan,bad,Error::InvalidTexture,"SSR accepted overlapping Hi-Z mip subresources.");
        bad=resources;bad.bindings[index(Resource::SsrDepthMip0)].view.instanceId++;
        error(plan,bad,Error::InvalidOwner,"Two views shared SSR depth ownership.");
        bad=resources;for(unsigned level=0;level<config.ssr_levels;++level)bad.bindings[index(static_cast<Resource>(unsigned(Resource::SsrColorMip0)+level))].textures[0].physicalId=bad.bindings[index(Resource::SceneHdr)].textures[0].physicalId;
        check(!validateRenderPassPlan(plan,bad),"SSR correction sampled its attached HDR destination as source.");
        for(const auto resource:{Resource::SsrRaw,Resource::SsrGuide,Resource::SsrFilterA,Resource::SsrFilterB}) {
            bad=resources;bad.bindings[index(resource)].textures[0].width++;
            error(plan,bad,Error::InvalidTexture,"SSR accepted a stale full-resolution trace/reconstruction extent.");
            bad=resources;bad.bindings[index(resource)].view.instanceId++;
            error(plan,bad,Error::InvalidOwner,"SSR trace/reconstruction image escaped its view owner.");
        }
        bad=resources;bad.bindings[index(Resource::SsrFilterA)].textures=bad.bindings[index(Resource::SsrRaw)].textures;
        error(plan,bad,Error::FeedbackLoop,"SSR first reconstruction sampled its own output.");
        bad=resources;bad.bindings[index(Resource::SsrFilterB)].textures=bad.bindings[index(Resource::SsrFilterA)].textures;
        error(plan,bad,Error::FeedbackLoop,"SSR later reconstruction sampled its own output.");
        bad=resources;bad.bindings[index(Resource::SsrFilterA)].textures[0].format++;
        error(plan,bad,Error::InvalidTexture,"SSR reconstruction changed raw HDR precision.");
        bad=resources;bad.bindings[index(Resource::SsrGuide)].textures[0].format++;
        error(plan,bad,Error::InvalidTexture,"SSR compute receiver directions lost full-precision guide storage.");
        config.ssr=false;const auto off=buildRenderPassPlan(config);
        check(!off.has(Kind::SsrPrepareDepth)&&!off.has(Kind::SsrTrace)&&!off.has(Kind::SsrTraceCompute)&&!off.has(Kind::SsrReconstruct1)&&!off.has(Kind::SsrReconstruct2)&&!off.has(Kind::SsrReconstruct4)&&
            !off.has(Kind::SsrOpaque)&&!off.has(Kind::DepthPrepass)&&off.has(Kind::RefractionDepth),
            "Disabled SSR retained preparation or changed the ordinary late glass depth recipe.");
        for(const auto& entry:off.passes())for(const auto& item:entry.uses())
            check(item.resource<Resource::SsrColorMip0||item.resource>Resource::SsrFilterB,
                "Disabled SSR retained source or scratch resources.");
        config.ssr=true;config.refraction=false;
        const auto standalone=buildRenderPassPlan(config);
        check(bool(validateRenderPassPlan(standalone,concrete(standalone)))&&!standalone.has(Kind::DepthPrepass)&&!standalone.has(Kind::RefractionDepth),
            "SSR alone retained a redundant readable-depth geometry prepass.");
        for(const auto& entry:standalone.passes())for(const auto& item:entry.uses())
            check(item.resource!=Resource::ReadableSceneDepth,"SSR alone still consumes quantized device depth.");
    }
    auto maximum=fullConfig();maximum.smaa=true;maximum.glowPrefilters=5;maximum.glowBaseWidth=maximum.glowBaseHeight=1;
    maximum.refraction=true;maximum.refraction_levels=MaximumRefractionMipLevels;maximum.ssr=true;maximum.ssr_levels=MaximumSsrMipLevels;
    const auto plan=buildRenderPassPlan(maximum);
    check(bool(validateRenderPassPlan(plan))&&plan.count<=RenderPassPlan::MaximumPasses,"Maximum SSR/lighting/Glow/AA/glass plan exceeds bounded capacity.");
    check(plan.find(Kind::Transparent)->resourceCount==59,"Worst-case transparent dependency count changed without updating its bounded capacity test.");
    maximum.shadow_contact=true;const auto contact_maximum=buildRenderPassPlan(maximum);
    check(bool(validateRenderPassPlan(contact_maximum))&&contact_maximum.count<=RenderPassPlan::MaximumPasses&&
        contact_maximum.find(Kind::Transparent)->resourceCount<=RenderPass::MaximumResourceUses,
        "Exact contact exceeded the bounded maximum complete lighting plan.");
    maximum.ssr_levels=0;error(buildRenderPassPlan(maximum),Error::InvalidPass,"SSR accepted no source/depth mip.");
    maximum.ssr_levels=MaximumSsrMipLevels+1;error(buildRenderPassPlan(maximum),Error::InvalidPass,"SSR accepted an unsupported seventeenth mip.");
    RenderPassPlanConfig one;one.ssr=true;const auto single=buildRenderPassPlan(one);
    check(bool(validateRenderPassPlan(single,concrete(single)))&&!single.has(Kind::SsrColorFilter1)&&!single.has(Kind::SsrDepthFilter1),
        "A one-pixel SSR chain retained lower-mip work.");
}
void ownershipAndOrderFailures() {
    const auto correct=buildRenderPassPlan(fullConfig());
    auto value=correct;value.bindings[index(Resource::SceneHdr)].view.instanceId++;error(value,Error::InvalidOwner,"Logical HDR belongs to another view.");
    value=correct;value.bindings[index(Resource::ShadowAtlas)].owner=RenderResourceOwner::ThumbnailWorldShadows;error(value,Error::InvalidOwner,"Active view uses thumbnail shadow ownership.");
    value=correct;value.bindings[index(Resource::SceneHdr)].initiallyAvailable=true;error(value,Error::InvalidExternalState,"Current HDR was marked imported.");
    value=correct;value.bindings[index(Resource::EnvironmentLighting)].initiallyAvailable=false;error(value,Error::InvalidExternalState,"Environment lost its explicit persistent import.");
    value=correct;std::swap(pass(value,Kind::EnvironmentPreparation),pass(value,Kind::LightAssignment));error(value,Error::PhysicalOrder,"Same-view preparation order was reversed.");
    value=correct;pass(value,Kind::Encode).physicalView=ToolsView;error(value,Error::InvalidPhysicalView,"Encode escaped its reserved view band.");
    value=correct;pass(value,Kind::PointShadowUpdates).viewCount=97;error(value,Error::InvalidPhysicalView,"Point shadow work exceeded its reserved face range.");
    value=correct;pass(value,Kind::Opaque).execution=RenderPassExecution::Compute;error(value,Error::InvalidExecution,"Raster color work claimed to be compute.");
    value=correct;
    const auto delayed=pass(value,Kind::EnvironmentPreparation);erase(value,Kind::EnvironmentPreparation);
    std::size_t position=0;while(value.entries[position].kind!=Kind::Opaque)++position;
    std::move_backward(value.entries.begin()+position+1,value.entries.begin()+value.count,value.entries.begin()+value.count+1);
    value.entries[position+1]=delayed;value.entries[position+1].physicalView=value.entries[position].physicalView;++value.count;
    error(value,Error::PhysicalOrder,"Compute submitted after raster in one view was accepted despite bgfx compute-first execution.");
    value=correct;value.entries[value.count++]=*correct.find(Kind::Encode);error(value,Error::DuplicatePass,"A stage was recorded twice in its plan.");
    value=correct;value.count=static_cast<std::uint8_t>(value.entries.size()+1);error(value,Error::CapacityExceeded,"Corrupt pass count exceeded bounded storage.");
    for(const auto purpose:{RenderViewPurpose::MaterialPreview,RenderViewPurpose::ParticlePreview,RenderViewPurpose::Thumbnail}) {
        RenderPassPlanConfig unsupported;unsupported.view.purpose=purpose;unsupported.ssgi=true;
        error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"GI escaped into an unsupported preview profile.");
        unsupported.ssgi=false;unsupported.ssao=true;error(buildRenderPassPlan(unsupported),Error::UnsupportedFeature,"AO escaped into an unsupported preview profile.");
    }
    auto invalid=fullConfig();invalid.view.purpose=static_cast<RenderViewPurpose>(255);error(buildRenderPassPlan(invalid),Error::InvalidViewPurpose,"Unknown view purpose was accepted.");
}
}
int main() {
    static_assert(std::is_trivially_copyable_v<RenderPassPlan> && std::is_trivially_copyable_v<RenderResourceTable>);
    try { profiles();attachmentAndDependencyContracts();effectTopology();shadow_contact_contracts();concreteHazards();glarePyramidContracts();spatialAntiAliasingContracts();optionalPresentationContracts();refractionContracts();ssrContracts();ownershipAndOrderFailures(); }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
    std::cout<<"Render pass/resource contracts passed.\n";return 0;
}
