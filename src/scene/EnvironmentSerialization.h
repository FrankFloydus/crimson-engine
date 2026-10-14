#pragma once

#include "crimson-renderer/RenderTypes.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace crimson {

// Shared by scene documents and the compatible legacy environment file.
// Scene-only entity references stay outside these render settings.
inline void validateEnvironmentSettings(const EnvironmentSettings& value) {
    if (!std::isfinite(value.intensity) || value.intensity<0 || !std::isfinite(value.rotation) ||
        !validToneMapping(value.toneMapping) || !validGlowSettings(value.glow))
        throw std::runtime_error("Environment intensity, rotation, tone mapping or Glow is invalid.");
    if (value.source!=EnvironmentSource::Hdri && value.source!=EnvironmentSource::ProceduralSky)
        throw std::runtime_error("Environment source is invalid.");
    const auto color=[](const Vec3& rgb) {
        return math::finite(rgb) && std::all_of(rgb.begin(),rgb.end(),[](float channel){return channel>=0 && channel<=1;});
    };
    const auto& sky=value.procedural;
    if (!color(sky.skyTop) || !color(sky.skyHorizon) || !color(sky.groundBottom) || !color(sky.groundHorizon) ||
        !color(value.background.color) || !color(value.fog.color))
        throw std::runtime_error("Environment colors must have finite RGB values between zero and one.");
    if (!std::isfinite(sky.skyCurve) || sky.skyCurve<=0 || !std::isfinite(sky.groundCurve) || sky.groundCurve<=0 ||
        !std::isfinite(sky.sunHaloCurve) || sky.sunHaloCurve<=0)
        throw std::runtime_error("Procedural sky curves must be finite and positive.");
    if (!std::isfinite(sky.sunDiameter) || sky.sunDiameter<0 || !std::isfinite(sky.sunHaloExtent) ||
        sky.sunHaloExtent<=sky.sunDiameter*.5f || sky.sunHaloExtent>180)
        throw std::runtime_error("Sun halo must extend beyond the disk radius and be at most 180 degrees.");
    if (!std::isfinite(value.fog.density) || value.fog.density<0)
        throw std::runtime_error("Fog density must be finite and nonnegative.");
    const auto& fog=value.fog;
    if (fog.mode!=FogMode::Exponential && fog.mode!=FogMode::Depth)
        throw std::runtime_error("Fog mode is invalid.");
    if (!std::isfinite(fog.depthBegin) || fog.depthBegin<0 || !std::isfinite(fog.depthEnd) || fog.depthEnd<0 ||
        (fog.depthEnd!=0 && fog.depthEnd<=fog.depthBegin) || !std::isfinite(fog.depthCurve) || fog.depthCurve<=0 ||
        !std::isfinite(fog.depthOpacity) || fog.depthOpacity<0 || fog.depthOpacity>1)
        throw std::runtime_error("Fog depth range, curve or opacity is invalid.");
    if (!std::isfinite(fog.height) || !std::isfinite(fog.heightDensity) || !std::isfinite(fog.lightEnergy) || fog.lightEnergy<0)
        throw std::runtime_error("Fog height and density must be finite, and light energy nonnegative.");
    if (!value.map.empty()) {
        const std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(value.map.data()),value.map.size()));
        if (path.has_root_path() || std::any_of(path.begin(),path.end(),[](const auto& part){return part=="..";}))
            throw std::runtime_error("Environment asset reference must stay inside the project.");
    }
}

