#include "NavigationTypes.h"
#include "resources/CacheIO.h"
#include <fstream>
#include <cstring>
#include <sstream>
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
void markWalkable(rcContext& context,const rcConfig& config,const std::vector<float>& vertices,const std::vector<int>& triangles,std::vector<unsigned char>& areas) {
    if(config.walkableSlopeAngle>0) {
        rcMarkWalkableTriangles(&context,config.walkableSlopeAngle,vertices.data(),static_cast<int>(vertices.size()/3),triangles.data(),static_cast<int>(triangles.size()/3),areas.data());return;
    }
    // Recast uses normal.y > cos(angle), which excludes even perfectly flat
    // triangles at angle zero. Accept upward flats with numerical tolerance.
    for(std::size_t triangle=0;triangle<areas.size();++triangle) {
        const auto vertex=[&](unsigned corner){const auto first=triangles[triangle*3+corner]*3;return Vec3{vertices[first],vertices[first+1],vertices[first+2]};};
        const auto a=vertex(0),normal=math::cross(vertex(1)-a,vertex(2)-a);
        areas[triangle]=normal[1]>0 && std::hypot(normal[0],normal[2])<=double(normal[1])*1e-6?RC_WALKABLE_AREA:RC_NULL_AREA;
    }
}
void checkpoint(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Navigation preparation canceled.");}
void rasterizeSolidShape(rcContext& context,const rcConfig& config,const std::vector<float>& vertices,const std::vector<int>& triangles,
    rcHeightfield& target,std::uint64_t& remainingCells) {
    const auto vertexCount=static_cast<int>(vertices.size()/3),triangleCount=static_cast<int>(triangles.size()/3);
    float minimum[3],maximum[3];rcCalcBounds(vertices.data(),vertexCount,minimum,maximum);
    const int beginX=std::clamp(static_cast<int>(std::floor((minimum[0]-config.bmin[0])/config.cs)),0,config.width-1);
    const int beginZ=std::clamp(static_cast<int>(std::floor((minimum[2]-config.bmin[2])/config.cs)),0,config.height-1);
    const int endX=std::clamp(static_cast<int>(std::ceil((maximum[0]-config.bmin[0])/config.cs)),beginX+1,config.width);
    const int endZ=std::clamp(static_cast<int>(std::ceil((maximum[2]-config.bmin[2])/config.cs)),beginZ+1,config.height);
    const auto cells=static_cast<std::uint64_t>(endX-beginX)*(endZ-beginZ);
    require(cells<=remainingCells,"overlapping colliders exceed the 16-million-cell rasterization limit");remainingCells-=cells;
    minimum[0]=config.bmin[0]+beginX*config.cs;minimum[1]=config.bmin[1];minimum[2]=config.bmin[2]+beginZ*config.cs;
    maximum[0]=config.bmin[0]+endX*config.cs;maximum[1]=config.bmax[1];maximum[2]=config.bmin[2]+endZ*config.cs;
    Owner<rcHeightfield,rcFreeHeightField> shape(rcAllocHeightfield(),rcFreeHeightField);
    require(shape!=nullptr,"convex heightfield allocation");
    require(rcCreateHeightfield(&context,*shape,endX-beginX,endZ-beginZ,minimum,maximum,config.cs,config.ch),"convex heightfield");
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangleCount));
    markWalkable(context,config,vertices,triangles,areas);
    require(rcRasterizeTriangles(&context,vertices.data(),vertexCount,triangles.data(),areas.data(),triangleCount,*shape,config.walkableClimb),"convex rasterization");
    // Recast rasterizes surfaces. A Collider is a solid convex volume, so fill
    // from each column's lowest to highest surface before merging shapes.
    // This prevents a coincident floor from becoming walkable inside a collider.
    for(int z=0;z<shape->height;++z)for(int x=0;x<shape->width;++x) {
        const auto* bottom=shape->spans[x+z*shape->width];if(!bottom)continue;
        auto* top=bottom;while(top->next)top=top->next;
        require(rcAddSpan(&context,target,x+beginX,z+beginZ,static_cast<unsigned short>(bottom->smin),
            static_cast<unsigned short>(top->smax),static_cast<unsigned char>(top->area),config.walkableClimb),"solid convex span");
    }
}
}

