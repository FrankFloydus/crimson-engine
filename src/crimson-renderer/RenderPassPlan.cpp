#include "RenderPassPlan.h"
#include "RenderTypes.h"

#include <algorithm>

namespace crimson {
namespace {
using Kind = RenderPassKind;
using Resource = RenderResource;
using Access = RenderResourceAccess;
using Owner = RenderResourceOwner;

constexpr std::size_t index(Resource resource) noexcept { return static_cast<std::size_t>(resource); }
constexpr bool validPurpose(RenderViewPurpose purpose) noexcept { return purpose<=RenderViewPurpose::Thumbnail; }
constexpr bool isWorld(RenderViewPurpose purpose) noexcept {
    return purpose==RenderViewPurpose::Scene || purpose==RenderViewPurpose::Game || purpose==RenderViewPurpose::Thumbnail;
}
Owner resourceOwner(Resource resource,RenderViewPurpose purpose) noexcept {
    switch(resource) {
    case Resource::PreparedScene: return Owner::PreparedFrame;
    case Resource::DeformedGeometry: return Owner::ModelGeometry;
    case Resource::EnvironmentLighting: return Owner::Environment;
    case Resource::ShadowAtlas: return purpose==RenderViewPurpose::Thumbnail?Owner::ThumbnailWorldShadows:Owner::ActiveWorldShadows;
    case Resource::PickReadback: case Resource::ThumbnailReadback: return Owner::Readback;
    default: return Owner::ViewTargets;
    }
}
bool imported(Resource resource) noexcept {
    // Previous-frame GI sources/history are coherent persistent imports. Their
    // validity is a separate GPU parameter; an invalid generation reads as zero.
    return resource==Resource::PreparedScene || resource==Resource::EnvironmentLighting || resource==Resource::ShadowAtlas ||
        resource==Resource::SsgiSources || resource==Resource::SsgiHistory;
}
bool reads(Access access) noexcept { return access==Access::Read || access==Access::ReadWrite || access==Access::DepthRead; }
bool writes(Access access) noexcept { return access==Access::Write || access==Access::ReadWrite || access==Access::DepthWrite; }

RenderPass makePass(Kind kind,const RenderPassPlanConfig& config) noexcept {
    const auto slots=renderViewPassSlots(config.view.purpose);
    RenderPass pass;pass.kind=kind;
    const auto use=[&](Resource resource,Access access) { pass.resources[pass.resourceCount++]={resource,access}; };
    switch(kind) {
    case Kind::EnvironmentPreparation:
        pass.physicalView=EnvironmentComputeView;use(Resource::PreparedScene,Access::Read);use(Resource::EnvironmentLighting,Access::ReadWrite);break;
    case Kind::ModelDeformation:
        pass.physicalView=EnvironmentComputeView;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Write);break;
    case Kind::LightAssignment:
        pass.physicalView=EnvironmentComputeView;use(Resource::PreparedScene,Access::Read);use(Resource::EnvironmentLighting,Access::Read);use(Resource::ClusteredLighting,Access::Write);break;
    case Kind::PointShadowUpdates:
        pass.physicalView=slots.pointShadow;pass.viewCount=96;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::ShadowAtlas,Access::ReadWrite);break;
    case Kind::DirectionalShadowUpdates:
        pass.physicalView=slots.directionalShadow;pass.viewCount=4;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::ShadowAtlas,Access::ReadWrite);break;
    case Kind::DepthPrepass:
        pass.physicalView=slots.depthPrepass;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);
        use(Resource::ReadableSceneDepth,Access::DepthWrite);use(Resource::ReceiverScopes,Access::Write);break;
    case Kind::SsgiTrace:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsgiSources,Access::Read);use(Resource::SsgiTraceScratch,Access::Write);break;
    case Kind::SsgiTemporal:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsgiSources,Access::Read);use(Resource::SsgiTraceScratch,Access::Read);
        use(Resource::SsgiHistory,Access::ReadWrite);use(Resource::SsgiTemporalScratch,Access::Write);break;
    case Kind::SsgiFilterHorizontal:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsgiTemporalScratch,Access::Read);use(Resource::SsgiFilterA,Access::Write);break;
    case Kind::SsgiFilterVertical:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsgiFilterA,Access::Read);use(Resource::SsgiFilterB,Access::Write);break;
    case Kind::SsgiPublish:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsgiFilterB,Access::Read);use(Resource::ClusteredLighting,Access::ReadWrite);use(Resource::SsgiIrradiance,Access::Write);break;
    case Kind::SsaoEvaluate:
        pass.physicalView=slots.lightingEffects;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::SsaoScratch,Access::Write);break;
    case Kind::SsaoPublish:
        pass.physicalView=slots.lightingEffects;use(Resource::SsaoScratch,Access::Read);use(Resource::ReadableSceneDepth,Access::Read);
        use(Resource::ReceiverScopes,Access::Read);use(Resource::ClusteredLighting,Access::ReadWrite);use(Resource::SsaoVisibility,Access::Write);break;
    case Kind::SceneColor:
        // Background/opaque work precedes the sorted blended queue.
        pass.physicalView=slots.scene;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::EnvironmentLighting,Access::Read);
        if(config.worldShadows)use(Resource::ShadowAtlas,Access::Read);
        use(Resource::ClusteredLighting,Access::Read);
        if(config.ssgi){use(Resource::SsgiIrradiance,Access::Read);use(Resource::SsgiSources,Access::Write);}
        if(config.ssao)use(Resource::SsaoVisibility,Access::Read);
        use(Resource::SceneHdr,Access::Write);use(Resource::SceneDepth,Access::DepthWrite);break;
    case Kind::GlowExtract:
        pass.physicalView=slots.glowExtract;use(Resource::SceneHdr,Access::Read);use(Resource::GlowA,Access::Write);break;
    case Kind::GlowHorizontal:
        pass.physicalView=slots.glowHorizontal;use(Resource::GlowA,Access::Read);use(Resource::GlowB,Access::Write);break;
    case Kind::GlowVertical:
        pass.physicalView=slots.glowVertical;use(Resource::GlowB,Access::Read);use(Resource::GlowA,Access::Write);break;
    case Kind::ToneMap:
        pass.physicalView=slots.present;use(Resource::SceneHdr,Access::Read);if(config.glow)use(Resource::GlowA,Access::Read);use(Resource::Composite,Access::Write);break;
    case Kind::Grid:
        pass.physicalView=slots.grid;use(Resource::SceneDepth,Access::DepthRead);use(Resource::Composite,Access::ReadWrite);break;
    case Kind::CollisionOverlays:
        pass.physicalView=slots.collisions;use(Resource::PreparedScene,Access::Read);use(Resource::SceneDepth,Access::DepthRead);use(Resource::Composite,Access::ReadWrite);break;
    case Kind::Encode:
        pass.physicalView=slots.encode;use(Resource::Composite,Access::Read);use(Resource::Display,Access::Write);break;
    case Kind::Picking:
        pass.physicalView=PickView;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::PickColor,Access::Write);use(Resource::PickDepth,Access::DepthWrite);break;
    case Kind::PickReadback:
        pass.physicalView=PickReadView;use(Resource::PickColor,Access::Read);use(Resource::PickReadback,Access::Write);break;
    case Kind::ThumbnailReadback:
        pass.physicalView=ThumbnailReadView;use(Resource::Display,Access::Read);use(Resource::ThumbnailReadback,Access::Write);break;
    case Kind::Count: break;
    }
    return pass;
}
bool expected(Kind kind,const RenderPassPlanConfig& config) noexcept {
    switch(kind) {
    case Kind::EnvironmentPreparation: return config.prepareEnvironment;
    case Kind::PointShadowUpdates: case Kind::DirectionalShadowUpdates: return config.worldShadows;
    case Kind::DepthPrepass: return config.ssgi || config.ssao;
    case Kind::SsgiTrace: case Kind::SsgiTemporal: case Kind::SsgiFilterHorizontal: case Kind::SsgiFilterVertical: case Kind::SsgiPublish: return config.ssgi;
    case Kind::SsaoEvaluate: case Kind::SsaoPublish: return config.ssao;
    case Kind::GlowExtract: case Kind::GlowHorizontal: case Kind::GlowVertical: return config.glow;
    case Kind::Grid: return config.grid;
    case Kind::CollisionOverlays:return config.collisionOverlays;
    case Kind::Picking: case Kind::PickReadback: return config.picking;
    case Kind::ThumbnailReadback: return config.thumbnailReadback;
    case Kind::ModelDeformation: case Kind::LightAssignment: case Kind::SceneColor: case Kind::ToneMap: case Kind::Encode: return true;
    case Kind::Count: return false;
    }
    return false;
}
RenderPassPlanValidation failure(RenderPassPlanError error,std::size_t pass=0,Resource resource=Resource::Count) noexcept {
    return {error,static_cast<std::uint8_t>(pass),resource};
}
}

