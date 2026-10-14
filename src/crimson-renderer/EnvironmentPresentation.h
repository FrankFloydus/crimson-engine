#pragma once
#include "ProceduralSky.h"
#include <bit>

namespace crimson {
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
    word(value.debanding);
    if(value.source==EnvironmentSource::ProceduralSky)word(procedural::cacheKey(value.procedural,sun));
    else {scalar(value.rotation);for(const auto c:value.map)word(static_cast<unsigned char>(c));}
    word(value.background.show);
    if(!value.background.show) {word(value.background.overrideColor);if(value.background.overrideColor)color(value.background.color);}
    word(value.fog.enabled);
    if(value.fog.enabled) {
        color(value.fog.color);scalar(value.fog.lightEnergy);
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
