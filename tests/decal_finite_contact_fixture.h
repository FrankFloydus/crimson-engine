#pragma once

#include "decals/DecalFiniteContact.h"
#include "decals/DecalEndpointIndex.h"
#include <array>
#include <stdexcept>

namespace crimson::decals::test {
namespace finite_contact_fixture {
inline void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
inline SurfaceReceiver quad(uint64_t id,Vec3 a,Vec3 b,Vec3 c,Vec3 d){SurfaceReceiver result;result.id.entity=id;result.generation=1;result.triangles={{0,{a,b,c},{}},{1,{a,c,d},{}}};return result;}

inline void intervalGate() {
    using namespace detail;
    const FiniteContactInterval interval{{3,5,7},{0,1,0},.25};
    const std::array<D3,4> before{{{2,4,7},{4,4,7},{4,5,7},{2,5,7}}};
    const std::array<D3,4> middle{{{2,5,7},{4,5,7},{4,5.25,7},{2,5.25,7}}};
    const std::array<D3,4> after{{{2,5.25,7},{4,5.25,7},{4,6,7},{2,6,7}}};
    require(!interval.intersects(before,1e-10)&&interval.intersects(middle,1e-10)&&!interval.intersects(after,1e-10),
        "A finite longitudinal contact cut escaped its endpoint slab.");
    const FiniteContactInterval reversed{{3,5.25,7},{0,-1,0},.25};
    require(!reversed.intersects(before,1e-10)&&reversed.intersects(middle,1e-10)&&!reversed.intersects(after,1e-10),
        "Reversing a finite contact changed its selected cells.");
}

inline void endpointProjection() {
    using namespace detail;
    const D3 normal{-0.99999999999797107,-1.2362158402201134e-13,2.0144214926930408e-6};
    const D3 corner{5.499999948103655,0,25.620676227171643};
    const D3 candidate{5.4999999481672406,0,25.620676227171643};
    const D3 other{5.4999999770494989,0,25.635045535263682};
    require(dot(candidate-corner,unit(other-corner))>0,
        "Captured off-plane endpoint no longer exercises the false positive edge parameter.");
    const SurfaceEndpointProjection projection(normal);
    require(projection.coincident(candidate,corner),
        "Off-plane jitter invented a boundary point at an existing surface corner.");
    D3 inside=corner;inside.z=std::nextafter(inside.z,INFINITY);
    require(!projection.coincident(inside,corner),
        "Exact endpoint eligibility erased a strictly distinct in-plane sample.");
    for(const D3 n:std::array<D3,3>{{{1,0,0},{0,1,0},{0,0,1}}}){
        const SurfaceEndpointProjection p(n);D3 a{},b{};
        if(n.x)b.x=1e-12;if(n.y)b.y=1e-12;if(n.z)b.z=1e-12;
        require(p.coincident(a,b),"Endpoint projection used the wrong surface coordinates.");
    }
}

inline void finiteBranch() {
    using namespace detail;
    // Both endpoints lie inside one common-grid cell. A broad-phase support
    // test alone used to extend this oblique line throughout that cell.
    const Vec3 p{.1f,.13f,0},q{.2f,.167f,0};
    DecalSurfaceSnapshot snapshot{{quad(1,{-3,-3,0},{3,-3,0},{3,3,0},{-3,3,0}),
        quad(2,p+Vec3{0,0,-.2f},q+Vec3{0,0,-.2f},q+Vec3{0,0,.2f},p+Vec3{0,0,.2f})}};
    DecalBuildRequest request;request.seed={{1,0,0},0,{.5f,0,.5f}};request.limits.sampleSpacing=1;
    const D3 first=d3(p),direction=unit(d3(q)-first);const double extent=length(d3(q)-first);
    for(const bool reversed:{false,true}) {
        auto source=snapshot;if(reversed){std::reverse(source.receivers.begin(),source.receivers.end());for(auto& receiver:source.receivers)std::reverse(receiver.triangles.begin(),receiver.triangles.end());}
        DecalBuildStatistics statistics;const auto analysis=buildTopology(source,request,statistics,{});
        size_t contactEdges=0;
        for(const auto& face:analysis.faces)for(size_t edge=0;edge<3;++edge) {
            const D3 a=analysis.positions[face.v[edge]],b=analysis.positions[face.v[(edge+1)%3]];
            const double ta=dot(a-first,direction),tb=dot(b-first,direction);
            const double da=length(a-first-direction*ta),db=length(b-first-direction*tb);
            if(da>1e-9||db>1e-9||length(a-b)<1e-8)continue;
            ++contactEdges;
            require(std::min(ta,tb)>=-1e-8&&std::max(ta,tb)<=extent+1e-8,
                "A finite contact split geometry beyond its actual segment.");
        }
        require(contactEdges>0,"The finite contact disappeared instead of remaining a real barrier.");
        const auto patch=buildDecalPatch(source,request);
        require(patch.usable()&&patch.status==DecalPatchStatus::Incomplete&&patch.chunks.size()==1&&patch.chunks.front().receiver.entity==1,
            "Finite contact localization removed its interior barrier or joined an ambiguous sheet.");
        for(const auto& chunk:patch.chunks)for(const auto& vertex:chunk.vertices)
            require(std::abs(vertex.position[2])<1e-8,"Finite contact localization moved the original receiver surface.");
    }
}
} // namespace finite_contact_fixture

inline void runFiniteContactProofs(){finite_contact_fixture::intervalGate();finite_contact_fixture::endpointProjection();finite_contact_fixture::finiteBranch();}
} // namespace crimson::decals::test
