#include "NavigationMesh.h"
#include <Recast.h>
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <cstring>

namespace crimson {
namespace {
template<class T,void (*Free)(T*)> using Owner=std::unique_ptr<T,decltype(Free)>;
constexpr int MaximumCorners=512;
constexpr unsigned MaximumReachableCandidates=8192,MaximumReachableQueries=24;

double squaredDistance(const Vec3& a,const Vec3& b) {
    double result=0;for(unsigned axis=0;axis<3;++axis){const double delta=double(a[axis])-b[axis];result+=delta*delta;}return result;
}
std::optional<NavigationPath> completePath(dtNavMeshQuery& query,dtPolyRef startPolygon,dtPolyRef endPolygon,
    const Vec3& start,const Vec3& nearStart,const Vec3& nearEnd,const Vec3& destination,const dtQueryFilter& filter,
    const dtNavMesh& mesh,const std::map<NavigationTileKey,NavigationTileVersion>& versions) {
    std::array<dtPolyRef,MaximumCorners> corridor{};int polygonCount=0;
    const auto result=query.findPath(startPolygon,endPolygon,nearStart.data(),nearEnd.data(),&filter,
        corridor.data(),&polygonCount,static_cast<int>(corridor.size()));
    if(dtStatusFailed(result) || dtStatusDetail(result,DT_PARTIAL_RESULT|DT_BUFFER_TOO_SMALL|DT_OUT_OF_NODES) ||
       polygonCount==0 || corridor[polygonCount-1]!=endPolygon)return std::nullopt;
    std::array<float,MaximumCorners*3> straight{};std::array<unsigned char,MaximumCorners> flags{};int cornerCount=0;
    const auto straightResult=query.findStraightPath(nearStart.data(),nearEnd.data(),corridor.data(),polygonCount,
        straight.data(),flags.data(),nullptr,&cornerCount,MaximumCorners);
    if(dtStatusFailed(straightResult) || dtStatusDetail(straightResult,DT_BUFFER_TOO_SMALL) || cornerCount==0 ||
       !(flags[cornerCount-1]&DT_STRAIGHTPATH_END))return std::nullopt;
    NavigationPath path;path.corners.reserve(cornerCount);
    for(int index=0;index<polygonCount;++index) {
        const dtMeshTile* tile{};const dtPoly* polygon{};
        if(dtStatusFailed(mesh.getTileAndPolyByRef(corridor[index],&tile,&polygon)))return {};
        const auto found=versions.find({tile->header->x,tile->header->y});if(found==versions.end())return {};
        if(std::find(path.tiles.begin(),path.tiles.end(),found->second)==path.tiles.end())path.tiles.push_back(found->second);
    }
    if(std::hypot(nearStart[0]-start[0],nearStart[2]-start[2])>.01f)path.corners.push_back(nearStart);
    for(int corner=1;corner<cornerCount;++corner)path.corners.push_back({straight[corner*3],straight[corner*3+1],straight[corner*3+2]});
    if(path.corners.empty())path.corners.push_back(destination);else path.corners.back()=destination;
    return path;
}

}
struct NavigationMesh::Impl {
    Owner<dtNavMesh,dtFreeNavMesh> mesh{nullptr,dtFreeNavMesh};
    Owner<dtNavMeshQuery,dtFreeNavMeshQuery> query{nullptr,dtFreeNavMeshQuery};
    std::uint64_t revision{};
    float radius{},height{},CellSize{.1f},CellHeight{.05f},MaximumStep{.3f};
    NavigationBakeSettings settings; AgentTraversalSettings agent; bool configured{},changed{};
    std::map<NavigationTileKey,NavigationTileVersion> versions;
    std::map<NavigationTileKey,std::shared_ptr<const NavigationTileData>> payloads;
    std::string diagnostic;
    std::unordered_map<dtPolyRef,std::size_t> componentByPolygon;
    std::vector<Aabb> componentBounds;
    Aabb bounds;
    bool overlapsMesh(const Vec3& position,const Vec3& extents) const {
        if(!bounds.valid)return false;
        for(unsigned axis=0;axis<3;++axis)if(double(position[axis])+extents[axis]<bounds.minimum[axis] ||
            double(position[axis])-extents[axis]>bounds.maximum[axis])return false;
        return true;
    }
    void cacheConnectivity() {
        componentByPolygon.clear();componentBounds.clear();bounds={};
        const auto& navigation=*static_cast<const dtNavMesh*>(mesh.get());
        std::vector<dtPolyRef> polygons;
        for(int tileIndex=0;tileIndex<navigation.getMaxTiles();++tileIndex) {
            const auto* tile=navigation.getTile(tileIndex);if(!tile || !tile->header)continue;
            const auto base=navigation.getPolyRefBase(tile);
            for(int index=0;index<tile->header->polyCount;++index) {
                const auto& polygon=tile->polys[index];
                if(!(polygon.flags&1) || polygon.getType()!=DT_POLYTYPE_GROUND)continue;
                const auto reference=base|static_cast<dtPolyRef>(index);polygons.push_back(reference);
                componentByPolygon.emplace(reference,std::numeric_limits<std::size_t>::max());
            }
        }
        // Recast's baked ground links are bidirectional. Off-mesh links are not
        // part of this static-geometry slice and are deliberately excluded above.
        std::vector<dtPolyRef> pending;
        for(const auto root:polygons) {
            if(componentByPolygon.at(root)!=std::numeric_limits<std::size_t>::max())continue;
            const auto component=componentBounds.size();componentBounds.emplace_back();pending.clear();pending.push_back(root);
            componentByPolygon.at(root)=component;
            for(std::size_t next=0;next<pending.size();++next) {
                const dtMeshTile* tile{};const dtPoly* polygon{};
                navigation.getTileAndPolyByRefUnsafe(pending[next],&tile,&polygon);
                auto& polygonBounds=componentBounds.back();
                for(unsigned vertex=0;vertex<polygon->vertCount;++vertex) {
                    const auto* position=tile->verts+polygon->verts[vertex]*3;math::include(polygonBounds,{position[0],position[1],position[2]});
                }
                const auto& detail=tile->detailMeshes[polygon-tile->polys];
                for(unsigned vertex=0;vertex<detail.vertCount;++vertex) {
                    const auto* position=tile->detailVerts+(detail.vertBase+vertex)*3;math::include(polygonBounds,{position[0],position[1],position[2]});
                }
                for(auto link=polygon->firstLink;link!=DT_NULL_LINK;link=tile->links[link].next) {
                    const auto found=componentByPolygon.find(tile->links[link].ref);
                    if(found!=componentByPolygon.end() && found->second==std::numeric_limits<std::size_t>::max()) {
                        found->second=component;pending.push_back(found->first);
                    }
                }
            }
        }
        for(const auto& component:componentBounds)math::include(bounds,component);
    }
    struct ReachableQuery final:dtPolyQuery {
        const Impl& owner;std::size_t component;Vec3 desired,start,closest{};dtPolyRef polygon{};
        double distanceSquared{std::numeric_limits<double>::infinity()},startDistanceSquared{std::numeric_limits<double>::infinity()};
        unsigned inspected{};bool exhausted{};std::unordered_set<dtPolyRef> visited;
        ReachableQuery(const Impl& state,std::size_t group,const Vec3& target,const Vec3& origin)
            :owner(state),component(group),desired(target),start(origin){visited.reserve(128);}
        void process(const dtMeshTile*,dtPoly**,dtPolyRef* references,int count) override {
            if(exhausted)return;
            for(int index=0;index<count;++index) {
                if(inspected++>=MaximumReachableCandidates){exhausted=true;return;}
                const auto reference=references[index];const auto label=owner.componentByPolygon.find(reference);
                if(label==owner.componentByPolygon.end() || label->second!=component || !visited.insert(reference).second)continue;
                Vec3 point;
                if(dtStatusFailed(owner.query->closestPointOnPoly(reference,desired.data(),point.data(),nullptr)) || !math::finite(point))continue;
                const auto distance=squaredDistance(point,desired),fromStart=squaredDistance(point,start);
                constexpr double tie=1e-6;
                if(!polygon || distance<distanceSquared-tie || (std::abs(distance-distanceSquared)<=tie &&
                    (fromStart<startDistanceSquared-tie || (std::abs(fromStart-startDistanceSquared)<=tie && reference<polygon)))) {
                    closest=point;polygon=reference;distanceSquared=distance;startDistanceSquared=fromStart;
                }
            }
        }
    };
    std::optional<std::pair<dtPolyRef,Vec3>> closestReachable(dtPolyRef startPolygon,const Vec3& start,const Vec3& desired,const dtQueryFilter& filter) const {
        const auto found=componentByPolygon.find(startPolygon);if(found==componentByPolygon.end())return {};
        const auto& componentBox=componentBounds[found->second];ReachableQuery candidates(*this,found->second,desired,start);
        double minimumSquared=0,maximumExtent=0;
        for(unsigned axis=0;axis<3;++axis) {
            const double position=desired[axis],low=componentBox.minimum[axis],high=componentBox.maximum[axis];
            const double nearest=position-std::clamp(position,low,high);minimumSquared+=nearest*nearest;
            maximumExtent=std::max({maximumExtent,std::abs(position-low),std::abs(position-high)});
        }
        double extent=std::max(.5,std::sqrt(minimumSquared)+CellSize);maximumExtent+=CellSize;
        for(unsigned iteration=0;iteration<MaximumReachableQueries;++iteration) {
            Vec3 center,halfExtents;
            // Clipping to cached component bounds keeps far-away finite hits
            // from overflowing Detour's world-to-tile integer conversion.
            for(unsigned axis=0;axis<3;++axis) {
                const double low=std::max(double(componentBox.minimum[axis])-CellSize,double(desired[axis])-extent);
                const double high=std::min(double(componentBox.maximum[axis])+CellSize,double(desired[axis])+extent);
                center[axis]=static_cast<float>((low+high)*.5);halfExtents[axis]=static_cast<float>((high-low)*.5+CellHeight);
            }
            if(dtStatusFailed(query->queryPolygons(center.data(),halfExtents.data(),&filter,&candidates)) || candidates.exhausted)return {};
            // Any polygon outside this cube is farther than extent. Thus a
            // candidate inside that Euclidean radius is the global closest
            // reachable projection, without searching disconnected surfaces.
            if(candidates.polygon && (candidates.distanceSquared<=extent*extent || extent>=maximumExtent))
                return std::pair{candidates.polygon,candidates.closest};
            if(extent>=maximumExtent)return {};
            extent=std::min(maximumExtent,extent*2);
        }
        return {};
    }

};
NavigationMesh::NavigationMesh():impl_(std::make_unique<Impl>()){}
NavigationMesh::~NavigationMesh()=default;

bool NavigationMesh::configure(const NavigationBakeSettings& settings,const AgentTraversalSettings& agent) {
    auto& state=*impl_;if(state.configured && state.settings==settings && state.agent==agent)return false;
    state.query.reset();state.mesh.reset();state.payloads.clear();state.versions.clear();state.componentByPolygon.clear();state.componentBounds.clear();state.bounds={};
    state.settings=settings;state.agent=agent;state.radius=agent.radius;state.height=agent.height;
    state.CellSize=settings.cellSize;state.CellHeight=settings.cellHeight;state.MaximumStep=agent.maxStep;state.configured=true;state.changed=true;++state.revision;
    if(!validNavigationBakeSettings(settings)||!validAgentTraversal(agent)){state.diagnostic="Invalid navigation settings.";return true;}
    state.mesh.reset(dtAllocNavMesh());state.query.reset(dtAllocNavMeshQuery());
    dtNavMeshParams parameters{};parameters.tileWidth=settings.tileSize*settings.cellSize;parameters.tileHeight=parameters.tileWidth;
    parameters.maxTiles=2048;parameters.maxPolys=2048;
    if(!state.mesh||!state.query||dtStatusFailed(state.mesh->init(&parameters))||dtStatusFailed(state.query->init(state.mesh.get(),8192))) {
        state.query.reset();state.mesh.reset();state.diagnostic="Cannot allocate tiled navigation world.";
    } else state.diagnostic.clear();return true;
}
bool NavigationMesh::publish(std::shared_ptr<const NavigationTileData> tile,std::uint64_t version) {
    auto& state=*impl_;if(!state.mesh || !tile)return false;
    remove(tile->tile);
    if(!tile->data.empty()) {
        auto* bytes=static_cast<unsigned char*>(dtAlloc(tile->data.size(),DT_ALLOC_PERM));
        if(!bytes){state.diagnostic="Cannot allocate navigation tile.";return false;}
        std::memcpy(bytes,tile->data.data(),tile->data.size());
        if(dtStatusFailed(state.mesh->addTile(bytes,static_cast<int>(tile->data.size()),DT_TILE_FREE_DATA,0,nullptr))) {
            dtFree(bytes);state.diagnostic="Navigation tile exceeds Detour capacity (2048 tiles / 2048 polygons per tile).";return false;
        }
    }
    state.versions[tile->tile]={tile->tile,version};state.payloads[tile->tile]=std::move(tile);state.changed=true;++state.revision;return true;
}
bool NavigationMesh::remove(NavigationTileKey key) {
    auto& state=*impl_;if(!state.mesh || !state.versions.erase(key))return false;
    if(const auto ref=state.mesh->getTileRefAt(key.x,key.z,0))state.mesh->removeTile(ref,nullptr,nullptr);
    state.payloads.erase(key);state.changed=true;++state.revision;return true;
}
void NavigationMesh::finishPublication(){auto& state=*impl_;if(state.mesh&&state.changed)state.cacheConnectivity();state.changed=false;}
bool NavigationMesh::valid(const NavigationPath& path) const {
    for(const auto& dependency:path.tiles){const auto found=impl_->versions.find(dependency.tile);if(found==impl_->versions.end()||found->second!=dependency)return false;}return !path.tiles.empty();
}
std::optional<Vec3> NavigationMesh::project(const Vec3& point,const Vec3& extents) const {
    const auto& state=*impl_;if(!state.query||!math::finite(point)||!math::finite(extents)||!state.overlapsMesh(point,extents))return {};
    for(float extent:extents)if(extent<0)return {};
    dtQueryFilter filter;filter.setIncludeFlags(1);dtPolyRef polygon{};Vec3 nearest{};
    if(dtStatusFailed(state.query->findNearestPoly(point.data(),extents.data(),&filter,&polygon,nearest.data()))||!polygon)return {};return nearest;
}
NavigationDebugSnapshot NavigationMesh::debugSnapshot() const {
    NavigationDebugSnapshot result;result.status=NavigationStatus::Ready;result.revision=impl_->revision;result.diagnostic=impl_->diagnostic;
    for(const auto& [key,tile]:impl_->payloads) {
        ++result.readyTiles;const auto base=static_cast<std::uint32_t>(result.vertices.size());
        result.vertices.insert(result.vertices.end(),tile->vertices.begin(),tile->vertices.end());
        for(auto index:tile->triangles)result.triangles.push_back(base+index);
    }return result;
}

std::optional<NavigationPath> NavigationMesh::findPath(const Vec3& start,const Vec3& destination) const {
    const auto& state=*impl_;
    if(!state.query || !math::finite(start) || !math::finite(destination))return std::nullopt;
    dtQueryFilter filter;filter.setIncludeFlags(1);
    const Vec3 startExtents{state.radius+state.CellSize,state.MaximumStep+.2f,state.radius+state.CellSize};
    const Vec3 endExtents{state.CellSize,state.CellHeight*4,state.CellSize};
    if(!state.overlapsMesh(start,startExtents) || !state.overlapsMesh(destination,endExtents))return {};
    dtPolyRef startPolygon{},endPolygon{};Vec3 nearStart{},nearEnd{};
    if(dtStatusFailed(state.query->findNearestPoly(start.data(),startExtents.data(),&filter,&startPolygon,nearStart.data())) || !startPolygon ||
       dtStatusFailed(state.query->findNearestPoly(destination.data(),endExtents.data(),&filter,&endPolygon,nearEnd.data())) || !endPolygon)return std::nullopt;
    // Never turn a click on an unreachable ledge/gap into a faraway destination.
    if(std::hypot(nearEnd[0]-destination[0],nearEnd[2]-destination[2])>state.CellSize*.5f ||
       std::abs(nearEnd[1]-destination[1])>state.CellHeight*4)return std::nullopt;
    if(state.componentByPolygon.at(startPolygon)!=state.componentByPolygon.at(endPolygon))return {};
    return completePath(*state.query,startPolygon,endPolygon,start,nearStart,nearEnd,destination,filter,*state.mesh,state.versions);
}
std::optional<NavigationPath> NavigationMesh::findReachablePath(const Vec3& start,const Vec3& destination) const {
    if(const auto exact=findPath(start,destination))return exact;
    const auto& state=*impl_;
    if(!state.query || !math::finite(start) || !math::finite(destination))return {};
    dtQueryFilter filter;filter.setIncludeFlags(1);
    const Vec3 extents{state.radius+state.CellSize,state.MaximumStep+.2f,state.radius+state.CellSize};dtPolyRef startPolygon{};Vec3 nearStart{};
    if(!state.overlapsMesh(start,extents))return {};
    if(dtStatusFailed(state.query->findNearestPoly(start.data(),extents.data(),&filter,&startPolygon,nearStart.data())) || !startPolygon)return {};
    const auto destinationOnMesh=state.closestReachable(startPolygon,nearStart,destination,filter);if(!destinationOnMesh)return {};
    return completePath(*state.query,startPolygon,destinationOnMesh->first,start,nearStart,destinationOnMesh->second,destinationOnMesh->second,filter,*state.mesh,state.versions);
}
std::uint64_t NavigationMesh::revision() const{return impl_->revision;}
const std::string& NavigationMesh::status() const{return impl_->diagnostic;}
}
