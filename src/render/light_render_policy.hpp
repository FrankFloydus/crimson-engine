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
#include "scene/light_properties.hpp"

#include <filament/LightManager.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <numbers>

namespace quader {

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
  const LightEngineValues values = light_engine_values(light);
  const double emission = light.type == LightType::Directional
      ? static_cast<double>(values.illuminance_lux)
      : static_cast<double>(values.luminous_intensity_candela) *
          (light.type == LightType::Point ? 4.0 * std::numbers::pi
                                         : std::numbers::pi);
  // Retain historical authored values while keeping derived backend inputs finite.
  return static_cast<float>(
      std::min(emission,
               static_cast<double>(std::numeric_limits<float>::max())));
}

/** Resource identity excludes editable emission and authoring-only metadata. */
[[nodiscard]] inline bool filament_light_structure_matches(
    const LightComponent &before, const LightComponent &after) {
  return before.type == after.type &&
         before.casts_shadows == after.casts_shadows;
}

/** Compares only properties consumed by the Filament light component. */
[[nodiscard]] inline bool filament_light_properties_match(
    const LightComponent &before, const LightComponent &after) {
  return filament_light_structure_matches(before, after) &&
         before.color.x == after.color.x && before.color.y == after.color.y &&
         before.color.z == after.color.z &&
         before.intensity == after.intensity && before.range == after.range &&
         before.distance_attenuation == after.distance_attenuation;
}

/** Updates retained resources; inactive preview pool entries remain unlit. */
inline void update_filament_light_properties(
    filament::LightManager &manager, filament::LightManager::Instance instance,
    const LightComponent &before, const LightComponent &after,
    bool was_enabled = true, bool enabled = true) {
  if (before.color.x != after.color.x || before.color.y != after.color.y ||
      before.color.z != after.color.z) {
    manager.setColor(instance, {after.color.x, after.color.y, after.color.z});
  }
  if (was_enabled != enabled || before.intensity != after.intensity) {
    manager.setIntensity(instance,
                         enabled ? filament_light_intensity(after) : 0.0f);
  }
  if (before.range != after.range) {
    manager.setFalloff(instance, filament_light_falloff(after));
  }
  if (before.distance_attenuation != after.distance_attenuation) {
    manager.setDistanceAttenuation(instance, after.distance_attenuation);
  }
}

} // namespace quader
