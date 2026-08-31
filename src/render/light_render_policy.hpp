////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <filament/LightManager.h>

#include <algorithm>
#include <cstddef>

namespace quader {

inline constexpr float kFilamentPunctualLightLumensPerIntensity = 1000.0f;
inline constexpr float kFilamentPunctualLightExposureCompensation = 10000.0f;
inline constexpr float kFilamentDirectionalLightLuxPerIntensity = 100000.0f;
// Filament exposes at most 255 punctual lights to one View. Keep the preview
// generation within the same backend contract so it never publishes an
// apparently complete candidate that the lighting pass must truncate.
inline constexpr std::size_t kMaximumFilamentPunctualLightCount = 255u;

[[nodiscard]] inline bool filament_light_is_punctual(LightType type) {
  return type == LightType::Point || type == LightType::Spot;
}

[[nodiscard]] inline filament::LightManager::Type
filament_light_type(LightType type) {
  switch (type) {
  case LightType::Directional:
    return filament::LightManager::Type::DIRECTIONAL;
  case LightType::Point:
    return filament::LightManager::Type::POINT;
  case LightType::Spot:
    return filament::LightManager::Type::SPOT;
  }
  return filament::LightManager::Type::DIRECTIONAL;
}

[[nodiscard]] inline filament::math::float3
filament_light_direction(const QMat4 &transform) {
  QVec3 direction = transform_vector(transform, {0.0f, -1.0f, 0.0f});
  if (length(direction) <= 0.000001f) {
    direction = {0.0f, -1.0f, 0.0f};
  }
  direction = normalize(direction);
  return {direction.x, direction.y, direction.z};
}

[[nodiscard]] inline filament::math::float3
filament_light_position(const QMat4 &transform) {
  const QVec3 position = transform_point(transform, {});
  return {position.x, position.y, position.z};
}

[[nodiscard]] inline float filament_light_falloff(const LightComponent &light) {
  return std::max(light.range, 0.001f);
}

[[nodiscard]] inline float
filament_light_intensity(const LightComponent &light) {
  const float energy = std::max(light.intensity, 0.0f);
  switch (light.type) {
  case LightType::Directional:
    return energy * kFilamentDirectionalLightLuxPerIntensity;
  case LightType::Point:
  case LightType::Spot:
    return energy * kFilamentPunctualLightLumensPerIntensity *
           kFilamentPunctualLightExposureCompensation;
  }
  return energy * kFilamentPunctualLightLumensPerIntensity *
         kFilamentPunctualLightExposureCompensation;
}

} // namespace quader
