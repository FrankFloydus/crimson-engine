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

namespace crimson {
namespace {
template<class T,void (*Free)(T*)> using Owner=std::unique_ptr<T,decltype(Free)>;
constexpr std::uint64_t MaximumCells=4'000'000;
constexpr std::uint64_t MaximumRasterCells=16'000'000;
constexpr int MaximumCorners=512;
constexpr unsigned MaximumReachableCandidates=8192,MaximumReachableQueries=24;
constexpr float Skin=.02f; // Jolt CharacterVirtual's default contact padding.

void appendBox(const PhysicsBox& box,std::vector<float>& vertices,std::vector<int>& triangles) {
    const int first=static_cast<int>(vertices.size()/3);
    for(int corner=0;corner<8;++corner) {
        const auto point=math::transformPoint(box.world,{box.size[0]*(corner&1?.5f:-.5f),
            box.size[1]*(corner&2?.5f:-.5f),box.size[2]*(corner&4?.5f:-.5f)});
        if(!math::finite(point))throw std::runtime_error("Navigation geometry contains nonfinite box corners.\n");
        vertices.insert(vertices.end(),point.begin(),point.end());
    }
    // Outward facing triangles; reflected transforms reverse their winding.
    constexpr int faces[]={0,4,6,0,6,2, 1,3,7,1,7,5, 0,1,5,0,5,4,
                           2,6,7,2,7,3, 0,2,3,0,3,1, 4,5,7,4,7,6};
    const bool reflected=math::handedness(box.world)<0;
    for(int index=0;index<36;index+=3) {
        triangles.push_back(first+faces[index]);
        triangles.push_back(first+faces[index+(reflected?2:1)]);
        triangles.push_back(first+faces[index+(reflected?1:2)]);
    }
}
void appendMesh(const PhysicsStaticMesh& source,std::vector<float>& vertices,std::vector<int>& triangles) {
    if(!source.mesh)return;
    const auto& mesh=*source.mesh;
    if(vertices.size()/3+mesh.positions.size()>4'000'000 || triangles.size()+mesh.indices.size()>12'000'000)
        throw std::runtime_error("Navigation triangle geometry exceeds its preparation budget.\n");
    const int first=static_cast<int>(vertices.size()/3);
    for(const auto& position:mesh.positions) {
        const auto world=math::transformPoint(source.world,position);
        if(!math::finite(world))throw std::runtime_error("Navigation mesh contains nonfinite positions.\n");
        vertices.insert(vertices.end(),world.begin(),world.end());
    }
    const bool reflected=math::handedness(source.world)<0;
    for(std::size_t index=0;index<mesh.indices.size();index+=3)for(unsigned corner=0;corner<3;++corner) {
        const auto vertex=mesh.indices.at(index+(reflected && corner?3-corner:corner));
        if(vertex>=mesh.positions.size())throw std::runtime_error("Navigation mesh index is out of bounds.\n");
        triangles.push_back(first+static_cast<int>(vertex));
    }
}
void require(bool success,const char* stage) {
    if(!success)throw std::runtime_error(std::string("Navigation build failed: ")+stage+".\n");
}
double squaredDistance(const Vec3& a,const Vec3& b) {
    double result=0;for(unsigned axis=0;axis<3;++axis){const double delta=double(a[axis])-b[axis];result+=delta*delta;}return result;
}
std::optional<NavigationPath> completePath(dtNavMeshQuery& query,dtPolyRef startPolygon,dtPolyRef endPolygon,
    const Vec3& start,const Vec3& nearStart,const Vec3& nearEnd,const Vec3& destination,const dtQueryFilter& filter) {
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
    if(std::hypot(nearStart[0]-start[0],nearStart[2]-start[2])>.01f)path.corners.push_back(nearStart);
    for(int corner=1;corner<cornerCount;++corner)path.corners.push_back({straight[corner*3],straight[corner*3+1],straight[corner*3+2]});
    if(path.corners.empty())path.corners.push_back(destination);else path.corners.back()=destination;
    return path;
}
void rasterizeSolidBox(rcContext& context,const rcConfig& config,const PhysicsBox& box,rcHeightfield& target,std::uint64_t& remainingCells) {
    std::vector<float> vertices;std::vector<int> triangles;appendBox(box,vertices,triangles);
    float minimum[3],maximum[3];rcCalcBounds(vertices.data(),8,minimum,maximum);
    const int beginX=std::clamp(static_cast<int>(std::floor((minimum[0]-config.bmin[0])/config.cs)),0,config.width-1);
    const int beginZ=std::clamp(static_cast<int>(std::floor((minimum[2]-config.bmin[2])/config.cs)),0,config.height-1);
    const int endX=std::clamp(static_cast<int>(std::ceil((maximum[0]-config.bmin[0])/config.cs)),beginX+1,config.width);
    const int endZ=std::clamp(static_cast<int>(std::ceil((maximum[2]-config.bmin[2])/config.cs)),beginZ+1,config.height);
    const auto cells=static_cast<std::uint64_t>(endX-beginX)*(endZ-beginZ);
    require(cells<=remainingCells,"overlapping colliders exceed the 16-million-cell rasterization limit");remainingCells-=cells;
    minimum[0]=config.bmin[0]+beginX*config.cs;minimum[1]=config.bmin[1];minimum[2]=config.bmin[2]+beginZ*config.cs;
    maximum[0]=config.bmin[0]+endX*config.cs;maximum[1]=config.bmax[1];maximum[2]=config.bmin[2]+endZ*config.cs;
    Owner<rcHeightfield,rcFreeHeightField> shape(rcAllocHeightfield(),rcFreeHeightField);
    require(shape!=nullptr,"box heightfield allocation");
    require(rcCreateHeightfield(&context,*shape,endX-beginX,endZ-beginZ,minimum,maximum,config.cs,config.ch),"box heightfield");
    std::array<unsigned char,12> areas{};
    rcMarkWalkableTriangles(&context,config.walkableSlopeAngle,vertices.data(),8,triangles.data(),12,areas.data());
    require(rcRasterizeTriangles(&context,vertices.data(),8,triangles.data(),areas.data(),12,*shape,config.walkableClimb),"box rasterization");
    // Recast rasterizes surfaces. A Collider is a solid convex volume, so fill
    // from each column's lowest to highest box surface before merging boxes.
    // This prevents a coincident floor from becoming walkable inside a tall box.
    for(int z=0;z<shape->height;++z)for(int x=0;x<shape->width;++x) {
        const auto* bottom=shape->spans[x+z*shape->width];if(!bottom)continue;
        auto* top=bottom;while(top->next)top=top->next;
        require(rcAddSpan(&context,target,x+beginX,z+beginZ,static_cast<unsigned short>(bottom->smin),
            static_cast<unsigned short>(top->smax),static_cast<unsigned char>(top->area),config.walkableClimb),"solid box span");
    }
}
}
struct NavigationMesh::Impl {
    Owner<dtNavMesh,dtFreeNavMesh> mesh{nullptr,dtFreeNavMesh};
    Owner<dtNavMeshQuery,dtFreeNavMeshQuery> query{nullptr,dtFreeNavMeshQuery};
    std::uint64_t sourceRevision{std::numeric_limits<std::uint64_t>::max()},revision{};
    float radius{},height{};
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
        // part of this Box-only slice and are deliberately excluded above.
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
    void build(const std::vector<PhysicsBox>& boxes,const std::vector<PhysicsStaticMesh>& meshes) {
        query.reset();mesh.reset();componentByPolygon.clear();componentBounds.clear();bounds={};diagnostic.clear();
        if(boxes.empty() && meshes.empty()){diagnostic="Navigation needs walkable static collision geometry.\n";return;}
        std::vector<float> vertices;std::vector<int> triangles;
        vertices.reserve(boxes.size()*24);triangles.reserve(boxes.size()*36);
        for(const auto& box:boxes)appendBox(box,vertices,triangles);
        for(const auto& source:meshes)appendMesh(source,vertices,triangles);
        if(vertices.empty()){diagnostic="Navigation needs walkable static collision geometry.\n";return;}
        const auto vertexCount=static_cast<int>(vertices.size()/3);
        rcConfig config{};config.cs=CellSize;config.ch=CellHeight;
        config.walkableSlopeAngle=MaximumSlope;
        config.walkableHeight=std::max(3,static_cast<int>(std::ceil((static_cast<double>(height)+Skin)/config.ch)));
        config.walkableClimb=static_cast<int>(std::floor(MaximumStep/config.ch));
        config.walkableRadius=static_cast<int>(std::ceil((static_cast<double>(radius)+Skin)/config.cs));
        config.maxEdgeLen=static_cast<int>(12/config.cs);config.maxSimplificationError=.5f;
        config.minRegionArea=0;config.mergeRegionArea=16;config.maxVertsPerPoly=6;
        config.detailSampleDist=config.cs*6;config.detailSampleMaxError=config.ch;
        rcCalcBounds(vertices.data(),vertexCount,config.bmin,config.bmax);
        config.bmin[1]-=config.ch;config.bmax[1]+=height+config.ch;
        const double width=std::ceil((static_cast<double>(config.bmax[0])-config.bmin[0])/config.cs);
        const double depth=std::ceil((static_cast<double>(config.bmax[2])-config.bmin[2])/config.cs);
        require(width>0 && depth>0 && width<=65535 && depth<=65535 && width*depth<=MaximumCells,
                "static geometry exceeds the 4-million-cell navigation limit");
        require((static_cast<double>(config.bmax[1])-config.bmin[1])/config.ch<RC_SPAN_MAX_HEIGHT,
                "static geometry exceeds the navigation height range");
        rcCalcGridSize(config.bmin,config.bmax,config.cs,&config.width,&config.height);
        require(config.width>0 && config.height>0,"collider bounds are smaller than one navigation cell");
        rcContext context(false);
        Owner<rcHeightfield,rcFreeHeightField> solid(rcAllocHeightfield(),rcFreeHeightField);
        require(solid!=nullptr,"heightfield allocation");
        require(rcCreateHeightfield(&context,*solid,config.width,config.height,config.bmin,config.bmax,config.cs,config.ch),"heightfield");
        auto remainingCells=MaximumRasterCells;
        for(const auto& box:boxes)rasterizeSolidBox(context,config,box,*solid,remainingCells);
        if(!meshes.empty()) {
            std::vector<float> meshVertices;std::vector<int> meshTriangles;
            for(const auto& source:meshes)appendMesh(source,meshVertices,meshTriangles);
            const auto count=static_cast<int>(meshTriangles.size()/3),meshVertexCount=static_cast<int>(meshVertices.size()/3);
            std::vector<unsigned char> areas(static_cast<std::size_t>(count));
            rcMarkWalkableTriangles(&context,config.walkableSlopeAngle,meshVertices.data(),meshVertexCount,meshTriangles.data(),count,areas.data());
            require(rcRasterizeTriangles(&context,meshVertices.data(),meshVertexCount,meshTriangles.data(),areas.data(),count,*solid,config.walkableClimb),"mesh collision rasterization");
        }
        rcFilterLowHangingWalkableObstacles(&context,config.walkableClimb,*solid);
        rcFilterLedgeSpans(&context,config.walkableHeight,config.walkableClimb,*solid);
        rcFilterWalkableLowHeightSpans(&context,config.walkableHeight,*solid);
        Owner<rcCompactHeightfield,rcFreeCompactHeightfield> compact(rcAllocCompactHeightfield(),rcFreeCompactHeightfield);
        require(compact!=nullptr,"compact heightfield allocation");
        require(rcBuildCompactHeightfield(&context,config.walkableHeight,config.walkableClimb,*solid,*compact),"compact heightfield");
        solid.reset();
        require(rcErodeWalkableArea(&context,config.walkableRadius,*compact),"capsule clearance");
        require(rcBuildDistanceField(&context,*compact),"distance field");
        require(rcBuildRegions(&context,*compact,0,config.minRegionArea,config.mergeRegionArea),"regions");
        Owner<rcContourSet,rcFreeContourSet> contours(rcAllocContourSet(),rcFreeContourSet);
        require(contours!=nullptr,"contour allocation");
        require(rcBuildContours(&context,*compact,config.maxSimplificationError,config.maxEdgeLen,*contours),"contours");
        Owner<rcPolyMesh,rcFreePolyMesh> polygons(rcAllocPolyMesh(),rcFreePolyMesh);
        require(polygons!=nullptr,"polygon allocation");
        require(rcBuildPolyMesh(&context,*contours,config.maxVertsPerPoly,*polygons),"polygons");
        if(polygons->npolys==0){diagnostic="No static surface has clearance for this Character.\n";return;}
        Owner<rcPolyMeshDetail,rcFreePolyMeshDetail> detail(rcAllocPolyMeshDetail(),rcFreePolyMeshDetail);
        require(detail!=nullptr,"detail allocation");
        require(rcBuildPolyMeshDetail(&context,*polygons,*compact,config.detailSampleDist,config.detailSampleMaxError,*detail),"surface detail");
        for(int polygon=0;polygon<polygons->npolys;++polygon){polygons->areas[polygon]=0;polygons->flags[polygon]=1;}
        dtNavMeshCreateParams parameters{};
        parameters.verts=polygons->verts;parameters.vertCount=polygons->nverts;
        parameters.polys=polygons->polys;parameters.polyAreas=polygons->areas;parameters.polyFlags=polygons->flags;
        parameters.polyCount=polygons->npolys;parameters.nvp=polygons->nvp;
        parameters.detailMeshes=detail->meshes;parameters.detailVerts=detail->verts;parameters.detailVertsCount=detail->nverts;
        parameters.detailTris=detail->tris;parameters.detailTriCount=detail->ntris;
        parameters.walkableHeight=height;parameters.walkableRadius=radius;parameters.walkableClimb=MaximumStep;
        std::copy_n(polygons->bmin,3,parameters.bmin);std::copy_n(polygons->bmax,3,parameters.bmax);
        parameters.cs=config.cs;parameters.ch=config.ch;parameters.buildBvTree=true;
        unsigned char* data=nullptr;int bytes=0;
        require(dtCreateNavMeshData(&parameters,&data,&bytes),"Detour mesh data");
        std::unique_ptr<unsigned char,decltype(&dtFree)> ownedData(data,dtFree);
        mesh.reset(dtAllocNavMesh());require(mesh!=nullptr,"Detour mesh allocation");
        require(dtStatusSucceed(mesh->init(data,bytes,DT_TILE_FREE_DATA)),"Detour mesh initialization");
        ownedData.release();query.reset(dtAllocNavMeshQuery());require(query!=nullptr,"Detour query allocation");
        require(dtStatusSucceed(query->init(mesh.get(),8192)),"Detour query initialization");
        cacheConnectivity();
    }
};
NavigationMesh::NavigationMesh():impl_(std::make_unique<Impl>()){}
NavigationMesh::~NavigationMesh()=default;
bool NavigationMesh::reconcile(const PhysicsWorld& world,const SceneCharacter& character) {
    auto& state=*impl_;
    if(state.sourceRevision==world.geometryRevision() && state.radius==character.radius && state.height==character.height)return false;
    state.sourceRevision=world.geometryRevision();state.radius=character.radius;state.height=character.height;++state.revision;
    try {
        require(math::finite(state.height) && math::finite(state.radius) && state.radius>0 && state.height>=state.radius*2,
                "invalid Character dimensions");
        require((static_cast<double>(state.height)+Skin)/CellHeight<RC_SPAN_MAX_HEIGHT &&
                (static_cast<double>(state.radius)+Skin)/CellSize<65535,
                "Character dimensions exceed the navigation voxel range");
        state.build(world.staticBoxes(),world.staticMeshes());
    } catch(const std::exception& error){state.query.reset();state.mesh.reset();state.componentByPolygon.clear();state.componentBounds.clear();state.bounds={};state.diagnostic=error.what();}
    return true;
}
std::optional<NavigationPath> NavigationMesh::findPath(const Vec3& start,const Vec3& destination) const {
    const auto& state=*impl_;
    if(!state.query || !math::finite(start) || !math::finite(destination))return std::nullopt;
    dtQueryFilter filter;filter.setIncludeFlags(1);
    const Vec3 startExtents{state.radius+CellSize,MaximumStep+.2f,state.radius+CellSize};
    const Vec3 endExtents{CellSize,CellHeight*4,CellSize};
    if(!state.overlapsMesh(start,startExtents) || !state.overlapsMesh(destination,endExtents))return {};
    dtPolyRef startPolygon{},endPolygon{};Vec3 nearStart{},nearEnd{};
    if(dtStatusFailed(state.query->findNearestPoly(start.data(),startExtents.data(),&filter,&startPolygon,nearStart.data())) || !startPolygon ||
       dtStatusFailed(state.query->findNearestPoly(destination.data(),endExtents.data(),&filter,&endPolygon,nearEnd.data())) || !endPolygon)return std::nullopt;
    // Never turn a click on an unreachable ledge/gap into a faraway destination.
    if(std::hypot(nearEnd[0]-destination[0],nearEnd[2]-destination[2])>CellSize*.5f ||
       std::abs(nearEnd[1]-destination[1])>CellHeight*4)return std::nullopt;
    if(state.componentByPolygon.at(startPolygon)!=state.componentByPolygon.at(endPolygon))return {};
    return completePath(*state.query,startPolygon,endPolygon,start,nearStart,nearEnd,destination,filter);
}
std::optional<NavigationPath> NavigationMesh::findReachablePath(const Vec3& start,const Vec3& destination) const {
    if(const auto exact=findPath(start,destination))return exact;
    const auto& state=*impl_;
    if(!state.query || !math::finite(start) || !math::finite(destination))return {};
    dtQueryFilter filter;filter.setIncludeFlags(1);
    const Vec3 extents{state.radius+CellSize,MaximumStep+.2f,state.radius+CellSize};dtPolyRef startPolygon{};Vec3 nearStart{};
    if(!state.overlapsMesh(start,extents))return {};
    if(dtStatusFailed(state.query->findNearestPoly(start.data(),extents.data(),&filter,&startPolygon,nearStart.data())) || !startPolygon)return {};
    const auto destinationOnMesh=state.closestReachable(startPolygon,nearStart,destination,filter);if(!destinationOnMesh)return {};
    return completePath(*state.query,startPolygon,destinationOnMesh->first,start,nearStart,destinationOnMesh->second,destinationOnMesh->second,filter);
}
std::uint64_t NavigationMesh::revision() const{return impl_->revision;}
const std::string& NavigationMesh::status() const{return impl_->diagnostic;}
}
