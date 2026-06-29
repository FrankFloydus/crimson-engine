////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_environment_library.hpp"

#include "core/logger.hpp"
#include "render/render_layers.hpp"

#include <image/Ktx1Bundle.h>
#include <ktxreader/Ktx1Reader.h>
#include <math/mat3.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <cstring>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kFilamentEnvironmentLuxPerIntensity = 30000.0f;

std::vector<std::uint8_t> readBinaryFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return {};
    }
    return std::vector<std::uint8_t>(
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>());
}

std::uint64_t hashFloat(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0u;
    std::memcpy(&bits, &value, sizeof(float));
    return bits;
}

void hashCombine(std::uint64_t& seed, std::uint64_t value) {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

std::uint64_t environmentSignature(const SceneEnvironmentSettings& settings) {
    std::uint64_t seed = std::hash<std::string>{}(settings.environmentId);
    hashCombine(seed, settings.enabled ? 1u : 0u);
    hashCombine(seed, settings.showBackground ? 1u : 0u);
    hashCombine(seed, hashFloat(settings.intensity));
    hashCombine(seed, hashFloat(settings.backgroundIntensity));
    hashCombine(seed, hashFloat(settings.rotationY));
    return seed;
}

float filamentEnvironmentIntensity(float intensity) {
    return std::max(intensity, 0.0f) * kFilamentEnvironmentLuxPerIntensity;
}

} // namespace

FilamentEnvironmentLibrary::~FilamentEnvironmentLibrary() {
    shutdown();
}

void FilamentEnvironmentLibrary::initialize(
        filament::Engine& engine,
        filament::Scene& scene,
        std::filesystem::path resourceRoot) {
    shutdown();
    engine_ = &engine;
    scene_ = &scene;
    resourceRoot_ = std::move(resourceRoot);
}

void FilamentEnvironmentLibrary::shutdown() {
    if (!engine_) {
        return;
    }

    detachEnvironment();
    destroyEnvironment();
    resourceRoot_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

void FilamentEnvironmentLibrary::apply(const SceneEnvironmentSettings& settings) {
    if (!initialized()) {
        return;
    }

    const std::uint64_t signature = environmentSignature(settings);
    if (signature == appliedSignature_) {
        return;
    }

    if (!settings.enabled) {
        detachEnvironment();
        appliedSignature_ = signature;
        return;
    }

    if (loadedEnvironmentId_ != settings.environmentId && !loadEnvironment(settings.environmentId)) {
        detachEnvironment();
        appliedSignature_ = signature;
        return;
    }

    attachEnvironment(settings);
    appliedSignature_ = signature;
}

bool FilamentEnvironmentLibrary::loadEnvironment(std::string environmentId) {
    if (!engine_ || environmentId.empty()) {
        return false;
    }

    detachEnvironment();
    destroyEnvironment();

    const std::filesystem::path directory = resourceRoot_ / "ibl" / environmentId;
    const std::filesystem::path prefix = directory / environmentId;
    const std::filesystem::path iblPath = prefix.string() + "_ibl.ktx";
    const std::filesystem::path skyboxPath = prefix.string() + "_skybox.ktx";
    const std::vector<std::uint8_t> iblBytes = readBinaryFile(iblPath);
    const std::vector<std::uint8_t> skyboxBytes = readBinaryFile(skyboxPath);
    if (iblBytes.empty() || skyboxBytes.empty() ||
            iblBytes.size() > std::numeric_limits<std::uint32_t>::max() ||
            skyboxBytes.size() > std::numeric_limits<std::uint32_t>::max()) {
        LOG_WARN("Unable to load Filament IBL environment '{}'", environmentId);
        return false;
    }

    auto* iblKtx = new image::Ktx1Bundle(iblBytes.data(), static_cast<std::uint32_t>(iblBytes.size()));
    auto* skyboxKtx = new image::Ktx1Bundle(skyboxBytes.data(), static_cast<std::uint32_t>(skyboxBytes.size()));
    hasSphericalHarmonics_ = iblKtx->getSphericalHarmonics(sphericalHarmonics_);

    reflectionTexture_ = ktxreader::Ktx1Reader::createTexture(engine_, iblKtx, false);
    skyboxTexture_ = ktxreader::Ktx1Reader::createTexture(engine_, skyboxKtx, false);
    if (reflectionTexture_ == nullptr || skyboxTexture_ == nullptr) {
        LOG_WARN("Filament IBL environment '{}' could not create GPU textures", environmentId);
        destroyEnvironment();
        return false;
    }

    loadedEnvironmentId_ = std::move(environmentId);
    return true;
}

void FilamentEnvironmentLibrary::attachEnvironment(const SceneEnvironmentSettings& settings) {
    if (!engine_ || !scene_ || reflectionTexture_ == nullptr || skyboxTexture_ == nullptr) {
        return;
    }

    if (indirectLight_ != nullptr) {
        scene_->setIndirectLight(nullptr);
        engine_->destroy(indirectLight_);
        indirectLight_ = nullptr;
    }
    if (skybox_ != nullptr) {
        scene_->setSkybox(nullptr);
        engine_->destroy(skybox_);
        skybox_ = nullptr;
    }

    filament::IndirectLight::Builder indirectBuilder;
    indirectBuilder
            .reflections(reflectionTexture_)
            .intensity(filamentEnvironmentIntensity(settings.intensity))
            .rotation(filament::math::mat3f::rotation(settings.rotationY, filament::math::float3{0.0f, 1.0f, 0.0f}));
    if (hasSphericalHarmonics_) {
        indirectBuilder.irradiance(3, sphericalHarmonics_);
    }
    indirectLight_ = indirectBuilder.build(*engine_);
    if (indirectLight_ != nullptr) {
        scene_->setIndirectLight(indirectLight_);
    }

    if (settings.showBackground) {
        skybox_ = filament::Skybox::Builder()
                .environment(skyboxTexture_)
                .showSun(true)
                .intensity(filamentEnvironmentIntensity(settings.backgroundIntensity))
                .build(*engine_);
        if (skybox_ != nullptr) {
            skybox_->setLayerMask(kViewportAllLayerMask, kViewportRenderedSceneLayerMask);
            scene_->setSkybox(skybox_);
        }
    }
}

void FilamentEnvironmentLibrary::detachEnvironment() {
    if (!scene_) {
        return;
    }
    scene_->setSkybox(nullptr);
    scene_->setIndirectLight(nullptr);
}

void FilamentEnvironmentLibrary::destroyEnvironment() {
    if (!engine_) {
        return;
    }
    if (skybox_ != nullptr) {
        engine_->destroy(skybox_);
        skybox_ = nullptr;
    }
    if (indirectLight_ != nullptr) {
        engine_->destroy(indirectLight_);
        indirectLight_ = nullptr;
    }
    if (skyboxTexture_ != nullptr) {
        engine_->destroy(skyboxTexture_);
        skyboxTexture_ = nullptr;
    }
    if (reflectionTexture_ != nullptr) {
        engine_->destroy(reflectionTexture_);
        reflectionTexture_ = nullptr;
    }
    loadedEnvironmentId_.clear();
    hasSphericalHarmonics_ = false;
    appliedSignature_ = 0;
}

} // namespace quader
