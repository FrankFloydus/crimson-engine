#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <algorithm>
#include <cmath>

namespace crimson::fog {
namespace equations {
inline float max(float a,float b) { return std::max(a,b); }
inline float min(float a,float b) { return std::min(a,b); }
inline float exp(float value) { return std::exp(value); }
inline float pow(float value,float exponent) { return std::pow(value,exponent); }
#define CC_FOG_CPU
#include "crimson-renderer/shaders/fog.sh"
#undef CC_FOG_CPU
}
struct Parameters {
    Vec4 colorDensity{},depth{},height{};
};
inline Parameters parameters(const FogSettings& settings,float cameraFar) {
    Parameters result;
    const auto finiteOr=[](float value,float fallback){return std::isfinite(value)?value:fallback;};
    const float energy=std::max(finiteOr(settings.lightEnergy,1.f),0.f);
    for(std::size_t i=0;i<3;++i) {
        const auto c=std::isfinite(settings.color[i])?std::clamp(settings.color[i],0.f,1.f):0.f;
        result.colorDensity[i]=(c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f))*energy;
    }
    result.colorDensity[3]=settings.enabled?std::max(finiteOr(settings.density,0.f),0.f):0.f;
    const float authoredEnd=std::max(finiteOr(settings.depthEnd,100.f),0.f);
    const float end=authoredEnd>0.f?authoredEnd:std::max(finiteOr(cameraFar,100.f),.001f);
    // Match Godot's per-camera range clamp without changing authored Start/End.
    const float begin=std::min(std::max(finiteOr(settings.depthBegin,10.f),0.f),end-.001f);
    const float curve=finiteOr(settings.depthCurve,1.f);
    result.depth={begin,end,curve>0.f?curve:1.f,std::clamp(finiteOr(settings.depthOpacity,1.f),0.f,1.f)};
    result.height={finiteOr(settings.height,0.f),finiteOr(settings.heightDensity,0.f),
        settings.mode==FogMode::Depth?1.f:0.f,settings.enabled?1.f:0.f};
    return result;
}
inline Parameters forMaterial(Parameters params,bool receiveFog) {
    // Gate both distance and height fog without changing the shared view state.
    if(!receiveFog)params.height[3]=0.f;
    return params;
}
inline float transmittance(const Parameters& params,float distance,float worldHeight) {
    return equations::fogCombinedTransmittance(params.colorDensity[3],distance,
        params.depth[0],params.depth[1],params.depth[2],params.depth[3],worldHeight,
        params.height[0],params.height[1],params.height[2],params.height[3]);
}
// Reference uses the exact scalar shader equations. Coverage is untouched;
// premultiplication follows fog, making zero-opacity sprites stay invisible.
inline Vec4 premultiplied(const Vec3& radiance,float opacity,const Parameters& params,float distance,float worldHeight,bool additive) {
    const float t=transmittance(params,distance,worldHeight);
    return {equations::fogPremultipliedChannel(equations::fogChannel(radiance[0],params.colorDensity[0],t,additive?1.f:0.f),opacity),
        equations::fogPremultipliedChannel(equations::fogChannel(radiance[1],params.colorDensity[1],t,additive?1.f:0.f),opacity),
        equations::fogPremultipliedChannel(equations::fogChannel(radiance[2],params.colorDensity[2],t,additive?1.f:0.f),opacity),opacity};
}
} // namespace crimson::fog
