#include "ToneMapping.h"
#include <algorithm>
#include <cmath>

// Godot's SDR ACES and AgX equations, adapted from tonemap.glsl and
// environment_storage.cpp. See shaders/tone_mapping.sh for source attribution
// and the full MIT notices. This reference intentionally retains separate CPU
// operations so readback tests detect shader matrix/encoding mistakes.
namespace crimson {
namespace {
Vec3 transform(const Vec3& value, const Vec3& r, const Vec3& g, const Vec3& b) {
    return {r[0]*value[0]+r[1]*value[1]+r[2]*value[2],
            g[0]*value[0]+g[1]*value[1]+g[2]*value[2],
            b[0]*value[0]+b[1]*value[1]+b[2]*value[2]};
}
float fitAces(float value) {
    return (value*(value+.0245786f)-.000090537f) /
        (value*(.983729f*value+.432951f)+.238081f);
}
float agxCurve(float value, const ToneMappingUniforms& coefficients) {
    constexpr float crossover=.18f;
    const float contrast=coefficients.curve[2],toeA=coefficients.curve[3];
    const float slope=coefficients.shoulder[0],shoulderW=coefficients.shoulder[1];
    if (value<crossover) {
        const float toe=std::pow(value,contrast);
        return toe/(toe+toeA);
    }
    const float shoulder=value-crossover;
    return crossover+slope*shoulder*(1+shoulder/shoulderW)/(1+slope*shoulder/(1-crossover));
}
}
bool validToneMappingParameters(const ToneMappingParameters& value) noexcept {
    const auto inRange=[](float v,float low,float high){return std::isfinite(v)&&v>=low&&v<=high;};
    return inRange(value.reinhardWhite,0.f,64.f)&&inRange(value.acesWhite,1.f,64.f)&&
        inRange(value.agxWhite,2.f,64.f)&&inRange(value.agxContrast,1.f,4.f);
}
ToneMappingUniforms toneMappingUniforms(const ToneMappingParameters& value) noexcept {
    const ToneMappingParameters defaults;
    const auto& parameters=validToneMappingParameters(value)?value:defaults;
    const float reinhardWhite=std::max(parameters.reinhardWhite,.01f);
    constexpr float crossover=.18f,shoulderMax=1.f-crossover;
    const float toePower=std::pow(crossover,parameters.agxContrast);
    const float toeA=(1.f/crossover-1.f)*toePower;
    const float slopeDenominator=toePower+toeA;
    // Match environment_get_tonemap_parameters in Godot's SDR path: the
    // shoulder joins the toe at the same value and first derivative.
    const float slope=(parameters.agxContrast*std::pow(crossover,parameters.agxContrast-1.f)*toeA)/
        (slopeDenominator*slopeDenominator);
    float shoulderW=parameters.agxWhite-crossover;
    shoulderW=shoulderW*shoulderW/shoulderMax*slope;
    return {{parameters.reinhardWhite>0.f?1.f/(reinhardWhite*reinhardWhite):0.f,
        fitAces(parameters.acesWhite*1.8f),parameters.agxContrast,toeA},{slope,shoulderW,0,0}};
}
Vec3 toneMap(Vec3 color, ToneMapping mode) {
    return toneMap(color,mode,ToneMappingParameters{});
}
Vec3 toneMap(Vec3 color, ToneMapping mode, const ToneMappingParameters& parameters) {
    for (auto& value:color) value=std::isfinite(value)?std::max(value,0.f):0.f;
    if (mode==ToneMapping::Linear) return color;
    const auto coefficients=toneMappingUniforms(parameters);
    if (mode==ToneMapping::ACES) {
        for (auto& value:color) value*=1.8f;
        color=transform(color,{.59719f,.35458f,.04823f},{.076f,.90834f,.01566f},{.0284f,.13383f,.83777f});
        for (auto& value:color) value=fitAces(value);
        color=transform(color,{1.60475f,-.53108f,-.07367f},{-.10208f,1.10813f,-.00605f},{-.00327f,-.07276f,1.07602f});
        for (auto& value:color) value=std::max(0.f,value/coefficients.curve[1]);
        return color;
    }
    if (mode==ToneMapping::AgX) {
        color=transform(color,{.544814746488245f,.373787398372697f,.0813978551390581f},
            {.140416948464053f,.754137554567394f,.105445496968552f},
            {.0888104196149096f,.178871756420858f,.732317823964232f});
        for (auto& value:color) value=std::min(1.f,agxCurve(value,coefficients));
        return transform(color,{1.96488741169489f,-.855988495690215f,-.108898916004672f},
            {-.299313364904742f,1.3263979646198f,-.0270845997150571f},
            {-.164352742528393f,-.238183969428088f,1.40253671195648f});
    }
    for (auto& value:color) value=coefficients.curve[0]>0.f?
        value*(1.f+value*coefficients.curve[0])/(1.f+value):1.f-1.f/(1.f+value);
    return color;
}
}
