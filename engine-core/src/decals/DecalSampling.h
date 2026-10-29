#pragma once
#include "DecalInternal.h"

namespace crimson::decals::detail {
// The host's source extraction and the topology builder must use the same
// geometric domain. An AABB alone is deliberately conservative and can contain
// many unrelated source triangles, especially near a room's ceiling or props.
struct SamplingDomain {
    D3 seedPosition{},seedNormal{},basisX{},basisY{};
    double ax{},ay{},bx{},by{},frameDet{},scale{},radius{},tolerance{},spacing{},intrinsicRadius{};
};
inline SamplingDomain samplingDomain(const DecalBuildRequest& request,D3 seedPosition,D3 seedNormal) {
    for(float value:request.frame)if(!std::isfinite(value))throw Failure(DecalPatchStatus::InvalidInput,"Footprint frame is nonfinite.");
    if(std::abs(request.frame.at(3,0))+std::abs(request.frame.at(3,1))+std::abs(request.frame.at(3,2))+std::abs(request.frame.at(3,3)-1)>1e-6)throw Failure(DecalPatchStatus::InvalidInput,"Footprint frame must be affine.");
    SamplingDomain result;result.seedPosition=seedPosition;result.seedNormal=seedNormal;
    const D3 fx=column(request.frame,0),fy=column(request.frame,1),px=fx-seedNormal*dot(fx,seedNormal),py=fy-seedNormal*dot(fy,seedNormal);
    result.basisX=unit(px);result.basisY=cross(seedNormal,result.basisX);result.ax=dot(px,result.basisX);result.ay=dot(px,result.basisY);result.bx=dot(py,result.basisX);result.by=dot(py,result.basisY);result.frameDet=result.ax*result.by-result.ay*result.bx;result.scale=std::min(length(px),length(py));
    if(!finite(seedPosition)||!finite(seedNormal)||length(seedNormal)<.5||result.scale<1e-6||std::abs(result.frameDet)<1e-10*length(px)*length(py))throw Failure(DecalPatchStatus::InvalidInput,"Footprint has a singular surface frame.");
    const double footprintRadius=.5*std::max(length(px+py),length(px-py))*request.limits.maximumStretch;
    result.radius=footprintRadius+result.scale*.5;
    // Refinement adds samples without simplifying source geometry. Bound the
    // automatic grid for narrow footprints while preserving explicit limits.
    result.tolerance=request.limits.seamTolerance>0?request.limits.seamTolerance:std::min(.001,.001*result.scale);
    result.spacing=request.limits.sampleSpacing>0?request.limits.sampleSpacing:std::max(result.scale/8,result.radius/64);
    if(!std::isfinite(result.radius)||!std::isfinite(result.tolerance)||!std::isfinite(result.spacing)||result.tolerance<=0||result.spacing<=result.tolerance*4)throw Failure(DecalPatchStatus::InvalidInput,"Invalid sampling or seam tolerance.");
    if(result.radius/result.spacing>128)throw Failure(DecalPatchStatus::LimitExceeded,"Sampling extent exceeds the bounded grid budget.");
    result.intrinsicRadius=footprintRadius+2*result.spacing;
    return result;
}
inline double triangleDistance(D3 point,const std::array<D3,3>& triangle) {
    const D3 crossNormal=cross(triangle[1]-triangle[0],triangle[2]-triangle[0]);
    if(length(crossNormal)<1e-20)return INFINITY;
    const D3 normal=unit(crossNormal);const double height=dot(point-triangle[0],normal);const D3 projected=point-normal*height;
    Source source;source.p=triangle;const D3 bary=barycentric(source,projected);
    if(std::min({bary.x,bary.y,bary.z})>=0)return std::abs(height);
    double nearest=INFINITY;
    for(size_t i=0;i<3;++i){const D3 edge=triangle[(i+1)%3]-triangle[i];const double along=std::clamp(dot(point-triangle[i],edge)/dot(edge,edge),0.,1.);nearest=std::min(nearest,length(point-triangle[i]-edge*along));}
    return nearest;
}
}
