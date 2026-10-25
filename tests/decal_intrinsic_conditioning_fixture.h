#pragma once
#include "decals/DecalInternal.h"
#include "decals/DecalResolve.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <sstream>

namespace crimson::decals::test {
inline void runIntrinsicConditioningProofs() {
    using namespace detail;
    const auto require=[](bool value,const std::string& message){if(!value)throw std::runtime_error(message);};
    // Curvature around the bowl's interior vertex rules out exact development.
    // A positive needle just inside its outer boundary lies outside the image,
    // but remains part of the intrinsic halo. Its long boundary edge cannot
    // flip away. Rounded lengths lose its strict triangle inequality even
    // though embedded area is positive; the image's source faces stay regular.
    constexpr size_t side=8;
    const auto position=[](float x,float y){return Vec3{x,y,.125*((double(x)-1)*(double(x)-1)+double(y)*y)};};
    SurfaceReceiver receiver;receiver.id={73,2,0};receiver.generation=9;
    size_t needleSource=SIZE_MAX,seedSource=SIZE_MAX;
    const auto add=[&](Vec3 a,Vec3 b,Vec3 c){SurfaceTriangle triangle;
        triangle.triangle=static_cast<uint32_t>(receiver.triangles.size());triangle.positions={a,b,c};receiver.triangles.push_back(triangle);};
    for(size_t y=0;y<side;++y)for(size_t x=0;x<side;++x){
        const float px=float(x)*.25f,py=-1+float(y)*.25f;
        const auto a=position(px,py),b=position(px+.25f,py);
        const auto c=position(px+.25f,py+.25f),d=position(px,py+.25f);
        if(x==4&&y==4)seedSource=receiver.triangles.size();
        add(a,b,c);
        if(x==0&&y==4){const Vec3 tip{1e-9f,.125f,.12890625f};add(a,c,tip);add(c,d,tip);needleSource=receiver.triangles.size();add(d,a,tip);}
        else add(a,c,d);
    }
    DecalSurfaceSnapshot snapshot{{receiver}};DecalBuildRequest request;
    request.seed={receiver.id,static_cast<uint32_t>(seedSource),{1,0,0}};request.limits.sampleSpacing=2;request.limits.seamTolerance=1e-12;
    const auto& seed=receiver.triangles[request.seed.triangle];Vec3 center{};
    for(size_t j=0;j<3;++j)center=center+seed.positions[j]*request.seed.barycentrics[j];
    const auto normal=math::normalized(math::cross(seed.positions[1]-seed.positions[0],seed.positions[2]-seed.positions[0]));
    request.frame=surfacePlacementFrame(center,normal);
    DecalBuildStatistics statistics;const auto analysis=buildTopology(snapshot,request,statistics,{});
    size_t unrepresentableMetrics=0,needleFaces=0;double minSlack=INFINITY;
    for(const auto& face:analysis.faces){const auto a=analysis.positions[face.v[0]],b=analysis.positions[face.v[1]],c=analysis.positions[face.v[2]];
        const double area=length(cross(b-a,c-a));std::array<double,3> edges{length(b-a),length(c-b),length(a-c)};std::sort(edges.begin(),edges.end());
        if(area>0&&edges[0]+edges[1]<=edges[2])++unrepresentableMetrics;
        minSlack=std::min(minSlack,edges[0]+edges[1]-edges[2]);needleFaces+=analysis.sources[face.source].triangle==needleSource;
    }
    if(!unrepresentableMetrics){std::ostringstream message;message.precision(17);message<<"No rounded-flat synthetic metric: faces="<<analysis.faces.size()<<", needle="<<needleFaces<<", minimum slack="<<minSlack;throw std::runtime_error(message.str());}
    const auto patch=buildDecalPatch(snapshot,request);
    require(patch.usable(),"A curved contact sheet failed intrinsic conditioning: "+patch.message);
    require(!patch.statistics.usedDevelopableUnfolding,"The curved conditioning fixture bypassed the intrinsic solver.");
    double uvArea=0;Vec2 lo{1,1},hi{};size_t outputVertices=0;bool curved=false;
    for(const auto& chunk:patch.chunks){
        require(chunk.receiver==receiver.id&&chunk.generation==receiver.generation,"Conditioned intrinsic output lost its receiver provenance.");
        for(const auto& vertex:chunk.vertices){
            require(vertex.sourceTriangle<receiver.triangles.size(),"Conditioned intrinsic output lost its original source triangle.");
            const auto& source=receiver.triangles[vertex.sourceTriangle];D3 restored{};double sum=0;
            for(size_t j=0;j<3;++j){const double weight=vertex.barycentrics[j];require(std::isfinite(weight)&&weight>=-1e-4&&weight<=1.0001,"Conditioned intrinsic output has invalid source barycentrics.");sum+=weight;restored=restored+d3(source.positions[j])*weight;}
            require(std::abs(sum-1)<1e-5&&length(restored-d3(vertex.position))<2e-6,"Intrinsic metric conditioning moved output away from its exact original surface.");
            for(size_t axis=0;axis<2;++axis){require(std::isfinite(vertex.uv[axis])&&vertex.uv[axis]>=0&&vertex.uv[axis]<=1,"Conditioned intrinsic UV escaped the image.");lo[axis]=std::min(lo[axis],vertex.uv[axis]);hi[axis]=std::max(hi[axis],vertex.uv[axis]);}
            curved|=std::abs(dot(d3(vertex.position)-d3(center),d3(normal)))>.01;++outputVertices;
        }
        for(size_t i=0;i<chunk.indices.size();i+=3){const auto a=chunk.vertices[chunk.indices[i]].uv,b=chunk.vertices[chunk.indices[i+1]].uv,c=chunk.vertices[chunk.indices[i+2]].uv;
            uvArea+=.5*std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-(double(b[1])-a[1])*(double(c[0])-a[0]));}
    }
    if(!(outputVertices>12&&curved&&uvArea>.95&&uvArea<1.0001&&lo[0]<.01f&&lo[1]<.01f&&hi[0]>.99f&&hi[1]>.99f)){
        std::ostringstream message;message<<"Conditioned synthetic coverage="<<uvArea<<", vertices="<<outputVertices<<", curved="<<curved<<", bounds="<<lo[0]<<','<<lo[1]<<" -> "<<hi[0]<<','<<hi[1]<<", status="<<patch.message;throw std::runtime_error(message.str());
    }
}
} // namespace crimson::decals::test
