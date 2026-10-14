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

namespace crimson {
namespace {
template<class T,void (*Free)(T*)> using Owner=std::unique_ptr<T,decltype(Free)>;
constexpr std::uint64_t MaximumCells=4'000'000;
constexpr std::uint64_t MaximumRasterCells=16'000'000;
constexpr int MaximumCorners=512;
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
void require(bool success,const char* stage) {
    if(!success)throw std::runtime_error(std::string("Navigation build failed: ")+stage+".\n");
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
    void build(const std::vector<PhysicsBox>& boxes) {
        query.reset();mesh.reset();diagnostic.clear();
        if(boxes.empty()){diagnostic="Navigation needs a walkable Box Collider.\n";return;}
        std::vector<float> vertices;std::vector<int> triangles;
        vertices.reserve(boxes.size()*24);triangles.reserve(boxes.size()*36);
        for(const auto& box:boxes)appendBox(box,vertices,triangles);
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
        state.build(world.staticBoxes());
    } catch(const std::exception& error){state.query.reset();state.mesh.reset();state.diagnostic=error.what();}
    return true;
}
std::optional<NavigationPath> NavigationMesh::findPath(const Vec3& start,const Vec3& destination) const {
    const auto& state=*impl_;
    if(!state.query || !math::finite(start) || !math::finite(destination))return std::nullopt;
    dtQueryFilter filter;filter.setIncludeFlags(1);
    const Vec3 startExtents{state.radius+CellSize,MaximumStep+.2f,state.radius+CellSize};
    const Vec3 endExtents{CellSize,CellHeight*4,CellSize};
    dtPolyRef startPolygon{},endPolygon{};Vec3 nearStart{},nearEnd{};
    if(dtStatusFailed(state.query->findNearestPoly(start.data(),startExtents.data(),&filter,&startPolygon,nearStart.data())) || !startPolygon ||
       dtStatusFailed(state.query->findNearestPoly(destination.data(),endExtents.data(),&filter,&endPolygon,nearEnd.data())) || !endPolygon)return std::nullopt;
    // Never turn a click on an unreachable ledge/gap into a faraway destination.
    if(std::hypot(nearEnd[0]-destination[0],nearEnd[2]-destination[2])>CellSize*.5f ||
       std::abs(nearEnd[1]-destination[1])>CellHeight*4)return std::nullopt;
    std::array<dtPolyRef,MaximumCorners> corridor{};int polygonCount=0;
    const auto result=state.query->findPath(startPolygon,endPolygon,nearStart.data(),nearEnd.data(),&filter,
                                          corridor.data(),&polygonCount,static_cast<int>(corridor.size()));
    if(dtStatusFailed(result) || dtStatusDetail(result,DT_PARTIAL_RESULT|DT_BUFFER_TOO_SMALL|DT_OUT_OF_NODES) ||
       polygonCount==0 || corridor[polygonCount-1]!=endPolygon)return std::nullopt;
    std::array<float,MaximumCorners*3> straight{};std::array<unsigned char,MaximumCorners> flags{};int cornerCount=0;
    const auto straightResult=state.query->findStraightPath(nearStart.data(),nearEnd.data(),corridor.data(),polygonCount,
        straight.data(),flags.data(),nullptr,&cornerCount,MaximumCorners);
    if(dtStatusFailed(straightResult) || dtStatusDetail(straightResult,DT_BUFFER_TOO_SMALL) || cornerCount==0 ||
       !(flags[cornerCount-1]&DT_STRAIGHTPATH_END))return std::nullopt;
    NavigationPath path;path.corners.reserve(cornerCount);
    if(std::hypot(nearStart[0]-start[0],nearStart[2]-start[2])>.01f)path.corners.push_back(nearStart);
    for(int corner=1;corner<cornerCount;++corner)path.corners.push_back({straight[corner*3],straight[corner*3+1],straight[corner*3+2]});
    if(path.corners.empty())path.corners.push_back(destination);else path.corners.back()=destination;
    return path;
}
std::uint64_t NavigationMesh::revision() const{return impl_->revision;}
const std::string& NavigationMesh::status() const{return impl_->diagnostic;}
}
