#pragma once

#include "assets/AssetLibrary.h"
#include <lodepng.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <stdexcept>
#include <vector>

namespace crimson {

// Explicit opt-in native integration check. App requires an isolated project root.
inline void verifyIblAssets(AssetLibrary& assets, SceneSettings& settings, const std::filesystem::path& root) {
    const auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    const auto folder = root / "smoke_sources";
    std::filesystem::create_directories(folder);
    std::array<std::string,2> normalIds;
    for (size_t kind = 0; kind < normalIds.size(); ++kind) {
        std::vector<uint8_t> pixels(128 * 64 * 4);
        for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) {
            const float u = (x+.5f)/128, v = (y+.5f)/64;
            // Periodic longitudinal ripples fade smoothly at both poles.
            const float tx = kind ? .6f*std::sin(u*16*std::numbers::pi_v<float>)*std::sin(v*std::numbers::pi_v<float>) : 0;
            const float ty = kind ? .35f*std::sin(v*12*std::numbers::pi_v<float>)*std::sin(v*std::numbers::pi_v<float>) : 0;
            const float tz = std::sqrt(1-tx*tx-ty*ty);
            const size_t offset = (static_cast<size_t>(y)*128+x)*4;
            pixels[offset] = static_cast<uint8_t>(std::lround((tx*.5f+.5f)*255));
            pixels[offset+1] = static_cast<uint8_t>(std::lround((ty*.5f+.5f)*255));
            pixels[offset+2] = static_cast<uint8_t>(std::lround((tz*.5f+.5f)*255));
            pixels[offset+3] = 255;
        }
        std::vector<uint8_t> encoded;
        require(lodepng::encode(encoded,pixels,128,64) == 0,"Normal fixture encoding failed.");
        const auto path = folder / (kind ? "IBL Detail Normal.png" : "IBL Neutral Normal.png");
        std::ofstream output(path,std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),static_cast<std::streamsize>(encoded.size()));
        output.close();
        const auto* imported = assets.importTexture(path);
        require(imported != nullptr,"Normal fixture import failed.");
        normalIds[kind] = imported->id;
    }
    // Radiance RGBE, uncompressed scanlines, with a bright HDR window and colored fill.
    const auto hdrPath = folder / "IBL Studio.hdr";
    std::ofstream hdr(hdrPath,std::ios::binary);
    hdr << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    for (int y=0;y<32;++y) for (int x=0;x<64;++x) {
        const bool window = x>=40 && x<47 && y>=7 && y<17;
        const std::array<float,3> rgb = window ? std::array<float,3>{12,10,8} :
            y < 16 ? std::array<float,3>{.35f,.5f,.8f} : std::array<float,3>{.08f,.06f,.04f};
        const float maximum = std::max({rgb[0],rgb[1],rgb[2]});
        int exponent = 0;
        const float scale = std::frexp(maximum,&exponent)*256/maximum;
        const std::array<uint8_t,4> rgbe{static_cast<uint8_t>(rgb[0]*scale),static_cast<uint8_t>(rgb[1]*scale),
            static_cast<uint8_t>(rgb[2]*scale),static_cast<uint8_t>(exponent+128)};
        hdr.write(reinterpret_cast<const char*>(rgbe.data()),4);
    }
    hdr.close();
    const auto* imported = assets.importTexture(hdrPath);
    require(imported && imported->hdr,"HDR environment import failed.");
    const std::string environmentId = imported->id;

    Material legacy;
    legacy.baseColor = {.27f,.31f,.63f}; legacy.roughness=.67f; legacy.metallic=.42f;
    { // Exercise the existing unversioned material format before adding normal data.
        std::ofstream output(root / "assets" / "material.json");
        output << R"({"baseColor":[0.27,0.31,0.63],"roughness":0.67,"metallic":0.42,"albedoMap":"","roughnessMap":"","metallicMap":""})";
    }
    Material restored;
    assets.loadMaterial(restored);
    require(restored.baseColor==legacy.baseColor && restored.roughness==legacy.roughness && restored.metallic==legacy.metallic &&
        restored.normalMap.empty() && restored.normalStrength==1,"Legacy material migration changed factors.");
    restored.normalStrength=.7f;
    require(assets.assign(restored,MaterialSlot::Normal,normalIds[0]),"Neutral normal assignment failed.");
    require(assets.assign(restored,MaterialSlot::Normal,normalIds[1]),"Normal replacement failed.");
    require(restored.baseColor==legacy.baseColor && restored.roughness==legacy.roughness && restored.metallic==legacy.metallic &&
        restored.normalStrength==.7f,"Normal assignment changed material factors.");
    assets.clear(restored,MaterialSlot::Normal);
    require(restored.normalMap.empty() && restored.normalStrength==.7f,"Normal clear changed strength.");
    require(!assets.assign(restored,MaterialSlot::Normal,environmentId),"HDR was accepted as a normal map.");
    require(assets.assignEnvironment(settings.environment,environmentId),"Environment assignment failed.");
    settings.environment.rotation=37; settings.environment.intensity=2.5f; settings.environment.enabled=false;
    assets.saveEnvironment(settings.environment);
    EnvironmentSettings reopened;
    assets.loadEnvironment(reopened);
    require(reopened.map==environmentId && reopened.rotation==37 && reopened.intensity==2.5f && !reopened.enabled,
        "Environment settings did not survive loading.");
    assets.clearEnvironment(reopened);
    require(reopened.map.empty() && reopened.rotation==37 && reopened.intensity==2.5f && !reopened.enabled,"Environment clear changed lighting settings.");

    settings.material = Material{};
    settings.material.baseColor={.8f,.8f,.8f}; settings.material.roughness=.12f;
    require(assets.assign(settings.material,MaterialSlot::Normal,normalIds[1]),"Final normal assignment failed.");
    assets.saveMaterial(settings.material);
    settings.environment = EnvironmentSettings{};
    require(assets.assignEnvironment(settings.environment,environmentId),"Final environment assignment failed.");
    Material reloaded;
    assets.loadMaterial(reloaded);
    require(reloaded.normalMap==settings.material.normalMap && reloaded.normalStrength==settings.material.normalStrength &&
        reloaded.baseColor==settings.material.baseColor && reloaded.roughness==settings.material.roughness,
        "Normal/material settings did not survive loading.");
    require(bgfx::isValid(assets.bindings(reloaded).normal),"Normal map GPU upload failed.");
}

} // namespace crimson
