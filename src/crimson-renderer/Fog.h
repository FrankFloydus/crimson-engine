#pragma once
#include "crimson-renderer/RenderTypes.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

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
    Vec4 sky{1,0,0,1}; // Sky Affect, Aerial Perspective, Sun Scatter, visual sky intensity.
    std::array<Vec4,4> sunDirections{},sunColors{};
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
    result.sky={std::clamp(finiteOr(settings.skyAffect,1.f),0.f,1.f),
        std::clamp(finiteOr(settings.aerialPerspective,0.f),0.f,1.f),
        std::clamp(finiteOr(settings.sunScatter,0.f),0.f,1.f),1};
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
inline Vec3 lightColor(const Parameters& params,const Vec3& skyRadiance,const Vec3& worldViewDirection) {
    Vec3 result{};
    for(std::size_t channel=0;channel<3;++channel)
        result[channel]=params.colorDensity[channel]*(1.f-params.sky[1])+skyRadiance[channel]*params.sky[1];
    if(params.sky[2]>0)for(std::size_t sun=0;sun<params.sunDirections.size();++sun) {
        const auto& direction=params.sunDirections[sun];
        if(direction[3]<=0)continue;
        const float cosine=std::max(worldViewDirection[0]*direction[0]+worldViewDirection[1]*direction[1]+worldViewDirection[2]*direction[2],0.f);
        const float scatter=std::pow(cosine,8.f)*std::numbers::pi_v<float>*params.sky[2];
        for(std::size_t channel=0;channel<3;++channel)result[channel]+=params.sunColors[sun][channel]*scatter;
    }
    for(auto& channel:result)channel=std::clamp(channel,0.f,60000.f);
    return result;
}
inline float backgroundOpacity(const Parameters& params,float distance,float worldHeight) {
    return equations::fogBackgroundOpacity(transmittance(params,distance,worldHeight),params.sky[0]);
}
// The camera's far distance is the background proxy distance. Hidden-source
// RGB is already premultiplied by fog opacity; alpha retains the clear matte.
inline Vec4 background(const Parameters& params,const Vec3& radiance,bool visibleSource,float distance,
    float worldHeight=0,const Vec3& worldViewDirection={0,0,-1}) {
    const float opacity=backgroundOpacity(params,distance,worldHeight);
    const auto color=lightColor(params,radiance,worldViewDirection);
    Vec4 result{0,0,0,visibleSource?1.f:opacity};
    for(std::size_t channel=0;channel<3;++channel)
        result[channel]=std::clamp((visibleSource?radiance[channel]*(1.f-opacity):0.f)+color[channel]*opacity,0.f,60000.f);
    return result;
}
// Reference uses the exact scalar shader equations. Coverage is untouched;
// premultiplication follows fog, making zero-opacity sprites stay invisible.
inline Vec4 premultiplied(const Vec3& radiance,float opacity,const Parameters& params,float distance,float worldHeight,bool additive,
    const Vec3& skyRadiance={},const Vec3& worldViewDirection={0,0,-1}) {
    const float t=transmittance(params,distance,worldHeight);
    const auto color=lightColor(params,skyRadiance,worldViewDirection);
    return {equations::fogPremultipliedChannel(equations::fogChannel(radiance[0],color[0],t,additive?1.f:0.f),opacity),
        equations::fogPremultipliedChannel(equations::fogChannel(radiance[1],color[1],t,additive?1.f:0.f),opacity),
        equations::fogPremultipliedChannel(equations::fogChannel(radiance[2],color[2],t,additive?1.f:0.f),opacity),opacity};
}
} // namespace crimson::fog
