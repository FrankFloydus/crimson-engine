#pragma once
#include "decals/DecalPaintSurface.h"
#include "decals/DecalModelSurface.h"
#include "decals/DecalInternal.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace crimson::decals::test {
namespace boundary_lineage_fixture {
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Action> inline void rejects(Action&& action,const char* message){bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,message);}
inline ModelPrimitive quad(const std::array<Vec3,4>& points,bool varied=false){
    ModelPrimitive result;result.material=0;
    for(size_t i=0;i<points.size();++i){ModelVertex vertex;vertex.position=points[i];vertex.normal=varied?math::normalized(Vec3{float(i)*.02f,float(i&1)*.03f,1}):Vec3{0,0,1};result.vertices.push_back(vertex);}
    result.indices={0,1,2,0,2,3};result.sourceFaces.push_back({{0,1,2,3},{0,1},4});return result;
}
inline ModelPrimitive slanted(float warp=0){return quad({Vec3{.1234567f,.2345678f,0},Vec3{1.9876543f,.4567891f,0},Vec3{2.112344f,2.343245f,warp},Vec3{.2481464f,2.1210237f,0}},true);}
struct Prepared {ModelPrimitive original,render;std::shared_ptr<const MeshPaintGeometry> geometry;SurfaceReceiver receiver;};
inline Prepared prepare(ModelPrimitive original,uint64_t entity,const Mat4& world={},uint32_t level=3){
    Prepared result;result.original=std::move(original);result.geometry=buildMeshPaintGeometry(result.original,level);result.render=meshPaintRenderPrimitive(result.original,*result.geometry);
    ModelAsset model;model.primitives.push_back(result.render);model.draws.push_back({0});
    const auto surface=buildModelSurface(model,17,SourceNormals::UseMeshNormals);require(surface.draws.size()==1,"Boundary fixture lost its fine receiver draw.");
    result.receiver=surface.draws[0].surface;result.receiver.id.entity=entity;result.receiver.localToWorld=world;result.receiver.boundary=meshPaintBoundaryLineage(result.original,*result.geometry);
    return result;
}
inline DecalBuildRequest request(const SurfaceReceiver& receiver,Vec3 nearLocal,float extent=.8f){
    double closest=INFINITY;const SurfaceTriangle* seed=nullptr;
    for(const auto& triangle:receiver.triangles){const auto center=(triangle.positions[0]+triangle.positions[1]+triangle.positions[2])*(1.f/3.f);const auto distance=math::length(center-nearLocal);if(distance<closest){closest=distance;seed=&triangle;}}
    require(seed,"Boundary fixture has no source seed.");DecalBuildRequest result;result.seed={receiver.id,seed->triangle,Vec3{1.f/3.f,1.f/3.f,1.f/3.f}};
    const auto center=(seed->positions[0]+seed->positions[1]+seed->positions[2])*(1.f/3.f);
    result.frame=receiver.localToWorld*math::translation(center)*math::scaling({extent,extent,1});return result;
}
inline const SurfaceBoundaryTriangle* record(const SurfaceReceiver& receiver,uint32_t triangle){
    if(!receiver.boundary)return nullptr;const auto& records=receiver.boundary->triangles;
    const auto found=std::lower_bound(records.begin(),records.end(),triangle,[](const auto& value,uint32_t id){return value.triangle<id;});return found!=records.end()&&found->triangle==triangle?&*found:nullptr;
}
inline void assertActualPatch(const DecalSurfaceSnapshot& snapshot,const DecalPatch& patch){
    require(patch.usable()&&!patch.chunks.empty(),"Boundary ancestry lost its valid fine surface patch.");
    bool variedNormal=false;
    for(const auto& chunk:patch.chunks){const auto receiver=std::find_if(snapshot.receivers.begin(),snapshot.receivers.end(),[&](const auto& value){return value.id==chunk.receiver;});require(receiver!=snapshot.receivers.end(),"Boundary ancestry changed the receiver identity.");
        for(const auto& value:chunk.vertices){const auto source=std::find_if(receiver->triangles.begin(),receiver->triangles.end(),[&](const auto& triangle){return triangle.triangle==value.sourceTriangle;});require(source!=receiver->triangles.end(),"Boundary ancestry published a working/proxy triangle ID.");
            detail::D3 local{},normal{};for(size_t i=0;i<3;++i){local=local+detail::d3(source->positions[i])*value.barycentrics[i];normal=normal+detail::normal(receiver->localToWorld,detail::d3(source->normals[i]))*value.barycentrics[i];}
            const auto expected=detail::point(receiver->localToWorld,local),expectedNormal=detail::unit(normal);
            require(detail::length(expected-detail::d3(value.position))<3e-5,"Boundary ancestry moved output off its actual fine source barycentrics.");
            require(detail::length(expectedNormal-detail::d3(value.baseNormal))<2e-5,"Boundary ancestry changed the actual fine shading normal field.");
            require(math::finite(value.uv)&&value.uv[0]>=0&&value.uv[0]<=1&&value.uv[1]>=0&&value.uv[1]<=1,"Boundary ancestry published invalid image coordinates.");
            variedNormal|=std::abs(source->normals[0][0])+std::abs(source->normals[1][0])+std::abs(source->normals[2][0])>0;
        }
    }
    require(variedNormal,"Boundary actual-source proof lost its varying-normal control.");
}
inline void workingVersusActual(){
    for(float warp:{0.f,3.29e-6f})for(bool affine:{false,true}){
        Mat4 world;if(affine){world=math::translation({3,-2,7})*math::scaling({-1.5f,.75f,2});world.at(0,1)=.2f;}
        const auto prepared=prepare(slanted(warp),91,world);const auto& receiver=prepared.receiver;
        require(receiver.boundary&&!receiver.boundary->triangles.empty(),"Slanted fine perimeter lost its original boundary ancestry.");
        const auto unchanged=prepared.render;const auto requestValue=request(receiver,{1.06f,.41f,0});DecalSurfaceSnapshot snapshot{{receiver}};DecalBuildStatistics statistics;
        const auto analysis=detail::buildTopology(snapshot,requestValue,statistics,{});size_t corrected=0,interior=0;
        for(const auto& source:analysis.sources){const auto& original=snapshot.receivers.at(source.receiver).triangles.at(source.triangle);const auto lineage=record(receiver,original.triangle);
            for(size_t corner=0;corner<3;++corner){const auto actual=detail::point(world,detail::d3(original.positions[corner]));
                require(detail::length(detail::actualPositions(source)[corner]-actual)<1e-13,"Working topology replaced authoritative actual fine positions.");
                detail::D3 expected=actual;
                if(lineage&&(lineage->corners&(1u<<corner))){const auto& point=lineage->points[corner];const auto first=detail::d3(point.first),last=detail::d3(point.last);expected=detail::point(world,first+(last-first)*point.parameter);corrected+=detail::length(expected-actual)>1e-12;}
                else {++interior;require(detail::length(source.p[corner]-actual)<1e-13,"Original-boundary ancestry altered an interior warp point.");}
                require(detail::length(source.p[corner]-expected)<1e-13,"Working boundary does not evaluate the original edge before its full affine transform.");
            }
        }
        require(corrected>0&&interior>0,"Boundary proof no longer contains both float-rounded perimeter and unchanged interior controls.");
        assertActualPatch(snapshot,buildDecalPatch(snapshot,requestValue));
        require(prepared.render.indices==unchanged.indices&&prepared.render.vertices.size()==unchanged.vertices.size()&&std::memcmp(prepared.render.vertices.data(),unchanged.vertices.data(),unchanged.vertices.size()*sizeof(ModelVertex))==0,
            "Boundary preparation changed rendered paint geometry or normals.");
        auto absent=snapshot;absent.receivers[0].boundary.reset();const auto without=detail::buildTopology(absent,requestValue,statistics,{});
        for(const auto& source:without.sources){require(!source.boundaryAdjusted,"Missing boundary ancestry acquired a fabricated construction.");for(size_t c=0;c<3;++c)require(detail::length(source.p[c]-detail::actualPositions(source)[c])==0,"Missing boundary ancestry changed working geometry.");}
    }
}
inline void invalidAndCancelled(){
    const auto prepared=prepare(slanted(),91);auto changed=prepared.original;changed.vertices[0].position[0]+=.01f;
    rejects([&]{meshPaintBoundaryLineage(changed,*prepared.geometry);},"Boundary lineage accepted a different original source signature.");
    std::stop_source stop;stop.request_stop();rejects([&]{meshPaintBoundaryLineage(prepared.original,*prepared.geometry,stop.get_token());},"Boundary lineage ignored cancellation.");
    const auto* first=&prepared.receiver.triangles.front();for(const auto& triangle:prepared.receiver.triangles)if(record(prepared.receiver,triangle.triangle)){first=&triangle;break;}
    for(unsigned corruption=0;corruption<3;++corruption){auto receiver=prepared.receiver;auto bad=std::make_shared<SurfaceBoundaryLineage>(*receiver.boundary);auto& value=*std::find_if(bad->triangles.begin(),bad->triangles.end(),[&](const auto& at){return at.triangle==first->triangle;});
        if(corruption==0)value.corners=0xff;else{size_t corner=0;while(!(value.corners&(1u<<corner)))++corner;if(corruption==1)value.points[corner].parameter=2;else value.points[corner].first[0]+=1;}
        receiver.boundary=bad;bool failed=false;try{detail::boundaryPositions(receiver,*first);}catch(const detail::Failure& error){failed=true;require(error.status==DecalPatchStatus::InvalidInput,"Malformed boundary ancestry reported an unrelated status.");}require(failed,"Malformed boundary ancestry was used as topology authority.");
    }
}
inline void barriers(){
    const auto left=prepare(quad({Vec3{-1,0,0},Vec3{0,0,0},Vec3{0,1,0},Vec3{-1,1,0}}),1,{},2);
    const auto right=prepare(quad({Vec3{0,0,0},Vec3{1,0,0},Vec3{1,1,0},Vec3{0,1,0}}),2,{},2);
    const auto wall=prepare(quad({Vec3{0,0,0},Vec3{0,1,0},Vec3{0,1,1},Vec3{0,0,1}}),3,{},2);
    auto r=request(left.receiver,{-.1f,.5f,0});r.limits.seamTolerance=.001;
    auto separated=right.receiver;separated.localToWorld=math::translation({.003f,0,0});
    for(bool threeSheets:{false,true}){DecalSurfaceSnapshot snapshot{{left.receiver,threeSheets?right.receiver:separated}};if(threeSheets)snapshot.receivers.push_back(wall.receiver);const auto patch=buildDecalPatch(snapshot,r);
        require(patch.usable()&&patch.status==DecalPatchStatus::Incomplete,"Boundary ancestry hid a genuine gap or ambiguity barrier.");
        for(const auto& chunk:patch.chunks)require(chunk.receiver.entity==1,"Original-boundary ancestry bridged an unrelated gap or true three-sheet junction.");
    }
}
}
inline void runBoundaryLineageProofs(){boundary_lineage_fixture::workingVersusActual();boundary_lineage_fixture::invalidAndCancelled();boundary_lineage_fixture::barriers();}
} // namespace crimson::decals::test
