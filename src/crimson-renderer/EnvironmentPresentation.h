#pragma once
#include "ProceduralSky.h"
#include <algorithm>
#include <bit>
#include <cmath>

namespace crimson {
// RGB is the linear, energy-weighted color contribution; W weights the
// selected source's irradiance. Reflection intensity is kept independent.
inline Vec4 environmentAmbientLight(const EnvironmentSettings& settings,bool authoredEnvironment) {
    if(!authoredEnvironment)return {0,0,0,1};
    const auto& ambient=settings.ambientLight;
    if(ambient.source==AmbientLightSource::Disabled)return {};
    const auto finiteOr=[](float value,float fallback){return std::isfinite(value)?value:fallback;};
    // Match the renderer's finite HDR envelope without changing authored data.
    const float energy=std::clamp(finiteOr(ambient.energy,1.f),0.f,60000.f);
    const float sky=ambient.source==AmbientLightSource::Sky?
        std::clamp(finiteOr(ambient.skyContribution,1.f),0.f,1.f):0.f;
    Vec4 result{0,0,0,sky};
    for(std::size_t channel=0;channel<3;++channel) {
        const float value=std::clamp(finiteOr(ambient.color[channel],0.f),0.f,1.f);
        const float linear=value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
        result[channel]=linear*energy*(1.f-sky);
    }
    return result;
}
struct EnvironmentPresentation {
    bool showBackground{},procedural{},debanding{};
    Vec3 matte{};
    FogSettings fog{};
};
inline EnvironmentPresentation environmentPresentation(const EnvironmentSettings& settings,const ViewportAppearance& appearance,
    bool authoredEnvironment,bool previewBackground=false) {
    EnvironmentPresentation result;
    result.showBackground=authoredEnvironment?settings.background.show:previewBackground;
    result.procedural=authoredEnvironment && settings.source==EnvironmentSource::ProceduralSky;
    result.debanding=authoredEnvironment && settings.debanding;
    result.matte=authoredEnvironment && !settings.background.show && settings.background.overrideColor?
        settings.background.color:appearance.backgroundSrgb;
    if(authoredEnvironment)result.fog=settings.fog;
    return result;
}

// Saved-scene thumbnails depend only on the settings currently affecting their
// image. Source content, geometry, shader version, camera and exposure are
// combined by AssetPreviews. Inactive source controls remain authored data.
inline std::uint64_t thumbnailEnvironmentKey(const EnvironmentSettings& value,const ProceduralSun& sun) {
    std::uint64_t hash=14695981039346656037ull;
    const auto word=[&](std::uint64_t v){hash=(hash^v)*1099511628211ull;};
    const auto scalar=[&](float v){word(std::bit_cast<std::uint32_t>(v));};
    const auto color=[&](const Vec3& v){for(const auto c:v)scalar(c);};
    word(static_cast<std::uint64_t>(value.source));word(value.enabled);scalar(value.intensity);
    word(static_cast<std::uint64_t>(value.toneMapping));
    const auto& tone=value.toneMappingParameters;
    switch(value.toneMapping) {
    case ToneMapping::Reinhard:scalar(tone.reinhardWhite);break;
    case ToneMapping::ACES:scalar(tone.acesWhite);break;
    case ToneMapping::AgX:scalar(tone.agxWhite);scalar(tone.agxContrast);break;
    default:break;
    }
    word(value.debanding);
    const auto ambient=environmentAmbientLight(value,true);
    for(const float component:ambient)scalar(component);
    if(value.source==EnvironmentSource::ProceduralSky)word(procedural::cacheKey(value.procedural,sun));
    else {scalar(value.rotation);for(const auto c:value.map)word(static_cast<unsigned char>(c));}
    word(value.background.show);
    if(!value.background.show) {word(value.background.overrideColor);if(value.background.overrideColor)color(value.background.color);}
    word(value.fog.enabled);
    if(value.fog.enabled) {
        color(value.fog.color);scalar(value.fog.lightEnergy);
        scalar(value.fog.skyAffect);scalar(value.fog.aerialPerspective);scalar(value.fog.sunScatter);
        word(static_cast<std::uint64_t>(value.fog.mode));
        if(value.fog.mode==FogMode::Depth) {
            scalar(value.fog.depthBegin);scalar(value.fog.depthEnd);
            scalar(value.fog.depthCurve);scalar(value.fog.depthOpacity);
        } else scalar(value.fog.density);
        scalar(value.fog.heightDensity);
        if(value.fog.heightDensity!=0.f)scalar(value.fog.height);
    }
    return hash;
}
} // namespace crimson
