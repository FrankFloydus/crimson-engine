#include "NavigationPreview.h"
#include <algorithm>
#include <limits>

namespace crimson {
NavigationPreview::NavigationPreview(ResourceManager& resources,const std::filesystem::path& root)
    :world_(resources,root/".crimson"/"cache"/"navigation"){}
void NavigationPreview::update(const SceneDocument& document,const CollisionMeshQuery& meshQuery,const NavigationAgentProfileQuery& profileQuery) {
    const auto now=std::chrono::steady_clock::now();
    const bool domainChanged=domain_!=document.documentGeneration();
    const bool sceneChanged=domainChanged || revision_!=document.scene().revision();
    if(domainChanged)physics_.reconcileStatics(Scene{});
    const bool released=transaction_ && !document.transactionActive();transaction_=document.transactionActive();
    coalescing_=!domainChanged && !released && transaction_ && now-lastUpdate_<std::chrono::milliseconds(100) && revision_!=document.scene().revision();
    if(coalescing_) {
        world_.setSourceReadiness(false,"Navigation is preparing the current edit.");
        world_.pumpPublication();return;
    }
    if(sceneChanged || released)lastUpdate_=now;
    domain_=document.documentGeneration();revision_=document.scene().revision();
    std::vector<PhysicsMeshCollider> meshes;collisionPending_=false;diagnostic_.clear();
    std::optional<NavigationVolumeDescriptor> volume;std::vector<NavigationAgentProfile> profiles;
    for(const auto& record:document.scene().records()) {
        if(record.collider && record.collider->shape==ColliderShape::Trimesh && (record.collider->mesh || !record.collider->source.empty())) {
            const auto source=colliderMeshResult(*record.collider,meshQuery);
            if(!source.mesh || !source.current) {
                collisionPending_=true;if(diagnostic_.empty())diagnostic_=source.diagnostic;
            } else meshes.push_back({record.id,source.mesh,source.revision});
        }
        if(record.navigationVolume && record.navigationVolume->enabled && !volume)if(const auto world=document.scene().worldTransform(record.id)) {
            volume=NavigationVolumeDescriptor{record.id,*world,record.navigationVolume->size,record.navigationVolume->bake,true};
            for(const auto& ref:record.navigationVolume->profiles) {
                if(const auto profile=resolveNavigationAgentProfile(ref.id,profileQuery))profiles.push_back(*profile);
                else diagnostic_="Agent Profile unavailable: "+ref.id;
            }
        }
    }
    physics_.setAgentProfiles(profileQuery);physics_.reconcileStatics(document.scene(),meshes);
    // Reconcile volume/profile policy even while a source is pending, so a
    // removed volume cannot retain its previous scene's derived geometry.
    // Physics retains only this domain's accepted collision replacements.
    world_.setSourceReadiness(!collisionPending_,diagnostic_);
    world_.reconcile(domain_,volume,profiles,physics_.staticBoxes(),physics_.staticMeshes());
    world_.setSourceReadiness(!collisionPending_,diagnostic_);
    world_.pumpPublication();
}
std::string NavigationPreview::status(const NavigationWorld* source,const std::string& profile) const {
    if(!source)return "Navigation unavailable.";
    if(source==&world_ && (collisionPending_ || coalescing_))return diagnostic_.empty()?"Preparing navigation...":diagnostic_;
    const auto snapshot=source->summary(profile);
    std::string text;
    switch(snapshot.status) {
    case NavigationStatus::Ready:text="Ready";break;
    case NavigationStatus::Preparing:text="Preparing";break;
    case NavigationStatus::Failed:text="Failed";break;
    case NavigationStatus::Unreachable:text="No walkable surfaces";break;
    default:text="Unavailable";break;
    }
    text+=" | "+std::to_string(snapshot.readyTiles)+" ready, "+std::to_string(snapshot.pendingTiles)+" pending";
    if(snapshot.failedTiles)text+=", "+std::to_string(snapshot.failedTiles)+" failed";
    if(!snapshot.diagnostic.empty())text+="\n"+snapshot.diagnostic;
    else if(source==&world_ && !diagnostic_.empty())text+="\n"+diagnostic_;
    return text;
}
std::vector<RenderCollisionOverlay> NavigationPreview::overlays(const NavigationWorld* source,const std::string& profile) {
    if(!source || profile.empty())return {};
    const auto metadata=source->summary(profile);
    const auto currentRevision=metadata.revision;
    const bool pending=metadata.pendingSources || (source==&world_ && (collisionPending_ || coalescing_));
    if(overlayWorld_!=source || overlayProfile_!=profile || overlayRevision_!=currentRevision || !geometry_) {
        const auto snapshot=source->debugSnapshot(profile);
        const auto convert=[](const std::vector<Vec3>& vertices,const std::vector<std::uint32_t>& triangles) {
            auto geometry=std::make_shared<CollisionOverlayGeometry>();geometry->vertices=vertices;geometry->triangles=triangles;
            for(std::size_t i=0;i+2<geometry->triangles.size();i+=3) {
                const auto a=geometry->triangles[i],b=geometry->triangles[i+1],c=geometry->triangles[i+2];
                if(a>=geometry->vertices.size() || b>=geometry->vertices.size() || c>=geometry->vertices.size())continue;
                const auto normal=math::normalized(math::cross(geometry->vertices[b]-geometry->vertices[a],geometry->vertices[c]-geometry->vertices[a]));
                for(const auto edge:{std::pair{a,b},std::pair{b,c},std::pair{c,a}})geometry->edges.push_back({edge.first,edge.second,normal,normal,false,false,true});
            }
            return geometry;
        };
        geometry_=convert(snapshot.vertices,snapshot.triangles);pendingGeometry_=convert(snapshot.pendingVertices,snapshot.pendingTriangles);
        overlayWorld_=source;overlayProfile_=profile;overlayRevision_=snapshot.revision;
    }
    std::vector<RenderCollisionOverlay> result;
    const Vec4 color=pending?Vec4{1.f,.65f,.12f,.75f}:Vec4{.15f,.85f,.4f,.75f};
    if(geometry_ && !geometry_->triangles.empty())result.push_back({geometry_,Mat4{},color,.35f});
    if(pendingGeometry_ && !pendingGeometry_->triangles.empty())result.push_back({pendingGeometry_,Mat4{},Vec4{1.f,.65f,.12f,.75f},.35f});
    return result;
}
}
