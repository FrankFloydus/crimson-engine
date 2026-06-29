////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/components.hpp"

#include <filament/Engine.h>
#include <filament/IndirectLight.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <math/vec3.h>

#include <cstdint>
#include <filesystem>
#include <string>

namespace quader {

/**
 * Owns Filament scene IBL and skybox resources for viewport rendered mode.
 */
class FilamentEnvironmentLibrary {
public:
    FilamentEnvironmentLibrary() = default;
    FilamentEnvironmentLibrary(const FilamentEnvironmentLibrary&) = delete;
    FilamentEnvironmentLibrary& operator=(const FilamentEnvironmentLibrary&) = delete;
    FilamentEnvironmentLibrary(FilamentEnvironmentLibrary&&) = delete;
    FilamentEnvironmentLibrary& operator=(FilamentEnvironmentLibrary&&) = delete;
    ~FilamentEnvironmentLibrary();

    void initialize(filament::Engine& engine, filament::Scene& scene, std::filesystem::path resourceRoot);
    void shutdown();

    bool initialized() const { return engine_ != nullptr && scene_ != nullptr; }
    void apply(const SceneEnvironmentSettings& settings);

private:
    bool loadEnvironment(std::string environmentId);
    void attachEnvironment(const SceneEnvironmentSettings& settings);
    void detachEnvironment();
    void destroyEnvironment();

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    std::filesystem::path resourceRoot_;
    std::string loadedEnvironmentId_;
    std::uint64_t appliedSignature_ = 0;
    filament::Texture* reflectionTexture_ = nullptr;
    filament::Texture* skyboxTexture_ = nullptr;
    filament::IndirectLight* indirectLight_ = nullptr;
    filament::Skybox* skybox_ = nullptr;
    filament::math::float3 sphericalHarmonics_[9]{};
    bool hasSphericalHarmonics_ = false;
};

} // namespace quader
