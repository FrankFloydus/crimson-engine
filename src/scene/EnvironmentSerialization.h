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
        !validToneMapping(value.toneMapping) || !validToneMappingParameters(value.toneMappingParameters) ||
        !validGlowSettings(value.glow) || !validBloomSettings(value.bloom))
        throw std::runtime_error("Environment intensity, rotation, tone mapping, Glow or Bloom is invalid.");
    if (value.source!=EnvironmentSource::Hdri && value.source!=EnvironmentSource::ProceduralSky)
        throw std::runtime_error("Environment source is invalid.");
    if (!validSsgiSettings(value.ssgi) || !validSsaoSettings(value.ssao))
        throw std::runtime_error("Screen-space lighting tuning is invalid.");
    const auto& ambient=value.ambientLight;
    if (ambient.source!=AmbientLightSource::Disabled && ambient.source!=AmbientLightSource::Color && ambient.source!=AmbientLightSource::Sky)
        throw std::runtime_error("Ambient light source is invalid.");
    if (!std::isfinite(ambient.skyContribution) || ambient.skyContribution<0 || ambient.skyContribution>1 ||
        !std::isfinite(ambient.energy) || ambient.energy<0)
        throw std::runtime_error("Ambient sky contribution must be between zero and one, and energy finite and nonnegative.");
    const auto color=[](const Vec3& rgb) {
        return math::finite(rgb) && std::all_of(rgb.begin(),rgb.end(),[](float channel){return channel>=0 && channel<=1;});
    };
    const auto& sky=value.procedural;
    if (!color(sky.skyTop) || !color(sky.skyHorizon) || !color(sky.groundBottom) || !color(sky.groundHorizon) ||
        !color(value.background.color) || !color(value.fog.color) || !color(ambient.color))
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
    for(const float factor:{fog.skyAffect,fog.aerialPerspective,fog.sunScatter})
        if(!std::isfinite(factor) || factor<0 || factor>1)
            throw std::runtime_error("Fog Sky Affect, Aerial Perspective and Sun Scatter must be finite values between zero and one.");
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
    if (json.contains("toneMappingParameters")) {
        const auto& source=json.at("toneMappingParameters");auto& tone=value.toneMappingParameters;
        if (!source.is_object())throw std::runtime_error("Tone mapping parameters must be an object.");
        for (const auto* name:{"reinhardWhite","acesWhite","agxWhite","agxContrast"})
            if (source.contains(name) && !source.at(name).is_number())
                throw std::runtime_error("Tone mapping parameters must be numbers.");
        tone.reinhardWhite=source.value("reinhardWhite",tone.reinhardWhite);
        tone.acesWhite=source.value("acesWhite",tone.acesWhite);
        tone.agxWhite=source.value("agxWhite",tone.agxWhite);
        tone.agxContrast=source.value("agxContrast",tone.agxContrast);
    }
    const auto quality=[](const nlohmann::json& source,int fallback,int maximum=2) {
        if (!source.contains("quality"))return fallback;
        const auto& encoded=source.at("quality");
        if (!encoded.is_number_integer() || encoded<0 || encoded>maximum)
            throw std::runtime_error("Screen-space lighting quality is invalid.");
        return encoded.get<int>();
    };
    if (json.contains("ssgi") || json.contains("gi")) {
        const bool previousWrapper=!json.contains("ssgi");
        const auto& source=json.at(previousWrapper?"gi":"ssgi");auto& gi=value.ssgi;
        if (!source.is_object())throw std::runtime_error("Global illumination settings must be an object.");
        if (source.contains("enabled") && !source.at("enabled").is_boolean())
            throw std::runtime_error("Global illumination enabled must be a boolean.");
        if (source.contains("strength") && !source.at("strength").is_number())
            throw std::runtime_error("Global illumination strength must be a number.");
        gi.enabled=source.value("enabled",gi.enabled);gi.strength=source.value("strength",gi.strength);
        if (source.contains("bounces")) {
            const auto& bounces=source.at("bounces");
            if (!bounces.is_number_integer() || bounces<1 || bounces>4)
                throw std::runtime_error("Global illumination bounces must be an integer between one and four.");
            gi.bounces=bounces.get<int>();
        }
        const auto readScreenSpace=[&](const nlohmann::json& tuning) {
            if (!tuning.is_object())throw std::runtime_error("Screen-space GI settings must be an object.");
            for (const auto* name:{"maxDistance","thickness","temporalStability","rayBias","denoiseStrength","radianceClamp"})
                if (tuning.contains(name) && !tuning.at(name).is_number())throw std::runtime_error("Screen-space GI values must be numbers.");
            gi.maxDistance=tuning.value("maxDistance",gi.maxDistance);gi.thickness=tuning.value("thickness",gi.thickness);
            gi.temporalStability=tuning.value("temporalStability",gi.temporalStability);
            gi.quality=static_cast<SsgiQuality>(quality(tuning,static_cast<int>(gi.quality),3));
            const auto integer=[&](const char* name,int fallback,int minimum,int maximum) {
                if (!tuning.contains(name))return fallback;
                const auto& encoded=tuning.at(name);
                if (!encoded.is_number_integer() || encoded<minimum || encoded>maximum)
                    throw std::runtime_error(std::string("Screen-space GI ")+name+" is outside its integer range.");
                return encoded.get<int>();
            };
            gi.rays=integer("rays",gi.rays,1,16);gi.steps=integer("steps",gi.steps,8,64);
            gi.denoiseRadius=integer("denoiseRadius",gi.denoiseRadius,0,4);
            gi.rayBias=tuning.value("rayBias",gi.rayBias);gi.denoiseStrength=tuning.value("denoiseStrength",gi.denoiseStrength);
            gi.radianceClamp=tuning.value("radianceClamp",gi.radianceClamp);
        };
        if (!previousWrapper)readScreenSpace(source);
        else if (source.contains("screenSpace"))readScreenSpace(source.at("screenSpace"));
    }
    if (json.contains("ssao")) {
        const auto& source=json.at("ssao");auto& ao=value.ssao;
        if (!source.is_object())throw std::runtime_error("Screen-space ambient occlusion settings must be an object.");
        if (source.contains("enabled") && !source.at("enabled").is_boolean())
            throw std::runtime_error("Screen-space ambient occlusion enabled must be a boolean.");
        for (const auto* name:{"strength","radius"})
            if (source.contains(name) && !source.at(name).is_number())throw std::runtime_error("Screen-space ambient occlusion values must be numbers.");
        ao.enabled=source.value("enabled",ao.enabled);ao.strength=source.value("strength",ao.strength);ao.radius=source.value("radius",ao.radius);
        ao.quality=static_cast<SsaoQuality>(quality(source,static_cast<int>(ao.quality)));
    }
    if (json.contains("ambientLight")) {
        const auto& source=json.at("ambientLight");auto& ambient=value.ambientLight;
        if (!source.is_object()) throw std::runtime_error("Ambient light settings must be an object.");
        if (source.contains("source")) {
            const auto& mode=source.at("source");
            if (!mode.is_number_integer() || mode<0 || mode>2) throw std::runtime_error("Ambient light source is invalid.");
            ambient.source=static_cast<AmbientLightSource>(mode.get<int>());
        }
        for (const auto* name:{"skyContribution","energy"})
            if (source.contains(name) && !source.at(name).is_number()) throw std::runtime_error("Ambient light values must be numbers.");
        ambient.color=source.value("color",ambient.color);
        ambient.skyContribution=source.value("skyContribution",ambient.skyContribution);
        ambient.energy=source.value("energy",ambient.energy);
    }
    const auto readHalo=[](const nlohmann::json& source,auto& effect) {
        if (!source.is_object())throw std::runtime_error("Glow/Bloom settings must be an object.");
        if (source.contains("enabled") && !source.at("enabled").is_boolean())
            throw std::runtime_error("Glow/Bloom enabled must be a boolean.");
        for(const auto* name:{"strength","radius","threshold","softKnee"})
            if(source.contains(name) && !source.at(name).is_number())
                throw std::runtime_error("Glow/Bloom tuning must be numeric.");
        effect.enabled=source.value("enabled",effect.enabled);effect.strength=source.value("strength",effect.strength);
        effect.radius=source.value("radius",effect.radius);effect.threshold=source.value("threshold",effect.threshold);
        effect.softKnee=source.value("softKnee",effect.softKnee);effect.tint=source.value("tint",effect.tint);
    };
    if (json.contains("bloom")) {
        const auto& source=json.at("bloom");auto& bloom=value.bloom;readHalo(source,bloom);
        if(source.contains("anamorphic") && !source.at("anamorphic").is_number())
            throw std::runtime_error("Bloom anamorphic stretch must be numeric.");
        bloom.anamorphic=source.value("anamorphic",bloom.anamorphic);
        if(source.contains("quality")) {
            const auto& bloomQuality=source.at("quality");
            if(!bloomQuality.is_number_integer() || bloomQuality<0 || bloomQuality>1)throw std::runtime_error("Bloom quality is invalid.");
            bloom.quality=static_cast<BloomQuality>(bloomQuality.get<int>());
        }
        if(source.contains("levels")) {
            const auto& levels=source.at("levels");
            if(!levels.is_array() || levels.size()!=BloomLevelCount ||
                std::any_of(levels.begin(),levels.end(),[](const auto& weight){return !weight.is_number();}))
                throw std::runtime_error("Bloom requires seven numeric level contributions.");
            bloom.levels=levels.get<decltype(bloom.levels)>();
        }
    }
    if (json.contains("glow")) {
        const auto& source=json.at("glow");GlowSettings glow;readHalo(source,glow);
        for(const auto* name:{"coreHalo","saturation"})
            if(source.contains(name) && !source.at(name).is_number())
                throw std::runtime_error("Glow artistic tuning must be numeric.");
        glow.coreHalo=source.value("coreHalo",glow.coreHalo);glow.saturation=source.value("saturation",glow.saturation);
        if(source.contains("mix") && !source.at("mix").is_number())throw std::runtime_error("Glow mix must be numeric.");
        glow.mix=source.value("mix",glow.mix);
        if(source.contains("blendMode")) {
            const auto& encoded=source.at("blendMode");
            constexpr std::array<const char*,5> names{"Additive","Screen","SoftLight","Replace","Mix"};
            if(!encoded.is_string())throw std::runtime_error("Glow blend mode must be a string.");
            const auto mode=encoded.get<std::string>();const auto found=std::find(names.begin(),names.end(),mode);
            if(found==names.end())throw std::runtime_error("Glow blend mode is invalid.");
            glow.blendMode=static_cast<GlowBlendMode>(std::distance(names.begin(),found));
        }
        if(source.contains("source") && source.at("source")!="Emission")
            throw std::runtime_error("Glow source is invalid.");
        if(!validGlowSettings(glow))throw std::runtime_error("Glow settings are invalid.");
        // The former Glow blurred the complete scene. Preserve that authored
        // source selection by converting it into Bloom once, only in memory.
        if(!source.contains("source") && !json.contains("bloom")) {
            value.bloom.enabled=glow.enabled;value.bloom.strength=glow.strength;
            value.bloom.radius=glow.radius;value.bloom.threshold=glow.threshold;
            value.bloom.tint=glow.tint;value.bloom.softKnee=glow.softKnee;
        } else value.glow=glow;
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
            for (const auto* name:{"density","depthBegin","depthEnd","depthCurve","depthOpacity","height","heightDensity","lightEnergy",
                    "skyAffect","aerialPerspective","sunScatter"})
                if (source.contains(name) && !source.at(name).is_number()) throw std::runtime_error("Fog values must be numbers.");
            fog.enabled=source.value("enabled",fog.enabled);fog.color=source.value("color",fog.color);fog.density=source.value("density",fog.density);
            fog.depthBegin=source.value("depthBegin",fog.depthBegin);fog.depthEnd=source.value("depthEnd",fog.depthEnd);
            fog.depthCurve=source.value("depthCurve",fog.depthCurve);fog.depthOpacity=source.value("depthOpacity",fog.depthOpacity);
            fog.height=source.value("height",fog.height);fog.heightDensity=source.value("heightDensity",fog.heightDensity);
            fog.lightEnergy=source.value("lightEnergy",fog.lightEnergy);
            fog.skyAffect=source.value("skyAffect",fog.skyAffect);
            fog.aerialPerspective=source.value("aerialPerspective",fog.aerialPerspective);
            fog.sunScatter=source.value("sunScatter",fog.sunScatter);
        }
    }
    validateEnvironmentSettings(value);
    return value;
}

