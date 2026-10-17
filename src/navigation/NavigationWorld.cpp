#include "NavigationWorld.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>
#include <set>

namespace crimson {
namespace {
std::atomic_uint64_t NextNavigationRevision{1};
constexpr std::size_t MaximumTiles=2048,MaximumJobs=16;
constexpr float Skin=.02f;
Aabb sourceBounds(const PhysicsBox& source) {
    Aabb bounds;for(int corner=0;corner<8;++corner)math::include(bounds,math::transformPoint(source.world,{source.size[0]*(corner&1?.5f:-.5f),source.size[1]*(corner&2?.5f:-.5f),source.size[2]*(corner&4?.5f:-.5f)}));return bounds;
}
Aabb sourceBounds(const PhysicsStaticMesh& source) {
    if(!source.mesh)return {};Aabb bounds;
    const auto& original=source.mesh->bounds;
    if(original.valid)for(int corner=0;corner<8;++corner)math::include(bounds,math::transformPoint(source.world,{corner&1?original.maximum[0]:original.minimum[0],corner&2?original.maximum[1]:original.minimum[1],corner&4?original.maximum[2]:original.minimum[2]}));
    else for(const auto& point:source.mesh->positions)math::include(bounds,math::transformPoint(source.world,point));return bounds;
}
bool overlaps(const Aabb& a,const Aabb& b) {
    if(!a.valid||!b.valid)return false;for(unsigned axis=0;axis<3;++axis)if(a.maximum[axis]<b.minimum[axis]||b.maximum[axis]<a.minimum[axis])return false;return true;
}
std::pair<float,float> verticalRange(const NavigationVolumeDescriptor& volume,const AgentTraversalSettings& agent) {
    const auto bounds=navigationVolumeBounds(volume);const auto ch=volume.bake.cellHeight;
    return {std::floor((bounds.minimum[1]-ch)/ch)*ch,std::ceil((bounds.maximum[1]+agent.height+Skin+ch)/ch)*ch};
}
std::set<NavigationTileKey> coverage(const NavigationVolumeDescriptor& volume) {
    const auto bounds=navigationVolumeBounds(volume);const double side=double(volume.bake.tileSize)*volume.bake.cellSize;
    const double lowX=std::floor(bounds.minimum[0]/side),lowZ=std::floor(bounds.minimum[2]/side),highX=std::ceil(bounds.maximum[0]/side),highZ=std::ceil(bounds.maximum[2]/side);
    if(!bounds.valid||!math::finite(bounds.minimum)||!math::finite(bounds.maximum)||lowX<-1'000'000||lowZ<-1'000'000||highX>1'000'000||highZ>1'000'000||
        highX<=lowX||highZ<=lowZ||(highX-lowX)*(highZ-lowZ)>MaximumTiles)throw std::runtime_error("NavigationVolume exceeds the 2048 active-tile budget.");
    std::set<NavigationTileKey> result;for(int z=static_cast<int>(lowZ);z<highZ;++z)for(int x=static_cast<int>(lowX);x<highX;++x)result.insert({x,z});return result;
}
Aabb tileBounds(NavigationTileKey key,const NavigationVolumeDescriptor& volume,const AgentTraversalSettings& agent) {
    const auto [low,high]=verticalRange(volume,agent);const float side=volume.bake.tileSize*volume.bake.cellSize;
    const float halo=(std::ceil((agent.radius+Skin)/volume.bake.cellSize)+3)*volume.bake.cellSize;
    return {{key.x*side-halo,low,key.z*side-halo},{(key.x+1)*side+halo,high,(key.z+1)*side+halo},true};
}
bool entirelyInside(const NavigationVolumeDescriptor& volume,const Aabb& bounds) {
    for(int corner=0;corner<8;++corner)if(!navigationVolumeContains(volume,{corner&1?bounds.maximum[0]:bounds.minimum[0],corner&2?bounds.maximum[1]:bounds.minimum[1],corner&4?bounds.maximum[2]:bounds.minimum[2]}))return false;return true;
}
std::string statusText(NavigationStatus status) {
    switch(status){case NavigationStatus::Ready:return {};case NavigationStatus::Preparing:return "Preparing navigation.";
    case NavigationStatus::Unreachable:return "No reachable walking surface is available for this actor.";
    case NavigationStatus::Failed:return "Navigation tile generation failed.";default:return "Add or enable a NavigationVolume and include this Agent Profile in Supported Agent Profiles.";}
}
}
struct NavigationWorld::Impl {
    struct Tile {std::uint64_t generation{},fingerprint{};ResourceHandle<NavigationTileData> pending;std::shared_ptr<const NavigationTileData> accepted,stale;NavigationStatus status{NavigationStatus::Preparing};std::string diagnostic;bool retry{};};
    struct Profile {AgentTraversalSettings agent;NavigationMesh mesh;std::map<NavigationTileKey,Tile> tiles;std::string diagnostic;std::uint64_t revision{NextNavigationRevision.fetch_add(1)};};
    ResourceManager& resources;std::filesystem::path cache;std::uint64_t domain{},builds{},version{};
    std::optional<NavigationVolumeDescriptor> volume;std::map<std::string,Profile> profiles;
    std::map<EntityId,PhysicsBox> boxes;std::map<EntityId,PhysicsStaticMesh> meshes;
    std::string diagnostic,sourceDiagnostic;bool sourcesCurrent{true};
    Impl(ResourceManager& manager,std::filesystem::path path):resources(manager),cache(std::move(path)){}
    void invalidate(Profile& profile,NavigationTileKey key) {
        auto& tile=profile.tiles[key];if(tile.accepted)tile.stale=std::move(tile.accepted);tile.pending={};tile.status=NavigationStatus::Preparing;tile.diagnostic.clear();tile.generation=++version;profile.revision=NextNavigationRevision.fetch_add(1);
        profile.mesh.remove(key);
    }
    void dirty(const Aabb& bounds) {
        if(!volume)return;
        for(auto& [id,profile]:profiles)for(auto& [key,tile]:profile.tiles)if(overlaps(bounds,tileBounds(key,*volume,profile.agent)))invalidate(profile,key);
    }
    NavigationTileInput input(NavigationTileKey key,const Profile& profile) const {
        NavigationTileInput result;result.tile=key;result.volume=*volume;result.agent=profile.agent;
        const auto [low,high]=verticalRange(*volume,profile.agent);result.minimumY=low;result.maximumY=high;
        const auto bounds=tileBounds(key,*volume,profile.agent);
        for(const auto& [id,source]:boxes)if(overlaps(bounds,sourceBounds(source)))result.boxes.push_back(source);
        for(const auto& [id,source]:meshes)if(overlaps(bounds,sourceBounds(source)))result.meshes.push_back(source);
        result.fingerprint=navigationTileFingerprint(result);return result;
    }
    NavigationStatus state(const Profile& profile) const {
        if(!profile.diagnostic.empty()||!diagnostic.empty())return NavigationStatus::Failed;
        bool pending=!sourcesCurrent,failed=false,walkable=false;for(const auto& [key,tile]:profile.tiles){pending|=tile.status==NavigationStatus::Preparing;failed|=tile.status==NavigationStatus::Failed;walkable|=tile.accepted&&!tile.accepted->triangles.empty();}
        if(pending)return NavigationStatus::Preparing;if(failed)return NavigationStatus::Failed;if(profile.tiles.empty())return NavigationStatus::Unavailable;return walkable?NavigationStatus::Ready:NavigationStatus::Unreachable;
    }
    void schedule() {
        if(!volume||!volume->enabled||!diagnostic.empty())return;
        std::size_t jobs=0;for(const auto& [id,profile]:profiles)for(const auto& [key,tile]:profile.tiles)jobs+=bool(tile.pending);
        for(auto& [id,profile]:profiles)for(auto& [key,tile]:profile.tiles) {
            if(jobs>=MaximumJobs)return;if(tile.status!=NavigationStatus::Preparing||tile.pending)continue;
            auto detached=input(key,profile);tile.fingerprint=detached.fingerprint;
            ResourceRequestOptions options;options.priority=ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;
            options.forceReload=tile.retry;tile.retry=false;
            tile.pending=resources.requestDerived<NavigationTileData>("navigation-tile-v3-"+std::to_string(tile.fingerprint),
                [detached=std::move(detached),path=cache](std::stop_token stop) {
                    auto data=buildNavigationTile(detached,stop,path);const auto bytes=sizeof(NavigationTileData)+data->data.size()+data->vertices.size()*sizeof(Vec3)+data->triangles.size()*sizeof(std::uint32_t);
                    return ResourceLoadResult<NavigationTileData>{data,bytes,data->fingerprint,{}};
                },std::move(options));++jobs;++builds;
        }
    }
};
NavigationWorld::NavigationWorld(ResourceManager& resources,std::filesystem::path cache):impl_(std::make_unique<Impl>(resources,std::move(cache))){}
NavigationWorld::~NavigationWorld()=default;
void NavigationWorld::clear(){auto& s=*impl_;s.profiles.clear();s.boxes.clear();s.meshes.clear();s.volume.reset();s.diagnostic.clear();s.sourceDiagnostic.clear();s.sourcesCurrent=true;s.domain=0;++s.version;}
void NavigationWorld::upsert(const PhysicsBox& source) {
    auto& s=*impl_;const auto found=s.boxes.find(source.entity);
    if(found!=s.boxes.end()&&found->second.world==source.world&&found->second.size==source.size)return;
    if(found!=s.boxes.end())s.dirty(sourceBounds(found->second));if(const auto old=s.meshes.find(source.entity);old!=s.meshes.end()){s.dirty(sourceBounds(old->second));s.meshes.erase(old);}
    s.boxes[source.entity]=source;s.dirty(sourceBounds(source));
}
void NavigationWorld::upsert(const PhysicsStaticMesh& source) {
    auto& s=*impl_;const auto found=s.meshes.find(source.entity);
    if(found!=s.meshes.end()&&found->second.world==source.world&&found->second.mesh==source.mesh&&found->second.solid==source.solid)return;
    if(found!=s.meshes.end())s.dirty(sourceBounds(found->second));if(const auto old=s.boxes.find(source.entity);old!=s.boxes.end()){s.dirty(sourceBounds(old->second));s.boxes.erase(old);}
    s.meshes[source.entity]=source;s.dirty(sourceBounds(source));
}
void NavigationWorld::removeSource(EntityId id) {
    auto& s=*impl_;if(const auto found=s.boxes.find(id);found!=s.boxes.end()){s.dirty(sourceBounds(found->second));s.boxes.erase(found);}
    if(const auto found=s.meshes.find(id);found!=s.meshes.end()){s.dirty(sourceBounds(found->second));s.meshes.erase(found);}
}
void NavigationWorld::setSourceReadiness(bool current,std::string diagnostic){if(impl_->sourcesCurrent!=current)for(auto& [id,profile]:impl_->profiles)profile.revision=NextNavigationRevision.fetch_add(1);impl_->sourcesCurrent=current;impl_->sourceDiagnostic=std::move(diagnostic);}
void NavigationWorld::reconcile(std::uint64_t generation,const std::optional<NavigationVolumeDescriptor>& volume,std::span<const NavigationAgentProfile> supported,
    std::span<const PhysicsBox> boxes,std::span<const PhysicsStaticMesh> meshes) {
    auto& s=*impl_;if(s.domain!=generation){const bool current=s.sourcesCurrent;auto sourceDiagnostic=std::move(s.sourceDiagnostic);clear();s.sourcesCurrent=current;s.sourceDiagnostic=std::move(sourceDiagnostic);s.domain=generation;}
    if(!volume||!volume->enabled){s.profiles.clear();s.volume=volume;s.boxes.clear();s.meshes.clear();s.diagnostic.clear();return;}
    std::set<NavigationTileKey> keys;
    try {
        if(!validNavigationBakeSettings(volume->bake)||!math::finite(volume->world)||!math::inverse(volume->world))throw std::runtime_error("NavigationVolume requires valid bake settings and an invertible transform.");
        for(auto size:volume->size)if(!std::isfinite(size)||size<=0)throw std::runtime_error("NavigationVolume requires positive finite dimensions.");
        keys=coverage(*volume);s.diagnostic.clear();
    }catch(const std::exception& error){s.profiles.clear();s.volume=volume;s.diagnostic=error.what();return;}
    const auto previous=s.volume;const bool changed=!previous||*previous!=*volume;s.volume=volume;
    std::set<std::string> kept;
    for(const auto& supportedProfile:supported) {
        if(supportedProfile.id.empty()||!validAgentTraversal(supportedProfile.traversal))continue;kept.insert(supportedProfile.id);
        auto& profile=s.profiles[supportedProfile.id];
        const bool reconfigured=profile.mesh.configure(volume->bake,supportedProfile.traversal);profile.agent=supportedProfile.traversal;
        if(reconfigured)profile.tiles.clear();
        std::erase_if(profile.tiles,[&](const auto& item){if(keys.contains(item.first))return false;profile.mesh.remove(item.first);profile.revision=NextNavigationRevision.fetch_add(1);return true;});
        for(const auto key:keys) {
            bool dirty=reconfigured||!profile.tiles.contains(key);
            if(changed && !dirty) {
                const auto oldRange=verticalRange(*previous,profile.agent),newRange=verticalRange(*volume,profile.agent);
                dirty=oldRange!=newRange;
                if(!dirty) {
                    auto region=tileBounds(key,*volume,profile.agent);const auto bounds=navigationVolumeBounds(*volume);
                    region.minimum[1]=bounds.minimum[1]+1e-4f;region.maximum[1]=bounds.maximum[1]-1e-4f;
                    dirty=!entirelyInside(*previous,region)||!entirelyInside(*volume,region);
                }
            }
            if(dirty)s.invalidate(profile,key);
        }
    }
    std::erase_if(s.profiles,[&](const auto& item){return !kept.contains(item.first);});
    std::set<EntityId> sources;for(const auto& source:boxes){sources.insert(source.entity);upsert(source);}for(const auto& source:meshes){sources.insert(source.entity);upsert(source);}
    std::vector<EntityId> removed;for(const auto& [id,value]:s.boxes)if(!sources.contains(id))removed.push_back(id);for(const auto& [id,value]:s.meshes)if(!sources.contains(id))removed.push_back(id);
    for(auto id:removed)removeSource(id);
    for(auto& [id,profile]:s.profiles)profile.mesh.finishPublication();pumpPublication();
}
void NavigationWorld::pumpPublication() {
    auto& s=*impl_;
    // CPU work/cache reads can complete ahead of collision. Live Detour and its
    // ready overlay are published only after this domain's source gate opens.
    if(!s.sourcesCurrent){s.schedule();return;}
    for(auto& [id,profile]:s.profiles) {
        for(auto& [key,tile]:profile.tiles)if(tile.pending) {
            if(const auto ready=tile.pending.snapshot()) {
                // A superseding input drops its handle; a late result cannot
                // reach this entry. Fingerprint validates this exact request.
                if(ready->fingerprint!=tile.fingerprint)continue;
                if(profile.mesh.publish(ready.shared(),tile.generation)){tile.status=NavigationStatus::Ready;tile.accepted=ready.shared();tile.stale.reset();profile.revision=NextNavigationRevision.fetch_add(1);}
                else {tile.status=NavigationStatus::Failed;tile.diagnostic=profile.mesh.status();profile.revision=NextNavigationRevision.fetch_add(1);}
                tile.pending={};
            }else if(tile.pending.state()==ResourceState::Failed){tile.status=NavigationStatus::Failed;tile.diagnostic=tile.pending.diagnostic();tile.pending={};profile.revision=NextNavigationRevision.fetch_add(1);}
        }
        profile.mesh.finishPublication();
    }
    s.schedule();
}
void NavigationWorld::prepare(const std::string& id) {
    auto& s=*impl_;const auto found=s.profiles.find(id);if(found==s.profiles.end())return;
    for(auto& [key,tile]:found->second.tiles)if(tile.status==NavigationStatus::Failed){s.invalidate(found->second,key);tile.retry=true;}pumpPublication();
}
NavigationStatus NavigationWorld::readiness(const std::string& id) const {const auto& s=*impl_;const auto found=s.profiles.find(id);if(found==s.profiles.end())return s.diagnostic.empty()?NavigationStatus::Unavailable:NavigationStatus::Failed;return s.state(found->second);}
NavigationResult NavigationWorld::query(const std::string& id,const Vec3& start,const Vec3& target) const {
    const auto& s=*impl_;const auto found=s.profiles.find(id);const auto status=readiness(id);
    if(found==s.profiles.end())return {status,{},s.diagnostic.empty()?statusText(status):s.diagnostic};
    if(!s.sourcesCurrent)return {NavigationStatus::Preparing,{},s.sourceDiagnostic.empty()?statusText(NavigationStatus::Preparing):s.sourceDiagnostic};
    if(auto exact=found->second.mesh.findPath(start,target))return {NavigationStatus::Ready,std::move(exact),{}};
    // Temporary tile holes must never become an artificial final approach point.
    if(status!=NavigationStatus::Ready)return {status,{},statusText(status)};
    if(auto reachable=found->second.mesh.findReachablePath(start,target))return {NavigationStatus::Ready,std::move(reachable),{}};
    return {NavigationStatus::Unreachable,{},statusText(NavigationStatus::Unreachable)};
}
NavigationProjection NavigationWorld::project(const std::string& id,const Vec3& point,const Vec3& extents) const {
    const auto& s=*impl_;const auto found=s.profiles.find(id);const auto status=readiness(id);
    if(found==s.profiles.end())return {status,{},statusText(status)};
    if(status!=NavigationStatus::Ready)return {status,{},statusText(status)};
    if(auto projected=found->second.mesh.project(point,extents))return {NavigationStatus::Ready,projected,{}};
    return {status==NavigationStatus::Ready?NavigationStatus::Unreachable:status,{},statusText(status==NavigationStatus::Ready?NavigationStatus::Unreachable:status)};
}
bool NavigationWorld::valid(const std::string& id,const NavigationPath& path) const {const auto found=impl_->profiles.find(id);return found!=impl_->profiles.end()&&found->second.mesh.valid(path);}
NavigationDebugSnapshot NavigationWorld::summary(const std::string& id) const {
    const auto& s=*impl_;const auto found=s.profiles.find(id);NavigationDebugSnapshot result;result.pendingSources=!s.sourcesCurrent;
    if(found!=s.profiles.end()) {result.revision=found->second.revision;for(const auto& [key,tile]:found->second.tiles){result.readyTiles+=tile.status==NavigationStatus::Ready;result.pendingTiles+=tile.status==NavigationStatus::Preparing;result.failedTiles+=tile.status==NavigationStatus::Failed;if(!tile.diagnostic.empty())result.diagnostic=tile.diagnostic;}}
    result.status=readiness(id);if(!s.diagnostic.empty())result.diagnostic=s.diagnostic;if(!s.sourcesCurrent&&!s.sourceDiagnostic.empty()&&found!=s.profiles.end())result.diagnostic=s.sourceDiagnostic;if(result.diagnostic.empty())result.diagnostic=statusText(result.status);return result;
}
NavigationDebugSnapshot NavigationWorld::debugSnapshot(const std::string& id) const {
    const auto& s=*impl_;const auto found=s.profiles.find(id);auto result=summary(id);
    if(found!=s.profiles.end()) {
        auto geometry=found->second.mesh.debugSnapshot();result.vertices=std::move(geometry.vertices);result.triangles=std::move(geometry.triangles);
        for(const auto& [key,tile]:found->second.tiles)if(tile.stale) {
            const auto base=static_cast<std::uint32_t>(result.pendingVertices.size());result.pendingVertices.insert(result.pendingVertices.end(),tile.stale->vertices.begin(),tile.stale->vertices.end());for(auto index:tile.stale->triangles)result.pendingTriangles.push_back(base+index);
        }
    }return result;
}
std::uint64_t NavigationWorld::revision(const std::string& id) const {const auto found=impl_->profiles.find(id);return found==impl_->profiles.end()?0:found->second.revision;}
std::uint64_t NavigationWorld::buildCount() const{return impl_->builds;}
}
