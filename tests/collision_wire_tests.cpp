#include "editor/MeshCollisionOverlay.h"
#include "physics/ColliderGeometry.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void unitNormals(const CollisionDebugEdge& edge) {
    require(math::finite(edge.normalA)&&math::finite(edge.normalB)&&std::abs(math::length(edge.normalA)-1)<1e-5&&
        std::abs(math::length(edge.normalB)-1)<1e-5,"An edge lost its finite unit adjacent surface normals.");
}
const CollisionDebugEdge& sharedEdge(const CollisionDebugMesh& mesh) {
    const auto found=std::find_if(mesh.edges.begin(),mesh.edges.end(),[](const auto& edge){return edge[0]==0&&edge[1]==1;});
    require(found!=mesh.edges.end(),"The shared crease was lost.");return *found;
}
StaticTriangleMesh quad() {
    StaticTriangleMesh result;result.positions={{0,0,0},{1,0,0},{1,1,0},{0,0,0},{1,1,0},{0,1,0}};
    result.indices={0,1,2,3,4,5};return result;
}
void boxesAndSplitSeams() {
    const auto& box=boxCollisionMesh();
    require(box.triangles.size()==12&&box.edges.size()==12&&box.silhouetteEdges.empty(),"Box must keep all fill faces and exactly twelve wire edges.");
    for(const auto& edge:box.edges) {
        unsigned differing{};for(unsigned axis=0;axis<3;++axis)differing+=box.wireVertices[edge[0]][axis]!=box.wireVertices[edge[1]][axis];
        require(differing==1,"Box wire contains a face diagonal.");
        unitNormals(edge);require(std::abs(math::dot(edge.normalA,edge.normalB))<1e-5&&!edge.concave&&!edge.planeFallback,"Box edges lost their convex perpendicular surface planes.");
    }
    StaticTriangleMesh split;
    for(const auto& triangle:box.triangles)for(const auto index:triangle){split.indices.push_back(static_cast<std::uint32_t>(split.positions.size()));split.positions.push_back(box.vertices[index]);}
    const auto debug=buildMeshCollisionDebug(split);
    require(debug.vertices==split.positions&&debug.triangles.size()==12&&debug.wireVertices.size()==8&&debug.edges.size()==12&&debug.silhouetteEdges.empty(),
        "Split UV/hard-normal seams changed fill data or produced duplicate/diagonal box wires.");
    for(const auto& edge:debug.edges){unitNormals(edge);require(!edge.concave,"An outward-wound split Box crease was classified as concave.");}
    auto plane=quad();plane.positions[3][0]=4e-7f;plane.positions[4][0]+=4e-7f;
    const auto planar=buildMeshCollisionDebug(plane);
    require(planar.vertices==plane.positions&&planar.triangles.size()==2&&planar.wireVertices.size()==4&&planar.edges.size()==4&&planar.silhouetteEdges.empty(),
        "Near-coincident geometric seams did not weld or coplanar triangulation remained visible.");
    for(const auto& edge:planar.edges) {
        unitNormals(edge);require(edge.normalA==Vec3{0,0,1}&&edge.normalB==edge.normalA&&!edge.concave&&!edge.planeFallback,
            "An open welded boundary did not preserve its single surface plane.");
    }
    plane.indices.insert(plane.indices.end(),{2,1,0,5,4,3});
    const auto doubled=buildMeshCollisionDebug(plane);
    require(doubled.triangles.size()==4&&doubled.edges.size()==4,"Repeated/reversed fill faces created false nonmanifold wire edges.");
}
void creasesBoundariesAndNonmanifold() {
    StaticTriangleMesh source;source.positions={{0,0,0},{1,0,0},{0,1,0},{0,0,1}};source.indices={0,1,2,1,0,3};
    const auto crease=buildMeshCollisionDebug(source);
    require(crease.edges.size()==5&&crease.silhouetteEdges.empty(),"A significant crease or open boundary was hidden.");
    const auto& concave=sharedEdge(crease);unitNormals(concave);
    require(concave.normalA==Vec3{0,0,1}&&concave.normalB==Vec3{0,1,0}&&concave.concave&&!concave.planeFallback,
        "A concave crease did not retain both original adjacent normals and its signed wedge classification.");
    auto convexSource=source;convexSource.positions[3][2]=-1;
    const auto convexMesh=buildMeshCollisionDebug(convexSource);const auto& convex=sharedEdge(convexMesh);unitNormals(convex);
    require(convex.normalA==Vec3{0,0,1}&&convex.normalB==Vec3{0,-1,0}&&!convex.concave&&!convex.planeFallback,
        "A convex crease was classified like a reentrant wedge.");
    auto reordered=source;reordered.indices={1,0,3,0,1,2};
    require(sharedEdge(buildMeshCollisionDebug(reordered)).concave,"Concavity depended on which adjacent triangle was encountered first.");
    source.positions.push_back({0,-1,0});source.indices.insert(source.indices.end(),{0,1,4});
    const auto nonmanifold=buildMeshCollisionDebug(source);
    require(nonmanifold.edges.size()==7,"A nonmanifold shared edge was dropped or duplicated.");
    const auto& ambiguous=sharedEdge(nonmanifold);unitNormals(ambiguous);
    require(ambiguous.normalA==concave.normalA&&ambiguous.normalB==concave.normalB&&!ambiguous.concave&&ambiguous.planeFallback,
        "A nonmanifold edge lost its available planes or acquired a closed-wedge concavity.");
    StaticTriangleMesh degenerate;degenerate.positions={{0,0,0},{1,0,0},{2,0,0}};degenerate.indices={0,1,2,0,0,1};
    const auto empty=buildMeshCollisionDebug(degenerate);
    require(empty.triangles.size()==2&&empty.edges.empty()&&empty.silhouetteEdges.empty(),"Degenerate fill faces generated invalid wires or were altered.");
}
void cylinderSilhouettesAndLargeIndices() {
    SceneCollider cylinder;cylinder.shape=ColliderShape::Cylinder;cylinder.radius=1;cylinder.height=2;
    const auto source=primitiveColliderMesh(cylinder);
    const auto debug=buildMeshCollisionDebug(*source);
    require(debug.edges.size()==64&&debug.silhouetteEdges.size()==32,"Cylinder wires should retain cap rims and silhouette candidates, without a facet cage.");
    for(const auto& edge:debug.edges){unitNormals(edge);require(!edge.concave,"A closed Cylinder cap rim was classified as concave.");}
    const Vec3 eye{5,0,3};unsigned silhouettes{};
    for(const auto& edge:debug.silhouetteEdges) {
        const auto midpoint=(debug.wireVertices[edge.vertices[0]]+debug.wireVertices[edge.vertices[1]])*.5f;
        const auto direction=eye-midpoint;
        silhouettes+=math::dot(edge.normalA,direction)*math::dot(edge.normalB,direction)<=0;
        unitNormals(edge);require(!edge.concave,"A smooth outward Cylinder facet was classified as concave.");
    }
    require(silhouettes==2,"Curved cylinder outline did not retain its two view-dependent sides.");
    auto plane=quad();StaticTriangleMesh large;large.positions.resize(70'000);
    for(const auto& point:plane.positions)large.positions.push_back(point);
    for(const auto index:plane.indices)large.indices.push_back(70'000+index);
    const auto wide=buildMeshCollisionDebug(large);
    require(wide.triangles.size()==2&&wide.triangles[0][0]==70'000&&wide.edges.size()==4,"Collision overlay narrowed original 32-bit indices or lost welded feature edges.");
}
void cancellation() {
    const auto source=quad();
    std::stop_source stop;stop.request_stop();bool canceled=false;
    try{(void)buildMeshCollisionDebug(source,stop.get_token());}catch(const std::runtime_error&){canceled=true;}
    require(canceled,"Canceled collision overlay preparation continued.");
}
}
int main(){try{boxesAndSplitSeams();creasesBoundariesAndNonmanifold();cylinderSilhouettesAndLargeIndices();cancellation();std::cout<<"Collision feature edges, welding, silhouettes, 32-bit indices and cancellation checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
