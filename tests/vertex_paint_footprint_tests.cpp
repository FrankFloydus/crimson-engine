#include "editor/VertexPaintFootprint.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool close(float a,float b,float epsilon=1e-5f){return std::abs(a-b)<=epsilon;}
bool basisClose(const VertexPaintFootprintInfluence& group,const std::array<float,4>& expected) {
    for(unsigned corner=0;corner<4;++corner)if(!close(group.basis[corner],expected[corner]))return false;return true;
}
std::shared_ptr<MeshPaintGeometry> quad(std::array<Vec3,4> positions) {
    auto geometry=std::make_shared<MeshPaintGeometry>();geometry->vertices.resize(4);
    for(unsigned corner=0;corner<4;++corner)geometry->vertices[corner].position=positions[corner];
    MeshPaintCell cell;cell.cornerCount=4;cell.vertices={0,1,2,3};geometry->cells.push_back(cell);return geometry;
}
auto square(float extent=1.f){return quad({Vec3{-extent,-extent,0},Vec3{extent,-extent,0},Vec3{extent,extent,0},Vec3{-extent,extent,0}});}
VertexPaintFootprintResult visible(VertexPaintFootprintCache& cache,Vec3 center,float radius=.05f,float strength=1.f) {
    return cache.collect(center,radius,strength,1,[](const auto&)->std::optional<bool>{return true;});
}
void coarseInteriorAndExactCorner() {
    const auto geometry=square();VertexPaintFootprintCache cache;
    require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},400,400),"Could not prepare a coarse visible quad.");
    auto result=visible(cache,{0,0,0});
    require(!result.missing&&!result.influences.empty()&&result.influences.size()<=4,"A small brush missed a quad interior with all control corners outside its radius.");
    const auto& group=result.influences.front();
    require(group.target==0&&group.cell==0&&group.cornerCount==4&&group.vertices==std::array<std::uint32_t,4>{0,1,2,3}&&group.amount==1,"The footprint lost logical cell ownership or authored brush opacity.");
    for(const auto weight:group.basis)require(close(weight,.25f),"A centered quad footprint did not retain its bilinear basis.");
    // Keep the corner just inside the viewport so the support sample can be
    // visible, even though the other three controls are far outside the brush.
    const auto small=square(.8f);require(cache.prepare(std::array{VertexPaintFootprintMesh{small,{}}},{},400,400),"Could not prepare the corner fixture.");
    result=visible(cache,{-.8f,-.8f,0});
    require(!result.influences.empty()&&close(result.influences[0].basis[0],1),"An exact quad corner did not retain sole interpolation ownership.");
    for(unsigned corner=1;corner<4;++corner)require(close(result.influences[0].basis[corner],0),"A corner footprint unnecessarily touched unrelated controls.");
    require(std::any_of(result.influences.begin(),result.influences.end(),[](const auto& influence){return influence.basis[1]>0||influence.basis[2]>0||influence.basis[3]>0;}),"A brush centered on an already-Base corner missed neighboring mask contributions inside its circle.");
}
void trapezoidAffineBasis() {
    const std::array<Vec3,4> corners{{{-.8f,-.8f,0},{.8f,-.8f,0},{.4f,.8f,0},{-.8f,.8f,0}}};
    const auto geometry=quad(corners);auto world=math::translation({.1f,.05f,0})*math::scaling({.8f,.6f,2});world.at(0,1)=.25f;
    constexpr float u=.3f,v=.6f;const std::array<float,4> expected{(1-u)*(1-v),u*(1-v),u*v,(1-u)*v};
    Vec3 local;for(unsigned corner=0;corner<4;++corner)local=local+corners[corner]*expected[corner];
    const auto center=math::transformPoint(world,local);VertexPaintFootprintCache cache;
    require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,world}},{},800,600),"Could not prepare affine trapezoid interpolation.");
    auto result=visible(cache,center,.01f,.7f);
    require(!result.influences.empty()&&close(result.influences[0].amount,.7f),"Affine scale changed world radius or opacity at the brush center.");
    require(std::any_of(result.influences.begin(),result.influences.end(),[&](const auto& influence){return basisClose(influence,expected);}),"The footprint used triangular rather than inverse-bilinear trapezoid interpolation.");
    const auto out=cache.collect(center+Vec3{0,0,.02f},.01f,1,1,[](const auto&)->std::optional<bool>{throw std::runtime_error("A distant surface queried visibility.");});
    require(out.influences.empty()&&!out.missing,"The world sphere reached a nonintersecting surface.");
}
void warpedAndReversedTriangles() {
    const auto geometry=quad({Vec3{-.8f,-.8f,0},Vec3{.8f,-.8f,0},Vec3{.8f,.8f,.2f},Vec3{-.8f,.8f,0}});
    // The actual triangle surface has z=.2*u in the upper-left triangle,
    // while the mask still uses the GPU's projected logical-quad basis.
    VertexPaintFootprintCache cache;require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},400,400),"Could not prepare a warped quad.");
    const auto result=visible(cache,{-.32f,.16f,.06f});const std::array<float,4> expected{.28f,.12f,.18f,.42f};
    require(!result.influences.empty(),"A warped quad interior did not produce a footprint.");
    require(std::any_of(result.influences.begin(),result.influences.end(),[&](const auto& influence){return basisClose(influence,expected);}),"Warped-quad paint departed from GPU local-space basis.");
    auto triangle=std::make_shared<MeshPaintGeometry>();triangle->vertices.resize(3);
    triangle->vertices[0].position={-.8f,-.8f,0};triangle->vertices[1].position={.8f,-.8f,0};triangle->vertices[2].position={0,.8f,0};
    MeshPaintCell cell;cell.cornerCount=3;cell.vertices={0,1,2,0};cell.triangleCorners={2,1,0,0,0,0};triangle->cells.push_back(cell);
    require(cache.prepare(std::array{VertexPaintFootprintMesh{triangle,{}}},{},400,400),"Could not prepare a reversed triangle.");
    const auto reversed=visible(cache,{0,0,0});
    require(!reversed.influences.empty()&&reversed.influences[0].cornerCount==3,"A reversed triangle lost its cell footprint.");
    const std::array<float,4> triangleExpected{.25f,.25f,.5f,0};
    for(unsigned corner=0;corner<4;++corner)require(close(reversed.influences[0].basis[corner],triangleExpected[corner]),"Triangle winding changed its corner basis.");
}
void offscreenControlsAndViewRefresh() {
    VertexPaintFootprintCache cache;const auto geometry=square(10);
    require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},400,400),"Could not prepare offscreen control vertices.");
    const auto result=visible(cache,{0,0,0},.01f);
    require(!result.influences.empty(),"Visible cell interiors depended on control vertices being inside the viewport.");
    auto projection=math::translation({3,0,0});
    require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},projection,800,600),"A view-only refresh failed.");
    require(visible(cache,{0,0,0},.01f).influences.empty(),"A reused spatial hierarchy retained its old viewport projection.");
    require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},800,600),"Could not restore the view.");
    require(!visible(cache,{0,0,0},.01f).influences.empty(),"View refresh discarded immutable geometry support.");
}
void hierarchySelectivityAndInsetBounds() {
    auto geometry=std::make_shared<MeshPaintGeometry>();
    for(unsigned y=0;y<9;++y)for(unsigned x=0;x<9;++x) {
        const auto first=std::uint32_t(geometry->vertices.size());const auto center=Vec3{-.8f+.2f*x,-.8f+.2f*y,0};
        for(const auto delta:std::array<Vec3,4>{{{-.04f,-.04f,0},{.04f,-.04f,0},{.04f,.04f,0},{-.04f,.04f,0}}}){ModelVertex vertex;vertex.position=center+delta;geometry->vertices.push_back(vertex);}
        MeshPaintCell cell;cell.cornerCount=4;cell.vertices={first,first+1,first+2,first+3};geometry->cells.push_back(cell);
    }
    VertexPaintFootprintCache cache;require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},400,400),"Could not prepare a multi-branch footprint hierarchy.");
    constexpr unsigned expectedCell=3*9+6;const Vec3 center{.4f,-.2f,0};std::size_t calls{};
    const auto result=cache.collect(center,.01f,1,1,[&](const auto& sample)->std::optional<bool>{++calls;require(sample.cell==expectedCell,"The sphere query visited a nonintersecting logical cell.");require(math::length(sample.world-center)<=.00501,"An inset sample exceeded half of the world brush radius.");return true;});
    require(!result.missing&&!result.influences.empty()&&calls<=4,"The bounded hierarchy lost the visible cell or repeated unbounded samples.");
    for(const auto& group:result.influences)require(group.cell==expectedCell,"A distant cell contributed paint controls.");
}
void occlusionUnknownAndInvalidInput() {
    auto geometry=square(.8f);auto extra=geometry->cells.front();extra.vertices={4,5,6,7};
    const auto original=geometry->vertices;geometry->vertices.insert(geometry->vertices.end(),original.begin(),original.end());geometry->cells.push_back(extra);
    VertexPaintFootprintCache cache;require(cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},400,400),"Could not prepare overlapping visibility fixtures.");
    std::size_t calls{};auto result=cache.collect({0,0,0},.05f,1,1,[&](const auto&)->std::optional<bool>{if(++calls==2)return {};return true;});
    require(calls==2&&result.missing&&result.influences.empty(),"Unknown footprint visibility published part of a batch.");
    result=cache.collect({0,0,0},.05f,1,1,[](const auto&)->std::optional<bool>{return false;});
    require(!result.missing&&result.influences.empty(),"Occluded footprint controls received paint.");
    const auto zero=cache.collect({0,0,0},.05f,0,1,[](const auto&)->std::optional<bool>{throw std::runtime_error("Zero strength queried visibility.");});
    require(zero.influences.empty()&&!zero.missing,"Zero strength produced a footprint.");
    const auto degenerate=quad({Vec3{0,0,0},Vec3{0,0,0},Vec3{0,0,0},Vec3{0,0,0}});
    require(cache.prepare(std::array{VertexPaintFootprintMesh{degenerate,{}}},{},400,400),"Degenerate cells invalidated otherwise valid cache preparation.");
    require(visible(cache,{0,0,0}).influences.empty(),"A degenerate cell produced controlling weights.");
    require(!cache.prepare(std::array{VertexPaintFootprintMesh{geometry,{}}},{},0,400)&&visible(cache,{0,0,0}).influences.empty(),"Invalid preparation retained a stale footprint cache.");
}
}
int main(){try{coarseInteriorAndExactCorner();trapezoidAffineBasis();warpedAndReversedTriangles();offscreenControlsAndViewRefresh();hierarchySelectivityAndInsetBounds();occlusionUnknownAndInvalidInput();std::cout<<"Vertex paint coarse-cell footprints, quad basis, world radius, visibility and view refresh passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
