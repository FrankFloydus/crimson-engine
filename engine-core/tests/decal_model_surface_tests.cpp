#include "decals/DecalModelSurface.h"
#include <crimson/renderer/forward/ModelData.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace crimson;
using namespace crimson::decals;
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class Action> void fails(Action action,const char* message) { bool failed{};try{action();}catch(const std::exception&){failed=true;}check(failed,message); }
ModelAsset quad() {
    ModelAsset model;model.id="source-proof";ModelPrimitive p;
    for(const auto point:std::array<Vec3,4>{{{0,0,0},{1,0,0},{1,1,0},{0,1,0}}}) {
        ModelVertex vertex;vertex.position=point;vertex.normal={0,0,1};p.vertices.push_back(vertex);
    }
    p.indices={0,1,2,0,2,3};model.primitives.push_back(std::move(p));model.draws.push_back({0});return model;
}
Aabb box(Vec3 centre,float radius) {
    Aabb result;const Vec3 delta{radius,radius,radius};math::include(result,centre-delta);math::include(result,centre+delta);return result;
}
void immutableGeometryAndBvh() {
    auto model=quad();const auto before=model.primitives[0].vertices;
    model.draws.push_back({0,math::translation({2,0,0})});
    const auto source=buildModelSurface(model,77,SourceNormals::UseMeshNormals);
    check(source.draws.size()==2,"Per-draw source occurrences must stay distinct.");
    check(source.draws[0].surface.id.draw==0&&source.draws[1].surface.id.draw==1,"Draw provenance lost.");
    check(source.draws[1].surface.generation==77,"Source fingerprint lost.");
    check(model.primitives[0].vertices[2].position==before[2].position&&model.primitives[0].indices==std::vector<uint32_t>{0,1,2,0,2,3},"Source geometry changed.");
    const auto frame=math::translation({5,0,0})*math::scaling({-2,3,1});
    const auto target=math::transformPoint(frame*model.draws[1].transform,{.2f,.6f,0});
    check(querySurfaceTriangles(source.draws[0],frame,box(target,.001f)).empty(),"BVH query crossed a distinct draw occurrence.");
    check(!querySurfaceTriangles(source.draws[1],frame,box(target,.001f)).empty(),"Affine BVH query missed its real receiver.");
    const auto hit=resolveModelSurfaceHit(source,91,frame,1,0,target);
    check(hit.status==SurfaceHitStatus::Found&&hit.hit.has_value(),"Visible surface point did not recover barycentrics.");
    check(hit.hit->seed.receiver==ReceiverId{91,1,0},"Scene occurrence provenance lost.");
    check(math::length(hit.hit->localPoint-Vec3{2.2f,.6f,0})<1e-5,"Draw transform was applied incorrectly.");
    check(source.bytes()>sizeof(ModelSurface),"Source memory accounting omitted geometry.");
}
void originalLoaderAndNormals() {
    auto model=quad();auto geometry=std::make_shared<const ModelPrimitive>(model.primitives[0]);
    model.primitives[0].vertices.clear();model.primitives[0].indices.clear();model.draws.push_back({0,math::translation({2,0,0})});
    unsigned loads{};
    const auto source=buildModelSurface(model,1,SourceNormals::Smooth,[&](uint32_t primitive,std::stop_token){check(primitive==0,"Wrong LOD primitive.");++loads;return geometry;});
    check(loads==1&&source.draws.size()==2,"Original geometry must load once per primitive.");
    check(math::length(source.draws[0].surface.triangles[0].normals[0]-Vec3{0,0,1})<1e-5,"Smooth source normals disagree on a plane.");
    fails([&]{(void)buildModelSurface(model,1,SourceNormals::Flat);},"Stripped geometry without an original loader must fail.");
    auto normals=quad();normals.primitives[0].vertices[0].normal={1,0,0};
    const auto flat=buildModelSurface(normals,1,SourceNormals::Flat);
    check(flat.draws[0].surface.triangles[0].normals[0]==Vec3{0,0,1},"Flat receiver used authored smooth normal.");
    const auto authored=buildModelSurface(normals,1,SourceNormals::UseMeshNormals);
    check(authored.draws[0].surface.triangles[0].normals[0]==Vec3{1,0,0},"Authored receiver normal was replaced.");
}
void visiblePositionAndAmbiguity() {
    auto model=quad();auto& p=model.primitives[0];
    const auto original=p.vertices;
    for(auto vertex:original){vertex.position[2]=1;p.vertices.push_back(vertex);}
    p.indices.insert(p.indices.end(),{4,5,6,4,6,7});
    const auto source=buildModelSurface(model,1,SourceNormals::UseMeshNormals);
    // The GPU discarded a front cutout at Z=1 and returned this deeper same-slot surface.
    const auto deeper=resolveModelSurfaceHit(source,5,Mat4{},0,0,{.2f,.6f,0});
    check(deeper.hit&&deeper.hit->seed.triangle<2,"Surface resolver substituted a nearer masked triangle.");
    const auto front=resolveModelSurfaceHit(source,5,Mat4{},0,0,{.2f,.6f,1});
    check(front.hit&&front.hit->seed.triangle>=2,"Visible front surface lost its triangle identity.");
    const auto edge=resolveModelSurfaceHit(source,5,Mat4{},0,0,{.5f,.5f,0});
    check(edge.hit&&edge.status==SurfaceHitStatus::Found,"A shared triangulation edge should have a deterministic seed.");
    p.indices.insert(p.indices.end(),{0,1,2});
    const auto overlapping=buildModelSurface(model,2,SourceNormals::UseMeshNormals);
    const auto ambiguous=resolveModelSurfaceHit(overlapping,5,Mat4{},0,0,{.7f,.2f,0});
    check(ambiguous.status==SurfaceHitStatus::Ambiguous&&!ambiguous.hit,"Overlapping interior triangles must not invent a seed.");
    check(resolveModelSurfaceHit(source,5,Mat4{},9,0,{.2f,.6f,0}).status==SurfaceHitStatus::Missing,"Invalid draw unexpectedly resolved.");
}
void invalidAndUnsupported() {
    auto model=quad();model.primitives[0].hasSkinning=true;
    check(buildModelSurface(model,1,SourceNormals::Flat).draws.empty(),"Skinned source admitted as static geometry.");
    model=quad();model.primitives[0].indices[2]=99;
    fails([&]{(void)buildModelSurface(model,1,SourceNormals::Flat);},"Invalid triangle index admitted.");
    model=quad();model.draws[0].transform=math::scaling({0,1,1});
    fails([&]{(void)buildModelSurface(model,1,SourceNormals::Flat);},"Singular source draw admitted.");
    model=quad();std::stop_source cancellation;cancellation.request_stop();
    fails([&]{(void)buildModelSurface(model,1,SourceNormals::Flat,{},cancellation.get_token());},"Cancelled source preparation continued.");
}
DecalSurfaceSnapshot snapshot(const ModelSurface& source,const Mat4& world,const DecalSourceQuery* query=nullptr) {
    DecalSurfaceSnapshot result;
    for(const auto& draw:source.draws){auto receiver=draw.surface;receiver.id.entity=91;receiver.localToWorld=world*receiver.localToWorld;
        if(query){receiver.triangles.clear();for(const auto index:querySurfaceTriangles(draw,world,*query))receiver.triangles.push_back(draw.surface.triangles[index]);}
        if(!receiver.triangles.empty())result.receivers.push_back(std::move(receiver));}
    return result;
}
size_t triangleCount(const DecalSurfaceSnapshot& source) {size_t count{};for(const auto& receiver:source.receivers)count+=receiver.triangles.size();return count;}
void samePatch(const DecalPatch& a,const DecalPatch& b) {
    check(a.status==b.status&&a.chunks.size()==b.chunks.size()&&a.statistics.inputTriangles==b.statistics.inputTriangles&&a.statistics.outputTriangles==b.statistics.outputTriangles,"Geometric source selection changed the chart or accepted source count.");
    for(size_t chunk=0;chunk<a.chunks.size();++chunk){const auto& x=a.chunks[chunk];const auto& y=b.chunks[chunk];check(x.receiver==y.receiver&&x.generation==y.generation&&x.indices==y.indices&&x.vertices.size()==y.vertices.size(),"Geometric source selection lost receiver provenance.");
        for(size_t i=0;i<x.vertices.size();++i){const auto& v=x.vertices[i];const auto& w=y.vertices[i];check(v.position==w.position&&v.baseNormal==w.baseNormal&&v.uv==w.uv&&v.sourceTriangle==w.sourceTriangle&&v.barycentrics==w.barycentrics,"Geometric source selection changed source interpolation.");}}
}
void exactPentacleWithBroadphaseClutter() {
    // Saved Main pentacle placement and dimensions. Its 4.40075 m AABB halo
    // contains dense room/prop geometry that the solver's exact frontier rejects.
    ModelAsset model;model.id="pentacle-neighborhood-proof";ModelPrimitive floor;
    for(const auto position:std::array<Vec3,4>{{{0,0,19.75f},{6,0,19.75f},{6,0,25.75f},{0,0,25.75f}}}){ModelVertex vertex;vertex.position=position;vertex.normal={0,1,0};floor.vertices.push_back(vertex);}
    floor.indices={0,2,1,0,3,2};model.primitives.push_back(floor);model.draws.push_back({0});
    ModelPrimitive ceiling;constexpr uint32_t side=184;
    for(uint32_t z=0;z<=side;++z)for(uint32_t x=0;x<=side;++x){ModelVertex vertex;vertex.position={6.f*x/side,4.1f,19.75f+6.f*z/side};vertex.normal={0,-1,0};ceiling.vertices.push_back(vertex);}
    for(uint32_t z=0;z<side;++z)for(uint32_t x=0;x<side;++x){const auto a=z*(side+1)+x,b=a+1,c=a+side+1,d=c+1;ceiling.indices.insert(ceiling.indices.end(),{a,b,d,a,d,c});}
    model.primitives.push_back(std::move(ceiling));model.draws.push_back({1});const auto source=buildModelSurface(model,9,SourceNormals::UseMeshNormals);
    const Vec3 center{3.0981249809265137f,0,21.585826873779297f};const auto hit=projectModelSurfaceHit(source,91,Mat4{},center,.005f);check(hit.hit.has_value(),"Pentacle seed is unavailable.");
    DecalBuildRequest request;request.seed=hit.hit->seed;request.frame=Mat4{3.180000066757202f,0,0,0,0,0,-3.180000066757202f,0,0,1,0,0,center[0],center[1],center[2],1};
    const auto query=decalSourceQuery(source,Mat4{},request);const Vec3 extent{query.radius,query.radius,query.radius};size_t broadCount{};
    for(const auto& draw:source.draws)broadCount+=querySurfaceTriangles(draw,Mat4{},{center-extent,center+extent,true}).size();
    check(broadCount>request.limits.maximumInputTriangles,"Pentacle fixture did not reproduce the old premature input rejection.");
    const auto filtered=snapshot(source,Mat4{},&query);check(triangleCount(filtered)==2,"Unrelated broadphase geometry consumed the exact source budget.");
    const auto unfilteredPatch=buildDecalPatch(snapshot(source,Mat4{}),request),filteredPatch=buildDecalPatch(filtered,request);check(filteredPatch.status==DecalPatchStatus::Ready,"Exact pentacle footprint failed despite a small receiving surface.");samePatch(unfilteredPatch,filteredPatch);
    auto strict=request;strict.limits.maximumInputTriangles=1;check(buildDecalPatch(filtered,strict).status==DecalPatchStatus::LimitExceeded,"Actual input complexity lost its explicit limit.");
    std::stop_source cancelled;cancelled.request_stop();fails([&]{(void)querySurfaceTriangles(source.draws[0],Mat4{},query,cancelled.get_token());},"Geometric source query ignored cancellation.");
}
void affineContactSelection() {
    auto model=quad();auto crossing=model.primitives[0];for(auto& vertex:crossing.vertices)vertex.position={.6f,vertex.position[0],vertex.position[1]-.5f};model.primitives.push_back(crossing);model.draws.push_back({1});
    // Also retain a coincident source. It must remain an ambiguity barrier,
    // rather than being removed as an apparently redundant coplanar mesh.
    model.primitives.push_back(model.primitives[0]);model.draws.push_back({2});
    const auto world=math::translation({-4,2,13})*math::scaling({-2,3,.75f});const auto source=buildModelSurface(model,11,SourceNormals::UseMeshNormals);
    DecalBuildRequest request;request.seed={{91,0,0},1,{.2f,.3f,.5f}};request.frame=world*math::translation({.3f,.8f,0})*math::scaling({2,2,1});
    const auto query=decalSourceQuery(source,world,request);const auto all=snapshot(source,world),filtered=snapshot(source,world,&query);
    check(triangleCount(filtered)==triangleCount(all),"Affine source query removed finite contacts or coplanar multiplicity.");samePatch(buildDecalPatch(all,request),buildDecalPatch(filtered,request));
}
}
int main() {
    try {
        immutableGeometryAndBvh();originalLoaderAndNormals();visiblePositionAndAmbiguity();invalidAndUnsupported();exactPentacleWithBroadphaseClutter();affineContactSelection();
        std::cout<<"decal_model_surface: source provenance, affine BVH, visible masked-point recovery and invalidation proofs passed\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