inline EnvironmentSettings readEnvironmentSettings(const nlohmann::json& json,bool extended=true) {
    if (!json.is_object()) throw std::runtime_error("Environment settings must be an object.");
    EnvironmentSettings value;
    value.map=json.value("map",value.map);
    value.enabled=json.value("enabled",value.enabled);
    value.intensity=json.value("intensity",value.intensity);
    value.rotation=json.value("rotation",value.rotation);
    value.toneMapping=static_cast<ToneMapping>(json.value("toneMapping",0));
    if (json.contains("glow")) {
        const auto& glow=json.at("glow");
        value.glow={glow.at("enabled").get<bool>(),glow.at("strength").get<float>(),
            glow.at("radius").get<float>(),glow.at("threshold").get<float>()};
    }
    if (extended) {
        if (json.contains("debanding")) {
            if (!json.at("debanding").is_boolean()) throw std::runtime_error("Environment Debanding must be a boolean.");
            value.debanding=json.at("debanding").get<bool>();
        }
        if (json.contains("source")) {
            const auto& source=json.at("source");
            if (!source.is_number_integer() || (source!=0 && source!=1))
                throw std::runtime_error("Environment source is invalid.");
            value.source=source==1?EnvironmentSource::ProceduralSky:EnvironmentSource::Hdri;
        }
        if (json.contains("procedural")) {
            const auto& source=json.at("procedural");auto& sky=value.procedural;
            sky.skyTop=source.value("skyTop",sky.skyTop);sky.skyHorizon=source.value("skyHorizon",sky.skyHorizon);
            sky.groundBottom=source.value("groundBottom",sky.groundBottom);sky.groundHorizon=source.value("groundHorizon",sky.groundHorizon);
            sky.skyCurve=source.value("skyCurve",sky.skyCurve);sky.groundCurve=source.value("groundCurve",sky.groundCurve);
            sky.sunDiameter=source.value("sunDiameter",sky.sunDiameter);sky.sunHaloExtent=source.value("sunHaloExtent",sky.sunHaloExtent);
            sky.sunHaloCurve=source.value("sunHaloCurve",sky.sunHaloCurve);
        }
        if (json.contains("background")) {
            const auto& source=json.at("background");auto& background=value.background;
            background.show=source.value("show",background.show);background.overrideColor=source.value("overrideColor",background.overrideColor);
            background.color=source.value("color",background.color);
        }
        if (json.contains("fog")) {
            const auto& source=json.at("fog");auto& fog=value.fog;
            if (!source.is_object()) throw std::runtime_error("Fog settings must be an object.");
            if (source.contains("mode")) {
                const auto& mode=source.at("mode");
                if (!mode.is_number_integer() || (mode!=0 && mode!=1)) throw std::runtime_error("Fog mode is invalid.");
                fog.mode=mode==1?FogMode::Depth:FogMode::Exponential;
            }
            for (const auto* name:{"density","depthBegin","depthEnd","depthCurve","depthOpacity","height","heightDensity","lightEnergy"})
                if (source.contains(name) && !source.at(name).is_number()) throw std::runtime_error("Fog values must be numbers.");
            fog.enabled=source.value("enabled",fog.enabled);fog.color=source.value("color",fog.color);fog.density=source.value("density",fog.density);
            fog.depthBegin=source.value("depthBegin",fog.depthBegin);fog.depthEnd=source.value("depthEnd",fog.depthEnd);
            fog.depthCurve=source.value("depthCurve",fog.depthCurve);fog.depthOpacity=source.value("depthOpacity",fog.depthOpacity);
            fog.height=source.value("height",fog.height);fog.heightDensity=source.value("heightDensity",fog.heightDensity);
            fog.lightEnergy=source.value("lightEnergy",fog.lightEnergy);
        }
    }
    validateEnvironmentSettings(value);
    return value;
}

inline nlohmann::json environmentSettingsJson(const EnvironmentSettings& value) {
    validateEnvironmentSettings(value);
    const auto& sky=value.procedural;const auto& background=value.background;const auto& fog=value.fog;
    return {{"map",value.map},{"enabled",value.enabled},{"intensity",value.intensity},{"rotation",value.rotation},
        {"toneMapping",static_cast<int>(value.toneMapping)},{"debanding",value.debanding},
        {"glow",{{"enabled",value.glow.enabled},{"strength",value.glow.strength},{"radius",value.glow.radius},{"threshold",value.glow.threshold}}},
        {"source",static_cast<int>(value.source)},
        {"procedural",{{"skyTop",sky.skyTop},{"skyHorizon",sky.skyHorizon},{"groundBottom",sky.groundBottom},{"groundHorizon",sky.groundHorizon},
            {"skyCurve",sky.skyCurve},{"groundCurve",sky.groundCurve},{"sunDiameter",sky.sunDiameter},{"sunHaloExtent",sky.sunHaloExtent},{"sunHaloCurve",sky.sunHaloCurve}}},
        {"background",{{"show",background.show},{"overrideColor",background.overrideColor},{"color",background.color}}},
        {"fog",{{"enabled",fog.enabled},{"color",fog.color},{"density",fog.density},{"mode",static_cast<int>(fog.mode)},
            {"depthBegin",fog.depthBegin},{"depthEnd",fog.depthEnd},{"depthCurve",fog.depthCurve},{"depthOpacity",fog.depthOpacity},
            {"height",fog.height},{"heightDensity",fog.heightDensity},{"lightEnergy",fog.lightEnergy}}}};
}

} // namespace crimson
