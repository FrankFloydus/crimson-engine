////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_environment_library.hpp"

#include "core/logger.hpp"
#include "render/render_layers.hpp"
#include "core/scene_environment_catalog.hpp"

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

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>());
}

std::uint64_t hash_float(float value) {
  static_assert(sizeof(float) == sizeof(std::uint32_t));
  std::uint32_t bits = 0u;
  std::memcpy(&bits, &value, sizeof(float));
  return bits;
}

void hash_combine(std::uint64_t &seed, std::uint64_t value) {
  seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

std::uint64_t environment_signature(const SceneEnvironmentSettings &settings) {
  std::uint64_t seed = std::hash<std::string>{}(settings.environment_id);
  hash_combine(seed, settings.enabled ? 1u : 0u);
  hash_combine(seed, settings.show_background ? 1u : 0u);
  hash_combine(seed, hash_float(settings.intensity));
  hash_combine(seed, hash_float(settings.background_intensity));
  hash_combine(seed, hash_float(settings.rotation_y));
  return seed;
}

float filament_environment_intensity(float intensity) {
  return std::max(intensity, 0.0f) * kFilamentEnvironmentLuxPerIntensity;
}

} // namespace

FilamentEnvironmentLibrary::~FilamentEnvironmentLibrary() {
    shutdown();
}

void FilamentEnvironmentLibrary::initialize(
    filament::Engine &engine, filament::Scene &scene,
    std::filesystem::path resource_root) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  resource_root_ = std::move(resource_root);
}

void FilamentEnvironmentLibrary::shutdown() {
    if (!engine_) {
        return;
    }

    detach_environment();
    destroy_environment();
    resource_root_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

void FilamentEnvironmentLibrary::apply(const SceneEnvironmentSettings& settings) {
    if (!initialized()) {
        return;
    }

    const std::uint64_t signature = environment_signature(settings);
    if (signature == applied_signature_) {
      return;
    }

    if (!settings.enabled) {
      detach_environment();
      applied_signature_ = signature;
      return;
    }

    if (loaded_environment_id_ != settings.environment_id &&
        !load_environment(settings.environment_id)) {
      detach_environment();
      applied_signature_ = signature;
      return;
    }

    attach_environment(settings);
    applied_signature_ = signature;
}

bool FilamentEnvironmentLibrary::load_environment(std::string environment_id) {
  if (!engine_ || environment_id.empty()) {
    return false;
  }

  detach_environment();
  destroy_environment();

  const SceneEnvironmentDescriptor *descriptor =
      scene_environment_descriptor(environment_id);
  if (descriptor == nullptr) {
    LOG_WARN("Unknown Filament IBL environment '{}'", environment_id);
    return false;
  }

  const std::filesystem::path directory =
      resource_root_ / "ibl" / std::string(descriptor->directory);
  const std::filesystem::path prefix =
      directory / std::string(descriptor->file_stem);
  const std::filesystem::path ibl_path = prefix.string() + "_ibl.ktx";
  const std::filesystem::path skybox_path = prefix.string() + "_skybox.ktx";
  const std::vector<std::uint8_t> ibl_bytes = read_binary_file(ibl_path);
  const std::vector<std::uint8_t> skybox_bytes = read_binary_file(skybox_path);
  if (ibl_bytes.empty() || skybox_bytes.empty() ||
      ibl_bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
      skybox_bytes.size() > std::numeric_limits<std::uint32_t>::max()) {
    LOG_WARN("Unable to load Filament IBL environment '{}'", environment_id);
    return false;
  }

  auto *ibl_ktx = new image::Ktx1Bundle(
      ibl_bytes.data(), static_cast<std::uint32_t>(ibl_bytes.size()));
  auto *skybox_ktx = new image::Ktx1Bundle(
      skybox_bytes.data(), static_cast<std::uint32_t>(skybox_bytes.size()));
  has_spherical_harmonics_ =
      ibl_ktx->getSphericalHarmonics(spherical_harmonics_);

  reflection_texture_ =
      ktxreader::Ktx1Reader::createTexture(engine_, ibl_ktx, false);
  skybox_texture_ =
      ktxreader::Ktx1Reader::createTexture(engine_, skybox_ktx, false);
  if (reflection_texture_ == nullptr || skybox_texture_ == nullptr) {
    LOG_WARN("Filament IBL environment '{}' could not create GPU textures",
             environment_id);
    destroy_environment();
    return false;
  }

  loaded_environment_id_ = std::move(environment_id);
  return true;
}

void FilamentEnvironmentLibrary::attach_environment(
    const SceneEnvironmentSettings &settings) {
  if (!engine_ || !scene_ || reflection_texture_ == nullptr ||
      skybox_texture_ == nullptr) {
    return;
  }

  if (indirect_light_ != nullptr) {
    scene_->setIndirectLight(nullptr);
    engine_->destroy(indirect_light_);
    indirect_light_ = nullptr;
  }
  if (skybox_ != nullptr) {
    scene_->setSkybox(nullptr);
    engine_->destroy(skybox_);
    skybox_ = nullptr;
  }

  filament::IndirectLight::Builder indirect_builder;
  indirect_builder.reflections(reflection_texture_)
      .intensity(filament_environment_intensity(settings.intensity))
      .rotation(filament::math::mat3f::rotation(
          settings.rotation_y, filament::math::float3{0.0f, 1.0f, 0.0f}));
  if (has_spherical_harmonics_) {
    indirect_builder.irradiance(3, spherical_harmonics_);
  }
  indirect_light_ = indirect_builder.build(*engine_);
  if (indirect_light_ != nullptr) {
    scene_->setIndirectLight(indirect_light_);
  }

  if (settings.show_background && settings.background_intensity > 0.0f) {
    skybox_ = filament::Skybox::Builder()
                  .environment(skybox_texture_)
                  .showSun(true)
                  .intensity(filament_environment_intensity(
                      settings.background_intensity))
                  .build(*engine_);
    if (skybox_ != nullptr) {
      skybox_->setLayerMask(kViewportAllLayerMask,
                            kViewportRenderedSceneLayerMask);
      scene_->setSkybox(skybox_);
    }
  }
}

void FilamentEnvironmentLibrary::detach_environment() {
  if (!scene_) {
    return;
  }
  scene_->setSkybox(nullptr);
  scene_->setIndirectLight(nullptr);
}

void FilamentEnvironmentLibrary::destroy_environment() {
  if (!engine_) {
    return;
  }
  if (skybox_ != nullptr) {
    engine_->destroy(skybox_);
    skybox_ = nullptr;
  }
  if (indirect_light_ != nullptr) {
    engine_->destroy(indirect_light_);
    indirect_light_ = nullptr;
  }
  if (skybox_texture_ != nullptr) {
    engine_->destroy(skybox_texture_);
    skybox_texture_ = nullptr;
  }
  if (reflection_texture_ != nullptr) {
    engine_->destroy(reflection_texture_);
    reflection_texture_ = nullptr;
  }
  loaded_environment_id_.clear();
  has_spherical_harmonics_ = false;
  applied_signature_ = 0;
}

} // namespace quader
