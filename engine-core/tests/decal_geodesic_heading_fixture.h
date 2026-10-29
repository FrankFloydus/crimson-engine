#pragma once
#include "decals/DecalExactGeodesics.h"
#include "decals/DecalGeodesicGeometry.h"
#include <geometrycentral/surface/manifold_surface_mesh.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>
#include <sstream>

namespace crimson::decals::test {
namespace geodesic_heading_fixture {
namespace gs=geometrycentral::surface;
namespace gc=geometrycentral;
using D2=std::array<double,2>;
inline void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Input {
    std::vector<std::vector<size_t>> faces;
    std::vector<D2> coordinates;
    std::map<std::pair<unsigned,unsigned>,size_t> ids;
    size_t vertex(unsigned x,unsigned y){const auto key=std::pair{x,y};const auto [found,added]=ids.try_emplace(key,coordinates.size());if(added)coordinates.push_back({x*.25,y*.25});return found->second;}
    void cell(unsigned x,unsigned y){const size_t a=vertex(x,y),b=vertex(x+1,y),c=vertex(x+1,y+1),d=vertex(x,y+1);
        if((x+y)&1){faces.push_back({a,b,d});faces.push_back({b,c,d});}else{faces.push_back({a,b,c});faces.push_back({a,c,d});}}
};
struct Alignment {
    double xx{},xy{},yx{},yy{};
    D2 apply(gc::Vector2 p)const{return {xx*p.x+xy*p.y,yx*p.x+yy*p.y};}
};
inline Alignment alignment(gs::ManifoldSurfaceMesh& mesh,detail::DecalGeodesicGeometry& geometry,size_t seed,const std::vector<D2>& coordinates){
    geometry.requireHalfedgeVectorsInVertex();
    bool first=true;gc::Vector2 a{};D2 u{};
    for(gs::Halfedge edge:mesh.vertex(seed).outgoingHalfedges()){
        const auto b=geometry.halfedgeVectorsInVertex[edge];const auto target=coordinates[edge.tipVertex().getIndex()];const D2 v{target[0]-coordinates[seed][0],target[1]-coordinates[seed][1]};
        if(first){a=b;u=v;first=false;continue;}const double determinant=a.x*b.y-a.y*b.x;if(std::abs(determinant)<1e-12)continue;
        return {(u[0]*b.y-v[0]*a.y)/determinant,(v[0]*a.x-u[0]*b.x)/determinant,
                (u[1]*b.y-v[1]*a.y)/determinant,(v[1]*a.x-u[1]*b.x)/determinant};
    }
    throw std::runtime_error("Geodesic heading fixture has a degenerate source tangent.");
}
inline void reindex(Input& input,unsigned variant){
    if(variant&1){for(auto& face:input.faces)std::reverse(face.begin(),face.end());std::reverse(input.faces.begin(),input.faces.end());}
    if(variant&2){const size_t last=input.coordinates.size()-1;for(auto& face:input.faces)for(auto& index:face)index=last-index;for(auto& [key,index]:input.ids){(void)key;index=last-index;}std::reverse(input.coordinates.begin(),input.coordinates.end());}
}
inline gc::Vector3 rigid(gc::Vector3 p,unsigned variant){
    // Integer orthogonal maps preserve the analytic metric exactly while
    // changing embedded normals, source edge direction and native edge IDs.
    if(variant&1)p={p.z,-p.x,p.y};if(variant&2)p={-p.x,p.y,p.z};return p+gc::Vector3{3,-2,7};
}
inline void foldedStrip(unsigned panels,unsigned variant){
    Input input;for(unsigned x=0;x<panels*4;++x)for(unsigned y=0;y<4;++y)input.cell(x,y);reindex(input,variant);
    gs::ManifoldSurfaceMesh mesh(input.faces);gs::VertexData<gc::Vector3> positions(mesh);
    for(gs::Vertex vertex:mesh.vertices()){const auto [u,v]=input.coordinates[vertex.getIndex()];gc::Vector3 p;
        if(u<=1)p={u,v,0};else if(u<=2)p={1,v,u-1};else if(u<=3)p={3-u,v,1};else p={0,v,4-u};positions[vertex]=rigid(p,variant);}
    detail::DecalGeodesicGeometry geometry(mesh,positions);const size_t seed=input.ids.at({2,2});const auto frame=alignment(mesh,geometry,seed,input.coordinates);
    detail::exact::BoundedGeo solver(mesh,geometry,{100000,10000000,10000});solver.propagateBounded(mesh.vertex(seed),8);
    for(gs::Vertex vertex:mesh.vertices()){const auto map=solver.logBounded(vertex);const auto distance=solver.distanceBounded(vertex);require(map&&distance,"Folded-strip propagation lost a reachable vertex.");const auto actual=frame.apply(*map);const auto parameter=input.coordinates[vertex.getIndex()];const D2 expected{parameter[0]-.5,parameter[1]-.5};
        require(std::abs(actual[0]-expected[0])<1e-8&&std::abs(actual[1]-expected[1])<1e-8,"MMP window heading changed an analytically unfolded strip coordinate.");
        require(std::abs(*distance-std::hypot(expected[0],expected[1]))<1e-9,"Folded-strip propagation changed the intrinsic metre distance.");}
    geometry.unrequireHalfedgeVectorsInVertex();
}
inline void concaveBoundary(unsigned variant){
    // The omitted upper-right quadrant is a real boundary. A straight segment
    // between these arm interiors leaves the domain. The shortest path passes
    // through the reentrant corner(1,1), so every target beyond that corner has
    // one frozen first heading, independently of its outgoing direction there.
    Input input;for(unsigned x=0;x<8;++x)for(unsigned y=0;y<8;++y)if(x<4||y<4)input.cell(x,y);reindex(input,variant);
    gs::ManifoldSurfaceMesh mesh(input.faces);gs::VertexData<gc::Vector3> positions(mesh);for(gs::Vertex vertex:mesh.vertices()){const auto p=input.coordinates[vertex.getIndex()];positions[vertex]=rigid({p[0],p[1],0},variant);}
    detail::DecalGeodesicGeometry geometry(mesh,positions);const size_t seed=input.ids.at({3,6});const auto frame=alignment(mesh,geometry,seed,input.coordinates);
    detail::exact::BoundedGeo solver(mesh,geometry,{100000,10000000,10000});solver.propagateBounded(mesh.vertex(seed),8);
    const D2 first{.25,-.5};const double firstLength=std::hypot(first[0],first[1]);
    for(const auto grid:std::array<std::pair<unsigned,unsigned>,4>{{{6,3},{7,2},{8,1},{8,0}}}){const size_t id=input.ids.at(grid);const auto p=input.coordinates[id];const double length=firstLength+std::hypot(p[0]-1,p[1]-1);const auto map=solver.logBounded(mesh.vertex(id));const auto distance=solver.distanceBounded(mesh.vertex(id));require(map&&distance,"Boundary-pseudosource propagation lost a reachable arm.");const auto actual=frame.apply(*map);
        require(std::abs(*distance-length)<1e-9,"MMP boundary-pseudosource distance crossed the missing quadrant.");
        require(std::abs(actual[0]-first[0]/firstLength*length)<1e-8&&std::abs(actual[1]-first[1]/firstLength*length)<1e-8,"MMP saddle window failed to preserve its seed-first heading.");}
    // A visible target in the same arm retains its ordinary straight heading.
    const size_t visible=input.ids.at({1,6});const auto direct=solver.logBounded(mesh.vertex(visible));require(bool(direct),"MMP heading fixture lost its visible control.");const auto actual=frame.apply(*direct);require(std::abs(actual[0]+.5)<1e-9&&std::abs(actual[1])<1e-9,"MMP frozen saddle heading leaked into an unrelated direct window.");
    geometry.unrequireHalfedgeVectorsInVertex();
}
inline void curvedSourceParity(unsigned variant){
    // A nonflat source fan uses geometry-central's normalized vertex tangent
    // angle, rather than one unscaled Euclidean face frame. Staggered outer
    // rings put propagated queries inside several different source windows.
    constexpr size_t sectors=12,rings=3;constexpr double pi=3.1415926535897932384626433832795;
    std::vector<gc::Vector3> embedded{{0,0,1}};std::vector<std::vector<size_t>> faces;
    const auto id=[](size_t ring,size_t sector){return 1+(ring-1)*sectors+sector%sectors;};
    for(size_t ring=1;ring<=rings;++ring)for(size_t sector=0;sector<sectors;++sector){const double radius=double(ring)*.7,angle=2*pi*(double(sector)+(ring==2?.25:0.))/sectors;embedded.push_back({radius*std::cos(angle),radius*std::sin(angle),1-radius});}
    for(size_t sector=0;sector<sectors;++sector){faces.push_back({0,id(1,sector),id(1,sector+1)});for(size_t ring=1;ring<rings;++ring){faces.push_back({id(ring,sector),id(ring+1,sector),id(ring+1,sector+1)});faces.push_back({id(ring,sector),id(ring+1,sector+1),id(ring,sector+1)});}}
    size_t source=0;if(variant&1){for(auto& face:faces)std::reverse(face.begin(),face.end());std::reverse(faces.begin(),faces.end());}if(variant&2){source=embedded.size()-1;for(auto& face:faces)for(auto& vertex:face)vertex=source-vertex;std::reverse(embedded.begin(),embedded.end());}
    gs::ManifoldSurfaceMesh mesh(faces);gs::VertexData<gc::Vector3> positions(mesh);for(gs::Vertex vertex:mesh.vertices())positions[vertex]=rigid(embedded[vertex.getIndex()],variant);
    detail::DecalGeodesicGeometry geometry(mesh,positions);geometry.requireVertexGaussianCurvatures();require(geometry.vertexGaussianCurvatures[mesh.vertex(source)]>.1,"The source-heading parity fixture lost its curved source fan.");
    detail::exact::BoundedGeo bounded(mesh,geometry,{100000,10000000,10000});bounded.propagateBounded(mesh.vertex(source),10);gs::GeodesicAlgorithmExact upstream(mesh,geometry);upstream.propagate(mesh.vertex(source));
    for(gs::Vertex vertex:mesh.vertices()){const auto actual=bounded.logBounded(vertex);const auto expected=upstream.getLog(vertex);if(!actual||!expected.isFinite()||(*actual-expected).norm()>=1e-8){std::ostringstream message;message.precision(17);message<<"MMP source-heading cone parity failed variant="<<variant<<" source="<<source<<" query="<<vertex.getIndex()<<" expected=("<<expected.x<<','<<expected.y<<")";if(actual)message<<" actual=("<<actual->x<<','<<actual->y<<") difference="<<(*actual-expected).norm()<<" lengths="<<actual->norm()<<'/'<<expected.norm();throw std::runtime_error(message.str());}const auto distance=bounded.distanceBounded(vertex);require(distance&&std::abs(*distance-expected.norm())<1e-8,"MMP source-heading windows changed the well-conditioned cone distance.");}
    geometry.unrequireVertexGaussianCurvatures();
}
}
inline void runGeodesicHeadingProofs(){for(unsigned variant=0;variant<4;++variant){geodesic_heading_fixture::foldedStrip(3,variant);geodesic_heading_fixture::foldedStrip(4,variant);geodesic_heading_fixture::concaveBoundary(variant);geodesic_heading_fixture::curvedSourceParity(variant);}}
} // namespace crimson::decals::test
