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
std::optional<Mat4> conformDecalFrame(const Mat4&authored,Vec3 point,Vec3 normal) {
    if(!math::isAffine(authored)||!math::finite(point)||!math::finite(normal))return {};
    const auto x=math::transformDirection(authored,{1,0,0}),y=math::transformDirection(authored,{0,1,0}),z=math::normalized(normal);
    const double lengthX=math::length(x),lengthY=math::length(y);if(lengthX<1e-6||lengthY<1e-6)return {};
    const double shear=math::dot(x,y)/lengthX,orthogonal=std::sqrt(std::max(0.,lengthY*lengthY-shear*shear));if(orthogonal<1e-6)return {};
    auto tangent=x-z*math::dot(x,z);if(math::length(tangent)<lengthX*1e-5)tangent=math::cross(y,z);
    if(math::length(tangent)<1e-8)tangent=math::transformDirection(surfacePlacementFrame({},z),{1,0,0});tangent=math::normalized(tangent);
    const auto first=tangent*lengthX,second=tangent*shear+math::cross(z,tangent)*(orthogonal*(math::handedness(authored)<0?-1:1));
    Mat4 result;for(size_t axis=0;axis<3;++axis){result.at(axis,0)=first[axis];result.at(axis,1)=second[axis];result.at(axis,2)=z[axis];result.at(axis,3)=point[axis];}return result;
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
}
