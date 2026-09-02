////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/glm_bridge.hpp"
#include "scene/scene_transform.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {

inline constexpr std::uint32_t kPathArrayLayoutMinimumCount = 2U;
inline constexpr std::uint32_t kPathArrayLayoutMaximumCount = 128U;

/** Selects the source-local direction that Follow Path aligns to the tangent.
 */
enum class PathArraySignedAxis : std::uint8_t {
  PositiveX,
  NegativeX,
  PositiveY,
  NegativeY,
  PositiveZ,
  NegativeZ,
};

/**
 * Stores one ordered, world-space path evaluation and its cumulative distance.
 *
 * Adjacent stations may share a distance at an authored corner. Their positions
 * must agree; the last valid tangent owns that station so sampler order selects
 * the outgoing side.
 */
struct PathArrayPathStation {
  double distance = 0.0;
  QVec3 position_world = {};
  QVec3 tangent_world = {};
};

inline constexpr QVec3 kPathArrayDefaultRotationOffsetDegrees = {};
inline constexpr QVec3 kPathArrayDefaultRotationRandomMinDegrees = {};
inline constexpr QVec3 kPathArrayDefaultRotationRandomMaxDegrees = {};
inline constexpr QVec3 kPathArrayDefaultScaleOffsetFactors = {1.0F, 1.0F,
                                                               1.0F};
inline constexpr QVec3 kPathArrayDefaultScaleRandomMinFactors = {1.0F, 1.0F,
                                                                  1.0F};
inline constexpr QVec3 kPathArrayDefaultScaleRandomMaxFactors = {1.0F, 1.0F,
                                                                  1.0F};

/**
 * Describes deterministic per-copy variation in the copy's oriented frame.
 *
 * Scale values are multiplicative factors. Index zero deliberately ignores
 * every field so the authored source remains the first array result exactly.
 */
struct PathArrayVariationSettings final {
  QVec3 rotation_offset_degrees = kPathArrayDefaultRotationOffsetDegrees;
  QVec3 rotation_random_min_degrees =
      kPathArrayDefaultRotationRandomMinDegrees;
  QVec3 rotation_random_max_degrees =
      kPathArrayDefaultRotationRandomMaxDegrees;
  QVec3 scale_offset_factors = kPathArrayDefaultScaleOffsetFactors;
  QVec3 scale_random_min_factors = kPathArrayDefaultScaleRandomMinFactors;
  QVec3 scale_random_max_factors = kPathArrayDefaultScaleRandomMaxFactors;
  std::uint64_t seed = 0U;

  friend bool operator==(const PathArrayVariationSettings &left,
                         const PathArrayVariationSettings &right) {
    const auto same = [](QVec3 first, QVec3 second) {
      return first.x == second.x && first.y == second.y &&
             first.z == second.z;
    };
    return same(left.rotation_offset_degrees,
                right.rotation_offset_degrees) &&
           same(left.rotation_random_min_degrees,
                right.rotation_random_min_degrees) &&
           same(left.rotation_random_max_degrees,
                right.rotation_random_max_degrees) &&
           same(left.scale_offset_factors, right.scale_offset_factors) &&
           same(left.scale_random_min_factors,
                right.scale_random_min_factors) &&
           same(left.scale_random_max_factors,
                right.scale_random_max_factors) &&
           left.seed == right.seed;
  }
};

/** Describes one source-inclusive path-array layout. */
struct PathArrayLayoutRequest {
  std::span<const PathArrayPathStation> path_stations;
  QMat4 source_world_transform;
  // Maps the source root's authored local axes into world space before its
  // local signed scale is applied. The 3x3 linear part is used; translation is
  // ignored. Native callers provide this explicitly so mirrored roots and
  // transformed parent hierarchies retain normal local-transform semantics.
  std::optional<QMat4> source_variation_frame_world;
  QVec3 source_pivot_world = {};
  std::uint32_t count = 4U;
  double distance = 1.0;
  bool follow_path = false;
  PathArraySignedAxis signed_axis = PathArraySignedAxis::PositiveX;
  bool closed = false;
  PathArrayVariationSettings variation;
};

/**
 * Returns source-inclusive matrices in authored path order.
 *
 * `effective_distance` is the exact requested spacing. Open paths extrapolate
 * beyond their authored end and closed paths wrap stations around the seam.
 */
struct PathArrayLayoutResult {
  bool ok = false;
  std::string message;
  double effective_distance = 0.0;
  double total_path_length = 0.0;
  std::vector<QMat4> world_transforms;
};

