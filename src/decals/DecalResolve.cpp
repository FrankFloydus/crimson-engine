#include "DecalResolve.h"
#include <crimson/renderer/forward/MaterialGraph.h>
#include <cmath>
#include <algorithm>
namespace crimson::decals {
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
    if(!math::finite(point))return {};
    auto result=decalAuthoringFrame(authored,normal);if(!result)return {};
    for(size_t axis=0;axis<3;++axis)result->at(axis,3)=point[axis];
    return result;
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
