#include <crimson/renderer/forward/ShadowRayGeometry.h>
#include <crimson/renderer/forward/ModelData.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
using Point=std::array<double,3>;
using Triangle=std::array<Point,3>;
using TriangleBits=std::array<std::uint32_t,9>;
void check(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
template<class F> void throws(const F& action,const char* message){
    bool failed{};try{action();}catch(const std::exception&){failed=true;}check(failed,message);
}
Point point(const Vec3& p){return {p[0],p[1],p[2]};}
Point minus(Point a,Point b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
Point cross(Point a,Point b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double dot(Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
Triangle original(const ModelPrimitive& source,std::size_t first){
    return {point(source.vertices.at(source.indices.at(first)).position),point(source.vertices.at(source.indices.at(first+1)).position),
        point(source.vertices.at(source.indices.at(first+2)).position)};
}
Triangle packed(const ShadowRayMesh& mesh,unsigned index){
    Triangle triangle;for(unsigned vertex=0;vertex<3;++vertex)for(unsigned axis=0;axis<3;++axis)
        triangle[vertex][axis]=mesh.triangles.at(std::size_t(index)*3+vertex)[axis];return triangle;
}
TriangleBits bits(const Triangle& triangle){
    TriangleBits result{};for(unsigned vertex=0;vertex<3;++vertex)for(unsigned axis=0;axis<3;++axis)
        result[vertex*3+axis]=std::bit_cast<std::uint32_t>(static_cast<float>(triangle[vertex][axis]));return result;
}
bool valid(const Triangle& t){const auto n=cross(minus(t[1],t[0]),minus(t[2],t[0]));return n[0]!=0||n[1]!=0||n[2]!=0;}
bool contains(const Vec4& low,const Vec4& high,Point p){
    for(unsigned axis=0;axis<3;++axis)if(p[axis]<low[axis]||p[axis]>high[axis])return false;return true;
}
void validatePacking(const ModelPrimitive& source,const ShadowRayMesh& mesh){
    check(mesh.nodes.size()%2==0&&!mesh.nodes.empty(),"Packed BVH has no complete root.");
    check(mesh.triangles.size()==std::size_t(mesh.triangleCount)*3,"Packed triangle count differs from storage.");
    const auto nodeCount=static_cast<unsigned>(mesh.nodes.size()/2);
    check(std::bit_cast<std::uint32_t>(mesh.nodes[0][3])==nodeCount,"Root escape is not the node-count sentinel.");
    std::map<TriangleBits,unsigned> expected,actual;
    for(std::size_t first=0;first<source.indices.size();first+=3){const auto triangle=original(source,first);if(valid(triangle))++expected[bits(triangle)];}
    for(unsigned triangle=0;triangle<mesh.triangleCount;++triangle){
        ++actual[bits(packed(mesh,triangle))];
        for(unsigned vertex=0;vertex<3;++vertex)check(mesh.triangles[std::size_t(triangle)*3+vertex][3]==0,"Triangle reserved lane changed.");
    }
    check(actual==expected,"BVH lost, duplicated or changed an original nondegenerate triangle/winding/vertex bit pattern.");
    std::vector<unsigned> visitedNodes(nodeCount),visitedTriangles(mesh.triangleCount);
    const auto visit=[&](auto&& self,unsigned node,const Vec4* parentLow,const Vec4* parentHigh)->unsigned{
        check(node<nodeCount&&!visitedNodes[node]++,"BVH child is invalid, cyclic or shared.");
        const auto& low=mesh.nodes[node*2];const auto& high=mesh.nodes[node*2+1];
        const auto escape=std::bit_cast<std::uint32_t>(low[3]),header=std::bit_cast<std::uint32_t>(high[3]);
        const auto first=header&0xffffffu,count=header>>24;
        check(escape>node&&escape<=nodeCount,"BVH escape does not advance inside its mesh.");
        for(unsigned axis=0;axis<3;++axis){
            check(std::isfinite(low[axis])&&std::isfinite(high[axis])&&low[axis]<=high[axis],"BVH bounds are invalid.");
            if(parentLow)check(low[axis]>=(*parentLow)[axis]&&high[axis]<=(*parentHigh)[axis],"Child escaped parent bounds.");
        }
        if(count){
            check(count<=4&&escape==node+1&&std::uint64_t(first)+count<=mesh.triangleCount,"BVH leaf encoding is invalid.");
            for(unsigned triangle=first;triangle<first+count;++triangle){
                check(!visitedTriangles[triangle]++,"A triangle belongs to multiple leaves.");
                for(const auto& vertex:packed(mesh,triangle))check(contains(low,high,vertex),"Leaf does not contain its original triangle.");
            }
        }else{
            check(first==0,"Interior node contains a nonzero triangle offset.");
            const auto right=self(self,node+1,&low,&high);
            check(right<escape,"Interior node has no second child.");
            check(self(self,right,&low,&high)==escape,"Binary children do not cover their parent's preorder interval.");
        }
        return escape;
    };
    check(visit(visit,0,nullptr,nullptr)==nodeCount,"BVH root misses a subtree.");
    check(std::all_of(visitedNodes.begin(),visitedNodes.end(),[](unsigned count){return count==1;})&&
        std::all_of(visitedTriangles.begin(),visitedTriangles.end(),[](unsigned count){return count==1;}),"BVH has unreachable storage.");
    check(mesh.bounds.valid,"Immutable mesh lacks aggregate bounds.");
    for(unsigned axis=0;axis<3;++axis)check(mesh.bounds.minimum[axis]==mesh.nodes[0][axis]&&mesh.bounds.maximum[axis]==mesh.nodes[1][axis],"Mesh bounds disagree with root.");
    check(mesh.bytes()>=sizeof(mesh)+(mesh.nodes.size()+mesh.triangles.size())*sizeof(Vec4),"CPU byte accounting omits payload.");
}
struct RayD {Point origin,direction;};
std::optional<double> intersect(const RayD& ray,const Triangle& triangle,double maximum){
    // Independent double-precision brute-force oracle. No BVH builder bounds,
    // centroid or leaf math participates in this triangle visibility predicate.
    const auto edge1=minus(triangle[1],triangle[0]),edge2=minus(triangle[2],triangle[0]);
    const auto p=cross(ray.direction,edge2);const double determinant=dot(edge1,p);
    if(determinant==0)return {};
    const auto offset=minus(ray.origin,triangle[0]);const double u=dot(offset,p)/determinant;
    if(u<0||u>1)return {};
    const auto q=cross(offset,edge1);const double v=dot(ray.direction,q)/determinant;
    if(v<0||u+v>1)return {};
    const double distance=dot(edge2,q)/determinant;
    return distance>0&&distance<=maximum?std::optional<double>{distance}:std::nullopt;
}
bool box(const RayD& ray,const Vec4& low,const Vec4& high,double maximum){
    double near=0,far=maximum;
    for(unsigned axis=0;axis<3;++axis){
        if(ray.direction[axis]==0){if(ray.origin[axis]<low[axis]||ray.origin[axis]>high[axis])return false;continue;}
        const auto a=(double(low[axis])-ray.origin[axis])/ray.direction[axis],b=(double(high[axis])-ray.origin[axis])/ray.direction[axis];
        near=std::max(near,std::min(a,b));far=std::min(far,std::max(a,b));if(near>far)return false;
    }
    return true;
}
std::optional<double> brute(const ModelPrimitive& source,const RayD& ray,double maximum){
    std::optional<double> result;
    for(std::size_t first=0;first<source.indices.size();first+=3)if(const auto hit=intersect(ray,original(source,first),result.value_or(maximum)))result=hit;
    return result;
}
std::optional<double> traverse(const ShadowRayMesh& mesh,const RayD& ray,double maximum){
    std::optional<double> result;unsigned node{},iterations{};const auto count=static_cast<unsigned>(mesh.nodes.size()/2);
    while(node<count){
        check(++iterations<=count,"Stackless BVH did not terminate.");
        const auto& low=mesh.nodes[node*2];const auto& high=mesh.nodes[node*2+1];
        const auto escape=std::bit_cast<std::uint32_t>(low[3]),header=std::bit_cast<std::uint32_t>(high[3]);
        if(!box(ray,low,high,result.value_or(maximum))){node=escape;continue;}
        const auto first=header&0xffffffu,triangles=header>>24;
        if(!triangles){++node;continue;}
        for(unsigned index=first;index<first+triangles;++index)if(const auto hit=intersect(ray,packed(mesh,index),result.value_or(maximum)))result=hit;
        node=escape;
    }
    return result;
}
void triangle(ModelPrimitive& mesh,Vec3 a,Vec3 b,Vec3 c){
    for(const auto& p:{a,b,c}){mesh.indices.push_back(static_cast<std::uint32_t>(mesh.vertices.size()));ModelVertex vertex;vertex.position=p;mesh.vertices.push_back(vertex);}
}
ModelPrimitive fixture(){
    ModelPrimitive mesh;
    constexpr std::array<Vec3,8> vertices{{{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}}};
    for(const auto& p:vertices){ModelVertex vertex;vertex.position=p;mesh.vertices.push_back(vertex);}
    mesh.indices={0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
    std::mt19937 random{0x53484144};std::uniform_real_distribution<float> value(-3,3);
    for(unsigned index=0;index<129;++index)triangle(mesh,{value(random),value(random),value(random)},
        {value(random),value(random),value(random)},{value(random),value(random),value(random)});
    // Coincident centroids exercise balanced deterministic tie handling.
    for(unsigned index=1;index<=33;++index){const float scale=float(index)/50;triangle(mesh,{-scale,-scale,0},{scale,-scale,0},{0,2*scale,0});}
    triangle(mesh,{0,0,0},{0,0,0},{0,0,0});
    triangle(mesh,{-0.f,0,2},{1,0,2},{0,1,2});
    triangle(mesh,{0,0,0},{1e-15f,0,0},{0,1e-20f,0});
    return mesh;
}
void validationCases(){
    check(!build_shadow_ray_mesh({}),"Empty geometry did not fall back.");
    ModelPrimitive mesh;triangle(mesh,{0,0,0},{1,0,0},{0,1,0});
    auto invalid=mesh;invalid.indices[2]=99;throws([&]{(void)build_shadow_ray_mesh(invalid);},"Invalid index was accepted.");
    invalid=mesh;invalid.vertices[1].position[0]=std::numeric_limits<float>::infinity();
    throws([&]{(void)build_shadow_ray_mesh(invalid);},"Infinite position was accepted.");
    invalid.vertices[1].position[0]=std::numeric_limits<float>::quiet_NaN();
    throws([&]{(void)build_shadow_ray_mesh(invalid);},"NaN position was accepted.");
    invalid=mesh;invalid.indices.pop_back();throws([&]{(void)build_shadow_ray_mesh(invalid);},"Incomplete triangle was accepted.");
    invalid=mesh;invalid.hasSkinning=true;check(!build_shadow_ray_mesh(invalid),"Skin metadata did not disable CPU rays.");
    invalid=mesh;invalid.skinWeights.resize(3);check(!build_shadow_ray_mesh(invalid),"Skin arrays did not disable CPU rays.");
    invalid=mesh;invalid.morphTargets.resize(1);check(!build_shadow_ray_mesh(invalid),"Morph targets did not disable CPU rays.");
    ModelPrimitive degenerate;triangle(degenerate,{0,0,0},{1,1,1},{2,2,2});check(!build_shadow_ray_mesh(degenerate),"All-degenerate mesh acquired an invalid BVH.");
    ModelPrimitive tiny;triangle(tiny,{0,0,0},{1e-15f,0,0},{0,1e-20f,0});const auto thin=build_shadow_ray_mesh(tiny);
    check(thin&&thin->triangleCount==1,"A world-unit epsilon discarded a tiny valid triangle.");validatePacking(tiny,*thin);
    const RayD thinRay{{2e-16,2e-21,1},{0,0,-1}};
    const auto thinExpected=brute(tiny,thinRay,2),thinActual=traverse(*thin,thinRay,2);
    check(thinExpected&&thinActual&&*thinExpected==*thinActual,"A tiny valid triangle disappeared from ray visibility.");
    std::stop_source stop;stop.request_stop();throws([&]{(void)build_shadow_ray_mesh(mesh,stop.get_token());},"Canceled builder published geometry.");
}
}
int main(){try{
    validationCases();const auto source=fixture();const auto savedVertices=source.vertices;const auto savedIndices=source.indices;
    const auto mesh=build_shadow_ray_mesh(source),again=build_shadow_ray_mesh(source);check(bool(mesh)&&bool(again),"Valid fixture was rejected.");
    validatePacking(source,*mesh);
    check(mesh->nodes.size()==again->nodes.size()&&mesh->triangles.size()==again->triangles.size()&&
        std::memcmp(mesh->nodes.data(),again->nodes.data(),mesh->nodes.size()*sizeof(Vec4))==0&&
        std::memcmp(mesh->triangles.data(),again->triangles.data(),mesh->triangles.size()*sizeof(Vec4))==0,"Repeated preparation changed packed bytes.");
    check(source.indices==savedIndices&&std::memcmp(source.vertices.data(),savedVertices.data(),source.vertices.size()*sizeof(ModelVertex))==0,"Builder mutated original geometry.");
    std::mt19937 random{0x425648};std::uniform_real_distribution<double> value(-6,6),direction(-1,1),length(.05,15);
    unsigned rays{},hits{},misses{};
    const auto compare=[&](const RayD& ray,double maximum){
        const auto expected=brute(source,ray,maximum),actual=traverse(*mesh,ray,maximum);++rays;
        check(bool(expected)==bool(actual),"BVH omitted or invented a brute-force ray intersection.");
        if(expected){++hits;check(std::abs(*expected-*actual)<=1e-11*std::max(1.,std::abs(*expected)),"BVH changed nearest hit distance.");}else ++misses;
    };
    for(unsigned index=0;index<4096;++index)compare({{value(random),value(random),value(random)},
        {direction(random),direction(random),direction(random)}},length(random));
    // Parallel slab axes, rays originating inside, shared cube edges/vertices,
    // and finite rays stopping before the nearest closed-room surface.
    for(const Point origin:{Point{0,0,0},Point{1,1,4},Point{0,0,4},Point{8,8,8}})
        for(const Point rayDirection:{Point{0,0,-1},Point{1,0,0},Point{0,1,0},Point{-1,-1,-1}})
            for(const double maximum:{.01,1.,20.})compare({origin,rayDirection},maximum);
    check(hits>100&&misses>100,"Random rays did not exercise both visibility outcomes.");
    std::cout<<"Shadow ray geometry: "<<mesh->triangleCount<<" triangles, "<<mesh->nodes.size()/2<<" BVH nodes, "<<rays
        <<" independent rays ("<<hits<<" hits / "<<misses<<" misses), deterministic packing and validation passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