namespace path_array_layout_detail {

inline constexpr float kVectorEpsilon = 0.000001F;
inline constexpr double kDistanceEpsilon = 0.000000001;

/** Stores one sampled station after endpoint tangent policy is resolved. */
struct EvaluatedStation {
  double distance = 0.0;
  glm::vec3 position = {};
  glm::vec3 tangent = {};
};

inline bool finite_vector(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

inline bool finite_vector(glm::vec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

inline bool finite_matrix(const glm::mat3 &value) {
  for (glm::length_t column = 0; column < 3; ++column) {
    if (!finite_vector(value[column])) {
      return false;
    }
  }
  return true;
}

inline glm::vec3 normalized_or_zero(glm::vec3 value) {
  const float magnitude = glm::length(value);
  return std::isfinite(magnitude) && magnitude > kVectorEpsilon
             ? value / magnitude
             : glm::vec3{};
}

inline bool near_distance(double left, double right) {
  const double scale = std::max({1.0, std::abs(left), std::abs(right)});
  return std::abs(left - right) <= kDistanceEpsilon * scale;
}

inline bool near_position(glm::vec3 left, glm::vec3 right) {
  constexpr float kAbsoluteTolerance = 0.00001F;
  const float scale = std::max({1.0F, glm::length(left), glm::length(right)});
  return glm::length(left - right) <= kAbsoluteTolerance * scale;
}

inline glm::vec3 deterministic_perpendicular(glm::vec3 unit_direction) {
  const glm::vec3 absolute = glm::abs(unit_direction);
  const glm::vec3 reference =
      absolute.x <= absolute.y && absolute.x <= absolute.z
          ? glm::vec3{1.0F, 0.0F, 0.0F}
          : (absolute.y <= absolute.z ? glm::vec3{0.0F, 1.0F, 0.0F}
                                      : glm::vec3{0.0F, 0.0F, 1.0F});
  return normalized_or_zero(glm::cross(unit_direction, reference));
}

inline glm::vec3
directed_tangent_fallback(const std::vector<EvaluatedStation> &stations,
                          std::size_t index) {
  if (stations.size() < 2U) {
    return {};
  }
  if (index + 1U < stations.size()) {
    const glm::vec3 outgoing = normalized_or_zero(
        stations[index + 1U].position - stations[index].position);
    if (glm::dot(outgoing, outgoing) > 0.0F) {
      return outgoing;
    }
  }
  return index > 0U ? normalized_or_zero(stations[index].position -
                                         stations[index - 1U].position)
                    : glm::vec3{};
}

inline bool canonicalize_stations(std::span<const PathArrayPathStation> input,
                                  bool closed,
                                  std::vector<EvaluatedStation> &output,
                                  std::string &message) {
  if (input.size() < 2U) {
    message = "Path Array requires at least two path stations.";
    return false;
  }
  for (const PathArrayPathStation &station : input) {
    if (!std::isfinite(station.distance) ||
        !finite_vector(station.position_world) ||
        !finite_vector(station.tangent_world)) {
      message = "Path Array path stations must be finite.";
      return false;
    }
  }

  const double authored_start_distance = input.front().distance;
  output.clear();
  output.reserve(input.size());
  for (std::size_t first = 0U; first < input.size();) {
    std::size_t end = first + 1U;
    while (end < input.size() &&
           near_distance(input[end].distance, input[first].distance)) {
      ++end;
    }
    if (input[first].distance + kDistanceEpsilon < authored_start_distance ||
        (!output.empty() &&
         input[first].distance + kDistanceEpsilon <
             output.back().distance + authored_start_distance)) {
      message = "Path Array station distances must be ordered.";
      return false;
    }

    const glm::vec3 position = to_glm(input[first].position_world);
    glm::vec3 last_tangent{};
    for (std::size_t index = first; index < end; ++index) {
      const glm::vec3 candidate = to_glm(input[index].position_world);
      if (!near_position(candidate, position)) {
        message = "Path Array stations at one distance must share a position.";
        return false;
      }
      const glm::vec3 tangent =
          normalized_or_zero(to_glm(input[index].tangent_world));
      if (glm::dot(tangent, tangent) > 0.0F) {
        last_tangent = tangent;
      }
    }
    const double relative_distance =
        input[first].distance - authored_start_distance;
    if (!std::isfinite(relative_distance)) {
      message = "Path Array station distance range is too large.";
      return false;
    }
    output.push_back({
        .distance = relative_distance,
        .position = position,
        .tangent = last_tangent,
    });
    first = end;
  }

  if (output.size() < 2U || output.back().distance <= kDistanceEpsilon) {
    message = "Path Array requires a path with nonzero length.";
    return false;
  }
  for (std::size_t index = 1U; index < output.size(); ++index) {
    if (output[index].distance <= output[index - 1U].distance) {
      message = "Path Array station distances must increase.";
      return false;
    }
  }
  if (closed &&
      !near_position(output.front().position, output.back().position)) {
    message = "A closed Path Array must include its closing station.";
    return false;
  }

  for (std::size_t index = 0U; index < output.size(); ++index) {
    if (glm::dot(output[index].tangent, output[index].tangent) == 0.0F) {
      output[index].tangent = directed_tangent_fallback(output, index);
    }
    if (glm::dot(output[index].tangent, output[index].tangent) == 0.0F) {
      message = "Path Array could not resolve a path tangent.";
      return false;
    }
  }

  return true;
}

inline EvaluatedStation
evaluate_at_distance(const std::vector<EvaluatedStation> &stations,
                     double distance) {
  if (distance <= 0.0) {
    return stations.front();
  }
  if (near_distance(distance, stations.back().distance)) {
    return stations.back();
  }
  if (distance > stations.back().distance) {
    const EvaluatedStation &endpoint = stations.back();
    const float extrapolation =
        static_cast<float>(distance - endpoint.distance);
    return {
        .distance = distance,
        .position = endpoint.position + endpoint.tangent * extrapolation,
        .tangent = endpoint.tangent,
    };
  }
  const auto upper =
      std::upper_bound(stations.begin(), stations.end(), distance,
                       [](double value, const EvaluatedStation &station) {
                         return value < station.distance;
                       });
  const auto lower = upper - 1;
  if (near_distance(distance, lower->distance)) {
    return *lower;
  }
  if (near_distance(distance, upper->distance)) {
    return *upper;
  }

  const double interval = upper->distance - lower->distance;
  const float factor =
      static_cast<float>((distance - lower->distance) / interval);
  const glm::vec3 position = glm::mix(lower->position, upper->position, factor);
  glm::vec3 tangent =
      normalized_or_zero(glm::mix(lower->tangent, upper->tangent, factor));
  if (glm::dot(tangent, tangent) == 0.0F) {
    tangent = normalized_or_zero(upper->position - lower->position);
  }
  if (glm::dot(tangent, tangent) == 0.0F) {
    tangent = factor < 0.5F ? lower->tangent : upper->tangent;
  }
  return {.distance = distance, .position = position, .tangent = tangent};
}

inline glm::vec3 local_axis(PathArraySignedAxis axis) {
  switch (axis) {
  case PathArraySignedAxis::PositiveX:
    return {1.0F, 0.0F, 0.0F};
  case PathArraySignedAxis::NegativeX:
    return {-1.0F, 0.0F, 0.0F};
  case PathArraySignedAxis::PositiveY:
    return {0.0F, 1.0F, 0.0F};
  case PathArraySignedAxis::NegativeY:
    return {0.0F, -1.0F, 0.0F};
  case PathArraySignedAxis::PositiveZ:
    return {0.0F, 0.0F, 1.0F};
  case PathArraySignedAxis::NegativeZ:
    return {0.0F, 0.0F, -1.0F};
  }
  return {};
}

inline std::array<glm::vec3, 2U> roll_reference_axes(PathArraySignedAxis axis) {
  switch (axis) {
  case PathArraySignedAxis::PositiveX:
  case PathArraySignedAxis::NegativeX:
    return {glm::vec3{0.0F, 1.0F, 0.0F}, glm::vec3{0.0F, 0.0F, 1.0F}};
  case PathArraySignedAxis::PositiveY:
  case PathArraySignedAxis::NegativeY:
    return {glm::vec3{0.0F, 0.0F, 1.0F}, glm::vec3{1.0F, 0.0F, 0.0F}};
  case PathArraySignedAxis::PositiveZ:
  case PathArraySignedAxis::NegativeZ:
    return {glm::vec3{1.0F, 0.0F, 0.0F}, glm::vec3{0.0F, 1.0F, 0.0F}};
  }
  return {};
}

inline glm::vec3 source_roll_reference(const glm::mat4 &source_world,
                                       glm::vec3 source_axis,
                                       PathArraySignedAxis signed_axis) {
  for (const glm::vec3 local : roll_reference_axes(signed_axis)) {
    const glm::vec3 transformed =
        glm::vec3{source_world * glm::vec4{local, 0.0F}};
    const glm::vec3 projected = normalized_or_zero(
        transformed - source_axis * glm::dot(transformed, source_axis));
    if (glm::dot(projected, projected) > 0.0F) {
      return projected;
    }
  }
  return deterministic_perpendicular(source_axis);
}

inline glm::quat shortest_arc(glm::vec3 from, glm::vec3 to,
                              glm::vec3 antiparallel_axis) {
  from = normalized_or_zero(from);
  to = normalized_or_zero(to);
  const float cosine = std::clamp(glm::dot(from, to), -1.0F, 1.0F);
  if (cosine >= 1.0F - kVectorEpsilon) {
    return {1.0F, 0.0F, 0.0F, 0.0F};
  }
  if (cosine <= -1.0F + kVectorEpsilon) {
    antiparallel_axis =
        antiparallel_axis - from * glm::dot(antiparallel_axis, from);
    antiparallel_axis = normalized_or_zero(antiparallel_axis);
    if (glm::dot(antiparallel_axis, antiparallel_axis) == 0.0F) {
      antiparallel_axis = deterministic_perpendicular(from);
    }
    return glm::angleAxis(std::numbers::pi_v<float>, antiparallel_axis);
  }
  const glm::vec3 rotation_axis = glm::cross(from, to);
  return glm::normalize(glm::quat{1.0F + cosine, rotation_axis.x,
                                  rotation_axis.y, rotation_axis.z});
}

inline float signed_angle(glm::vec3 from, glm::vec3 to, glm::vec3 unit_axis) {
  from = normalized_or_zero(from - unit_axis * glm::dot(from, unit_axis));
  to = normalized_or_zero(to - unit_axis * glm::dot(to, unit_axis));
  if (glm::dot(from, from) == 0.0F || glm::dot(to, to) == 0.0F) {
    return 0.0F;
  }
  return std::atan2(glm::dot(unit_axis, glm::cross(from, to)),
                    std::clamp(glm::dot(from, to), -1.0F, 1.0F));
}

inline bool finite_station(const EvaluatedStation &station) {
  return std::isfinite(station.distance) && finite_vector(station.position) &&
         finite_vector(station.tangent);
}

inline double positive_modulo(double value, double modulus) {
  double wrapped = std::fmod(value, modulus);
  if (wrapped < 0.0) {
    wrapped += modulus;
  }
  if (near_distance(wrapped, 0.0) || near_distance(wrapped, modulus)) {
    return 0.0;
  }
  return wrapped;
}

inline bool componentwise_less_equal(QVec3 minimum, QVec3 maximum) {
  return minimum.x <= maximum.x && minimum.y <= maximum.y &&
         minimum.z <= maximum.z;
}

inline bool componentwise_strictly_positive(QVec3 value) {
  return value.x > 0.0F && value.y > 0.0F && value.z > 0.0F;
}

inline bool zero_vector(QVec3 value) {
  return value.x == 0.0F && value.y == 0.0F && value.z == 0.0F;
}

inline std::uint64_t splitmix64(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

/** Returns one stable unit sample for an independent station/channel pair. */
inline double variation_unit_sample(std::uint64_t seed, std::uint32_t index,
                                    std::uint32_t channel) {
  std::uint64_t key = splitmix64(seed ^ 0x4f1bbcdc6762c19dULL);
  key ^= splitmix64(static_cast<std::uint64_t>(index) ^
                    0x632be59bd9b4e019ULL);
  key ^= splitmix64(static_cast<std::uint64_t>(channel) ^
                    0x8cb92baa3f3d8dd7ULL);
  const std::uint64_t bits = splitmix64(key);
  return static_cast<double>(bits >> 11U) * 0x1.0p-53;
}

inline double sample_range(double minimum, double maximum, double unit) {
  return minimum == maximum ? minimum : std::lerp(minimum, maximum, unit);
}

inline float reduced_degrees_to_radians(double degrees) {
  const double reduced = std::remainder(degrees, 360.0);
  return static_cast<float>(reduced * std::numbers::pi_v<double> / 180.0);
}

inline glm::quat rotation_from_degrees(QVec3 degrees) {
  return glm::normalize(to_glm(quat_from_euler_radians(
      {reduced_degrees_to_radians(degrees.x),
       reduced_degrees_to_radians(degrees.y),
       reduced_degrees_to_radians(degrees.z)})));
}

inline QVec3 sampled_rotation_degrees(
    const PathArrayVariationSettings &settings, std::uint32_t index) {
  return {
      static_cast<float>(sample_range(
          settings.rotation_random_min_degrees.x,
          settings.rotation_random_max_degrees.x,
          variation_unit_sample(settings.seed, index, 0U))),
      static_cast<float>(sample_range(
          settings.rotation_random_min_degrees.y,
          settings.rotation_random_max_degrees.y,
          variation_unit_sample(settings.seed, index, 1U))),
      static_cast<float>(sample_range(
          settings.rotation_random_min_degrees.z,
          settings.rotation_random_max_degrees.z,
          variation_unit_sample(settings.seed, index, 2U))),
  };
}

inline std::optional<glm::vec3> sampled_scale_factors(
    const PathArrayVariationSettings &settings, std::uint32_t index) {
  const std::array<double, 3U> offset{
      settings.scale_offset_factors.x,
      settings.scale_offset_factors.y,
      settings.scale_offset_factors.z,
  };
  const std::array<double, 3U> minimum{
      settings.scale_random_min_factors.x,
      settings.scale_random_min_factors.y,
      settings.scale_random_min_factors.z,
  };
  const std::array<double, 3U> maximum{
      settings.scale_random_max_factors.x,
      settings.scale_random_max_factors.y,
      settings.scale_random_max_factors.z,
  };
  glm::vec3 factors{1.0F};
  for (std::size_t axis = 0U; axis < 3U; ++axis) {
    const double cumulative = std::pow(offset[axis], index);
    const double random = sample_range(
        minimum[axis], maximum[axis],
        variation_unit_sample(settings.seed, index,
                              static_cast<std::uint32_t>(axis + 3U)));
    const double factor = cumulative * random;
    if (!std::isfinite(factor) || factor <= 0.0 ||
        factor > std::numeric_limits<float>::max()) {
      return std::nullopt;
    }
    const float stored_factor = static_cast<float>(factor);
    if (!std::isfinite(stored_factor) || stored_factor <= 0.0F) {
      return std::nullopt;
    }
    factors[static_cast<glm::length_t>(axis)] = stored_factor;
  }
  return factors;
}

inline bool apply_variation(const QMat4 &base, glm::vec3 pivot,
                            std::uint32_t index,
                            const glm::quat &cumulative_rotation,
                            const glm::mat3 &variation_frame,
                            const glm::mat3 &variation_frame_inverse,
                            const PathArrayVariationSettings &settings,
                            QMat4 &world, std::string &message) {
  if (index == 0U) {
    world = base;
    return true;
  }
  const QVec3 random_degrees = sampled_rotation_degrees(settings, index);
  if (!finite_vector(random_degrees)) {
    message = "Path Array generated a non-finite random rotation.";
    return false;
  }
  const std::optional<glm::vec3> scale =
      sampled_scale_factors(settings, index);
  if (!scale.has_value()) {
    message = "Path Array generated a non-finite or singular scale.";
    return false;
  }
  const bool rotation_is_identity =
      zero_vector(settings.rotation_offset_degrees) &&
      zero_vector(random_degrees);
  const bool scale_is_identity =
      scale->x == 1.0F && scale->y == 1.0F && scale->z == 1.0F;
  if (rotation_is_identity && scale_is_identity) {
    world = base;
    return true;
  }

  glm::mat4 oriented_frame{1.0F};
  glm::mat4 oriented_frame_inverse{1.0F};
  for (glm::length_t column = 0; column < 3; ++column) {
    for (glm::length_t row = 0; row < 3; ++row) {
      oriented_frame[column][row] = variation_frame[column][row];
      oriented_frame_inverse[column][row] =
          variation_frame_inverse[column][row];
    }
  }
  const glm::quat random_rotation = rotation_from_degrees(random_degrees);
  const glm::quat variation_rotation =
      glm::normalize(cumulative_rotation * random_rotation);
  const glm::mat4 varied =
      glm::translate(glm::mat4{1.0F}, pivot) * oriented_frame *
      glm::mat4_cast(variation_rotation) *
      glm::scale(glm::mat4{1.0F}, *scale) * oriented_frame_inverse *
      glm::translate(glm::mat4{1.0F}, -pivot) * to_glm(base);
  world = from_glm(varied);
  if (!scene_matrix_is_finite(world)) {
    message = "Path Array generated a non-finite variation transform.";
    return false;
  }
  return true;
}

} // namespace path_array_layout_detail

/** Returns an empty string when all authored variation inputs are valid. */
inline std::string path_array_variation_validation_error(
    const PathArrayVariationSettings &settings) {
  using namespace path_array_layout_detail;
  if (!finite_vector(settings.rotation_offset_degrees) ||
      !finite_vector(settings.rotation_random_min_degrees) ||
      !finite_vector(settings.rotation_random_max_degrees)) {
    return "Path Array rotation variation must be finite.";
  }
  if (!componentwise_less_equal(settings.rotation_random_min_degrees,
                                settings.rotation_random_max_degrees)) {
    return "Path Array random rotation minimum must not exceed maximum.";
  }
  if (!finite_vector(settings.scale_offset_factors) ||
      !finite_vector(settings.scale_random_min_factors) ||
      !finite_vector(settings.scale_random_max_factors) ||
      !componentwise_strictly_positive(settings.scale_offset_factors) ||
      !componentwise_strictly_positive(settings.scale_random_min_factors) ||
      !componentwise_strictly_positive(settings.scale_random_max_factors)) {
    return "Path Array scale variation must be finite and strictly positive.";
  }
  if (!componentwise_less_equal(settings.scale_random_min_factors,
                                settings.scale_random_max_factors)) {
    return "Path Array random scale minimum must not exceed maximum.";
  }
  return {};
}

/** Describes the distance-independent inputs for one prepared path backbone. */
struct PathArrayLayoutBackboneRequest {
  std::span<const PathArrayPathStation> path_stations;
  QMat4 source_world_transform;
  std::optional<QMat4> source_variation_frame_world;
  QVec3 source_pivot_world = {};
  bool follow_path = false;
  PathArraySignedAxis signed_axis = PathArraySignedAxis::PositiveX;
  bool closed = false;
};

/** Describes the lightweight placement inputs evaluated on one backbone. */
struct PathArrayLayoutPlacementRequest {
  std::uint32_t count = 4U;
  double distance = 1.0;
  PathArrayVariationSettings variation;
};

/**
 * Retains canonical path stations and their rotation-minimizing frames.
 *
 * Count and Distance deliberately do not participate in this state. The same
 * prepared backbone can therefore evaluate every lightweight spacing preview
 * while its pinned path, source, Follow Path mode, and signed axis stay fixed.
 * Construction remains private so evaluation only accepts validated state
 * produced by `prepare_path_array_layout_backbone`.
 */
class PathArrayLayoutBackbone final {
public:
  PathArrayLayoutBackbone(const PathArrayLayoutBackbone &) = default;
  PathArrayLayoutBackbone &
  operator=(const PathArrayLayoutBackbone &) = default;
  ~PathArrayLayoutBackbone() = default;

private:
  PathArrayLayoutBackbone() = default;

  bool ok_ = false;
  std::string message_;
  double total_path_length_ = 0.0;
  glm::mat4 source_world_ = glm::mat4{1.0F};
  glm::vec3 source_pivot_ = {};
  glm::mat3 source_variation_frame_ = glm::mat3{1.0F};
  glm::mat3 source_variation_frame_inverse_ = glm::mat3{1.0F};
  bool follow_path_ = false;
  bool closed_ = false;
  std::vector<path_array_layout_detail::EvaluatedStation> stations_;
  glm::vec3 source_normal_ = {};
  std::vector<glm::quat> station_rotations_;
  float residual_twist_ = 0.0F;

  friend PathArrayLayoutBackbone prepare_path_array_layout_backbone(
      const PathArrayLayoutBackboneRequest &request);
  friend PathArrayLayoutResult build_path_array_layout(
      const PathArrayLayoutBackbone &backbone,
      const PathArrayLayoutPlacementRequest &request);
};

/** Builds the distance-independent path and Follow Path frame backbone. */
inline PathArrayLayoutBackbone prepare_path_array_layout_backbone(
    const PathArrayLayoutBackboneRequest &request) {
  using namespace path_array_layout_detail;
  const auto failure = [](std::string message) {
    PathArrayLayoutBackbone result;
    result.message_ = std::move(message);
    return result;
  };

  if (!scene_matrix_is_finite(request.source_world_transform) ||
      !finite_vector(request.source_pivot_world)) {
    return failure("Path Array source inputs must be finite.");
  }

  std::vector<EvaluatedStation> stations;
  std::string station_error;
  if (!canonicalize_stations(request.path_stations, request.closed, stations,
                             station_error)) {
    return failure(std::move(station_error));
  }
  const double path_length = stations.back().distance;

  PathArrayLayoutBackbone backbone;
  backbone.ok_ = true;
  backbone.total_path_length_ = path_length;
  backbone.source_world_ = to_glm(request.source_world_transform);
  backbone.source_pivot_ = to_glm(request.source_pivot_world);
  backbone.follow_path_ = request.follow_path;
  backbone.closed_ = request.closed;
  backbone.stations_ = std::move(stations);

  if (request.source_variation_frame_world.has_value()) {
    if (!scene_matrix_is_finite(*request.source_variation_frame_world)) {
      return failure("Path Array source variation frame must be finite.");
    }
    backbone.source_variation_frame_ =
        glm::mat3{to_glm(*request.source_variation_frame_world)};
  } else {
    const std::optional<glm::mat3> fallback =
        try_scene_pure_world_rotation_basis(request.source_world_transform);
    if (!fallback.has_value()) {
      return failure("Path Array could not resolve the source variation frame.");
    }
    backbone.source_variation_frame_ = *fallback;
  }
  const float frame_determinant =
      glm::determinant(backbone.source_variation_frame_);
  if (!finite_matrix(backbone.source_variation_frame_) ||
      !std::isfinite(frame_determinant) || frame_determinant == 0.0F) {
    return failure("Path Array source variation frame must be invertible.");
  }
  backbone.source_variation_frame_inverse_ =
      glm::inverse(backbone.source_variation_frame_);
  if (!finite_matrix(backbone.source_variation_frame_inverse_)) {
    return failure("Path Array source variation frame must be invertible.");
  }
  if (!request.follow_path) {
    return backbone;
  }

  const glm::vec3 selected_local_axis = local_axis(request.signed_axis);
  if (glm::dot(selected_local_axis, selected_local_axis) == 0.0F) {
    return failure("Path Array signed axis is invalid.");
  }
  const glm::vec3 source_axis = normalized_or_zero(
      glm::vec3{backbone.source_world_ *
                glm::vec4{selected_local_axis, 0.0F}});
  if (glm::dot(source_axis, source_axis) == 0.0F) {
    return failure("Path Array source axis must have nonzero scale.");
  }
  backbone.source_normal_ = source_roll_reference(
      backbone.source_world_, source_axis, request.signed_axis);
  if (glm::dot(backbone.source_normal_, backbone.source_normal_) == 0.0F) {
    return failure("Path Array could not resolve the source orientation.");
  }

  // Frames depend only on the pinned path/source backbone. A placement never
  // participates in another placement's frame, so increasing Count preserves
  // every existing prefix transform exactly, including after a closed wrap.
  backbone.station_rotations_.resize(backbone.stations_.size());
  backbone.station_rotations_.front() = shortest_arc(
      source_axis, backbone.stations_.front().tangent, backbone.source_normal_);
  for (std::size_t index = 1U; index < backbone.stations_.size(); ++index) {
    const glm::vec3 transported_normal =
        backbone.station_rotations_[index - 1U] * backbone.source_normal_;
    const glm::quat transport = shortest_arc(
        backbone.stations_[index - 1U].tangent,
        backbone.stations_[index].tangent, transported_normal);
    backbone.station_rotations_[index] =
        glm::normalize(transport * backbone.station_rotations_[index - 1U]);
  }

  if (request.closed && backbone.stations_.size() > 1U) {
    const glm::vec3 start_tangent = backbone.stations_.front().tangent;
    const glm::vec3 start_normal =
        backbone.station_rotations_.front() * backbone.source_normal_;
    const glm::vec3 end_normal =
        backbone.station_rotations_.back() * backbone.source_normal_;
    const glm::quat seam_transport = shortest_arc(
        backbone.stations_.back().tangent, start_tangent, end_normal);
    const glm::quat wrapped_end_rotation =
        glm::normalize(seam_transport * backbone.station_rotations_.back());
    const glm::vec3 wrapped_end_normal =
        wrapped_end_rotation * backbone.source_normal_;
    backbone.residual_twist_ =
        signed_angle(wrapped_end_normal, start_normal, start_tangent);
  }
  return backbone;
}

/** Evaluates one source-inclusive layout on a prepared path backbone. */
inline PathArrayLayoutResult build_path_array_layout(
    const PathArrayLayoutBackbone &backbone,
    const PathArrayLayoutPlacementRequest &request) {
  using namespace path_array_layout_detail;
  const auto failure = [](std::string message) {
    return PathArrayLayoutResult{
        .ok = false,
        .message = std::move(message),
    };
  };

  if (!backbone.ok_) {
    return failure(backbone.message_);
  }
  if (request.count < kPathArrayLayoutMinimumCount ||
      request.count > kPathArrayLayoutMaximumCount) {
    return failure("Path Array count must be between 2 and 128.");
  }
  if (!std::isfinite(request.distance) || request.distance < 0.0) {
    return failure("Path Array distance must be finite and nonnegative.");
  }
  const std::string variation_error =
      path_array_variation_validation_error(request.variation);
  if (!variation_error.empty()) {
    return failure(variation_error);
  }

  const double path_length = backbone.total_path_length_;
  const std::vector<EvaluatedStation> &stations = backbone.stations_;

  std::vector<double> placement_distances;
  placement_distances.reserve(request.count);
  for (std::uint32_t index = 0U; index < request.count; ++index) {
    const double authored_distance =
        static_cast<double>(index) * request.distance;
    if (!std::isfinite(authored_distance)) {
      return failure("Path Array station distance multiplication overflowed.");
    }
    const double placement_distance =
        backbone.closed_ ? positive_modulo(authored_distance, path_length)
                        : authored_distance;
    if (!std::isfinite(placement_distance)) {
      return failure("Path Array generated a non-finite station distance.");
    }
    placement_distances.push_back(placement_distance);
  }

  PathArrayLayoutResult result{
      .ok = true,
      .effective_distance = request.distance,
      .total_path_length = path_length,
  };
  result.world_transforms.reserve(request.count);
  const glm::quat rotation_step =
      rotation_from_degrees(request.variation.rotation_offset_degrees);
  glm::quat cumulative_rotation{1.0F, 0.0F, 0.0F, 0.0F};
  if (!backbone.follow_path_) {
    for (std::uint32_t index = 0U; index < request.count; ++index) {
      if (index > 0U) {
        cumulative_rotation =
            glm::normalize(cumulative_rotation * rotation_step);
      }
      const double distance = placement_distances[index];
      const EvaluatedStation evaluated =
          evaluate_at_distance(stations, distance);
      if (!finite_station(evaluated)) {
        return failure("Path Array generated a non-finite path evaluation.");
      }
      const glm::vec3 translation = evaluated.position - backbone.source_pivot_;
      const QMat4 base =
          from_glm(glm::translate(glm::mat4{1.0F}, translation) *
                   backbone.source_world_);
      if (!scene_matrix_is_finite(base)) {
        return failure("Path Array generated a non-finite transform.");
      }
      QMat4 world;
      std::string variation_message;
      if (!apply_variation(base, evaluated.position, index,
                           cumulative_rotation,
                           backbone.source_variation_frame_,
                           backbone.source_variation_frame_inverse_,
                           request.variation, world, variation_message)) {
        return failure(std::move(variation_message));
      }
      result.world_transforms.push_back(world);
    }
    return result;
  }

  const glm::mat4 from_pivot =
      glm::translate(glm::mat4{1.0F}, -backbone.source_pivot_);
  for (std::uint32_t index = 0U; index < request.count; ++index) {
    if (index > 0U) {
      cumulative_rotation =
          glm::normalize(cumulative_rotation * rotation_step);
    }
    const double distance = placement_distances[index];
    const EvaluatedStation evaluated =
        evaluate_at_distance(stations, distance);
    if (!finite_station(evaluated)) {
      return failure("Path Array generated a non-finite path evaluation.");
    }

    const auto upper = std::upper_bound(
        stations.begin(), stations.end(), distance,
        [](double value, const EvaluatedStation &station) {
          return value < station.distance;
        });
    std::size_t base_index =
        upper == stations.begin()
            ? 0U
            : static_cast<std::size_t>((upper - stations.begin()) - 1);
    if (base_index + 1U < stations.size() &&
        near_distance(distance, stations[base_index + 1U].distance)) {
      ++base_index;
    }

    glm::quat rotation = backbone.station_rotations_[base_index];
    if (!near_distance(distance, stations[base_index].distance)) {
      const glm::vec3 transported_normal = rotation * backbone.source_normal_;
      const glm::quat transport = shortest_arc(
          stations[base_index].tangent, evaluated.tangent,
          transported_normal);
      rotation = glm::normalize(transport * rotation);
    }
    if (backbone.closed_ && distance > 0.0) {
      const float fraction = static_cast<float>(distance / path_length);
      const glm::quat correction =
          glm::angleAxis(backbone.residual_twist_ * fraction,
                         evaluated.tangent);
      rotation = glm::normalize(correction * rotation);
    }

    const glm::mat4 to_path =
        glm::translate(glm::mat4{1.0F}, evaluated.position);
    const QMat4 base = from_glm(to_path * glm::mat4_cast(rotation) *
                                from_pivot * backbone.source_world_);
    if (!scene_matrix_is_finite(base)) {
      return failure("Path Array generated a non-finite transform.");
    }
    QMat4 world;
    std::string variation_message;
    const glm::mat3 station_rotation = glm::mat3_cast(rotation);
    const glm::mat3 variation_frame =
        station_rotation * backbone.source_variation_frame_;
    const glm::mat3 variation_frame_inverse =
        backbone.source_variation_frame_inverse_ *
        glm::transpose(station_rotation);
    if (!apply_variation(base, evaluated.position, index,
                         cumulative_rotation, variation_frame,
                         variation_frame_inverse, request.variation, world,
                         variation_message)) {
      return failure(std::move(variation_message));
    }
    result.world_transforms.push_back(world);
  }
  return result;
}

/** Builds one source-inclusive path layout without mutating editor state. */
inline PathArrayLayoutResult
build_path_array_layout(const PathArrayLayoutRequest &request) {
  const auto failure = [](std::string message) {
    return PathArrayLayoutResult{
        .ok = false,
        .message = std::move(message),
    };
  };
  if (request.count < kPathArrayLayoutMinimumCount ||
      request.count > kPathArrayLayoutMaximumCount) {
    return failure("Path Array count must be between 2 and 128.");
  }
  if (!std::isfinite(request.distance) || request.distance < 0.0) {
    return failure("Path Array distance must be finite and nonnegative.");
  }
  const PathArrayLayoutBackbone backbone =
      prepare_path_array_layout_backbone({
          .path_stations = request.path_stations,
          .source_world_transform = request.source_world_transform,
          .source_variation_frame_world =
              request.source_variation_frame_world,
          .source_pivot_world = request.source_pivot_world,
          .follow_path = request.follow_path,
          .signed_axis = request.signed_axis,
          .closed = request.closed,
      });
  return build_path_array_layout(backbone, {
                                               .count = request.count,
                                               .distance = request.distance,
                                               .variation = request.variation,
                                           });
}

} // namespace quader