RenderViewPassSlots renderViewPassSlots(RenderViewPurpose purpose) noexcept {
    switch(purpose) {
    case RenderViewPurpose::Scene:
        return {SceneView,PresentView,GridView,EncodeView,SceneView+1,SceneView+2,SceneView+3,PointShadowView,DirectionalShadowView,SceneDepthPrepassView,SceneLightingEffectsView,GridView};
    case RenderViewPurpose::Game:
        return {GameSceneView,GamePresentView,InvalidRenderView,GameEncodeView,GameSceneView+1,GameSceneView+2,GameSceneView+3,PointShadowView,GameDirectionalShadowView,GameDepthPrepassView,GameLightingEffectsView,GamePresentView};
    case RenderViewPurpose::MaterialPreview:
        return {MaterialPreviewView,MaterialPreviewView+4,MaterialPreviewView+5,MaterialPreviewView+6,MaterialPreviewView+1,MaterialPreviewView+2,MaterialPreviewView+3,InvalidRenderView,InvalidRenderView};
    case RenderViewPurpose::ParticlePreview:
        return {ParticlePreviewView,ParticlePreviewView+4,ParticlePreviewView+5,ParticlePreviewView+6,ParticlePreviewView+1,ParticlePreviewView+2,ParticlePreviewView+3,InvalidRenderView,InvalidRenderView};
    case RenderViewPurpose::Thumbnail:
        return {ThumbnailSceneView,ThumbnailSceneView+1,ThumbnailSceneView+2,ThumbnailSceneView+3,InvalidRenderView,InvalidRenderView,InvalidRenderView,ThumbnailPointShadowView,ThumbnailDirectionalShadowView,ThumbnailDepthPrepassView,ThumbnailLightingEffectsView};
    }
    return {InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView,InvalidRenderView};
}