inline nlohmann::json environmentSettingsJson(const EnvironmentSettings& value) {
    validateEnvironmentSettings(value);
    constexpr std::array<const char*,5> glowBlendModes{"Additive","Screen","SoftLight","Replace","Mix"};
    const auto& sky=value.procedural;const auto& background=value.background;const auto& fog=value.fog;
    return {{"map",value.map},{"enabled",value.enabled},{"intensity",value.intensity},{"rotation",value.rotation},
        {"toneMapping",static_cast<int>(value.toneMapping)},
        {"toneMappingParameters",{{"reinhardWhite",value.toneMappingParameters.reinhardWhite},{"acesWhite",value.toneMappingParameters.acesWhite},
            {"agxWhite",value.toneMappingParameters.agxWhite},{"agxContrast",value.toneMappingParameters.agxContrast}}},
        {"debanding",value.debanding},
        {"ssgi",{{"enabled",value.ssgi.enabled},{"strength",value.ssgi.strength},{"bounces",value.ssgi.bounces},
            {"maxDistance",value.ssgi.maxDistance},{"thickness",value.ssgi.thickness},
            {"quality",static_cast<int>(value.ssgi.quality)},{"temporalStability",value.ssgi.temporalStability},
            {"rays",value.ssgi.rays},{"steps",value.ssgi.steps},{"rayBias",value.ssgi.rayBias},
            {"denoiseRadius",value.ssgi.denoiseRadius},{"denoiseStrength",value.ssgi.denoiseStrength},{"radianceClamp",value.ssgi.radianceClamp}}},
        {"ssao",{{"enabled",value.ssao.enabled},{"strength",value.ssao.strength},{"radius",value.ssao.radius},
            {"quality",static_cast<int>(value.ssao.quality)}}},
        {"ambientLight",{{"source",static_cast<int>(value.ambientLight.source)},{"color",value.ambientLight.color},
            {"skyContribution",value.ambientLight.skyContribution},{"energy",value.ambientLight.energy}}},
        {"glow",{{"source","Emission"},{"enabled",value.glow.enabled},{"strength",value.glow.strength},
            {"radius",value.glow.radius},{"threshold",value.glow.threshold},{"tint",value.glow.tint},
            {"coreHalo",value.glow.coreHalo},{"saturation",value.glow.saturation},{"softKnee",value.glow.softKnee},
            {"blendMode",glowBlendModes[static_cast<std::size_t>(value.glow.blendMode)]},{"mix",value.glow.mix}}},
        {"bloom",{{"enabled",value.bloom.enabled},{"strength",value.bloom.strength},{"radius",value.bloom.radius},
            {"threshold",value.bloom.threshold},{"tint",value.bloom.tint},{"softKnee",value.bloom.softKnee},
            {"anamorphic",value.bloom.anamorphic},{"quality",static_cast<int>(value.bloom.quality)},{"levels",value.bloom.levels}}},
        {"source",static_cast<int>(value.source)},
        {"procedural",{{"skyTop",sky.skyTop},{"skyHorizon",sky.skyHorizon},{"groundBottom",sky.groundBottom},{"groundHorizon",sky.groundHorizon},
            {"skyCurve",sky.skyCurve},{"groundCurve",sky.groundCurve},{"sunDiameter",sky.sunDiameter},{"sunHaloExtent",sky.sunHaloExtent},{"sunHaloCurve",sky.sunHaloCurve}}},
        {"background",{{"show",background.show},{"overrideColor",background.overrideColor},{"color",background.color}}},
        {"fog",{{"enabled",fog.enabled},{"color",fog.color},{"density",fog.density},{"mode",static_cast<int>(fog.mode)},
            {"depthBegin",fog.depthBegin},{"depthEnd",fog.depthEnd},{"depthCurve",fog.depthCurve},{"depthOpacity",fog.depthOpacity},
            {"height",fog.height},{"heightDensity",fog.heightDensity},{"lightEnergy",fog.lightEnergy},
            {"skyAffect",fog.skyAffect},{"aerialPerspective",fog.aerialPerspective},{"sunScatter",fog.sunScatter}}}};
}

} // namespace crimson
