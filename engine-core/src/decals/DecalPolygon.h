#pragma once
#include "DecalInternal.h"
#include "DecalPolygonIntervals.h"
#include "DecalOrientation.h"
#include <array>
#include <cstddef>
#include <vector>

namespace crimson::decals::detail {
// Preserve every boundary segment, including collinear contact vertices. A fan
// can skip those vertices through zero-area triangles and leave a T junction.
// Epsilon is a positional tolerance. Failure invokes no callbacks; successful
// triangles retain the supplied IDs and face the supplied geometric normal.
template<class Emit>
bool triangulateConvexPolygon(std::vector<size_t> boundary,const std::vector<D3>& positions,
    D3 geometricNormal,double epsilon,Emit&& emit) {
    if(boundary.size()<3||!finite(geometricNormal)||!std::isfinite(epsilon)||epsilon<0)return false;
    const double normalLength=length(geometricNormal);if(!std::isfinite(normalLength)||normalLength<1e-20)return false;
    const D3 normal=geometricNormal/normalLength;
    for(const auto id:boundary)if(id>=positions.size()||!finite(positions[id]))return false;
    auto unique=boundary;std::sort(unique.begin(),unique.end());
    if(std::adjacent_find(unique.begin(),unique.end())!=unique.end())return false;
    const double minimumArea=epsilon*epsilon;
    size_t axis=0;for(size_t i=1;i<3;++i)if(std::abs(normal[i])>std::abs(normal[axis]))axis=i;
    const size_t u=(axis+1)%3,v=(axis+2)%3;
    // Cell geometry is planar. Project only the orientation predicate onto its
    // best-conditioned coordinate plane; emitted positions and chart geometry
    // retain their original three-dimensional coordinates.
    const auto area=[&](size_t a,size_t b,size_t c){return robustOrient2D(positions[a][u],positions[a][v],positions[b][u],positions[b][v],positions[c][u],positions[c][v])/normal[axis];};
    double polygonArea=0;for(size_t i=1;i+1<boundary.size();++i)polygonArea+=area(boundary[0],boundary[i],boundary[i+1]);
    if(!std::isfinite(polygonArea)||std::abs(polygonArea)<=minimumArea)return false;
    if(polygonArea<0)std::reverse(boundary.begin(),boundary.end());
    const auto originalBoundary=boundary;
    const auto quality=[&](size_t a,size_t b,size_t c){
        // Positional epsilon gates the complete cell, not each constituent
        // triangle. Conforming edge samples can force a positive tip triangle
        // much smaller than epsilon squared; rejecting it loses valid topology.
        const double twiceArea=area(a,b,c);if(twiceArea<=0)return 0.;
        const std::array<size_t,3> triangle{a,b,c};double squaredEdges=0;
        for(size_t j=0;j<3;++j){const D3 edge=positions[triangle[j]]-positions[triangle[(j+1)%3]];squaredEdges+=dot(edge,edge);}
        const double score=twiceArea/squaredEdges;return std::isfinite(score)?score:0.;
    };
    const auto completeTriangulation=[&]{
        const auto result=triangulatePolygonIntervals(originalBoundary.size(),[&](size_t a,size_t b,size_t c){return quality(originalBoundary[a],originalBoundary[b],originalBoundary[c]);});
        if(result.status==PolygonIntervalStatus::LimitExceeded)throw Failure(DecalPatchStatus::LimitExceeded,"Contact-cell triangulation search budget exceeded.");
        if(result.status!=PolygonIntervalStatus::Ready)return false;
        for(const auto&triangle:result.triangles)emit(std::array<size_t,3>{originalBoundary[triangle[0]],originalBoundary[triangle[1]],originalBoundary[triangle[2]]});
        return true;
    };
    const auto contains=[&](const std::array<size_t,3>& triangle,size_t point){
        for(size_t i=0;i<3;++i){
            const auto a=triangle[i],b=triangle[(i+1)%3];
            // An expanded half-plane can cover a real, very thin ear even
            // though its area is above the degeneracy threshold. Use the same
            // geometric orientation for occupancy and winding. Exact points
            // on an edge still block an ear, preserving contact subdivisions.
            if(area(a,b,point)<0)return false;
        }
        return true;
    };
    std::vector<std::array<size_t,3>> triangles;triangles.reserve(boundary.size()-2);
    struct Ear {size_t corner;std::array<size_t,3> triangle;double quality;};
    std::vector<Ear> ears;ears.reserve(boundary.size());
    while(boundary.size()>3){
        ears.clear();
        for(size_t i=0;i<boundary.size();++i){
            const std::array<size_t,3> triangle{boundary[(i+boundary.size()-1)%boundary.size()],boundary[i],boundary[(i+1)%boundary.size()]};
            // Prefer avoiding sub-epsilon² triangles in the greedy fast path.
            // This is a quality target; the complete solver admits every
            // strictly positive triangle when conformity makes one necessary.
            if(area(triangle[0],triangle[1],triangle[2])<=minimumArea)continue;
            const double score=quality(triangle[0],triangle[1],triangle[2]);if(score<=0)continue;
            ears.push_back({i,triangle,score});
        }
        // Prefer well-shaped legal ears instead of peeling near-collinear
        // contact chains into needles. Ranking is scale independent and ties
        // retain boundary order, so source IDs and repeat builds stay stable.
        std::sort(ears.begin(),ears.end(),[](const Ear&a,const Ear&b){return a.quality!=b.quality?a.quality>b.quality:a.corner<b.corner;});
        bool found=false;
        for(const auto&ear:ears){
            const auto&triangle=ear.triangle;
            // The remainder needs positive triangles. Removing its last
            // off-line corner can strand a collinear contact chain even when
            // this ear itself is well shaped. Recompute its area directly;
            // subtracting two large areas loses exactly these narrow strips.
            size_t anchor=SIZE_MAX,previous=SIZE_MAX;double remainingArea=0;
            for(size_t i=0;i<boundary.size();++i)if(i!=ear.corner){if(anchor==SIZE_MAX)anchor=boundary[i];else {if(previous!=SIZE_MAX)remainingArea+=area(anchor,previous,boundary[i]);previous=boundary[i];}}
            if(remainingArea<=minimumArea*static_cast<double>(boundary.size()-3))continue;
            bool occupied=false;
            for(const auto id:boundary)if(id!=triangle[0]&&id!=triangle[1]&&id!=triangle[2]&&contains(triangle,id)){occupied=true;break;}
            if(occupied)continue;
            triangles.push_back(triangle);boundary.erase(boundary.begin()+static_cast<std::ptrdiff_t>(ear.corner));found=true;break;
        }
        if(!found)return completeTriangulation();
    }
    if(area(boundary[0],boundary[1],boundary[2])<=minimumArea)return completeTriangulation();
    triangles.push_back({boundary[0],boundary[1],boundary[2]});
    for(const auto& triangle:triangles)emit(triangle);
    return true;
}
}