const RenderPass* RenderPassPlan::find(RenderPassKind kind) const noexcept {
    for(std::size_t i=0;i<std::min<std::size_t>(count,entries.size());++i)if(entries[i].kind==kind)return &entries[i];
    return nullptr;
}
RenderPassPlan buildRenderPassPlan(const RenderPassPlanConfig& config) noexcept {
    RenderPassPlan plan;plan.config=config;
    for(std::size_t i=0;i<plan.bindings.size();++i) {
        const auto resource=static_cast<Resource>(i);const auto owner=resourceOwner(resource,config.view.purpose);
        plan.bindings[i]={resource,owner,owner==Owner::ViewTargets || owner==Owner::Readback?config.view:RenderViewIdentity{},imported(resource)};
    }
    for(unsigned kind=0;kind<static_cast<unsigned>(Kind::Count);++kind)
        if(expected(static_cast<Kind>(kind),config)) {
            // Invalid feature combinations can request more work than a valid
            // profile. Retain an invalid count for validation without writing
            // beyond fixed storage before UnsupportedFeature is reported.
            if(plan.count==plan.entries.size()){++plan.count;return plan;}
            plan.entries[plan.count++]=makePass(static_cast<Kind>(kind),config);
        }
    // Picking belongs to the Scene adjunct views regardless of when
    // CPU draw collection happens. All other entries are already monotonic.
    for(std::size_t i=1;i<plan.count;++i) {
        const auto value=plan.entries[i];std::size_t destination=i;
        while(destination && plan.entries[destination-1].physicalView>value.physicalView) {
            plan.entries[destination]=plan.entries[destination-1];--destination;
        }
        plan.entries[destination]=value;
    }
    return plan;
}

