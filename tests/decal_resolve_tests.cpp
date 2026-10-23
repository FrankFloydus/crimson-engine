#include "decals/DecalResolve.h"
#include <crimson/renderer/forward/MaterialGraph.h>
#include <iostream>
#include <stdexcept>

using namespace crimson;
using namespace crimson::decals;
namespace {
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
ModelAsset plane() {
    ModelAsset model;model.id="seed-proof";ModelPrimitive primitive;
    for(Vec3 point:std::array<Vec3,4>{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}}) {
        ModelVertex vertex;vertex.position=point;vertex.normal={0,0,1};primitive.vertices.push_back(vertex);
    }
    primitive.indices={0,1,2,0,2,3};model.primitives.push_back(std::move(primitive));model.draws.push_back({0});return model;
}
SceneDecalSeed seedAt(const ModelSurface&surface,Vec3 point) {
    const auto hit=resolveModelSurfaceHit(surface,91,Mat4{},0,0,point);
    check(hit.hit.has_value(),"Fixture seed did not resolve.");SceneDecalSeed seed;
    seed.receiver=91;seed.fingerprint=surface.fingerprint;seed.draw=0;seed.primitive=0;
    seed.triangle=hit.hit->seed.triangle;seed.barycentric=hit.hit->seed.barycentrics;
    seed.localPoint=hit.hit->localPoint;seed.localNormal=hit.hit->localNormal;return seed;
}
void receiverPolicy() {
    SceneDecalReceiverPolicy policy;
    check(receiverPolicyAllows(policy,10,0,10,0),"Ordinary seed was excluded.");
    check(!receiverPolicyAllows(policy,10,0,11,0),"Ordinary meshes unexpectedly share a receiver set.");
    check(receiverPolicyAllows(policy,10,4,11,4),"Separate meshes in the same Quader import were excluded.");
    check(!receiverPolicyAllows(policy,10,4,11,5),"Receiver policy crossed Quader occurrences.");
    policy.included={19};check(receiverPolicyAllows(policy,10,4,19,0),"Explicit included peer was excluded.");
    policy.excluded={10,11,19};
    check(!receiverPolicyAllows(policy,10,4,10,4)&&!receiverPolicyAllows(policy,10,4,11,4)&&!receiverPolicyAllows(policy,10,4,19,0),"Exclusion must override seed, Quader, and explicit inclusion.");
    policy={};policy.scope=SceneDecalReceiverScope::Explicit;
    check(receiverPolicyAllows(policy,10,4,10,4)&&!receiverPolicyAllows(policy,10,4,11,4),"Explicit policy did not retain seed-only default.");
}
void materialEligibility() {
    ModelMaterial material;check(receiverMaterialEligible(material),"Opaque imported material excluded.");
    material.alphaMask=true;check(receiverMaterialEligible(material),"Masked imported material excluded.");
    material.alphaBlend=true;check(!receiverMaterialEligible(material),"Imported blend material admitted.");
    auto graph=std::make_shared<MaterialGraph>();MaterialNode output;output.id=7;output.kind=MaterialNodeKind::Output;graph->nodes.push_back(output);material.graph=graph;
    check(receiverMaterialEligible(material),"Native opaque graph should own blend semantics.");
    graph->nodes[0].output.blend=MaterialBlend::Mask;check(receiverMaterialEligible(material),"Native masked graph excluded.");
    graph->nodes[0].output.blend=MaterialBlend::Alpha;check(!receiverMaterialEligible(material),"Native transparent graph admitted.");
    graph->nodes[0].output.blend=MaterialBlend::Opaque;graph->nodes[0].output.shading=MaterialShading::Unlit;check(!receiverMaterialEligible(material),"Unlit graph admitted.");
    graph->nodes[0].output.shading=MaterialShading::StandardPbr;graph->nodes[0].output.tessellation.enabled=true;check(!receiverMaterialEligible(material),"Displaced graph admitted.");
    graph->nodes[0].output.tessellation.enabled=false;graph->nodes[0].output.opacity=.5f;check(!receiverMaterialEligible(material),"Faded opaque graph admitted.");
    graph->nodes[0].output.opacity=1;graph->links.push_back({1,99,7,MaterialPin::Value,MaterialPin::Opacity});
    check(!receiverMaterialEligible(material),"Runtime opacity graph admitted.");
    graph->links[0].toPin=MaterialPin::OpacityMask;check(receiverMaterialEligible(material),"Masked coverage incorrectly treated as runtime opacity.");
}
void boundedSameOccurrenceRebind() {
    auto model=plane();auto surface=buildModelSurface(model,1,SourceNormals::UseMeshNormals);
    const Vec3 point{.4f,-.2f,0};const auto seed=seedAt(surface,point);const Mat4 frame=math::translation(point);
    const auto exact=resolveAuthoredSeed(surface,seed,Mat4{},frame);
    check(exact.hit&&exact.hit->seed.triangle==seed.triangle,"Saved seed did not resolve against unchanged geometry.");
    check(!resolveAuthoredSeed(surface,seed,math::translation({5,0,0}),frame).hit,"World-anchored decal silently followed a moved receiver.");
    const Mat4 moved=math::translation({2,3,4})*math::scaling({-2,3,1});
    check(resolveAuthoredSeed(surface,seed,moved,moved*frame).hit.has_value(),"A shared affine parent move lost the same receiver.");
    auto changed=surface;changed.fingerprint=2;
    for(auto&triangle:changed.draws[0].surface.triangles)triangle.triangle+=7;
    const auto rebound=resolveAuthoredSeed(changed,seed,Mat4{},frame);
    check(rebound.hit&&rebound.hit->seed.triangle>=7,"Changed geometry reused stale triangle indices instead of bounded point matching.");
    check(!resolveAuthoredSeed(changed,seed,Mat4{},math::translation(point+Vec3{0,0,.2f})).hit,"Rebind crossed the bounded receiver distance.");
    for(auto&triangle:changed.draws[0].surface.triangles)for(auto&normal:triangle.normals)normal={0,0,-1};
    check(!resolveAuthoredSeed(changed,seed,Mat4{},frame).hit,"Rebind jumped to an opposing surface.");
}
void ambiguousRebindStops() {
    auto model=plane();const auto original=buildModelSurface(model,1,SourceNormals::UseMeshNormals);
    const auto seed=seedAt(original,{.4f,-.2f,0});model.primitives[0].indices.insert(model.primitives[0].indices.end(),{0,1,2});
    const auto overlapping=buildModelSurface(model,2,SourceNormals::UseMeshNormals);
    const auto ambiguous=resolveAuthoredSeed(overlapping,seed,Mat4{},math::translation(seed.localPoint));
    check(!ambiguous.hit&&ambiguous.status==SurfaceHitStatus::Ambiguous,"Ambiguous changed geometry silently rebound to one overlapping sheet.");
}
void reanchorRetainsAffineFootprint() {
    const Vec3 oldNormal=math::normalized({.2f,.1f,1}),newNormal={0,1,0};
    const auto oldBasis=surfacePlacementFrame({},oldNormal);
    Mat4 residual=math::eulerRotationXYZ({0,0,.43f})*math::scaling({-2,3,.7f});
    residual.at(0,1)+=.6f;residual.at(1,2)-=.2f;
    Mat4 authored=oldBasis*residual;authored.at(0,3)=4;authored.at(1,3)=5;authored.at(2,3)=6;
    const auto target=surfacePlacementFrame({-2,8,1},newNormal);
    const auto anchored=reanchorDecalFrame(authored,oldNormal,target);
    check(math::length(math::transformPoint(anchored,{})-Vec3{-2,8,1})<1e-5,"Explicit reanchor did not replace translation.");
    check(math::handedness(anchored)==math::handedness(authored),"Explicit reanchor lost authored reflection.");
    const auto inverseNew=math::inverse(surfacePlacementFrame({},newNormal));
    const auto recovered=(*inverseNew)*anchored;
    for(size_t row=0;row<3;++row)for(size_t column=0;column<3;++column)
        check(std::abs(recovered.at(row,column)-residual.at(row,column))<2e-5,"Explicit reanchor lost residual rotation, shear, or scale.");
    for(size_t i=0;i<3;++i)for(size_t j=0;j<3;++j){
        const Vec3 beforeI{authored.at(0,i),authored.at(1,i),authored.at(2,i)},beforeJ{authored.at(0,j),authored.at(1,j),authored.at(2,j)};
        const Vec3 afterI{anchored.at(0,i),anchored.at(1,i),anchored.at(2,i)},afterJ{anchored.at(0,j),anchored.at(1,j),anchored.at(2,j)};
        check(std::abs(math::dot(beforeI,beforeJ)-math::dot(afterI,afterJ))<2e-5,"Explicit reanchor changed the affine Gram matrix.");
    }
}
}
int main() {
    try {receiverPolicy();materialEligibility();boundedSameOccurrenceRebind();ambiguousRebindStops();reanchorRetainsAffineFootprint();
        std::cout<<"decal_resolve: receiver policy, PBR coverage eligibility, affine anchored seed recovery and ambiguous rebind proofs passed\n";return 0;
    } catch(const std::exception&error){std::cerr<<error.what()<<'\n';return 1;}
}
