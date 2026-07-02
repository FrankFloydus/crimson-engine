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

    void initialize(filament::Engine &engine, filament::Scene &scene,
                    std::filesystem::path resource_root);
    void shutdown();

    bool initialized() const { return engine_ != nullptr && scene_ != nullptr; }
    void apply(const SceneEnvironmentSettings& settings);

private:
  bool load_environment(std::string environment_id);
  void attach_environment(const SceneEnvironmentSettings &settings);
  void detach_environment();
  void destroy_environment();

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  std::filesystem::path resource_root_;
  std::string loaded_environment_id_;
  std::uint64_t applied_signature_ = 0;
  filament::Texture *reflection_texture_ = nullptr;
  filament::Texture *skybox_texture_ = nullptr;
  filament::IndirectLight *indirect_light_ = nullptr;
  filament::Skybox *skybox_ = nullptr;
  filament::math::float3 spherical_harmonics_[9]{};
  bool has_spherical_harmonics_ = false;
};

} // namespace quader
