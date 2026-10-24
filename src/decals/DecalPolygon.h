#pragma once
#include "DecalInternal.h"
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
    const auto area=[&](size_t a,size_t b,size_t c){return dot(cross(positions[b]-positions[a],positions[c]-positions[a]),normal);};
    double polygonArea=0;for(size_t i=1;i+1<boundary.size();++i)polygonArea+=area(boundary[0],boundary[i],boundary[i+1]);
    if(!std::isfinite(polygonArea)||std::abs(polygonArea)<=minimumArea)return false;
    if(polygonArea<0)std::reverse(boundary.begin(),boundary.end());
    const auto contains=[&](const std::array<size_t,3>& triangle,size_t point){
        for(size_t i=0;i<3;++i){
            const auto a=triangle[i],b=triangle[(i+1)%3];
            if(area(a,b,point)< -epsilon*length(positions[b]-positions[a]))return false;
        }
        return true;
    };
    std::vector<std::array<size_t,3>> triangles;triangles.reserve(boundary.size()-2);
    while(boundary.size()>3){
        bool found=false;
        for(size_t i=0;i<boundary.size();++i){
            const std::array<size_t,3> triangle{boundary[(i+boundary.size()-1)%boundary.size()],boundary[i],boundary[(i+1)%boundary.size()]};
            if(area(triangle[0],triangle[1],triangle[2])<=minimumArea)continue;
            bool occupied=false;
            for(const auto id:boundary)if(id!=triangle[0]&&id!=triangle[1]&&id!=triangle[2]&&contains(triangle,id)){occupied=true;break;}
            if(occupied)continue;
            triangles.push_back(triangle);boundary.erase(boundary.begin()+static_cast<std::ptrdiff_t>(i));found=true;break;
        }
        if(!found)return false;
    }
    if(area(boundary[0],boundary[1],boundary[2])<=minimumArea)return false;
    triangles.push_back({boundary[0],boundary[1],boundary[2]});
    for(const auto& triangle:triangles)emit(triangle);
    return true;
}
}
