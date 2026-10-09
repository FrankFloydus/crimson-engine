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
  // Standalone transforms are rooted in the supplied frame. Scene hierarchy
  // consumers additionally account for parent placement through their owner.
  return transform_point(
      inverse(compose_transform(transform.spatial_transform())),
      transform_point(inverse(transform.parenting_offset), world_pivot));
}

inline void pin_local_pivot_to_world_point(TransformComponent &transform,
                                           QVec3 local_pivot,
                                           QVec3 world_pivot) {
  QTransform spatial = transform.spatial_transform();
  spatial.location = {};
  transform.location =
      transform_point(inverse(transform.parenting_offset), world_pivot) -
      transform_point(compose_transform(spatial), local_pivot);
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

/**
 * Extracts equivalent XYZ angles using one shared decision at the Y singularity.
 * Independent pitch/roll singularity checks can select incompatible branches
 * for float quaternions and change the represented orientation by half a turn.
 */
template <typename Scalar, glm::qualifier Qualifier>
inline QVec3 euler_radians_from_rotation_closest_to(
    const glm::qua<Scalar, Qualifier> &rotation,
    QVec3 reference_euler_radians) {
  const glm::dquat normalized = glm::normalize(glm::dquat{rotation});
  const glm::dmat3 basis = glm::mat3_cast(normalized);
  const double cosine_y = std::hypot(basis[0][0], basis[0][1]);

  // Float-authored rotations need a shared tolerance around gimbal lock.
  // Collapsing this band to the singular frame changes orientation by at most
  // about two microradians, while retaining the reference-nearest coupled X/Z.
  constexpr double kGimbalCosineTolerance = 0.000001;
  if (cosine_y <= kGimbalCosineTolerance) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kTwoPi = 2.0 * kPi;
    const double sign_y = basis[0][2] < 0.0 ? 1.0 : -1.0;
    const double reference_x = reference_euler_radians.x;
    const double reference_z = reference_euler_radians.z;
    const double reference_coupling = reference_x - sign_y * reference_z;
    const double coupling = std::atan2(-basis[2][1], basis[1][1]);
    const double closest_coupling =
        coupling +
        kTwoPi * std::round((reference_coupling - coupling) / kTwoPi);
    const double adjustment = 0.5 * (closest_coupling - reference_coupling);
    const double y = sign_y * 0.5 * kPi;
    return {
        static_cast<float>(reference_x + adjustment),
        static_cast<float>(
            y + kTwoPi * std::round((reference_euler_radians.y - y) / kTwoPi)),
        static_cast<float>(reference_z - sign_y * adjustment),
    };
  }

  return equivalent_euler_radians_closest_to(
      {static_cast<float>(std::atan2(basis[1][2], basis[2][2])),
       static_cast<float>(std::atan2(-basis[0][2], cosine_y)),
       static_cast<float>(std::atan2(basis[0][1], basis[0][0]))},
      reference_euler_radians);
}

inline QVec3
euler_radians_after_world_rotation_delta(QVec3 current_euler_radians,
                                         QVec3 world_rotation_delta_radians) {
  const glm::quat delta = glm::quat(to_glm(world_rotation_delta_radians));
  const glm::quat current =
      to_glm(quat_from_euler_radians(current_euler_radians));
  return euler_radians_from_rotation_closest_to(
      glm::normalize(delta * current),
      current_euler_radians);
}

inline void
apply_world_rotation_delta_around_pivot(TransformComponent &transform,
                                        QVec3 world_rotation_delta_radians,
                                        QVec3 local_pivot, QVec3 world_pivot) {
  // This legacy standalone helper rotates the editable channel frame. Scene
  // world-space rotation is owned by SceneTransformDragService, which validates
  // representability against the complete compensated parent matrix.
  transform.rotation_euler = euler_radians_after_world_rotation_delta(
      transform.rotation_euler, world_rotation_delta_radians);
  pin_local_pivot_to_world_point(transform, local_pivot, world_pivot);
}

} // namespace quader