Aabb navigationVolumeBounds(const NavigationVolumeDescriptor& volume) {
    Aabb bounds;for(int corner=0;corner<8;++corner)math::include(bounds,math::transformPoint(volume.world,{volume.size[0]*(corner&1?.5f:-.5f),volume.size[1]*(corner&2?.5f:-.5f),volume.size[2]*(corner&4?.5f:-.5f)}));return bounds;
}
bool navigationVolumeContains(const NavigationVolumeDescriptor& volume,const Vec3& point) {
    const auto inverse=math::inverse(volume.world);if(!inverse||!math::finite(point))return false;
    const auto local=math::transformPoint(*inverse,point);for(unsigned axis=0;axis<3;++axis)if(std::abs(local[axis])>volume.size[axis]*.5f+1e-5f)return false;return true;
}
std::uint64_t navigationTileFingerprint(const NavigationTileInput& input) {
    using namespace streaming::detail;auto value=HashSeed;
    constexpr std::uint32_t Generator=3;hash(value,Generator);hash(value,std::uint32_t(DT_NAVMESH_VERSION));hash(value,input.tile.x);hash(value,input.tile.z);
    hash(value,input.volume.world);hash(value,input.volume.size);hash(value,input.volume.bake.cellSize);hash(value,input.volume.bake.cellHeight);hash(value,input.volume.bake.tileSize);hash(value,input.volume.bake.maxSimplificationError);
    hash(value,input.agent.height);hash(value,input.agent.radius);hash(value,input.agent.maxSlope);hash(value,input.agent.maxStep);hash(value,input.minimumY);hash(value,input.maximumY);
    std::vector<const PhysicsBox*> boxes;for(const auto& source:input.boxes)boxes.push_back(&source);
    std::sort(boxes.begin(),boxes.end(),[](const auto* a,const auto* b){return a->entity<b->entity;});
    for(const auto* box:boxes){hash(value,box->entity);hash(value,box->world);hash(value,box->size);}
    std::vector<const PhysicsStaticMesh*> meshes;for(const auto& source:input.meshes)meshes.push_back(&source);
    std::sort(meshes.begin(),meshes.end(),[](const auto* a,const auto* b){return a->entity<b->entity;});
    for(const auto* source:meshes){hash(value,source->entity);hash(value,source->world);hash(value,source->solid);if(source->mesh){
        hash(value,source->mesh->fingerprint);
        // Primitive sampling is immutable but predates content fingerprints.
        if(!source->mesh->fingerprint){for(const auto& point:source->mesh->positions)hash(value,point);for(auto index:source->mesh->indices)hash(value,index);}
    }}
    return value;
}
namespace {
std::shared_ptr<const NavigationTileData> generate(const NavigationTileInput& input,std::stop_token stop) {
        auto result=std::make_shared<NavigationTileData>();result->tile=input.tile;result->fingerprint=input.fingerprint;
        const auto& boxes=input.boxes;const auto& meshes=input.meshes;const auto& agent=input.agent;
        const float height=agent.height,radius=agent.radius;
        checkpoint(stop);
        require(validNavigationBakeSettings(input.volume.bake)&&validAgentTraversal(agent),"invalid bake or profile settings");
        require((double(height)+Skin)/input.volume.bake.cellHeight<RC_SPAN_MAX_HEIGHT &&
            (double(radius)+Skin)/input.volume.bake.cellSize<1024 &&
            double(agent.maxStep)/input.volume.bake.cellHeight<RC_SPAN_MAX_HEIGHT,"agent exceeds the navigation voxel range");
        if(boxes.empty() && meshes.empty())return result;
        rcConfig config{};config.cs=input.volume.bake.cellSize;config.ch=input.volume.bake.cellHeight;
        config.walkableSlopeAngle=agent.maxSlope;
        config.walkableHeight=std::max(3,static_cast<int>(std::ceil((static_cast<double>(height)+Skin)/config.ch)));
        config.walkableClimb=static_cast<int>(std::floor((static_cast<double>(agent.maxStep)+1e-7)/config.ch));
        config.walkableRadius=static_cast<int>(std::ceil((static_cast<double>(radius)+Skin)/config.cs));
        config.maxEdgeLen=static_cast<int>(12/config.cs);config.maxSimplificationError=input.volume.bake.maxSimplificationError;
        config.minRegionArea=0;config.mergeRegionArea=16;config.maxVertsPerPoly=6;
        config.detailSampleDist=config.cs*6;config.detailSampleMaxError=config.ch;
        config.tileSize=static_cast<int>(input.volume.bake.tileSize);config.borderSize=config.walkableRadius+3;
        config.width=config.height=config.tileSize+2*config.borderSize;
        require(config.width>0 && std::uint64_t(config.width)*config.height<=MaximumCells,"tile plus capsule halo exceeds its cell budget");
        const float side=config.tileSize*config.cs;
        config.bmin[0]=input.tile.x*side-config.borderSize*config.cs;config.bmin[2]=input.tile.z*side-config.borderSize*config.cs;
        config.bmax[0]=(input.tile.x+1)*side+config.borderSize*config.cs;config.bmax[2]=(input.tile.z+1)*side+config.borderSize*config.cs;
        config.bmin[1]=input.minimumY;config.bmax[1]=input.maximumY;
        require(std::isfinite(config.bmin[1]) && std::isfinite(config.bmax[1]) && config.bmax[1]>config.bmin[1] &&
            (double(config.bmax[1])-config.bmin[1])/config.ch<RC_SPAN_MAX_HEIGHT,"volume exceeds navigation vertical range");
        rcContext context(false);
        Owner<rcHeightfield,rcFreeHeightField> solid(rcAllocHeightfield(),rcFreeHeightField);
        require(solid!=nullptr,"heightfield allocation");
        require(rcCreateHeightfield(&context,*solid,config.width,config.height,config.bmin,config.bmax,config.cs,config.ch),"heightfield");
        auto remainingCells=MaximumRasterCells;
        for(const auto& box:boxes) {
            checkpoint(stop);
            std::vector<float> shapeVertices;std::vector<int> shapeTriangles;appendBox(box,shapeVertices,shapeTriangles);
            rasterizeSolidShape(context,config,shapeVertices,shapeTriangles,*solid,remainingCells);
        }
        for(const auto& source:meshes)if(source.solid && source.mesh) {
            checkpoint(stop);
            std::vector<float> shapeVertices;std::vector<int> shapeTriangles;appendMesh(source,shapeVertices,shapeTriangles);
            rasterizeSolidShape(context,config,shapeVertices,shapeTriangles,*solid,remainingCells);
        }
        if(!meshes.empty()) {
            std::vector<float> meshVertices;std::vector<int> meshTriangles;
            for(const auto& source:meshes)if(!source.solid)appendMesh(source,meshVertices,meshTriangles);
            const auto count=static_cast<int>(meshTriangles.size()/3),meshVertexCount=static_cast<int>(meshVertices.size()/3);
            if(count) {
                std::vector<unsigned char> areas(static_cast<std::size_t>(count));
                markWalkable(context,config,meshVertices,meshTriangles,areas);
                require(rcRasterizeTriangles(&context,meshVertices.data(),meshVertexCount,meshTriangles.data(),areas.data(),count,*solid,config.walkableClimb),"mesh collision rasterization");
            }
        }
        checkpoint(stop);
        rcFilterLowHangingWalkableObstacles(&context,config.walkableClimb,*solid);
        rcFilterLedgeSpans(&context,config.walkableHeight,config.walkableClimb,*solid);
        rcFilterWalkableLowHeightSpans(&context,config.walkableHeight,*solid);
        Owner<rcCompactHeightfield,rcFreeCompactHeightfield> compact(rcAllocCompactHeightfield(),rcFreeCompactHeightfield);
        require(compact!=nullptr,"compact heightfield allocation");
        require(rcBuildCompactHeightfield(&context,config.walkableHeight,config.walkableClimb,*solid,*compact),"compact heightfield");
        solid.reset();checkpoint(stop);
        const auto inverse=math::inverse(input.volume.world);require(inverse.has_value(),"volume transform is singular");
        // Test all XYZ coordinates; the AABB only chose candidate tiles. Recast's
        // convex-area helper is an XZ extrusion and cannot mask tilted boxes.
        const float guard=config.cs*(.75f+config.maxSimplificationError);
        for(int z=0;z<compact->height;++z)for(int x=0;x<compact->width;++x) {
            const auto& cell=compact->cells[x+z*compact->width];
            for(unsigned index=cell.index;index<cell.index+cell.count;++index) {
                const Vec3 floor{compact->bmin[0]+(x+.5f)*config.cs,compact->bmin[1]+compact->spans[index].y*config.ch,compact->bmin[2]+(z+.5f)*config.cs};
                bool inside=true;
                for(int corner=0;corner<8 && inside;++corner) {
                    const Vec3 point{floor[0]+(corner&1?guard:-guard),floor[1]+(corner&2?config.ch:-config.ch),floor[2]+(corner&4?guard:-guard)};
                    const auto local=math::transformPoint(*inverse,point);
                    for(unsigned axis=0;axis<3;++axis)inside=inside && std::abs(local[axis])<=input.volume.size[axis]*.5f;
                }
                if(!inside)compact->areas[index]=RC_NULL_AREA;
            }
        }
        require(rcErodeWalkableArea(&context,config.walkableRadius,*compact),"capsule clearance");
        require(rcBuildDistanceField(&context,*compact),"distance field");
        require(rcBuildRegions(&context,*compact,config.borderSize,config.minRegionArea,config.mergeRegionArea),"regions");
        Owner<rcContourSet,rcFreeContourSet> contours(rcAllocContourSet(),rcFreeContourSet);
        require(contours!=nullptr,"contour allocation");
        require(rcBuildContours(&context,*compact,config.maxSimplificationError,config.maxEdgeLen,*contours),"contours");
        Owner<rcPolyMesh,rcFreePolyMesh> polygons(rcAllocPolyMesh(),rcFreePolyMesh);
        require(polygons!=nullptr,"polygon allocation");
        require(rcBuildPolyMesh(&context,*contours,config.maxVertsPerPoly,*polygons),"polygons");
        if(polygons->npolys==0)return result;
        Owner<rcPolyMeshDetail,rcFreePolyMeshDetail> detail(rcAllocPolyMeshDetail(),rcFreePolyMeshDetail);
        require(detail!=nullptr,"detail allocation");
        require(rcBuildPolyMeshDetail(&context,*polygons,*compact,config.detailSampleDist,config.detailSampleMaxError,*detail),"surface detail");
        checkpoint(stop);
        for(int polygon=0;polygon<polygons->npolys;++polygon) {
            polygons->areas[polygon]=0;bool inside=true;
            const auto base=detail->meshes[polygon*4],count=detail->meshes[polygon*4+1];
            for(unsigned vertex=0;vertex<count;++vertex) {
                const auto* point=detail->verts+(base+vertex)*3;
                inside=inside && navigationVolumeContains(input.volume,{point[0],point[1],point[2]});
            }
            polygons->flags[polygon]=inside?1:0;
            if(!inside)continue;
            const auto first=static_cast<std::uint32_t>(result->vertices.size());
            for(unsigned vertex=0;vertex<count;++vertex) {const auto* point=detail->verts+(base+vertex)*3;result->vertices.push_back({point[0],point[1],point[2]});}
            const auto triangleBase=detail->meshes[polygon*4+2],triangleCount=detail->meshes[polygon*4+3];
            for(unsigned triangle=0;triangle<triangleCount;++triangle) {
                const auto* indices=detail->tris+(triangleBase+triangle)*4;
                result->triangles.insert(result->triangles.end(),{first+indices[0],first+indices[1],first+indices[2]});
            }
        }
        dtNavMeshCreateParams parameters{};
        parameters.verts=polygons->verts;parameters.vertCount=polygons->nverts;
        parameters.polys=polygons->polys;parameters.polyAreas=polygons->areas;parameters.polyFlags=polygons->flags;
        parameters.polyCount=polygons->npolys;parameters.nvp=polygons->nvp;
        parameters.detailMeshes=detail->meshes;parameters.detailVerts=detail->verts;parameters.detailVertsCount=detail->nverts;
        parameters.detailTris=detail->tris;parameters.detailTriCount=detail->ntris;
        parameters.walkableHeight=height;parameters.walkableRadius=radius;parameters.walkableClimb=agent.maxStep;
        parameters.tileX=input.tile.x;parameters.tileY=input.tile.z;parameters.tileLayer=0;
        std::copy_n(polygons->bmin,3,parameters.bmin);std::copy_n(polygons->bmax,3,parameters.bmax);
        parameters.cs=config.cs;parameters.ch=config.ch;parameters.buildBvTree=true;
        unsigned char* data=nullptr;int bytes=0;
        require(dtCreateNavMeshData(&parameters,&data,&bytes),"Detour mesh data");
        std::unique_ptr<unsigned char,decltype(&dtFree)> ownedData(data,dtFree);
        result->data.assign(data,data+bytes);return result;
}
std::string serializeTile(const NavigationTileData& data) {
    using namespace streaming::detail;std::string payload;
    append(payload,data.tile.x);append(payload,data.tile.z);append(payload,data.fingerprint);
    append(payload,static_cast<std::uint32_t>(data.data.size()));append(payload,static_cast<std::uint32_t>(data.vertices.size()));append(payload,static_cast<std::uint32_t>(data.triangles.size()));
    if(!data.data.empty())appendBytes(payload,data.data.data(),data.data.size());if(!data.vertices.empty())appendBytes(payload,data.vertices.data(),data.vertices.size()*sizeof(Vec3));if(!data.triangles.empty())appendBytes(payload,data.triangles.data(),data.triangles.size()*sizeof(std::uint32_t));
    std::uint64_t checksum=HashSeed;hash(checksum,payload.data(),payload.size());
    std::string bytes="CRNAVT03";append(bytes,checksum);bytes+=payload;return bytes;
}
std::shared_ptr<const NavigationTileData> readTile(const std::filesystem::path& path,const NavigationTileInput& input) {
    using namespace streaming::detail;std::ifstream stream(path,std::ios::binary|std::ios::ate);if(!stream)return {};
    const auto size=stream.tellg();if(size<44 || size>64*1024*1024)return {};stream.seekg(0);
    std::string bytes(static_cast<std::size_t>(size),'\0');readBytes(stream,bytes.data(),bytes.size());stream.close();
    if(bytes.substr(0,8)!="CRNAVT03")return {};std::uint64_t checksum{};std::memcpy(&checksum,bytes.data()+8,8);
    auto computed=HashSeed;hash(computed,bytes.data()+16,bytes.size()-16);if(computed!=checksum)return {};
    std::istringstream data(bytes.substr(16),std::ios::binary);auto tile=std::make_shared<NavigationTileData>();
    tile->tile={read<int>(data),read<int>(data)};tile->fingerprint=read<std::uint64_t>(data);
    const auto count=read<std::uint32_t>(data),vertices=read<std::uint32_t>(data),indices=read<std::uint32_t>(data);
    if(tile->tile!=input.tile || tile->fingerprint!=input.fingerprint || count>32*1024*1024 || vertices>1'000'000 || indices>3'000'000 || indices%3 ||
        44ull+count+std::uint64_t(vertices)*sizeof(Vec3)+std::uint64_t(indices)*4!=bytes.size())return {};
    tile->data.resize(count);tile->vertices.resize(vertices);tile->triangles.resize(indices);
    readBytes(data,tile->data.data(),count);readBytes(data,tile->vertices.data(),vertices*sizeof(Vec3));readBytes(data,tile->triangles.data(),indices*4);
    for(const auto& point:tile->vertices)if(!navigationVolumeContains(input.volume,point))return {};
    for(auto index:tile->triangles)if(index>=tile->vertices.size())return {};
    if(count){if(count<sizeof(dtMeshHeader))return {};dtMeshHeader header{};std::memcpy(&header,tile->data.data(),sizeof header);if(header.magic!=DT_NAVMESH_MAGIC || header.version!=DT_NAVMESH_VERSION || header.x!=input.tile.x || header.y!=input.tile.z || header.polyCount<0 || header.polyCount>2048 || header.vertCount<0 || header.vertCount>65535)return {};}
    return tile;
}
}
std::shared_ptr<const NavigationTileData> buildNavigationTile(const NavigationTileInput& original,std::stop_token stop,const std::filesystem::path& cacheRoot) {
    checkpoint(stop);auto input=original;if(!input.fingerprint)input.fingerprint=navigationTileFingerprint(input);
    std::filesystem::path path;
    if(!cacheRoot.empty()) {path=cacheRoot/(streaming::detail::hex(input.fingerprint)+".navtile");try{if(auto cached=readTile(path,input))return cached;}catch(const std::exception&){} }
    auto result=generate(input,stop);checkpoint(stop);
    if(!path.empty()) {std::filesystem::create_directories(cacheRoot);streaming::detail::writeCacheFileAtomically(path,serializeTile(*result));}
    return result;
}
}