RenderPassPlanValidation validateRenderPassPlan(const RenderPassPlan& plan) noexcept {
    const auto& config=plan.config;const auto purpose=config.view.purpose;
    if(!validPurpose(purpose))return failure(RenderPassPlanError::InvalidViewPurpose);
    if((config.glow && purpose==RenderViewPurpose::Thumbnail) ||
       (config.grid && purpose!=RenderViewPurpose::Scene && purpose!=RenderViewPurpose::ParticlePreview) ||
       (config.picking && purpose!=RenderViewPurpose::Scene) ||
       (config.thumbnailReadback && purpose!=RenderViewPurpose::Thumbnail) ||
       (config.worldShadows && !isWorld(purpose)) ||
       (config.collisionOverlays && purpose!=RenderViewPurpose::Scene && purpose!=RenderViewPurpose::Game) ||
       (config.ssgi && purpose!=RenderViewPurpose::Scene && purpose!=RenderViewPurpose::Game) ||
       (config.ssao && purpose!=RenderViewPurpose::Scene && purpose!=RenderViewPurpose::Game))return failure(RenderPassPlanError::UnsupportedFeature);
    if(plan.count>plan.entries.size())return failure(RenderPassPlanError::CapacityExceeded);
    std::array<bool,RenderPassPlan::ResourceCount> available{};
    for(std::size_t i=0;i<plan.bindings.size();++i) {
        const auto resource=static_cast<Resource>(i);const auto& binding=plan.bindings[i];const auto owner=resourceOwner(resource,purpose);
        if(binding.resource!=resource)return failure(RenderPassPlanError::InvalidResource,0,resource);
        if(binding.owner!=owner || ((owner==Owner::ViewTargets || owner==Owner::Readback) && binding.view!=config.view))return failure(RenderPassPlanError::InvalidOwner,0,resource);
        if(binding.initiallyAvailable!=imported(resource))return failure(RenderPassPlanError::InvalidExternalState,0,resource);
        available[i]=binding.initiallyAvailable;
    }
    std::array<bool,static_cast<std::size_t>(Kind::Count)> seen{};
    for(std::size_t i=0;i<plan.count;++i) {
        const auto& pass=plan.entries[i];
        if(pass.kind>=Kind::Count)return failure(RenderPassPlanError::InvalidPass,i);
        if(seen[static_cast<std::size_t>(pass.kind)])return failure(RenderPassPlanError::DuplicatePass,i);
        seen[static_cast<std::size_t>(pass.kind)]=true;
        if(!expected(pass.kind,config))return failure(RenderPassPlanError::UnexpectedPass,i);
        const auto contract=makePass(pass.kind,config);
        if(pass.physicalView==InvalidRenderView || pass.physicalView!=contract.physicalView || pass.viewCount!=contract.viewCount)
            return failure(RenderPassPlanError::InvalidPhysicalView,i);
        if(i && pass.physicalView<=plan.entries[i-1].lastPhysicalView()) {
            const bool sharedCompute=pass.physicalView==EnvironmentComputeView && plan.entries[i-1].physicalView==EnvironmentComputeView &&
                ((pass.kind==Kind::ModelDeformation && plan.entries[i-1].kind==Kind::EnvironmentPreparation) ||
                 (pass.kind==Kind::LightAssignment && (plan.entries[i-1].kind==Kind::ModelDeformation ||
                    plan.entries[i-1].kind==Kind::EnvironmentPreparation)));
            // Screen-space effects share their camera's existing compute slot.
            const bool giCompute=pass.physicalView==plan.entries[i-1].physicalView &&
                ((pass.kind==Kind::SsgiTemporal && plan.entries[i-1].kind==Kind::SsgiTrace) ||
                 (pass.kind==Kind::SsgiFilterHorizontal && plan.entries[i-1].kind==Kind::SsgiTemporal) ||
                 (pass.kind==Kind::SsgiFilterVertical && plan.entries[i-1].kind==Kind::SsgiFilterHorizontal) ||
                 (pass.kind==Kind::SsgiPublish && plan.entries[i-1].kind==Kind::SsgiFilterVertical) ||
                 (pass.kind==Kind::SsaoEvaluate && (plan.entries[i-1].kind==Kind::SsgiPublish)) ||
                 (pass.kind==Kind::SsaoPublish && plan.entries[i-1].kind==Kind::SsaoEvaluate));
            // Post-tone-map collision overlays reuse their view's composite target.
            const bool postOverlay=pass.kind==Kind::CollisionOverlays &&
                (plan.entries[i-1].kind==Kind::ToneMap || plan.entries[i-1].kind==Kind::Grid) &&
                pass.physicalView==plan.entries[i-1].physicalView;
            if(!sharedCompute&&!giCompute&&!postOverlay)return failure(RenderPassPlanError::PhysicalOrder,i);
        }
        if(pass.resourceCount>pass.resources.size())return failure(RenderPassPlanError::CapacityExceeded,i);
        std::array<bool,RenderPassPlan::ResourceCount> used{};
        for(const auto use:pass.uses()) {
            if(use.resource>=Resource::Count)return failure(RenderPassPlanError::InvalidResource,i,use.resource);
            if(used[index(use.resource)])return failure(RenderPassPlanError::DuplicateResourceUse,i,use.resource);
            used[index(use.resource)]=true;
            if(std::find(contract.uses().begin(),contract.uses().end(),use)==contract.uses().end())return failure(RenderPassPlanError::InvalidAccess,i,use.resource);
            if(reads(use.access) && !available[index(use.resource)])return failure(RenderPassPlanError::MissingProducer,i,use.resource);
            if(writes(use.access))available[index(use.resource)]=true;
        }
        if(pass.resourceCount!=contract.resourceCount)return failure(RenderPassPlanError::InvalidAccess,i);
    }
    for(unsigned kind=0;kind<static_cast<unsigned>(Kind::Count);++kind)
        if(expected(static_cast<Kind>(kind),config) && !seen[kind])return failure(RenderPassPlanError::MissingPass);
    return {};
}

