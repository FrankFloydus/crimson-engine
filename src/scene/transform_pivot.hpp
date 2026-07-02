////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "scene/components.hpp"

namespace quader {

inline QVec3 local_pivot_for_world_point(const TransformComponent &transform,
                                         QVec3 world_pivot) {
  return transform_point(
      inverse(compose_transform(transform.spatial_transform())), world_pivot);
}

inline void pin_local_pivot_to_world_point(TransformComponent &transform,
                                           QVec3 local_pivot,
                                           QVec3 world_pivot) {
  QTransform spatial = transform.spatial_transform();
  spatial.position = {};
  transform.position =
      world_pivot - transform_point(compose_transform(spatial), local_pivot);
}

inline float radians_near_reference(float radians, float reference) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  if (!std::isfinite(radians) || !std::isfinite(reference)) {
    return radians;
  }
  return radians + kTwoPi * std::round((reference - radians) / kTwoPi);
}

inline float positive_radians(float radians) {
  constexpr float kTwoPi = 6.28318530717958647692f;
  if (!std::isfinite(radians)) {
    return radians;
  }
  float value = std::fmod(radians, kTwoPi);
  if (value < 0.0f) {
    value += kTwoPi;
  }
  if (std::abs(value) <= 0.00001f || std::abs(value - kTwoPi) <= 0.00001f) {
    return 0.0f;
  }
  return value;
}

inline QVec3 positive_euler_radians(QVec3 euler_radians) {
  return {
      positive_radians(euler_radians.x),
      positive_radians(euler_radians.y),
      positive_radians(euler_radians.z),
  };
}

inline QVec3
equivalent_euler_radians_closest_to(QVec3 euler_radians,
                                    QVec3 reference_euler_radians) {
  constexpr float kPi = 3.14159265358979323846f;
  const auto normalize_candidate = [&](QVec3 candidate) {
    return QVec3{
        radians_near_reference(candidate.x, reference_euler_radians.x),
        radians_near_reference(candidate.y, reference_euler_radians.y),
        radians_near_reference(candidate.z, reference_euler_radians.z),
    };
  };
  const auto score = [&](QVec3 candidate) {
    const QVec3 delta = candidate - reference_euler_radians;
    return dot(delta, delta);
  };

  const QVec3 primary = normalize_candidate(euler_radians);
  const QVec3 alternate = normalize_candidate({
      euler_radians.x + kPi,
      kPi - euler_radians.y,
      euler_radians.z + kPi,
  });
  return score(alternate) < score(primary) ? alternate : primary;
}

inline QVec3
euler_radians_after_world_rotation_delta(QVec3 current_euler_radians,
                                         QVec3 world_rotation_delta_radians) {
  const glm::quat delta = glm::quat(to_glm(world_rotation_delta_radians));
  const glm::quat current =
      to_glm(quat_from_euler_radians(current_euler_radians));
  return equivalent_euler_radians_closest_to(
      from_glm(glm::eulerAngles(glm::normalize(delta * current))),
      current_euler_radians);
}

inline void
apply_world_rotation_delta_around_pivot(TransformComponent &transform,
                                        QVec3 world_rotation_delta_radians,
                                        QVec3 local_pivot, QVec3 world_pivot) {
  transform.rotation_euler = euler_radians_after_world_rotation_delta(
      transform.rotation_euler, world_rotation_delta_radians);
  pin_local_pivot_to_world_point(transform, local_pivot, world_pivot);
}

} // namespace quader
