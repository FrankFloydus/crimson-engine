#include "editor/VertexPaintSurface.h"
#include <crimson/renderer/forward/MeshPaint.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace crimson;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
ModelPrimitive plane(float z=0) {
    ModelPrimitive p;p.vertices.resize(4);
    p.vertices[0].position={-1,-1,z};p.vertices[1].position={1,-1,z};p.vertices[2].position={1,1,z};p.vertices[3].position={-1,1,z};
    p.indices={0,1,2,0,2,3};p.sourceFaces={{{0,1,2,3},{0,1},4}};return p;
}
VertexPaintSurfaceMesh occurrence(const ModelPrimitive& primitive,Mat4 world={},EntityId entity=1,std::uint32_t draw=0,std::uint32_t index=0) {
    const auto mesh=build_shadow_ray_mesh(primitive);require(bool(mesh),"No surface BVH.");
    return {mesh,world,*math::inverse(world),mesh->bounds,entity,draw,index,71,false,true};
}
void visibleSurface() {
    const auto transform=math::translation({3,.6666667f,22.75f})*math::rotation(math::axisAngle({0,1,0},1.57079632679))*math::scaling({1,1.3333334f,1.5f});
    const auto source=plane();const auto geometry=buildMeshPaintGeometry(source,4);
    const auto expanded=meshPaintRenderPrimitive(source,*geometry);
    const auto snapshot=makeVertexPaintSurfaceSnapshot({occurrence(expanded,transform,0x6f1f1f91d24b1ed1ull,2,2)},99);
    for(const Vec3 local:std::array<Vec3,5>{{{0,0,0},{.137f,-.229f,0},{-.999f,.35f,0},{1,1,0},{-.5f,-.5f,0}}}) {
        const auto point=math::transformPoint(transform,local),normal=math::normalized(math::transformDirection(*math::normalMatrix(transform),{0,0,1}));
        const auto hit=snapshot.raycast({point+normal*4,-normal});
        require(hit&&hit->exact&&hit->entity==0x6f1f1f91d24b1ed1ull&&hit->draw==2&&hit->primitive==2&&hit->resourceRevision==71,"Room-shaped affine subdivision lost exact draw identity or edge hit.");
        require(math::length(hit->worldPosition-point)<1e-5,"Affine surface hit moved away from the brush center.");
    }
}
void occlusionAndSlots() {
    const auto snapshot=makeVertexPaintSurfaceSnapshot({occurrence(plane(),{},1,0,0),occurrence(plane(1),{},1,1,1)},101);
    auto hit=snapshot.raycast({{0,0,4},{0,0,-1}});
    require(hit&&hit->draw==1&&hit->primitive==1&&std::abs(hit->worldPosition[2]-1)<1e-6,"A rear material draw won over its same-entity occluder.");
    const auto different=makeVertexPaintSurfaceSnapshot({occurrence(plane(),{},1),occurrence(plane(2),{},2,3,7)},102);
    hit=different.raycast({{0,0,4},{0,0,-1}});require(hit&&hit->entity==2&&hit->draw==3&&hit->primitive==7,"An unselected mesh failed to occlude paint.");
    require(!different.raycast({{3,0,4},{0,0,-1}}),"A ray outside the surface hit its bounding box.");
}
void coverageAndCulling() {
    auto solid=occurrence(plane());auto unsupported=occurrence(plane(1),{},3);unsupported.mesh.reset();unsupported.exact=false;
    const auto query=makeVertexPaintSurfaceSnapshot({solid,unsupported},103);
    const auto hit=query.raycast({{0,0,4},{0,0,-1}});require(hit&&hit->entity==3&&!hit->exact,"Unknown coverage allowed a hidden painted surface through.");
    require(!makeVertexPaintSurfaceSnapshot({solid},104).raycast({{0,0,-4},{0,0,1}}),"A culled back face remained paintable.");
    solid.doubleSided=true;require(bool(makeVertexPaintSurfaceSnapshot({solid},105).raycast({{0,0,-4},{0,0,1}})),"Double-sided material lost its back surface.");
    const auto coplanar=makeVertexPaintSurfaceSnapshot({occurrence(plane(),{},1,2,4),occurrence(plane(),{},1,3,5)},107);
    const auto first=coplanar.raycast({{0,0,4},{0,0,-1}});require(first&&!first->exact,"Coplanar material draw ambiguity bypassed GPU confirmation.");
    auto masked=occurrence(plane(),{},1,3,5);masked.exact=false;
    const auto ambiguous=makeVertexPaintSurfaceSnapshot({occurrence(plane(),{},1,2,4),masked},108);
    const auto unknown=ambiguous.raycast({{0,0,4},{0,0,-1}});require(unknown&&!unknown->exact,"Coplanar unknown coverage bypassed the GPU fallback.");
}
void frozenOwnership() {
    VertexPaintSurfaceSnapshot frozen;
    {auto primitive=plane();auto mesh=occurrence(primitive);frozen=makeVertexPaintSurfaceSnapshot({std::move(mesh)},106);}
    require(bool(frozen.raycast({{0,0,2},{0,0,-1}})),"A frozen stroke lost its immutable geometry owner.");
}
void transformedVerticesAndGaps() {
    const auto source=plane();const auto geometry=buildMeshPaintGeometry(source,4);const auto expanded=meshPaintRenderPrimitive(source,*geometry);
    for(unsigned turn=0;turn<24;++turn) {
        const auto world=math::translation({3,.6666667f,22.75f})*math::rotation(math::axisAngle(math::normalized({.31f,.72f,.19f}),.173+turn*.213))*math::scaling({1,1.3333334f,1.5f});
        const auto snapshot=makeVertexPaintSurfaceSnapshot({occurrence(expanded,world)},200+turn);
        const auto normal=math::normalized(math::transformDirection(*math::normalMatrix(world),{0,0,1}));
        for(const auto& vertex:geometry->vertices) {
            const auto point=math::transformPoint(world,vertex.position);const auto hit=snapshot.raycast({point+normal*4,-normal});
            require(hit&&hit->exact&&math::length(hit->worldPosition-point)<1e-5,"A rotated logical vertex or boundary missed its visible surface.");
        }
    }
    auto left=plane(),right=plane();for(auto& vertex:left.vertices)vertex.position[0]-=1.00005f;for(auto& vertex:right.vertices)vertex.position[0]+=1.00005f;
    const auto gap=makeVertexPaintSurfaceSnapshot({occurrence(left),occurrence(right,{},2)},300);
    require(!gap.raycast({{0,0,4},{0,0,-1}}),"Numerical edge tolerance closed a real 0.1 mm gap.");
    const auto layers=makeVertexPaintSurfaceSnapshot({occurrence(plane()),occurrence(plane(.00005f),{},2)},301);
    const auto closest=layers.raycast({{0,0,4},{0,0,-1}});require(closest&&closest->exact&&closest->entity==2,"Numerical tolerance skipped a nearer surface separated by 0.05 mm.");
    auto farSurface=occurrence(plane(1000));farSurface.doubleSided=true;
    const auto far=makeVertexPaintSurfaceSnapshot({farSurface},302);
    const auto eye=Vec3{0,0,.05f};
    const auto farHit=far.raycast({eye,math::normalized(Vec3{1,1,1000}-eye)});
    require(farHit&&farHit->exact,"FP32 ray direction error lost a far logical corner.");
    const auto verified=layers.raycastDraw({{0,0,4},{0,0,-1}},1,0,0);
    require(verified&&verified->entity==1&&verified->worldNormal[2]>.99f,"Verified GPU draw normal could not be recovered from its BVH.");
}
}
int main(){try{visibleSurface();occlusionAndSlots();coverageAndCulling();frozenOwnership();transformedVerticesAndGaps();std::cout<<"Vertex paint surface checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
