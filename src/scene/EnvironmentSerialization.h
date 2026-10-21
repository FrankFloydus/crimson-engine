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
        !validGlowSettings(value.glow))
        throw std::runtime_error("Environment intensity, rotation, tone mapping or Glow is invalid.");
    if (value.source!=EnvironmentSource::Hdri && value.source!=EnvironmentSource::ProceduralSky)
        throw std::runtime_error("Environment source is invalid.");
    if (!validSsgiSettings(value.ssgi) || !validSsaoSettings(value.ssao) || !validSsrSettings(value.ssr))
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
    if (json.contains("ssr")) {
        const auto& source=json.at("ssr");auto& reflection=value.ssr;
        if (!source.is_object())throw std::runtime_error("Screen-space reflection settings must be an object.");
        if (source.contains("enabled") && !source.at("enabled").is_boolean())
            throw std::runtime_error("Screen-space reflection enabled must be a boolean.");
        for (const auto* name:{"strength","maxDistance","depthTolerance","maxRoughness","bias"})
            if (source.contains(name) && !source.at(name).is_number())
                throw std::runtime_error("Screen-space reflection values must be numbers.");
        reflection.enabled=source.value("enabled",reflection.enabled);
        reflection.strength=source.value("strength",reflection.strength);
        reflection.quality=static_cast<SsrQuality>(quality(source,static_cast<int>(reflection.quality)));
        reflection.maxDistance=source.value("maxDistance",reflection.maxDistance);
        reflection.depthTolerance=source.value("depthTolerance",reflection.depthTolerance);
        reflection.maxRoughness=source.value("maxRoughness",reflection.maxRoughness);
        reflection.bias=source.value("bias",reflection.bias);
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
    const auto readGlow=[](const nlohmann::json& source,GlowSettings& glow) {
        if (!source.is_object())throw std::runtime_error("Glow settings must be an object.");
        if (source.contains("enabled") && !source.at("enabled").is_boolean())
            throw std::runtime_error("Glow enabled must be a boolean.");
        for(const auto* name:{"strength","radius","threshold","softKnee","saturation","anamorphic","mix"})
            if(source.contains(name) && !source.at(name).is_number())
                throw std::runtime_error("Glow tuning must be numeric.");
        glow.enabled=source.value("enabled",glow.enabled);glow.strength=source.value("strength",glow.strength);
        glow.radius=source.value("radius",glow.radius);glow.threshold=source.value("threshold",glow.threshold);
        glow.softKnee=source.value("softKnee",glow.softKnee);glow.tint=source.value("tint",glow.tint);
        glow.saturation=source.value("saturation",glow.saturation);glow.anamorphic=source.value("anamorphic",glow.anamorphic);
        glow.mix=source.value("mix",glow.mix);
        if(source.contains("quality")) {
            const auto& glowQuality=source.at("quality");
            if(!glowQuality.is_number_integer() || glowQuality<0 || glowQuality>1)throw std::runtime_error("Glow quality is invalid.");
            glow.quality=static_cast<GlowQuality>(glowQuality.get<int>());
        }
        if(source.contains("levels")) {
            const auto& levels=source.at("levels");
            if(!levels.is_array() || levels.size()!=GlowLevelCount ||
                std::any_of(levels.begin(),levels.end(),[](const auto& gain){return !gain.is_number();}))
                throw std::runtime_error("Glow requires seven numeric level contributions.");
            glow.levels=levels.get<decltype(glow.levels)>();
        }
        if(source.contains("blendMode")) {
            const auto& encoded=source.at("blendMode");
            constexpr std::array<const char*,5> names{"Additive","Screen","SoftLight","Replace","Mix"};
            if(!encoded.is_string())throw std::runtime_error("Glow blend mode must be a string.");
            const auto mode=encoded.get<std::string>();const auto found=std::find(names.begin(),names.end(),mode);
            if(found==names.end())throw std::runtime_error("Glow blend mode is invalid.");
            glow.blendMode=static_cast<GlowBlendMode>(std::distance(names.begin(),found));
        }
        if(!validGlowSettings(glow))throw std::runtime_error("Glow settings are invalid.");
    };
    GlowSettings previousGlow,previousBloom;
    bool currentGlow=false;
    if (json.contains("glow")) {
        const auto& source=json.at("glow");
        if(!source.is_object())throw std::runtime_error("Glow settings must be an object.");
        bool emissionSource=false;
        if(source.contains("source")) {
            const auto& encoded=source.at("source");
            if(!encoded.is_string() || (encoded!="Scene" && encoded!="Emission"))
                throw std::runtime_error("Glow source is invalid.");
            currentGlow=encoded=="Scene";emissionSource=encoded=="Emission";
        }
        // Original scene-wide Glow and the replaced emission-only Glow both
        // defaulted to .8; the current full-scene representation defaults to .1.
        if(!currentGlow)previousGlow.strength=.8f;
        readGlow(source,previousGlow);
        if(source.contains("coreHalo")) {
            const auto& encoded=source.at("coreHalo");
            if(!encoded.is_number())throw std::runtime_error("Previous Glow core/halo balance must be numeric.");
            const float balance=encoded.get<float>();
            if(!std::isfinite(balance) || balance<0 || balance>1)
                throw std::runtime_error("Previous Glow core/halo balance is invalid.");
        }
        if(emissionSource) {
            constexpr std::array<float,GlowLevelCount> core{.65f,.25f,.10f,0,0,0,0};
            constexpr std::array<float,GlowLevelCount> halo{0,.02f,.08f,.15f,.25f,.25f,.25f};
            const float balance=source.value("coreHalo",.5f);
            for(std::size_t level=0;level<GlowLevelCount;++level)
                previousGlow.levels[level]=core[level]+(halo[level]-core[level])*balance;
        }
    }
    if (json.contains("bloom")) {
        // The removed separate Bloom normalized its authored levels. Convert
        // that profile once into direct gains; current Glow never normalizes.
        readGlow(json.at("bloom"),previousBloom);
        float sum=0;for(float gain:previousBloom.levels)sum+=gain;
        if(sum>0)for(float& gain:previousBloom.levels)gain/=sum;
    }
    const auto activeGlow=[](const GlowSettings& glow) {
        return glow.enabled && glow.strength>0 && glow.radius>0 &&
            !(glow.blendMode==GlowBlendMode::Mix && glow.mix==0) &&
            std::any_of(glow.levels.begin(),glow.levels.end(),[](float gain){return gain>0;});
    };
    // Collapse the replaced two-effect representation without summing two
    // halos: prefer active full-scene Bloom, otherwise active Glow. If neither
    // contributes, preserve Bloom's inactive tuning when it exists.
    if(currentGlow)value.glow=previousGlow;
    else if(json.contains("bloom") && activeGlow(previousBloom))value.glow=previousBloom;
    else if(json.contains("glow") && activeGlow(previousGlow))value.glow=previousGlow;
    else if(json.contains("bloom"))value.glow=previousBloom;
    else if(json.contains("glow"))value.glow=previousGlow;
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
        {"ssr",{{"enabled",value.ssr.enabled},{"strength",value.ssr.strength},{"quality",static_cast<int>(value.ssr.quality)},
            {"maxDistance",value.ssr.maxDistance},{"depthTolerance",value.ssr.depthTolerance},
            {"maxRoughness",value.ssr.maxRoughness},{"bias",value.ssr.bias}}},
        {"ambientLight",{{"source",static_cast<int>(value.ambientLight.source)},{"color",value.ambientLight.color},
            {"skyContribution",value.ambientLight.skyContribution},{"energy",value.ambientLight.energy}}},
        {"glow",{{"source","Scene"},{"enabled",value.glow.enabled},{"strength",value.glow.strength},
            {"radius",value.glow.radius},{"threshold",value.glow.threshold},{"tint",value.glow.tint},
            {"saturation",value.glow.saturation},{"softKnee",value.glow.softKnee},
            {"blendMode",glowBlendModes[static_cast<std::size_t>(value.glow.blendMode)]},{"mix",value.glow.mix},
            {"anamorphic",value.glow.anamorphic},{"quality",static_cast<int>(value.glow.quality)},{"levels",value.glow.levels}}},
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
