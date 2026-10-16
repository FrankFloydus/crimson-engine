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
    return resource==Resource::PreparedScene || resource==Resource::EnvironmentLighting || resource==Resource::ShadowAtlas;
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
    case Kind::CharacterShadowUpdates:
        pass.physicalView=slots.characterShadow;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::ShadowAtlas,Access::ReadWrite);break;
    case Kind::DepthPrepass:
        pass.physicalView=slots.depthPrepass;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);
        use(Resource::ReadableSceneDepth,Access::DepthWrite);use(Resource::ReceiverScopes,Access::Write);break;
    case Kind::AmbientVisibility:
        pass.physicalView=slots.ambient;use(Resource::ReadableSceneDepth,Access::Read);use(Resource::ReceiverScopes,Access::Read);
        use(Resource::ClusteredLighting,Access::Read);if(config.worldShadows)use(Resource::ShadowAtlas,Access::Read);
        use(Resource::AmbientScratch,Access::Write);break;
    case Kind::AmbientPublish:
        pass.physicalView=slots.ambient;use(Resource::AmbientScratch,Access::Read);
        use(Resource::ClusteredLighting,Access::ReadWrite);use(Resource::AmbientVisibility,Access::Write);break;
    case Kind::SceneColor:
        // Background, opaque/masked meshes/particles, then one sorted blended
        // mesh/particle queue retain the existing sequential physical view.
        pass.physicalView=slots.scene;use(Resource::PreparedScene,Access::Read);use(Resource::DeformedGeometry,Access::Read);use(Resource::EnvironmentLighting,Access::Read);
        if(config.worldShadows)use(Resource::ShadowAtlas,Access::Read);
        use(Resource::ClusteredLighting,Access::Read);if(config.ambientVisibility)use(Resource::AmbientVisibility,Access::Read);
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
    case Kind::PointShadowUpdates: case Kind::DirectionalShadowUpdates: case Kind::CharacterShadowUpdates: return config.worldShadows;
    case Kind::DepthPrepass: case Kind::AmbientVisibility: case Kind::AmbientPublish: return config.ambientVisibility;
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
        return {SceneView,PresentView,GridView,EncodeView,SceneView+1,SceneView+2,SceneView+3,PointShadowView,DirectionalShadowView,SceneDepthPrepassView,SceneAmbientView,CharacterShadowView,GridView};
    case RenderViewPurpose::Game:
        return {GameSceneView,GamePresentView,InvalidRenderView,GameEncodeView,GameSceneView+1,GameSceneView+2,GameSceneView+3,PointShadowView,GameDirectionalShadowView,GameDepthPrepassView,GameAmbientView,CharacterShadowView,GamePresentView};
    case RenderViewPurpose::MaterialPreview:
        return {MaterialPreviewView,MaterialPreviewView+4,MaterialPreviewView+5,MaterialPreviewView+6,MaterialPreviewView+1,MaterialPreviewView+2,MaterialPreviewView+3,InvalidRenderView,InvalidRenderView};
    case RenderViewPurpose::ParticlePreview:
        return {ParticlePreviewView,ParticlePreviewView+4,ParticlePreviewView+5,ParticlePreviewView+6,ParticlePreviewView+1,ParticlePreviewView+2,ParticlePreviewView+3,InvalidRenderView,InvalidRenderView};
    case RenderViewPurpose::Thumbnail:
        return {ThumbnailSceneView,ThumbnailSceneView+1,ThumbnailSceneView+2,ThumbnailSceneView+3,InvalidRenderView,InvalidRenderView,InvalidRenderView,ThumbnailPointShadowView,ThumbnailDirectionalShadowView,ThumbnailDepthPrepassView,ThumbnailAmbientView,ThumbnailCharacterShadowView};
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
       (config.ambientVisibility && !isWorld(purpose)))return failure(RenderPassPlanError::UnsupportedFeature);
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
            const bool ambientCompute=pass.kind==Kind::AmbientPublish && plan.entries[i-1].kind==Kind::AmbientVisibility &&
                pass.physicalView==plan.entries[i-1].physicalView;
            // Game reuses its post-tone-map view; Scene reuses the grid view.
            // Both submit sequentially into the same color/depth framebuffer.
            const bool postOverlay=pass.kind==Kind::CollisionOverlays &&
                (plan.entries[i-1].kind==Kind::ToneMap || plan.entries[i-1].kind==Kind::Grid) &&
                pass.physicalView==plan.entries[i-1].physicalView;
            if(!sharedCompute&&!ambientCompute&&!postOverlay)return failure(RenderPassPlanError::PhysicalOrder,i);
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
