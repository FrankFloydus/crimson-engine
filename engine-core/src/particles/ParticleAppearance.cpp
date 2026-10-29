#include "ParticleAppearance.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace crimson {
namespace {
uint64_t hash(uint64_t value) {
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
double random(uint64_t seed,uint64_t ordinal,uint32_t lane) {
    const auto bits=hash(seed+0x9e3779b97f4a7c15ULL*(ordinal+1)+0xd1b54a32d192ed03ULL*(static_cast<uint64_t>(lane)+1));
    return static_cast<double>(bits>>11)*0x1.0p-53;
}
double boundedAge(double age) { return std::isnan(age)?0.0:std::clamp(age,0.0,1.0); }
void interpolation(ParticleInterpolation value) {
    if(value!=ParticleInterpolation::Linear && value!=ParticleInterpolation::Constant)
        throw std::invalid_argument("Unsupported particle appearance interpolation.");
}
template<class Keys> void ages(const Keys& keys) {
    if(keys.empty())return;
    if(keys.size()<2 || keys.size()>MaximumParticleAppearanceKeys)
        throw std::invalid_argument("Particle appearance keys require 2 to 32 ordered keys at normalized ages between 0 and 1.");
    double previous=-1;
    for(const auto& key:keys) {
        if(!math::finite(key.age) || key.age<0 || key.age>1 ||
            (previous>=0 && (key.age<=previous || static_cast<double>(key.age)-previous+1e-7<MinimumParticleKeySpacing)))
            throw std::invalid_argument("Particle appearance ages must be finite, ordered and separated.");
        previous=key.age;
    }
}
Vec4 linearColor(const Vec4& value) {
    for(std::size_t channel=0;channel<value.size();++channel)
        if(!math::finite(value[channel]) || value[channel]<0 || value[channel]>(channel==3?1.f:MaximumParticleColor))
            throw std::invalid_argument("Particle colors require finite linear HDR RGB from 0 to 1000000 and opacity from 0 to 1.");
    return value;
}
PreparedParticleScalar scalar(const ParticleScalarTrack& source,float constant) {
    PreparedParticleScalar result;result.mode=source.mode;result.constant=constant;result.maximumValue=constant;
    result.curve=prepareParticleCurve(source.curve);
    if(source.range) {
        if(!math::finite(source.range->minimum) || !math::finite(source.range->maximum) || source.range->maximum<source.range->minimum)
            throw std::invalid_argument("Particle appearance range requires finite ordered endpoints.");
        result.rangeMinimum=source.range->minimum;
        result.rangeSpan=static_cast<double>(source.range->maximum)-source.range->minimum;
    }
    switch(source.mode) {
    case ParticleAppearanceMode::Constant:break;
    case ParticleAppearanceMode::RandomRange:
        if(!source.range)throw std::invalid_argument("Random particle appearance requires a range.");
        result.maximumValue=source.range->maximum;break;
    case ParticleAppearanceMode::Curve:
        if(result.curve.empty())throw std::invalid_argument("Curve particle appearance requires keys.");
        result.maximumValue=result.curve.maximum();break;
    default:throw std::invalid_argument("Unsupported particle appearance mode.");
    }
    return result;
}
}

float particleSrgbToLinear(float value) {
    return value<=0.04045f?value/12.92f:std::pow((value+0.055f)/1.055f,2.4f);
}
uint64_t particleAppearanceSeed(uint64_t effectSeed,uint64_t emitterId) { return hash(effectSeed^hash(emitterId)); }
float particleAppearanceRange(const ParticleAppearanceRange& range,uint64_t effectSeed,uint64_t emitterId,uint64_t ordinal,uint32_t lane) {
    if(!math::finite(range.minimum) || !math::finite(range.maximum) || range.maximum<range.minimum)
        throw std::invalid_argument("Particle appearance range requires finite ordered endpoints.");
    return static_cast<float>(range.minimum+(static_cast<double>(range.maximum)-range.minimum)*random(particleAppearanceSeed(effectSeed,emitterId),ordinal,lane));
}
PreparedParticleCurve prepareParticleCurve(const ParticleScalarCurve& source) {
    interpolation(source.interpolation);ages(source.keys);
    PreparedParticleCurve result;if(source.keys.empty())return result;
    result.count_=source.keys.size()-1;result.finalValue_=source.keys.back().value;
    result.maximum_=source.keys.front().value;
    for(const auto& key:source.keys) {
        if(!math::finite(key.value))throw std::invalid_argument("Particle curve values must be finite.");
        result.maximum_=std::max(result.maximum_,key.value);
    }
    for(std::size_t i=0;i<result.count_;++i) {
        const auto& a=source.keys[i];const auto& b=source.keys[i+1];
        result.segments_[i]={a.age,b.age,source.interpolation==ParticleInterpolation::Linear?
            (static_cast<double>(b.value)-a.value)/(static_cast<double>(b.age)-a.age):0,a.value};
    }
    return result;
}
float PreparedParticleCurve::evaluate(double age) const {
    if(!count_)return 0;age=boundedAge(age);
    if(age<=segments_.front().age)return segments_.front().value;
    const auto found=std::upper_bound(segments_.begin(),segments_.begin()+static_cast<std::ptrdiff_t>(count_),age,
        [](double value,const Segment& segment){return value<segment.endAge;});
    if(found==segments_.begin()+static_cast<std::ptrdiff_t>(count_))return finalValue_;
    return static_cast<float>(found->value+found->slope*(age-found->age));
}
PreparedParticleGradient prepareParticleGradient(const ParticleColorGradient& source) {
    interpolation(source.interpolation);ages(source.stops);
    PreparedParticleGradient result;if(source.stops.empty())return result;
    result.count_=source.stops.size()-1;result.finalValue_=linearColor(source.stops.back().color);
    for(std::size_t i=0;i<result.count_;++i) {
        const auto& a=source.stops[i];const auto& b=source.stops[i+1];
        const auto start=linearColor(a.color),end=linearColor(b.color);Vec4 difference{};
        if(source.interpolation==ParticleInterpolation::Linear)
            for(std::size_t component=0;component<4;++component)difference[component]=end[component]-start[component];
        result.segments_[i]={a.age,b.age,1/(static_cast<double>(b.age)-a.age),start,difference};
    }
    return result;
}
Vec4 PreparedParticleGradient::evaluate(double age) const {
    if(!count_)return {};age=boundedAge(age);
    if(age<=segments_.front().age)return segments_.front().value;
    const auto found=std::upper_bound(segments_.begin(),segments_.begin()+static_cast<std::ptrdiff_t>(count_),age,
        [](double value,const Segment& segment){return value<segment.endAge;});
    if(found==segments_.begin()+static_cast<std::ptrdiff_t>(count_))return finalValue_;
    auto value=found->value;const auto factor=(age-found->age)*found->inverseDuration;
    for(std::size_t component=0;component<4;++component)value[component]=static_cast<float>(value[component]+found->difference[component]*factor);
    return value;
}
float PreparedParticleScalar::evaluate(double age,uint64_t mixedSeed,uint64_t ordinal,uint32_t lane) const {
    switch(mode) {
    case ParticleAppearanceMode::RandomRange:return static_cast<float>(rangeMinimum+rangeSpan*random(mixedSeed,ordinal,lane));
    case ParticleAppearanceMode::Curve:return curve.evaluate(age);
    default:return constant;
    }
}
PreparedParticleAppearance prepareParticleAppearance(const ParticleEmitterDefinition& emitter) {
    PreparedParticleAppearance result;
    result.size=scalar(emitter.sizeTrack,emitter.size);result.intensity=scalar(emitter.intensityTrack,emitter.intensity);
    result.rotation=scalar(emitter.rotationTrack,emitter.rotation);result.colorMode=emitter.colorMode;
    result.constantColor=linearColor({emitter.color[0],emitter.color[1],emitter.color[2],emitter.opacity});
    result.gradient=prepareParticleGradient(emitter.colorGradient);
    if(result.colorMode==ParticleColorMode::Gradient && result.gradient.empty())throw std::invalid_argument("Gradient particle color requires stops.");
    if(result.colorMode!=ParticleColorMode::Constant && result.colorMode!=ParticleColorMode::Gradient)throw std::invalid_argument("Unsupported particle color mode.");
    return result;
}
ParticleAppearanceSample PreparedParticleAppearance::sample(double age,uint64_t mixedSeed,uint64_t ordinal) const {
    ParticleAppearanceSample result;
    result.size=size.evaluate(age,mixedSeed,ordinal,7);
    // The authored function stays absolute in degrees, but an equivalent
    // bounded angle keeps shader sine/cosine accurate over many full turns.
    result.rotation=static_cast<float>(std::remainder(static_cast<double>(rotation.evaluate(age,mixedSeed,ordinal,9)),360.0)*std::numbers::pi/180);
    result.color=colorMode==ParticleColorMode::Gradient?gradient.evaluate(age):constantColor;
    const auto strength=intensity.evaluate(age,mixedSeed,ordinal,8);
    for(std::size_t component=0;component<3;++component)result.color[component]*=strength;
    return result;
}

} // namespace crimson
