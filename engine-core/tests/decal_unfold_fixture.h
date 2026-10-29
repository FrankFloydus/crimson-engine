#pragma once
#include "decals/DecalInternal.h"
#include <array>
#include <cmath>
#include <stdexcept>

namespace crimson::decals::test {
inline void runUnfoldProofs() {
    using namespace detail;
    // Contact cuts can form a thin strip even though its distinct boundary
    // vertices are far apart. Its two small triangles are the only edge path
    // between the regular lower and upper faces. Losing their altitude through
    // squared-length subtraction disconnects a visible portion of the image.
    constexpr double altitude=1e-9;
    AnalysisPatch analysis;
    analysis.positions={{0,-.5,0},{1,-.5,0},{1,1,0},{0,1,0},
        {0,.5,0},{.25,.5-altitude,0},{1,.5,0},{.75,.5+altitude,0},{.5,.1,0}};
    for(const auto vertices:std::array<std::array<size_t,3>,10>{{
        {0,1,8},{1,5,8},{5,0,8},{1,6,5},{0,5,4},
        {4,5,6},{4,6,7},{4,7,3},{7,2,3},{7,6,2}}})
        analysis.faces.push_back({vertices,0});
    Source source;source.p={D3{-2,-2,0},D3{4,-2,0},D3{-2,4,0}};
    source.n={D3{0,0,1},D3{0,0,1},D3{0,0,1}};source.geometricNormal={0,0,1};
    analysis.sources.push_back(source);
    analysis.seedVertex=8;analysis.seedFace=0;analysis.seedPosition=analysis.positions[8];
    analysis.seedNormal={0,0,1};analysis.basisX={1,0,0};analysis.basisY={0,1,0};
    analysis.ax=analysis.by=analysis.frameDet=analysis.scale=1;analysis.tolerance=.001;

    SurfaceReceiver receiver;receiver.id={1,0,0};receiver.generation=1;
    SurfaceTriangle original;original.triangle=17;
    original.positions={Vec3{-2,-2,0},Vec3{4,-2,0},Vec3{-2,4,0}};
    original.normals={Vec3{0,0,1},Vec3{0,0,1},Vec3{0,0,1}};
    receiver.triangles.push_back(original);
    DecalSurfaceSnapshot snapshot{{receiver}};
    DecalBuildRequest request;request.seed.receiver=receiver.id;request.seed.triangle=original.triangle;
    request.seed.barycentrics=v3(barycentric(source,analysis.seedPosition));
    request.frame=math::translation({.5f,.1f,0});
    DecalPatch patch;buildChart(snapshot,request,analysis,patch,{});
    const auto require=[](bool value,const char* message){if(!value)throw std::runtime_error(message);};
    require(patch.status==DecalPatchStatus::Ready,
        "A planar contact strip lost its seed-connected image during intrinsic unfolding.");
    require(patch.statistics.usedDevelopableUnfolding,
        "A planar contact strip unexpectedly required the curved-surface solver.");
    double uvArea=0;bool upper=false;
    for(const auto& chunk:patch.chunks) {
        require(chunk.receiver==receiver.id&&chunk.generation==receiver.generation,
            "Contact-strip unfolding lost its receiver provenance.");
        for(const auto& vertex:chunk.vertices) {
            require(vertex.sourceTriangle==original.triangle,
                "Contact-strip unfolding lost its original triangle identity.");
            const D3 restored=source.p[0]*vertex.barycentrics[0]+source.p[1]*vertex.barycentrics[1]+source.p[2]*vertex.barycentrics[2];
            require(length(restored-d3(vertex.position))<2e-6,
                "Contact-strip unfolding moved a vertex away from its original source triangle.");
            require(std::abs(vertex.uv[0]-vertex.position[0])<2e-6&&
                std::abs(vertex.uv[1]-(.6-vertex.position[1]))<2e-6,
                "Contact-strip unfolding changed the planar metre metric or image orientation.");
            upper|=vertex.position[1]>.59f;
        }
        for(size_t i=0;i<chunk.indices.size();i+=3) {
            const auto& a=chunk.vertices[chunk.indices[i]].uv;
            const auto& b=chunk.vertices[chunk.indices[i+1]].uv;
            const auto& c=chunk.vertices[chunk.indices[i+2]].uv;
            uvArea+=.5*std::abs((double(b[0])-a[0])*(double(c[1])-a[1])-
                (double(b[1])-a[1])*(double(c[0])-a[0]));
        }
    }
    require(upper&&uvArea>.99999&&uvArea<1.00001,
        "A numerically thin contact strip disconnected the upper part of a fully covered planar image.");
}
} // namespace crimson::decals::test
