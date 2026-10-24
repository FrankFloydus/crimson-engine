#pragma once
#include "decals/DecalPolygon.h"
#include <map>
#include <set>
#include <stdexcept>

namespace crimson::decals::test {
inline void polygonCheck(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
inline void conformingPolygon(std::vector<detail::D3> positions,std::vector<size_t> boundary,detail::D3 normal,double expectedArea){
    using namespace detail;
    std::vector<std::array<size_t,3>> triangles;
    polygonCheck(triangulateConvexPolygon(boundary,positions,normal,1e-10,[&](auto triangle){triangles.push_back(triangle);}),"Convex polygon with collinear contact vertices failed triangulation.");
    polygonCheck(triangles.size()==boundary.size()-2,"Triangulation did not preserve all boundary vertices.");
    std::map<std::pair<size_t,size_t>,unsigned> edges;std::set<size_t> used;double area=0;
    for(const auto& triangle:triangles){
        const double oriented=dot(cross(positions[triangle[1]]-positions[triangle[0]],positions[triangle[2]]-positions[triangle[0]]),unit(normal));
        polygonCheck(oriented>0,"Triangulation emitted a degenerate or reversed triangle.");area+=oriented*.5;
        for(size_t i=0;i<3;++i){used.insert(triangle[i]);++edges[std::minmax(triangle[i],triangle[(i+1)%3])];}
    }
    polygonCheck(std::abs(area-expectedArea)<1e-8,"Triangulation changed the surface area.");
    polygonCheck(used.size()==boundary.size(),"Triangulation discarded a collinear contact vertex.");
    for(size_t i=0;i<boundary.size();++i){const auto edge=std::minmax(boundary[i],boundary[(i+1)%boundary.size()]);polygonCheck(edges[edge]==1,"Triangulation skipped a boundary segment and left a T junction.");edges.erase(edge);}
    for(const auto&[edge,count]:edges){(void)edge;polygonCheck(count==2,"Triangulation introduced a hole or overlapping boundary.");}
}
inline void runPolygonProofs(){
    using namespace detail;
    std::vector<D3> rectangle{{0,0,0},{1,0,0},{2,0,0},{2,1,0},{2,2,0},{1,2,0},{0,2,0},{0,1,0}};
    std::vector<size_t> boundary{0,1,2,3,4,5,6,7};conformingPolygon(rectangle,boundary,{0,0,1},4);
    std::reverse(boundary.begin(),boundary.end());conformingPolygon(rectangle,boundary,{0,0,1},4);
    for(auto& p:rectangle)p={170+p.x,110,1000+p.y};conformingPolygon(rectangle,boundary,{0,-1,0},4);
    conformingPolygon({{0,0,0},{.5,0,0},{1,0,0},{1.5,0,0},{2,0,0},{1,1,0}}, {0,1,2,3,4,5},{0,0,1},1);
    unsigned emitted=0;
    polygonCheck(!triangulateConvexPolygon({0,1,2},{{0,0,0},{1,0,0},{2,0,0}},{0,0,1},1e-10,[&](auto){++emitted;})&&emitted==0,"Degenerate polygon partially published triangles.");
    polygonCheck(!triangulateConvexPolygon({0,1,2,1},{{0,0,0},{1,0,0},{0,1,0}},{0,0,1},1e-10,[&](auto){++emitted;})&&emitted==0,"Repeated boundary identity partially published triangles.");
}
}
