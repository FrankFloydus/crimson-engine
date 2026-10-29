#include "editor/VertexPaintSubdivisionOverlay.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class Function>void rejects(Function function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid subdivision overlay input was accepted.");}
ModelPrimitive quad() {
    ModelPrimitive source;source.vertices.resize(4);
    source.vertices[0].position={0,0,0};source.vertices[1].position={1,0,0};source.vertices[2].position={1,1,0};source.vertices[3].position={0,1,0};
    source.indices={0,1,2,0,2,3};source.sourceFaces.push_back({{0,1,2,3},{0,1},4});return source;
}
auto keys(const CollisionOverlayGeometry& overlay) {
    std::set<std::array<std::uint32_t,2>> edges;
    for(const auto& edge:overlay.edges) {
        require(edge.a<overlay.vertices.size()&&edge.b<overlay.vertices.size()&&edge.a<edge.b,"Overlay edge indices are invalid or unordered.");
        require(edges.insert({edge.a,edge.b}).second,"A shared subdivision edge was emitted twice.");
        require(!edge.silhouetteOnly,"Subdivision edges were hidden as smooth silhouettes.");
    }
    return edges;
}
void logicalGrid() {
    const auto source=quad();
    for(std::uint32_t level=0;level<=3;++level) {
        const auto geometry=buildMeshPaintGeometry(source,level);
        const auto overlay=buildVertexPaintSubdivisionOverlay(*geometry);
        const auto edges=keys(*overlay);
        const auto intervals=std::size_t(1)<<level;
        require(edges.size()==2*intervals*(intervals+1),"Quad grid contains triangulation diagonals or lost shared edges.");
        require(overlay->triangles.empty()&&overlay->wireVertices.empty(),"Subdivision grid added a surface fill or alternate positions.");
        require(overlay->vertices.size()==geometry->vertices.size(),"Subdivision grid lost logical control points.");
        for(std::size_t index=0;index<overlay->vertices.size();++index)require(overlay->vertices[index]==geometry->vertices[index].position,"Overlay changed source positions.");
        for(const auto& edge:overlay->edges) {
            const auto direction=overlay->vertices[edge.b]-overlay->vertices[edge.a];
            require(direction[0]==0||direction[1]==0,"Quad grid displayed a GPU triangle diagonal.");
            require(!edge.planeFallback&&edge.normalA[2]>.99f&&edge.normalB[2]>.99f,"Planar grid lost its incident depth planes.");
        }
    }
    auto triangulated=source;triangulated.indices={0,1,3,1,2,3};
    const auto alternate=buildVertexPaintSubdivisionOverlay(*buildMeshPaintGeometry(triangulated,0));
    require(keys(*alternate)==std::set<std::array<std::uint32_t,2>>{{0,1},{0,3},{1,2},{2,3}},"An alternate quad diagonal entered the logical grid.");
    auto triangle=source;triangle.vertices.resize(3);triangle.indices={0,1,2};triangle.sourceFaces.clear();
    require(buildVertexPaintSubdivisionOverlay(*buildMeshPaintGeometry(triangle,0))->edges.size()==3,"A genuine Level-0 triangle lost its perimeter.");
    require(buildVertexPaintSubdivisionOverlay(*buildMeshPaintGeometry(triangle,1))->edges.size()==9,"Triangle corner patches did not show their logical quad boundaries.");
}
void warpedSurfacePlanes() {
    auto source=quad();source.vertices[2].position[2]=.5f;
    const auto geometry=buildMeshPaintGeometry(source,0);const auto overlay=buildVertexPaintSubdivisionOverlay(*geometry);
    const auto first=math::normalized(math::cross(source.vertices[1].position-source.vertices[0].position,source.vertices[2].position-source.vertices[0].position));
    const auto second=math::normalized(math::cross(source.vertices[2].position-source.vertices[0].position,source.vertices[3].position-source.vertices[0].position));
    for(const auto& edge:overlay->edges) {
        const auto expected=((edge.a==0&&edge.b==3)||(edge.a==2&&edge.b==3))?second:first;
        require(math::length(edge.normalA-expected)<1e-6&&edge.normalA==edge.normalB,"Warped quad perimeter used an averaged plane instead of its actual incident triangle.");
    }
}
void validationAndCancel() {
    const auto geometry=buildMeshPaintGeometry(quad(),0);
    auto invalid=*geometry;invalid.cells[0].vertices[0]=999;rejects([&]{buildVertexPaintSubdivisionOverlay(invalid);});
    invalid=*geometry;invalid.cells[0].cornerCount=2;rejects([&]{buildVertexPaintSubdivisionOverlay(invalid);});
    invalid=*geometry;invalid.cells[0].triangleCorners[0]=9;rejects([&]{buildVertexPaintSubdivisionOverlay(invalid);});
    invalid=*geometry;invalid.vertices[0].position[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{buildVertexPaintSubdivisionOverlay(invalid);});
    std::stop_source cancellation;cancellation.request_stop();rejects([&]{buildVertexPaintSubdivisionOverlay(*geometry,cancellation.get_token());});
    require(geometry->cells[0].vertices[0]==0&&geometry->vertices[0].position==Vec3{},"Overlay preparation changed immutable subdivision data.");
}
}
int main(){try{logicalGrid();warpedSurfacePlanes();validationAndCancel();std::cout<<"Vertex paint subdivision overlay CPU checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
