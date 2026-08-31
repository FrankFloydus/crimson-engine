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
#include <numbers>
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

/** Describes one source-inclusive path-array layout. */
struct PathArrayLayoutRequest {
  std::span<const PathArrayPathStation> path_stations;
  QMat4 source_world_transform;
  QVec3 source_pivot_world = {};
  std::uint32_t count = 4U;
  double distance = 1.0;
  bool follow_path = false;
  PathArraySignedAxis signed_axis = PathArraySignedAxis::PositiveX;
  bool closed = false;
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

} // namespace path_array_layout_detail

/** Builds one source-inclusive path layout without mutating editor state. */
inline PathArrayLayoutResult
build_path_array_layout(const PathArrayLayoutRequest &request) {
  using namespace path_array_layout_detail;
  const auto failure = [](std::string message) {
    return PathArrayLayoutResult{
        .ok = false,
        .message = std::move(message),
    };
  };

  if (!scene_matrix_is_finite(request.source_world_transform) ||
      !finite_vector(request.source_pivot_world)) {
    return failure("Path Array source inputs must be finite.");
  }
  if (request.count < kPathArrayLayoutMinimumCount ||
      request.count > kPathArrayLayoutMaximumCount) {
    return failure("Path Array count must be between 2 and 128.");
  }
  if (!std::isfinite(request.distance) || request.distance < 0.0) {
    return failure("Path Array distance must be finite and nonnegative.");
  }

  std::vector<EvaluatedStation> stations;
  std::string station_error;
  if (!canonicalize_stations(request.path_stations, request.closed, stations,
                             station_error)) {
    return failure(std::move(station_error));
  }
  const double path_length = stations.back().distance;

  std::vector<double> placement_distances;
  placement_distances.reserve(request.count);
  for (std::uint32_t index = 0U; index < request.count; ++index) {
    const double authored_distance =
        static_cast<double>(index) * request.distance;
    if (!std::isfinite(authored_distance)) {
      return failure("Path Array station distance multiplication overflowed.");
    }
    const double placement_distance =
        request.closed ? positive_modulo(authored_distance, path_length)
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
  const glm::mat4 source_world = to_glm(request.source_world_transform);
  const glm::vec3 source_pivot = to_glm(request.source_pivot_world);
  if (!request.follow_path) {
    for (const double distance : placement_distances) {
      const EvaluatedStation evaluated =
          evaluate_at_distance(stations, distance);
      if (!finite_station(evaluated)) {
        return failure("Path Array generated a non-finite path evaluation.");
      }
      const glm::vec3 translation = evaluated.position - source_pivot;
      const QMat4 world =
          from_glm(glm::translate(glm::mat4{1.0F}, translation) * source_world);
      if (!scene_matrix_is_finite(world)) {
        return failure("Path Array generated a non-finite transform.");
      }
      result.world_transforms.push_back(world);
    }
    return result;
  }

  const glm::vec3 selected_local_axis = local_axis(request.signed_axis);
  if (glm::dot(selected_local_axis, selected_local_axis) == 0.0F) {
    return failure("Path Array signed axis is invalid.");
  }
  const glm::vec3 source_axis = normalized_or_zero(
      glm::vec3{source_world * glm::vec4{selected_local_axis, 0.0F}});
  if (glm::dot(source_axis, source_axis) == 0.0F) {
    return failure("Path Array source axis must have nonzero scale.");
  }
  const glm::vec3 source_normal =
      source_roll_reference(source_world, source_axis, request.signed_axis);
  if (glm::dot(source_normal, source_normal) == 0.0F) {
    return failure("Path Array could not resolve the source orientation.");
  }

  // Frames are built only from the pinned path backbone. A placement never
  // participates in another placement's frame, so increasing Count preserves
  // every existing prefix transform exactly, including after a closed wrap.
  std::vector<glm::quat> station_rotations(stations.size());
  station_rotations.front() =
      shortest_arc(source_axis, stations.front().tangent, source_normal);
  for (std::size_t index = 1U; index < stations.size(); ++index) {
    const glm::vec3 transported_normal =
        station_rotations[index - 1U] * source_normal;
    const glm::quat transport = shortest_arc(
        stations[index - 1U].tangent, stations[index].tangent,
        transported_normal);
    station_rotations[index] =
        glm::normalize(transport * station_rotations[index - 1U]);
  }

  float residual_twist = 0.0F;
  if (request.closed && stations.size() > 1U) {
    const glm::vec3 start_tangent = stations.front().tangent;
    const glm::vec3 start_normal = station_rotations.front() * source_normal;
    const glm::vec3 end_normal = station_rotations.back() * source_normal;
    const glm::quat seam_transport =
        shortest_arc(stations.back().tangent, start_tangent, end_normal);
    const glm::quat wrapped_end_rotation =
        glm::normalize(seam_transport * station_rotations.back());
    const glm::vec3 wrapped_end_normal = wrapped_end_rotation * source_normal;
    residual_twist =
        signed_angle(wrapped_end_normal, start_normal, start_tangent);
  }

  const glm::mat4 from_pivot = glm::translate(glm::mat4{1.0F}, -source_pivot);
  for (const double distance : placement_distances) {
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

    glm::quat rotation = station_rotations[base_index];
    if (!near_distance(distance, stations[base_index].distance)) {
      const glm::vec3 transported_normal = rotation * source_normal;
      const glm::quat transport = shortest_arc(
          stations[base_index].tangent, evaluated.tangent,
          transported_normal);
      rotation = glm::normalize(transport * rotation);
    }
    if (request.closed && distance > 0.0) {
      const float fraction = static_cast<float>(distance / path_length);
      const glm::quat correction =
          glm::angleAxis(residual_twist * fraction, evaluated.tangent);
      rotation = glm::normalize(correction * rotation);
    }

    const glm::mat4 to_path =
        glm::translate(glm::mat4{1.0F}, evaluated.position);
    const QMat4 world = from_glm(to_path * glm::mat4_cast(rotation) *
                                 from_pivot * source_world);
    if (!scene_matrix_is_finite(world)) {
      return failure("Path Array generated a non-finite transform.");
    }
    result.world_transforms.push_back(world);
  }
  return result;
}

} // namespace quader
