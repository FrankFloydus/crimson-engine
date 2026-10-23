#include "DecalResolve.h"
#include <crimson/renderer/forward/MaterialGraph.h>
#include <cmath>
#include <algorithm>
namespace crimson::decals {
Mat4 surfacePlacementFrame(Vec3 point,Vec3 normal) {
    const auto z=math::normalized(normal,{0,0,1});
    const bool horizontal=std::abs(z[1])>.95f;
    const Vec3 reference=horizontal?Vec3{1,0,0}:Vec3{0,1,0};
    const auto x=horizontal?math::normalized(reference-z*math::dot(reference,z),{1,0,0}):math::normalized(math::cross(reference,z),{1,0,0});
    const auto y=math::normalized(math::cross(z,x),{0,1,0});
    Mat4 result;
    for(size_t axis=0;axis<3;++axis){result.at(axis,0)=x[axis];result.at(axis,1)=y[axis];result.at(axis,2)=z[axis];result.at(axis,3)=point[axis];}
    return result;
}
Mat4 reanchorDecalFrame(const Mat4& previousWorld,Vec3 previousNormal,const Mat4& newFrame) {
    const auto oldBasis=surfacePlacementFrame({},previousNormal);
    const auto newBasis=surfacePlacementFrame({},math::transformDirection(newFrame,{0,0,1}));
    const auto inverseOld=math::inverse(oldBasis);
    Mat4 result=newBasis*(*inverseOld)*previousWorld;
    const auto point=math::transformPoint(newFrame,{});
    for(size_t axis=0;axis<3;++axis)result.at(axis,3)=point[axis];
    return result;
}
bool receiverPolicyAllows(const SceneDecalReceiverPolicy&p,uint64_t seed,uint64_t owner,uint64_t candidate,uint64_t candidateOwner){
    if(!candidate||std::find(p.excluded.begin(),p.excluded.end(),candidate)!=p.excluded.end())return false;
    if(candidate==seed||std::find(p.included.begin(),p.included.end(),candidate)!=p.included.end())return true;
    return p.scope==SceneDecalReceiverScope::SeedQuaderScene&&owner&&candidateOwner==owner;
}
bool receiverMaterialEligible(const ModelMaterial&m,std::string* diagnostic){
    const auto reject=[&](const char*s){if(diagnostic)*diagnostic=s;return false;};
    if(m.graph&&!m.graph->empty()){
        const auto found=std::find_if(m.graph->nodes.begin(),m.graph->nodes.end(),[](const auto&node){return node.kind==MaterialNodeKind::Output;});
        if(found==m.graph->nodes.end())return reject("Invalid receiver material output.");const auto&output=found->output;
        if(output.shading!=MaterialShading::StandardPbr)return reject("Unlit receivers are unsupported.");
        if(output.blend!=MaterialBlend::Opaque&&output.blend!=MaterialBlend::Mask)return reject("Transparent receivers are unsupported.");
        if(output.tessellation.enabled)return reject("Displaced receivers are unsupported.");
        if(output.opacity<1)return reject("Partially faded receivers are unsupported.");
        const auto id=found->id;
        for(const auto&link:m.graph->links)if(link.toNode==id&&link.toPin==MaterialPin::Opacity)return reject("Runtime opacity receivers are unsupported.");
    }else if(m.alphaBlend)return reject("Transparent receivers are unsupported.");
    if(diagnostic)diagnostic->clear();return true;
}
SeedResolution resolveAuthoredSeed(const ModelSurface&surface,const SceneDecalSeed&seed,const Mat4&world,const Mat4&frame){
    SeedResolution result;
    if(!seed.receiver||!math::isAffine(frame)||!math::inverse(world)){result.diagnostic="Missing or invalid surface frame.";return result;}
    const Vec3 center=math::transformPoint(frame,{}),x=math::transformDirection(frame,{1,0,0}),y=math::transformDirection(frame,{0,1,0});
    const float limit=static_cast<float>(std::max(.002,.1*std::min(math::length(x),math::length(y))));
    float tolerance=limit;
    if(seed.fingerprint==surface.fingerprint){const Vec3 previous=math::transformPoint(world,seed.localPoint);if(math::length(previous-center)<1e-5)tolerance=0;}
    auto hit=resolveModelSurfaceHit(surface,seed.receiver,world,seed.draw,seed.primitive,center,tolerance);
    result.status=hit.status;result.hit=hit.hit;
    if(!hit.hit){result.diagnostic=hit.status==SurfaceHitStatus::Ambiguous?"Ambiguous surface; place the decal again.":"Missing surface; place the decal again.";return result;}
    const auto normal=math::normalMatrix(world);
    if(!normal||math::dot(math::normalized(math::transformDirection(*normal,seed.localNormal)),math::normalized(math::transformDirection(*normal,hit.hit->localNormal)))<.5){result.hit.reset();result.status=SurfaceHitStatus::Missing;result.diagnostic="Surface orientation changed; place the decal again.";}
    return result;
}
}