const char* renderPassPlanErrorText(RenderPassPlanError error) noexcept {
    switch(error) {
    case RenderPassPlanError::None:return "Valid renderer pass plan.";
    case RenderPassPlanError::InvalidViewPurpose:return "Unknown render view purpose.";
    case RenderPassPlanError::UnsupportedFeature:return "Pass feature is unsupported for this view purpose.";
    case RenderPassPlanError::CapacityExceeded:return "Renderer pass-plan fixed capacity exceeded.";
    case RenderPassPlanError::InvalidResource:return "Pass refers to an invalid renderer resource.";
    case RenderPassPlanError::InvalidOwner:return "Renderer resource belongs to the wrong owner or view.";
    case RenderPassPlanError::InvalidExternalState:return "Resource initialization disagrees with persistent import ownership.";
    case RenderPassPlanError::InvalidPass:return "Unknown renderer pass.";
    case RenderPassPlanError::InvalidPhysicalView:return "Pass does not use its reserved physical view range.";
    case RenderPassPlanError::PhysicalOrder:return "Pass dependencies violate the backend's numeric view order.";
    case RenderPassPlanError::DuplicatePass:return "Renderer pass appears more than once.";
    case RenderPassPlanError::InvalidAccess:return "Pass reads or writes resources outside its implemented contract.";
    case RenderPassPlanError::DuplicateResourceUse:return "A pass declares one resource more than once.";
    case RenderPassPlanError::MissingProducer:return "Pass reads a view resource before its producer.";
    case RenderPassPlanError::MissingPass:return "Renderer pass plan is missing required work.";
    case RenderPassPlanError::UnexpectedPass:return "Renderer pass plan contains disabled work.";
    }
    return "Unknown renderer pass-plan error.";
}
} // namespace crimson
