////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/face_texture_actions.hpp>

#include "polygon_document_native.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/polygon/face_texture_scale_steps.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

using quader::QVec2;
using quader::QVec3;
using NativeDocument = quader_poly::Document;
using NativeFace = quader_poly::Face;
using NativeTransform = quader_poly::Transform3;
using NativeTextureTransform = quader_poly::FaceTextureTransform;
using FaceBasis = quader_poly::document_internal::FaceUvBasis;
using quader_geometry::QVec3d;

constexpr float kTextureDensity =
    quader_poly::document_internal::kGeneratedUvTilesPerWorldUnit;
constexpr double kPi = 3.141592653589793238462643383279502884;
constexpr double kTau = 2.0 * kPi;

[[nodiscard]] QVec3 native_vector(Vec3 value) {
  return {value.x, value.y, value.z};
}

[[nodiscard]] NativeTransform native_transform(const Transform3 &value) {
  return {.x_axis = native_vector(value.x_axis),
          .y_axis = native_vector(value.y_axis),
          .z_axis = native_vector(value.z_axis),
          .origin = native_vector(value.origin)};
}

[[nodiscard]] bool finite(QVec3 value) {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] bool finite(QVec2 value) {
  return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool near(float left, float right, double span = 0.0) {
  return quader_poly::texture_uv_coordinate_close(left, right, span);
}

[[nodiscard]] quader_geometry::QVec2d texture_span(std::span<const QVec2> uvs);

[[nodiscard]] bool same_uvs(std::span<const QVec2> left,
                            std::span<const QVec2> right) {
  if (left.size() != right.size()) {
    return false;
  }
  if (left.empty()) {
    return true;
  }
  const auto left_span = texture_span(left);
  const auto right_span = texture_span(right);
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (!near(left[index].x, right[index].x, std::max(left_span.x, right_span.x)) ||
        !near(left[index].y, right[index].y, std::max(left_span.y, right_span.y))) {
      return false;
    }
  }
  return true;
}

/// Holds inverse affine rows in double precision for texture-gradient projection.
struct InverseTransform {
  quader_geometry::QVec3d x_row;
  quader_geometry::QVec3d y_row;
  quader_geometry::QVec3d z_row;
};

[[nodiscard]] std::optional<InverseTransform>
inverse_transform(const NativeTransform &transform) {
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return std::nullopt;
  }
  using quader_geometry::QVec3d;
  const QVec3d x{transform.x_axis};
  const QVec3d y{transform.y_axis};
  const QVec3d z{transform.z_axis};
  const auto x_row = quader_geometry::cross(y, z);
  const auto y_row = quader_geometry::cross(z, x);
  const auto z_row = quader_geometry::cross(x, y);
  const double determinant = quader_geometry::dot(x, x_row);
  const double scale = std::sqrt(quader_geometry::dot(x, x) *
                                quader_geometry::dot(y, y) *
                                quader_geometry::dot(z, z));
  if (!std::isfinite(determinant) || !std::isfinite(scale) || scale == 0.0 ||
      std::abs(determinant) <= 1.0e-10 * scale) {
    return std::nullopt;
  }
  return InverseTransform{x_row / determinant, y_row / determinant,
                           z_row / determinant};
}

[[nodiscard]] QVec3 geometric_normal(std::span<const QVec3> points) {
  if (points.size() < 3U) {
    return {};
  }
  quader_geometry::QVec3d area{};
  const quader_geometry::QVec3d origin{points.front()};
  for (std::size_t index = 1U; index + 1U < points.size(); ++index) {
    area = area + quader_geometry::cross(
        quader_geometry::QVec3d{points[index]} - origin,
        quader_geometry::QVec3d{points[index + 1U]} - origin);
  }
  const double length = std::sqrt(quader_geometry::dot(area, area));
  if (!std::isfinite(length) || length == 0.0) {
    return {};
  }
  return {static_cast<float>(area.x / length),
          static_cast<float>(area.y / length),
          static_cast<float>(area.z / length)};
}

/// Captures all values from the original publication, before any preparation.
struct FaceWork {
  std::size_t target_index = 0;
  FaceTextureActionFace requested;
  const NativeFace *face = nullptr;
  std::vector<QVec3> local_positions;
  std::vector<QVec3> world_positions;
  std::vector<QVec2> effective_uvs;
  QVec3 normal;
  QVec3 world_normal;
};

/// Lazily allocates a native candidate only when the target actually changes.
struct TargetWork {
  const FaceTextureActionTarget *target = nullptr;
  const NativeDocument *original = nullptr;
  NativeTransform world;
  NativeTransform sampling;
  InverseTransform inverse_world;
  std::optional<NativeDocument> candidate;
  std::set<quader_poly::ElementId> modified_faces;
  bool materials_changed = false;

  [[nodiscard]] NativeDocument &edit() {
    if (!candidate.has_value()) {
      candidate = *original;
    }
    return *candidate;
  }
};

[[nodiscard]] Error face_error(const FaceWork &face, std::string message) {
  return make_error(ErrorCode::ValidationFailed,
                    "Face " + std::to_string(face.requested.face.index) +
                        ": " + std::move(message));
}

[[nodiscard]] std::optional<Error> capture_face(
    const TargetWork &target, std::size_t target_index,
    const FaceTextureActionFace &requested, FaceWork &output,
    bool capture_world = true) {
  output.target_index = target_index;
  output.requested = requested;
  output.face = quader_poly::find_face(*target.original, requested.face.index);
  if (output.face == nullptr) {
    return make_error(ErrorCode::StaleId, "The selected face is unavailable.");
  }
  const NativeFace &face = *output.face;
  if (face.vertices.size() < 3U ||
      !quader_poly::valid_face_texture_transform(face.texture_transform) ||
      (!face.uvs.empty() && face.uvs.size() != face.vertices.size())) {
    return face_error(output, "The source texture mapping is invalid.");
  }
  output.local_positions.reserve(face.vertices.size());
  if (capture_world) {
    output.world_positions.reserve(face.vertices.size());
  }
  for (const auto id : face.vertices) {
    const auto *vertex = quader_poly::find_vertex(*target.original, id);
    if (vertex == nullptr || !finite(vertex->position)) {
      return face_error(output, "The face contains an invalid vertex.");
    }
    if (capture_world) {
      const auto world = quader_poly::document_internal::transform_point(
          target.world, vertex->position);
      if (!finite(world)) {
        return face_error(output, "The world-space face is non-finite.");
      }
      output.world_positions.push_back(world);
    }
    output.local_positions.push_back(vertex->position);
  }
  output.normal = quader_poly::face_normal(*target.original, face);
  if (capture_world) {
    output.world_normal = geometric_normal(output.world_positions);
  }
  if (!finite(output.normal) || quader::length_squared(output.normal) == 0.0F ||
      (capture_world && (!finite(output.world_normal) ||
                        quader::length_squared(output.world_normal) == 0.0F))) {
    return face_error(output, "The face has no usable geometric plane.");
  }
  const auto carrier = quader_poly::face_texture_carrier(
      output.local_positions, face.uvs, output.normal, face.texture_transform);
  output.effective_uvs.reserve(face.vertices.size());
  for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
    const auto position = output.local_positions[corner];
    const auto base = face.uvs.empty()
                          ? quader_poly::generated_texture_uv(position,
                                                               output.normal)
                          : face.uvs[corner];
    const auto effective = quader_poly::evaluate_texture_mapping(
        quader_poly::apply_face_texture_transform(base, face.texture_transform),
        position, output.normal, carrier, target.sampling);
    if (!finite(effective)) {
      return face_error(output, "The effective texture mapping is non-finite.");
    }
    output.effective_uvs.push_back(effective);
  }
  return std::nullopt;
}

[[nodiscard]] FaceBasis geometric_basis(const NativeDocument &document,
                                        const NativeFace &face, QVec3 normal,
                                        QVec3 origin) {
  const auto generated = quader_poly::document_internal::generated_face_uv_basis(
      document, face, normal);
  // Axis projection must not lose a unit vector when added to a distant
  // world-space anchor. Project at zero, then attach the chart's real origin.
  auto basis = quader_poly::document_internal::project_uv_basis_to_face_plane(
      generated, normal, {});
  basis.origin = origin;
  return basis;
}

[[nodiscard]] FaceBasis geometric_basis(const TargetWork &target,
                                        const FaceWork &face) {
  return geometric_basis(*target.original, *face.face, face.world_normal,
                          face.world_positions.front());
}

/// One planar, world-space affine chart inherited from the active face.
struct SharedChart {
  FaceBasis plane;
  std::array<double, 4> carrier{};
  quader_geometry::QVec2d precise_offset;

  [[nodiscard]] QVec2 project(QVec3 point) const {
    const auto delta = QVec3d{point} - QVec3d{plane.origin};
    const double x = quader_geometry::dot(delta, QVec3d{plane.u_axis}) * kTextureDensity;
    const double y = quader_geometry::dot(delta, QVec3d{plane.v_axis}) * kTextureDensity;
    return {static_cast<float>(carrier[0] * x + carrier[1] * y + precise_offset.x),
            static_cast<float>(carrier[2] * x + carrier[3] * y + precise_offset.y)};
  }
};

[[nodiscard]] bool full_rank(const std::array<double, 4> &matrix) {
  const double u_length = std::hypot(matrix[0], matrix[1]);
  const double v_length = std::hypot(matrix[2], matrix[3]);
  if (!std::isfinite(u_length) || !std::isfinite(v_length) ||
      u_length == 0.0 || v_length == 0.0) {
    return false;
  }
  // Independent texture-axis units do not determine whether the chart is
  // planar. Test the angle between normalized gradients instead of density.
  return std::abs((matrix[0] / u_length) * (matrix[3] / v_length) -
                  (matrix[1] / u_length) * (matrix[2] / v_length)) > 1.0e-10;
}

[[nodiscard]] std::optional<SharedChart> fit_shared_chart(
    std::span<const QVec3> positions, std::span<const QVec2> effective_uvs,
    const FaceBasis &plane, bool allow_collapsed_mapping = false) {
  SharedChart chart;
  chart.plane = plane;
  if (!chart.plane.valid) {
    return std::nullopt;
  }
  std::vector<std::array<double, 2>> coordinates;
  coordinates.reserve(positions.size());
  for (const auto position : positions) {
    const auto delta = QVec3d{position} - QVec3d{plane.origin};
    coordinates.push_back({
        quader_geometry::dot(delta, QVec3d{plane.u_axis}) * kTextureDensity,
        quader_geometry::dot(delta, QVec3d{plane.v_axis}) * kTextureDensity});
  }
  const auto fitted = quader_poly::fit_texture_affine_chart(coordinates,
                                                          effective_uvs);
  if (!fitted.has_value()) {
    return std::nullopt;
  }
  chart.carrier = fitted->carrier;
  if (!allow_collapsed_mapping && !full_rank(chart.carrier)) {
    return std::nullopt;
  }
  chart.precise_offset = {fitted->offset[0], fitted->offset[1]};
  return chart;
}

[[nodiscard]] std::optional<SharedChart> shared_chart(
    const TargetWork &target, const FaceWork &reference) {
  return fit_shared_chart(reference.world_positions, reference.effective_uvs,
                          geometric_basis(target, reference));
}

/// Keeps transformed plane and seam arithmetic out of float world storage.
struct TransferPlane {
  std::vector<QVec3d> positions;
  QVec3d normal;
  QVec3d center;
  bool planar = true;
};

[[nodiscard]] std::optional<TransferPlane> transfer_plane(
    const TargetWork &target, const FaceWork &face) {
  TransferPlane plane;
  plane.positions.reserve(face.local_positions.size());
  for (const auto position : face.local_positions) {
    plane.positions.push_back(
        QVec3d{target.world.origin} +
        QVec3d{target.world.x_axis} * static_cast<double>(position.x) +
        QVec3d{target.world.y_axis} * static_cast<double>(position.y) +
        QVec3d{target.world.z_axis} * static_cast<double>(position.z));
  }
  const auto origin = plane.positions.front();
  QVec3d area{};
  QVec3d center_offset{};
  double radius_squared = 0.0;
  for (std::size_t corner = 0U; corner < plane.positions.size(); ++corner) {
    const auto delta = plane.positions[corner] - origin;
    center_offset = center_offset + delta;
    radius_squared = std::max(radius_squared, quader_geometry::dot(delta, delta));
    if (corner + 1U < plane.positions.size()) {
      area = area + quader_geometry::cross(
          delta, plane.positions[corner + 1U] - origin);
    }
  }
  const double area_length = std::sqrt(quader_geometry::dot(area, area));
  if (!std::isfinite(area_length) || !std::isfinite(radius_squared) ||
      radius_squared == 0.0 || area_length <= 1.0e-12 * radius_squared) {
    return std::nullopt;
  }
  plane.normal = area / area_length;
  plane.center = origin + center_offset / static_cast<double>(plane.positions.size());
  constexpr double kPlanarityTolerance =
      32.0 * std::numeric_limits<float>::epsilon();
  for (const auto position : plane.positions) {
    if (std::abs(quader_geometry::dot(position - origin, plane.normal)) >
        kPlanarityTolerance * std::sqrt(radius_squared)) {
      plane.planar = false;
    }
  }
  return plane;
}

/// Tangential world gradients retain the source chart's shear and handedness.
struct TransferChart {
  QVec3d origin;
  QVec3d u_gradient;
  QVec3d v_gradient;
  quader_geometry::QVec2d offset;

  [[nodiscard]] quader_geometry::QVec2d project(QVec3d point) const {
    const auto delta = point - origin;
    return {quader_geometry::dot(u_gradient, delta) + offset.x,
            quader_geometry::dot(v_gradient, delta) + offset.y};
  }
};

/// A local frame and target-center pullback, independent of chart values or state.
struct ContinuationPlan {
  std::optional<std::array<std::size_t, 3>> source_corners;
  bool rotates = false;
  QVec3d source_reference{};
  QVec3d target_reference{};
  QVec3d target_pullback_offset{};
  QVec3d source_normal{};
  QVec3d source_transverse{};
  double sine = 0.0;
  double one_minus_cosine = 0.0;
};

/// Perimeter indices retain each polygon's own edge direction.
struct ContinuationEdges {
  std::size_t source_start = 0;
  std::size_t target_start = 0;
  bool shared = false;
};

/// Exact endpoint coincidence distinguishes shared topology from displaced copies.
[[nodiscard]] bool continuation_same_position(QVec3d left, QVec3d right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

/// Every finite, nonzero normal is usable, even when its magnitude is tiny.
[[nodiscard]] QVec3d continuation_unit_or_zero(QVec3d value) {
  const double magnitude = std::hypot(value.x, value.y, value.z);
  if (!(magnitude > 0.0) || !std::isfinite(magnitude)) {
    return {};
  }
  return value / magnitude;
}

/// The proximity policy treats a componentwise tiny direction as its first point.
[[nodiscard]] bool continuation_direction_is_point(QVec3d direction) {
  constexpr double kPointThreshold = 1.0e-6;
  return std::abs(direction.x) < kPointThreshold &&
         std::abs(direction.y) < kPointThreshold &&
         std::abs(direction.z) < kPointThreshold;
}

/// Uses a relative displacement so distant world origins do not enter projection.
[[nodiscard]] double continuation_point_line_distance_squared(
    QVec3d displacement, QVec3d direction, double direction_squared) {
  const auto perpendicular = quader_geometry::cross(displacement, direction);
  return quader_geometry::dot(perpendicular, perpendicular) / direction_squared;
}

/// Scores aligned edges by their supporting lines and the prescribed overlap sample.
[[nodiscard]] double continuation_edge_distance_squared(
    QVec3d source_first, QVec3d source_direction,
    QVec3d target_first, QVec3d target_direction) {
  const auto displacement = target_first - source_first;
  const bool source_is_point = continuation_direction_is_point(source_direction);
  const bool target_is_point = continuation_direction_is_point(target_direction);
  const double source_squared =
      quader_geometry::dot(source_direction, source_direction);
  const double target_squared =
      quader_geometry::dot(target_direction, target_direction);
  if (source_is_point && target_is_point) {
    return quader_geometry::dot(displacement, displacement);
  }
  if (source_is_point) {
    return continuation_point_line_distance_squared(
        displacement, target_direction, target_squared);
  }
  if (target_is_point) {
    return continuation_point_line_distance_squared(
        displacement, source_direction, source_squared);
  }

  constexpr double kCrossSquaredThreshold = 1.0e-6;
  const auto line_normal =
      quader_geometry::cross(source_direction, target_direction);
  const double cross_squared = quader_geometry::dot(line_normal, line_normal);
  if (cross_squared >= kCrossSquaredThreshold) {
    const double distance = quader_geometry::dot(
        displacement, line_normal / std::sqrt(cross_squared));
    return distance * distance;
  }

  const double first_parameter =
      quader_geometry::dot(displacement, source_direction) / source_squared;
  const double second_parameter = quader_geometry::dot(
      displacement + target_direction, source_direction) / source_squared;
  const double lower = std::max(0.0, std::min(first_parameter, second_parameter));
  const double upper = std::min(1.0, std::max(first_parameter, second_parameter));
  // Crossed bounds are intentional when the projected intervals do not overlap.
  const double sample = 0.5 * (lower + upper);
  const auto target_displacement =
      source_first - target_first + source_direction * sample;
  return continuation_point_line_distance_squared(
      target_displacement, target_direction, target_squared);
}

/// Shared edges have priority; strict comparisons preserve both perimeter orders.
[[nodiscard]] std::optional<ContinuationEdges> find_continuation_edges(
    const TransferPlane &source, const TransferPlane &target,
    std::span<const quader_poly::ElementId> source_vertices,
    std::span<const quader_poly::ElementId> target_vertices,
    bool same_occurrence) {
  const std::size_t source_count = source.positions.size();
  const std::size_t target_count = target.positions.size();
  if (source_count < 2 || target_count < 2) {
    return std::nullopt;
  }
  if (same_occurrence && source_vertices.size() == source_count &&
      target_vertices.size() == target_count) {
    for (std::size_t source_start = 0; source_start < source_count; ++source_start) {
      const std::size_t source_end = (source_start + 1) % source_count;
      for (std::size_t target_start = 0; target_start < target_count; ++target_start) {
        const std::size_t target_end = (target_start + 1) % target_count;
        const bool forward =
            source_vertices[source_start] == target_vertices[target_start] &&
            source_vertices[source_end] == target_vertices[target_end] &&
            continuation_same_position(source.positions[source_start],
                                       target.positions[target_start]) &&
            continuation_same_position(source.positions[source_end],
                                       target.positions[target_end]);
        const bool reverse =
            source_vertices[source_start] == target_vertices[target_end] &&
            source_vertices[source_end] == target_vertices[target_start] &&
            continuation_same_position(source.positions[source_start],
                                       target.positions[target_end]) &&
            continuation_same_position(source.positions[source_end],
                                       target.positions[target_start]);
        if (forward || reverse) {
          return ContinuationEdges{source_start, target_start, true};
        }
      }
    }
  }

  constexpr double kMinimumAlignment = 0.95;
  std::optional<ContinuationEdges> best;
  double best_distance = std::numeric_limits<double>::infinity();
  for (std::size_t source_start = 0; source_start < source_count; ++source_start) {
    const auto source_direction = source.positions[(source_start + 1) % source_count] -
                                  source.positions[source_start];
    const double source_length =
        std::hypot(source_direction.x, source_direction.y, source_direction.z);
    if (!(source_length > 0.0) || !std::isfinite(source_length)) {
      continue;
    }
    const auto source_unit = source_direction / source_length;
    for (std::size_t target_start = 0; target_start < target_count; ++target_start) {
      const auto target_direction = target.positions[(target_start + 1) % target_count] -
                                    target.positions[target_start];
      const double target_length =
          std::hypot(target_direction.x, target_direction.y, target_direction.z);
      if (!(target_length > 0.0) || !std::isfinite(target_length)) {
        continue;
      }
      const double alignment = std::abs(quader_geometry::dot(
          source_unit, target_direction / target_length));
      if (alignment < kMinimumAlignment) {
        continue;
      }
      const double distance = continuation_edge_distance_squared(
          source.positions[source_start], source_direction,
          target.positions[target_start], target_direction);
      if (std::isfinite(distance) && distance < best_distance) {
        best_distance = distance;
        best = ContinuationEdges{source_start, target_start, false};
      }
    }
  }
  return best;
}

/// Conditions a local seam chart using the earliest best corner after its endpoint.
[[nodiscard]] std::optional<std::array<std::size_t, 3>>
continuation_source_corners(const TransferPlane &source, std::size_t source_start) {
  const std::size_t count = source.positions.size();
  const std::size_t source_end = (source_start + 1) % count;
  const auto edge = source.positions[source_start] - source.positions[source_end];
  const double edge_squared = quader_geometry::dot(edge, edge);
  if (!(edge_squared > 0.0) || !std::isfinite(edge_squared)) {
    return std::nullopt;
  }
  std::optional<std::array<std::size_t, 3>> best;
  double best_score = 0.0;
  for (std::size_t offset = 2; offset < count; ++offset) {
    const std::size_t candidate = (source_start + offset) % count;
    const auto reach = source.positions[candidate] - source.positions[source_end];
    const double reach_squared = quader_geometry::dot(reach, reach);
    if (!(reach_squared > 0.0) || !std::isfinite(reach_squared)) {
      continue;
    }
    const double absolute_cosine = std::clamp(std::abs(
        quader_geometry::dot(edge, reach) /
        (std::sqrt(edge_squared) * std::sqrt(reach_squared))), 0.0, 1.0);
    const double score = edge_squared * reach_squared * (1.0 - absolute_cosine);
    if (std::isfinite(score) && score > best_score) {
      best_score = score;
      best = std::array<std::size_t, 3>{source_start, source_end, candidate};
    }
  }
  return best;
}

/// The chosen edge and its first farthest corner determine orientation, even on concave faces.
[[nodiscard]] QVec3d continuation_edge_normal(
    const TransferPlane &plane, std::size_t start) {
  constexpr double kShortEdgeSquared = 1.0e-5;
  const auto first = plane.positions[start];
  const auto direction = plane.positions[(start + 1) % plane.positions.size()] - first;
  const double direction_squared = quader_geometry::dot(direction, direction);
  double farthest_distance = -1.0;
  QVec3d farthest_displacement{};
  for (const auto position : plane.positions) {
    const auto displacement = position - first;
    const double distance = direction_squared < kShortEdgeSquared
        ? quader_geometry::dot(displacement, displacement)
        : continuation_point_line_distance_squared(
              displacement, direction, direction_squared);
    if (distance > farthest_distance) {
      farthest_distance = distance;
      farthest_displacement = displacement;
    }
  }
  return continuation_unit_or_zero(
      quader_geometry::cross(direction, farthest_displacement));
}

/// A warped face supplies its local chart through a conditioned boundary triangle.
[[nodiscard]] std::optional<std::array<std::size_t, 3>>
continuation_local_corners(const TransferPlane &plane,
                           std::optional<std::size_t> preferred_edge) {
  if (preferred_edge.has_value()) {
    return continuation_source_corners(plane, *preferred_edge);
  }
  for (std::size_t edge = 0U; edge < plane.positions.size(); ++edge) {
    if (const auto corners = continuation_source_corners(plane, edge)) {
      return corners;
    }
  }
  return std::nullopt;
}

[[nodiscard]] QVec3d continuation_triangle_normal(
    const TransferPlane &plane, const std::array<std::size_t, 3> &corners) {
  return continuation_unit_or_zero(quader_geometry::cross(
      plane.positions[corners[1]] - plane.positions[corners[0]],
      plane.positions[corners[2]] - plane.positions[corners[0]]));
}

/// Plans seam conditioning and the acute-angle spatial pullback without fitting UVs.
[[nodiscard]] ContinuationPlan make_continuation_plan(
    const TransferPlane &source, const TransferPlane &target,
    std::span<const quader_poly::ElementId> source_vertices,
    std::span<const quader_poly::ElementId> target_vertices,
    bool same_occurrence) {
  ContinuationPlan plan;
  auto source_normal = source.normal;
  auto target_normal = target.normal;
  auto source_reference = source.positions.front();
  auto target_reference = target.center;
  const auto edges = find_continuation_edges(
      source, target, source_vertices, target_vertices, same_occurrence);
  if (edges) {
    if (edges->shared) {
      plan.source_corners = continuation_source_corners(source, edges->source_start);
    }
    source_normal = continuation_edge_normal(source, edges->source_start);
    target_normal = continuation_edge_normal(target, edges->target_start);
    if (!source.planar || !target.planar) {
      // The polygon center need not lie on the local plane after vertex edits.
      // Actual boundary points keep a shared seam fixed during the fold.
      source_reference = source.positions[edges->source_start];
      target_reference = target.positions[edges->target_start];
    }
  }
  if (!source.planar) {
    plan.source_corners = continuation_local_corners(
        source, edges ? std::optional{edges->source_start} : std::nullopt);
    if (plan.source_corners) {
      source_normal = continuation_triangle_normal(source, *plan.source_corners);
      source_reference = source.positions[plan.source_corners->front()];
    }
  }
  if (!target.planar) {
    if (const auto corners = continuation_local_corners(
            target, edges ? std::optional{edges->target_start} : std::nullopt)) {
      target_normal = continuation_triangle_normal(target, *corners);
      target_reference = target.positions[corners->front()];
    }
  }

  const auto normal_cross = quader_geometry::cross(source_normal, target_normal);
  const double plane_sine =
      std::hypot(normal_cross.x, normal_cross.y, normal_cross.z);
  if (!std::isfinite(plane_sine) ||
      plane_sine < static_cast<double>(std::numeric_limits<float>::epsilon())) {
    return plan;
  }
  const double plane_cosine = std::clamp(
      quader_geometry::dot(source_normal, target_normal), -1.0, 1.0);
  const double angle = std::atan2(plane_sine, std::abs(plane_cosine));
  plan.sine = std::sin(angle);
  // This half-angle identity avoids subtracting nearly equal values near parallel.
  plan.one_minus_cosine = plan.sine * plan.sine / (1.0 + std::cos(angle));
  plan.source_normal = source_normal;
  plan.source_transverse =
      quader_geometry::cross(normal_cross / plane_sine, source_normal);
  plan.source_reference = source_reference;
  plan.target_reference = target_reference;

  const auto reference_delta = plan.target_reference - plan.source_reference;
  const double normal_distance = quader_geometry::dot(source_normal, reference_delta);
  const double normal_ratio = plane_cosine / plane_sine;
  // In the (transverse, normal) frame, the true intersection fixes the target
  // center's transverse distance from the pivot to -normal_ratio*normal_distance.
  // Apply the inverse rotation to that displacement without constructing a
  // potentially distant pivot or subtracting large world-space plane constants.
  const double transverse_correction = normal_distance *
      (plan.one_minus_cosine * normal_ratio - plan.sine);
  const double normal_correction = normal_distance *
      (-plan.sine * normal_ratio - plan.one_minus_cosine);
  plan.target_pullback_offset = reference_delta +
      plan.source_transverse * transverse_correction +
      plan.source_normal * normal_correction;
  plan.rotates = true;
  return plan;
}

/// Pushes a covector through the frame rotation while retaining its axial component.
[[nodiscard]] QVec3d continuation_transport_gradient(
    QVec3d gradient, const ContinuationPlan &plan) {
  const double transverse = quader_geometry::dot(gradient, plan.source_transverse);
  const double normal = quader_geometry::dot(gradient, plan.source_normal);
  return gradient + plan.source_transverse *
      (-plan.one_minus_cosine * transverse + plan.sine * normal) +
      plan.source_normal * (-plan.sine * transverse - plan.one_minus_cosine * normal);
}

/// Reanchors the transported affine field locally; constant and rank-one fields need no special case.
[[nodiscard]] TransferChart transport_continuation_chart(
    const TransferChart &source, const ContinuationPlan &plan) {
  if (!plan.rotates) {
    return source;
  }
  const auto pullback_delta =
      (plan.source_reference - source.origin) + plan.target_pullback_offset;
  TransferChart transported = source;
  transported.origin = plan.target_reference;
  transported.u_gradient = continuation_transport_gradient(source.u_gradient, plan);
  transported.v_gradient = continuation_transport_gradient(source.v_gradient, plan);
  transported.offset = {
      source.offset.x + quader_geometry::dot(source.u_gradient, pullback_delta),
      source.offset.y + quader_geometry::dot(source.v_gradient, pullback_delta)};
  return transported;
}

[[nodiscard]] quader_geometry::QVec2d texture_span(std::span<const QVec2> uvs) {
  QVec2 minimum = uvs.front();
  QVec2 maximum = minimum;
  for (const auto uv : uvs) {
    minimum.x = std::min(minimum.x, uv.x);
    minimum.y = std::min(minimum.y, uv.y);
    maximum.x = std::max(maximum.x, uv.x);
    maximum.y = std::max(maximum.y, uv.y);
  }
  return {static_cast<double>(maximum.x) - minimum.x,
          static_cast<double>(maximum.y) - minimum.y};
}

[[nodiscard]] bool transfer_uv_close(double fitted, float stored, double span) {
  return quader_poly::texture_uv_coordinate_close(fitted, stored, span);
}

[[nodiscard]] bool same_transfer_uvs(std::span<const QVec2> left,
                                    std::span<const QVec2> right) {
  if (left.size() != right.size() || left.empty()) {
    return false;
  }
  const auto left_span = texture_span(left);
  const auto right_span = texture_span(right);
  for (std::size_t corner = 0U; corner < left.size(); ++corner) {
    if (!transfer_uv_close(left[corner].x, right[corner].x,
                            std::max(left_span.x, right_span.x)) ||
        !transfer_uv_close(left[corner].y, right[corner].y,
                            std::max(left_span.y, right_span.y))) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::optional<TransferChart> transfer_chart(
    const TargetWork &source, const FaceWork &face, const TransferPlane &plane) {
  // Fit in local coordinates so a distant occurrence cannot quantize a valid
  // affine chart before it is transported through the world transform.
  const auto basis = geometric_basis(*source.original, *face.face, face.normal,
                                     face.local_positions.front());
  const auto local = fit_shared_chart(face.local_positions, face.effective_uvs,
                                      basis, true);
  if (!local.has_value()) {
    return std::nullopt;
  }
  const auto span = texture_span(face.effective_uvs);
  const auto precision = quader_poly::face_texture_precision_bounds(
      face.local_positions, face.face->uvs, face.normal,
      face.face->texture_transform, source.sampling,
      quader_poly::TextureUvPrecision::AffineResidual);
  if (precision.size() != face.local_positions.size()) {
    return std::nullopt;
  }
  for (std::size_t corner = 0U; corner < face.local_positions.size(); ++corner) {
    const auto delta = QVec3d{face.local_positions[corner]} - QVec3d{basis.origin};
    const double u = quader_geometry::dot(delta, QVec3d{basis.u_axis}) * kTextureDensity;
    const double v = quader_geometry::dot(delta, QVec3d{basis.v_axis}) * kTextureDensity;
    const quader_geometry::QVec2d fitted{
        local->carrier[0] * u + local->carrier[1] * v + local->precise_offset.x,
        local->carrier[2] * u + local->carrier[3] * v + local->precise_offset.y};
    const auto expected = face.effective_uvs[corner];
    // A fitted carrier alone would silently discard authored UV residuals.
    const auto close = [](double value, float stored, double extent,
                           double uncertainty) {
      const double error = value - stored;
      return std::isfinite(uncertainty) &&
             (std::abs(error) <= uncertainty ||
              transfer_uv_close(value - std::copysign(uncertainty, error),
                                  stored, extent));
    };
    if (!close(fitted.x, expected.x, span.x, precision[corner][0]) ||
        !close(fitted.y, expected.y, span.y, precision[corner][1])) {
      return std::nullopt;
    }
  }
  const auto world_gradient = [&](double u, double v) {
    const auto gradient =
        (QVec3d{basis.u_axis} * static_cast<double>(u) +
         QVec3d{basis.v_axis} * static_cast<double>(v)) *
        static_cast<double>(kTextureDensity);
    const auto transformed = source.inverse_world.x_row * gradient.x +
                             source.inverse_world.y_row * gradient.y +
                             source.inverse_world.z_row * gradient.z;
    return transformed - plane.normal * quader_geometry::dot(transformed, plane.normal);
  };
  return TransferChart{
      .origin = plane.positions.front(),
      .u_gradient = world_gradient(local->carrier[0], local->carrier[1]),
      .v_gradient = world_gradient(local->carrier[2], local->carrier[3]),
      .offset = local->precise_offset,
  };
}

/// Fits selected source samples through the existing local precision owner.
[[nodiscard]] std::optional<TransferChart> fit_continuation_chart(
    const TargetWork &source, const FaceWork &source_face,
    const TransferPlane &source_plane,
    const std::optional<std::array<std::size_t, 3>> &source_corners) {
  if (source_corners.has_value()) {
    NativeFace sampled_face = *source_face.face;
    sampled_face.vertices.clear();
    sampled_face.uvs.clear();
    FaceWork samples{
        .target_index = source_face.target_index, .requested = source_face.requested,
        .face = &sampled_face, .normal = source_face.normal};
    TransferPlane sample_plane{
        .normal = source_plane.normal, .center = source_plane.center};
    for (const auto corner : *source_corners) {
      sampled_face.vertices.push_back(source_face.face->vertices[corner]);
      if (!source_face.face->uvs.empty()) {
        sampled_face.uvs.push_back(source_face.face->uvs[corner]);
      }
      samples.local_positions.push_back(source_face.local_positions[corner]);
      samples.effective_uvs.push_back(source_face.effective_uvs[corner]);
      sample_plane.positions.push_back(source_plane.positions[corner]);
    }
    if (!source_plane.planar) {
      // Fit and transport the same triangle, rather than projecting its chart
      // back onto the unrelated whole-polygon area normal.
      samples.normal = geometric_normal(samples.local_positions);
      sample_plane.normal = continuation_triangle_normal(
          sample_plane, std::array<std::size_t, 3>{0U, 1U, 2U});
    }
    if (auto chart = transfer_chart(source, samples, sample_plane)) {
      return chart;
    }
  }
  return transfer_chart(source, source_face, source_plane);
}

[[nodiscard]] bool chart_has_area(std::span<const QVec2> points) {
  double mean_x = 0.0;
  double mean_y = 0.0;
  for (const auto point : points) {
    if (!finite(point)) {
      return false;
    }
    mean_x += point.x;
    mean_y += point.y;
  }
  const double count = static_cast<double>(points.size());
  if (count < 3.0) {
    return false;
  }
  mean_x /= count;
  mean_y /= count;
  double xx = 0.0;
  double xy = 0.0;
  double yy = 0.0;
  for (const auto point : points) {
    const double x = point.x - mean_x;
    const double y = point.y - mean_y;
    xx += x * x;
    xy += x * y;
    yy += y * y;
  }
  if (!std::isfinite(xx) || !std::isfinite(xy) || !std::isfinite(yy) ||
      xx <= 0.0 || yy <= 0.0) {
    return false;
  }
  // Normalize each axis independently so a valid narrow UV rectangle has
  // the same rank as a square, while collinear projected points still fail.
  const double correlation = (xy / std::sqrt(xx)) / std::sqrt(yy);
  return 1.0 - correlation * correlation > 1.0e-12;
}

template <typename Uv>
[[nodiscard]] int texture_rank(const std::vector<Uv> &points) {
  double minimum_x = points.front().x;
  double minimum_y = points.front().y;
  double maximum_x = minimum_x;
  double maximum_y = minimum_y;
  for (const auto point : points) {
    minimum_x = std::min(minimum_x, static_cast<double>(point.x));
    minimum_y = std::min(minimum_y, static_cast<double>(point.y));
    maximum_x = std::max(maximum_x, static_cast<double>(point.x));
    maximum_y = std::max(maximum_y, static_cast<double>(point.y));
  }
  const double span_x = maximum_x - minimum_x;
  const double span_y = maximum_y - minimum_y;
  if (span_x == 0.0 || span_y == 0.0) {
    return span_x == 0.0 && span_y == 0.0 ? 0 : 1;
  }
  // Normalize each UV axis before classifying the same target footprint.
  // A narrow but representable face must not look like storage rank loss.
  double mean_x = 0.0;
  double mean_y = 0.0;
  for (const auto point : points) {
    mean_x += (point.x - minimum_x) / span_x;
    mean_y += (point.y - minimum_y) / span_y;
  }
  const double count = static_cast<double>(points.size());
  mean_x /= count;
  mean_y /= count;
  double xx = 0.0;
  double xy = 0.0;
  double yy = 0.0;
  for (const auto point : points) {
    const double x = (point.x - minimum_x) / span_x - mean_x;
    const double y = (point.y - minimum_y) / span_y - mean_y;
    xx += x * x;
    xy += x * y;
    yy += y * y;
  }
  const double trace = xx + yy;
  return xx * yy - xy * xy > 1.0e-12 * trace * trace ? 2 : 1;
}

/// Bounds measured in effective or shared normalized texture coordinates.
struct TextureBounds {
  double min_u = std::numeric_limits<double>::infinity();
  double min_v = std::numeric_limits<double>::infinity();
  double max_u = -std::numeric_limits<double>::infinity();
  double max_v = -std::numeric_limits<double>::infinity();

  void include(std::span<const QVec2> values) {
    for (const auto value : values) {
      min_u = std::min(min_u, static_cast<double>(value.x));
      min_v = std::min(min_v, static_cast<double>(value.y));
      max_u = std::max(max_u, static_cast<double>(value.x));
      max_v = std::max(max_v, static_cast<double>(value.y));
    }
  }
};

/// Output-space affine edit; unselected axes remain exactly unmodified.
struct TextureAffine {
  double scale_u = 1.0;
  double scale_v = 1.0;
  double shift_u = 0.0;
  double shift_v = 0.0;
  bool edit_u = false;
  bool edit_v = false;
};

[[nodiscard]] std::optional<TextureAffine> bounds_affine(
    FaceTextureAction action, const TextureBounds &bounds) {
  TextureAffine result;
  switch (action) {
  case FaceTextureAction::FitBoth:
  case FaceTextureAction::FitX:
  case FaceTextureAction::FitY:
    result.edit_u = action != FaceTextureAction::FitY;
    result.edit_v = action != FaceTextureAction::FitX;
    if (result.edit_u) {
      const double extent = bounds.max_u - bounds.min_u;
      if (!std::isfinite(extent) || extent <= 0.0) {
        return std::nullopt;
      }
      result.scale_u = 1.0 / extent;
      result.shift_u = -bounds.min_u / extent;
    }
    if (result.edit_v) {
      const double extent = bounds.max_v - bounds.min_v;
      if (!std::isfinite(extent) || extent <= 0.0) {
        return std::nullopt;
      }
      result.scale_v = 1.0 / extent;
      result.shift_v = -bounds.min_v / extent;
    }
    break;
  case FaceTextureAction::JustifyLeft:
    result.edit_u = true;
    result.shift_u = -bounds.min_u;
    break;
  case FaceTextureAction::JustifyRight:
    result.edit_u = true;
    result.shift_u = 1.0 - bounds.max_u;
    break;
  case FaceTextureAction::JustifyTop:
    result.edit_v = true;
    result.shift_v = -bounds.min_v;
    break;
  case FaceTextureAction::JustifyBottom:
    result.edit_v = true;
    result.shift_v = 1.0 - bounds.max_v;
    break;
  case FaceTextureAction::JustifyCenter:
    result.edit_u = true;
    result.edit_v = true;
    result.shift_u = 0.5 - (bounds.min_u + bounds.max_u) * 0.5;
    result.shift_v = 0.5 - (bounds.min_v + bounds.max_v) * 0.5;
    break;
  default:
    return std::nullopt;
  }
  return result;
}

[[nodiscard]] std::optional<Error> set_transform(
    TargetWork &target, const FaceWork &face,
    const NativeTextureTransform &desired) {
  if (!quader_poly::valid_face_texture_transform(desired)) {
    return face_error(face, "The action requires finite, nonzero texture scale.");
  }
  if (desired == face.face->texture_transform) {
    return std::nullopt;
  }
  const auto carrier = quader_poly::face_texture_carrier(
      face.local_positions, face.face->uvs, face.normal, desired);
  for (std::size_t corner = 0U; corner < face.local_positions.size(); ++corner) {
    const auto base = face.face->uvs.empty()
                          ? quader_poly::generated_texture_uv(
                                face.local_positions[corner], face.normal)
                          : face.face->uvs[corner];
    const auto effective = quader_poly::evaluate_texture_mapping(
        quader_poly::apply_face_texture_transform(base, desired),
        face.local_positions[corner], face.normal, carrier, target.sampling);
    if (!finite(effective)) {
      return face_error(face, "The action produces non-finite texture coordinates.");
    }
  }
  auto *candidate = quader_poly::find_face(target.edit(), face.face->id);
  candidate->texture_transform = desired;
  target.modified_faces.insert(face.face->id);
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> set_effective_uvs(
    TargetWork &target, const FaceWork &face, std::span<const QVec2> desired_uvs,
    const NativeTextureTransform &desired_transform,
    bool transfer_comparison = false) {
  if (desired_transform == face.face->texture_transform &&
      (transfer_comparison ? same_transfer_uvs(desired_uvs, face.effective_uvs)
                             : same_uvs(desired_uvs, face.effective_uvs))) {
    return std::nullopt;
  }
  auto base = quader_poly::solve_face_texture_base_uvs(
      face.local_positions, desired_uvs, face.normal, desired_transform,
      target.sampling);
  if (!base.has_value()) {
    return face_error(face,
        "The current texture-lock mapping is too distorted to apply this UV edit accurately.");
  }
  if (desired_transform == face.face->texture_transform &&
      std::ranges::equal(*base, face.face->uvs, [](QVec2 left, QVec2 right) {
        return left.x == right.x && left.y == right.y;
      })) {
    return std::nullopt;
  }
  auto *candidate = quader_poly::find_face(target.edit(), face.face->id);
  candidate->uvs = std::move(*base);
  candidate->texture_transform = desired_transform;
  target.modified_faces.insert(face.face->id);
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> apply_increment(
    TargetWork &target, const FaceWork &face,
    const FaceTextureActionOptions &options) {
  auto desired = face.face->texture_transform;
  const auto scale_step = [](float &scale, bool up) -> bool {
    const double stepped =
        quader_poly::stepped_face_texture_scale(scale, up ? 1 : -1);
    if (!std::isfinite(stepped) || stepped == 0.0) {
      return false;
    }
    scale = static_cast<float>(stepped);
    return true;
  };
  const float width = static_cast<float>(
      face.requested.texture_width == 0U ? 64U : face.requested.texture_width);
  const float height = static_cast<float>(
      face.requested.texture_height == 0U ? 64U : face.requested.texture_height);
  bool valid_scale = true;
  switch (options.action) {
  case FaceTextureAction::ScaleXUp:
    valid_scale = scale_step(desired.scale.x, true);
    break;
  case FaceTextureAction::ScaleXDown:
    valid_scale = scale_step(desired.scale.x, false);
    break;
  case FaceTextureAction::ScaleYUp:
    valid_scale = scale_step(desired.scale.y, true);
    break;
  case FaceTextureAction::ScaleYDown:
    valid_scale = scale_step(desired.scale.y, false);
    break;
  case FaceTextureAction::ShiftLeft:
    desired.shift_uv.x += 1.0F / width;
    break;
  case FaceTextureAction::ShiftRight:
    desired.shift_uv.x -= 1.0F / width;
    break;
  case FaceTextureAction::ShiftUp:
    desired.shift_uv.y += 1.0F / height;
    break;
  case FaceTextureAction::ShiftDown:
    desired.shift_uv.y -= 1.0F / height;
    break;
  case FaceTextureAction::RotateClockwise:
  case FaceTextureAction::RotateCounterClockwise:
    desired.rotation_degrees = face.requested.target_rotation_degrees.value_or(
        static_cast<float>(desired.rotation_degrees + options.rotation_degrees *
            (options.action == FaceTextureAction::RotateClockwise ? -1.0 : 1.0)));
    if (!quader_poly::valid_face_texture_transform(desired)) {
      return face_error(face, "The action requires a finite texture rotation.");
    }
    if (width != height && desired.rotation_degrees !=
                               face.face->texture_transform.rotation_degrees) {
      const double angle = std::remainder(
          static_cast<double>(desired.rotation_degrees) -
              face.face->texture_transform.rotation_degrees,
          360.0) * kPi / 180.0;
      const double cosine = std::cos(angle);
      const double sine = std::sin(angle);
      const double u_units = static_cast<double>(width) * desired.scale.x;
      const double v_units = static_cast<double>(height) * desired.scale.y;
      std::vector<QVec2> rotated;
      rotated.reserve(face.effective_uvs.size());
      for (const auto uv : face.effective_uvs) {
        // Rotate in physical image-axis units. Rotating normalized tile
        // coordinates directly swaps texel density on rectangular images.
        // This affine edit retains authored residuals and occurrence sampling.
        const double u = (static_cast<double>(uv.x) - desired.shift_uv.x) * u_units;
        const double v = (static_cast<double>(uv.y) - desired.shift_uv.y) * v_units;
        rotated.push_back({
            static_cast<float>((cosine * u - sine * v) / u_units +
                               desired.shift_uv.x),
            static_cast<float>((sine * u + cosine * v) / v_units +
                               desired.shift_uv.y)});
      }
      return set_effective_uvs(target, face, rotated, desired);
    }
    break;
  case FaceTextureAction::FlipTextureHorizontal:
    desired.scale.x = -desired.scale.x;
    break;
  case FaceTextureAction::FlipTextureVertical:
    desired.scale.y = -desired.scale.y;
    break;
  default:
    return make_error(ErrorCode::UnsupportedOperation,
                      "The texture action is not an incremental edit.");
  }
  if (!valid_scale) {
    return face_error(face, "The scale step would reach or cross zero.");
  }
  return set_transform(target, face, desired);
}

[[nodiscard]] std::optional<Error> apply_individual_bounds(
    TargetWork &target, const FaceWork &face, FaceTextureAction action) {
  TextureBounds bounds;
  bounds.include(face.effective_uvs);
  const auto affine = bounds_affine(action, bounds);
  if (!affine.has_value()) {
    return face_error(face, "Cannot fit a collapsed texture axis.");
  }
  const bool unchanged = std::ranges::all_of(face.effective_uvs,
      [&](const QVec2 uv) {
        return (!affine->edit_u || near(uv.x, static_cast<float>(
                    uv.x * affine->scale_u + affine->shift_u))) &&
               (!affine->edit_v || near(uv.y, static_cast<float>(
                    uv.y * affine->scale_v + affine->shift_v)));
      });
  if (unchanged) {
    return std::nullopt;
  }
  auto desired = face.face->texture_transform;
  // Multiplying a row of the face transform composes the affine edit before
  // occurrence compensation, retaining procedural UVs and authored residuals.
  if (affine->edit_u) {
    desired.scale.x = static_cast<float>(desired.scale.x / affine->scale_u);
    desired.shift_uv.x = static_cast<float>(
        desired.shift_uv.x * affine->scale_u + affine->shift_u);
  }
  if (affine->edit_v) {
    desired.scale.y = static_cast<float>(desired.scale.y / affine->scale_v);
    desired.shift_uv.y = static_cast<float>(
        desired.shift_uv.y * affine->scale_v + affine->shift_v);
  }
  return set_transform(target, face, desired);
}

[[nodiscard]] std::optional<Error> apply_group_bounds(
    std::vector<TargetWork> &targets, std::span<const FaceWork> faces,
    const FaceWork &reference, FaceTextureAction action) {
  const auto chart = shared_chart(targets[reference.target_index], reference);
  if (!chart.has_value()) {
    return face_error(reference, "The active face has no usable shared planar mapping.");
  }
  TextureBounds bounds;
  std::vector<std::vector<QVec2>> projections;
  projections.reserve(faces.size());
  for (const auto &face : faces) {
    auto &projected = projections.emplace_back();
    projected.reserve(face.world_positions.size());
    for (const auto position : face.world_positions) {
      projected.push_back(chart->project(position));
    }
    if (!chart_has_area(projected)) {
      return face_error(face,
          "Treat as One collapses this face in the active face's planar mapping.");
    }
    bounds.include(projected);
  }
  const auto affine = bounds_affine(action, bounds);
  if (!affine.has_value()) {
    return face_error(reference, "Cannot fit a collapsed shared texture axis.");
  }
  for (std::size_t index = 0U; index < faces.size(); ++index) {
    const auto &face = faces[index];
    auto &projected = projections[index];
    for (std::size_t corner = 0U; corner < projected.size(); ++corner) {
      projected[corner].x = affine->edit_u
          ? static_cast<float>(projected[corner].x * affine->scale_u +
                               affine->shift_u)
          : face.effective_uvs[corner].x;
      projected[corner].y = affine->edit_v
          ? static_cast<float>(projected[corner].y * affine->scale_v +
                               affine->shift_v)
          : face.effective_uvs[corner].y;
    }
    if (auto error = set_effective_uvs(targets[face.target_index], face,
                                       projected, face.face->texture_transform)) {
      return error;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> apply_alignment(
    TargetWork &target, const FaceWork &face,
    const FaceTextureActionOptions &options) {
  auto desired_transform = face.face->texture_transform;
  std::vector<QVec2> desired;
  desired.reserve(face.world_positions.size());
  FaceBasis basis;
  if (options.action == FaceTextureAction::AlignToFace) {
    basis = geometric_basis(target, face);
    if (!basis.valid) {
      return face_error(face, "The face has no usable projection plane.");
    }
  } else if (options.action == FaceTextureAction::AlignToView) {
    const auto right = native_vector(options.view_right);
    const auto up = native_vector(options.view_up);
    if (!finite(right) || !finite(up) ||
        quader::length_squared(right) <= 1.0e-12F) {
      return face_error(face, "The captured view axes are invalid.");
    }
    basis.u_axis = quader::normalize_or_zero(right);
    const auto vertical = up - basis.u_axis * quader::dot(up, basis.u_axis);
    if (quader::length_squared(vertical) <= 1.0e-12F) {
      return face_error(face, "The captured view axes are parallel.");
    }
    basis.v_axis = quader::normalize_or_zero(vertical) * -1.0F;
    basis.valid = true;
  } else {
    // Grid alignment resets the settings; Face and View retain them.
    constexpr float kGridMapUnitsPerTexel = 0.125F;
    desired_transform = {.scale = {kGridMapUnitsPerTexel, kGridMapUnitsPerTexel}};
  }
  constexpr float kGeneratedTileSize =
      quader_poly::document_internal::kGeneratedUvTileSizeMapUnits;
  const float width = face.requested.texture_width == 0U
      ? kGeneratedTileSize : static_cast<float>(face.requested.texture_width);
  const float height = face.requested.texture_height == 0U
      ? kGeneratedTileSize : static_cast<float>(face.requested.texture_height);
  auto unshifted_transform = desired_transform;
  unshifted_transform.shift_uv = {};
  for (const auto position : face.world_positions) {
    const auto base = options.action == FaceTextureAction::AlignToGrid
        ? quader_poly::generated_texture_uv(position, face.world_normal)
        : quader_poly::document_internal::generated_face_uv(position, basis);
    const auto transformed = quader_poly::apply_face_texture_transform(
        base, unshifted_transform);
    // Face scale measures map units per texel. Convert the rotated image
    // axes from the generated tile size before adding normalized UV shifts.
    desired.push_back({transformed.x * (kGeneratedTileSize / width) +
                           desired_transform.shift_uv.x,
                       transformed.y * (kGeneratedTileSize / height) +
                           desired_transform.shift_uv.y});
  }
  return set_effective_uvs(target, face, desired, desired_transform);
}

/// Target-local bounds of exactly the faces included in one projection request.
struct ProjectionBounds {
  double min_x = std::numeric_limits<double>::infinity();
  double min_y = std::numeric_limits<double>::infinity();
  double min_z = std::numeric_limits<double>::infinity();
  double max_x = -std::numeric_limits<double>::infinity();
  double max_y = -std::numeric_limits<double>::infinity();
  double max_z = -std::numeric_limits<double>::infinity();

  void include(QVec3 point) {
    min_x = std::min(min_x, static_cast<double>(point.x));
    min_y = std::min(min_y, static_cast<double>(point.y));
    min_z = std::min(min_z, static_cast<double>(point.z));
    max_x = std::max(max_x, static_cast<double>(point.x));
    max_y = std::max(max_y, static_cast<double>(point.y));
    max_z = std::max(max_z, static_cast<double>(point.z));
  }

  [[nodiscard]] double extent_x() const { return max_x - min_x; }
  [[nodiscard]] double extent_y() const { return max_y - min_y; }
  [[nodiscard]] double extent_z() const { return max_z - min_z; }
  [[nodiscard]] double largest_extent() const {
    return std::max({extent_x(), extent_y(), extent_z()});
  }
  [[nodiscard]] double center_x() const { return (min_x + max_x) * 0.5; }
  [[nodiscard]] double center_y() const { return (min_y + max_y) * 0.5; }
  [[nodiscard]] double center_z() const { return (min_z + max_z) * 0.5; }
};

[[nodiscard]] bool usable_extent(double extent) {
  return std::isfinite(extent) && extent > 0.0;
}

[[nodiscard]] double projection_extent(double extent,
                                       const ProjectionBounds &bounds) {
  return usable_extent(extent) ? extent : bounds.largest_extent();
}

[[nodiscard]] bool cylinder_cap(const FaceWork &face) {
  const double vertical = std::abs(static_cast<double>(face.normal.y));
  const double horizontal = std::max(
      std::abs(static_cast<double>(face.normal.x)),
      std::abs(static_cast<double>(face.normal.z)));
  constexpr double kAxisTolerance =
      64.0 * std::numeric_limits<float>::epsilon();
  return vertical > 0.0 && horizontal <= kAxisTolerance * vertical;
}

[[nodiscard]] double longitude_u(double normalized_x, double normalized_z) {
  double result = std::atan2(normalized_z, normalized_x) / kTau;
  if (result < 0.0) {
    result += 1.0;
  }
  return result;
}

/// Keeps a face crossing the longitude branch from spanning almost a full map.
[[nodiscard]] bool correct_longitude_branch(
    std::vector<QVec2> &uvs, std::span<const std::uint8_t> pole_corners = {}) {
  double minimum = std::numeric_limits<double>::infinity();
  double maximum = -std::numeric_limits<double>::infinity();
  bool found = false;
  for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
    if (!pole_corners.empty() && pole_corners[corner] != 0U) {
      continue;
    }
    if (!finite(uvs[corner])) {
      return false;
    }
    minimum = std::min(minimum, static_cast<double>(uvs[corner].x));
    maximum = std::max(maximum, static_cast<double>(uvs[corner].x));
    found = true;
  }
  if (!found) {
    return false;
  }
  if (maximum - minimum > 0.5) {
    for (std::size_t corner = 0U; corner < uvs.size(); ++corner) {
      if ((!pole_corners.empty() && pole_corners[corner] != 0U) ||
          uvs[corner].x >= 0.5F) {
        continue;
      }
      uvs[corner].x += 1.0F;
    }
  }
  return true;
}

[[nodiscard]] std::optional<Error> apply_cube_projection(
    TargetWork &target, const FaceWork &face, const ProjectionBounds &bounds) {
  const double extent =
      bounds.largest_extent();
  if (!usable_extent(extent)) {
    return face_error(face, "Cube projection requires non-collapsed bounds.");
  }
  std::vector<QVec2> desired;
  desired.reserve(face.local_positions.size());
  const double scale = 1.0 / (static_cast<double>(kTextureDensity) * extent);
  for (const auto position : face.local_positions) {
    const QVec3 centered{
        static_cast<float>(static_cast<double>(position.x) - bounds.center_x()),
        static_cast<float>(static_cast<double>(position.y) - bounds.center_y()),
        static_cast<float>(static_cast<double>(position.z) - bounds.center_z())};
    const auto planar = quader_poly::generated_texture_uv(centered, face.normal);
    const QVec2 projected{
        static_cast<float>(0.5 + static_cast<double>(planar.x) * scale),
        static_cast<float>(0.5 + static_cast<double>(planar.y) * scale)};
    if (!finite(projected)) {
      return face_error(face, "Cube projection produced non-finite coordinates.");
    }
    desired.push_back(projected);
  }
  return set_effective_uvs(target, face, desired, NativeTextureTransform{});
}

[[nodiscard]] std::optional<Error> apply_cylinder_projection(
    TargetWork &target, const FaceWork &face, const ProjectionBounds &bounds) {
  const double largest_extent = bounds.largest_extent();
  if (!usable_extent(largest_extent)) {
    return face_error(
        face, "Cylinder projection requires non-collapsed selected geometry.");
  }
  const double extent_x = projection_extent(bounds.extent_x(), bounds);
  const double extent_y = projection_extent(bounds.extent_y(), bounds);
  const double extent_z = projection_extent(bounds.extent_z(), bounds);
  const bool cap = cylinder_cap(face);
  const double radius_x = extent_x * 0.5;
  const double radius_z = extent_z * 0.5;
  std::vector<QVec2> desired;
  desired.reserve(face.local_positions.size());
  for (const auto position : face.local_positions) {
    const double normalized_x =
        (static_cast<double>(position.x) - bounds.center_x()) / radius_x;
    const double normalized_z =
        (static_cast<double>(position.z) - bounds.center_z()) / radius_z;
    QVec2 projected;
    if (cap) {
      projected = {
          static_cast<float>(0.5 + normalized_x * 0.5),
          static_cast<float>(0.5 + normalized_z * 0.5 *
                                       (face.normal.y >= 0.0F ? 1.0 : -1.0))};
    } else {
      const double radius_squared =
          normalized_x * normalized_x + normalized_z * normalized_z;
      double longitude_x = normalized_x;
      double longitude_z = normalized_z;
      if (!std::isfinite(radius_squared)) {
        return face_error(face, "Cylinder projection radius is non-finite.");
      }
      if (radius_squared <= 0.0) {
        longitude_x = static_cast<double>(face.normal.x);
        longitude_z = static_cast<double>(face.normal.z);
        if (longitude_x == 0.0 && longitude_z == 0.0) {
          longitude_x = 1.0;
        }
      }
      projected = {
          static_cast<float>(longitude_u(longitude_x, longitude_z)),
          static_cast<float>((static_cast<double>(position.y) - bounds.min_y) /
                             extent_y)};
    }
    if (!finite(projected)) {
      return face_error(
          face, "Cylinder projection produced non-finite coordinates.");
    }
    desired.push_back(projected);
  }
  if (!cap && !correct_longitude_branch(desired)) {
    return face_error(face, "Cylinder longitude correction is invalid.");
  }
  return set_effective_uvs(target, face, desired, NativeTextureTransform{});
}

[[nodiscard]] std::optional<Error> apply_sphere_projection(
    TargetWork &target, const FaceWork &face, const ProjectionBounds &bounds) {
  const double largest_extent = bounds.largest_extent();
  if (!usable_extent(largest_extent)) {
    return face_error(
        face, "Sphere projection requires non-collapsed selected geometry.");
  }
  const double extent_x = projection_extent(bounds.extent_x(), bounds);
  const double extent_y = projection_extent(bounds.extent_y(), bounds);
  const double extent_z = projection_extent(bounds.extent_z(), bounds);
  const double radius_x = extent_x * 0.5;
  const double radius_y = extent_y * 0.5;
  const double radius_z = extent_z * 0.5;
  std::vector<QVec2> desired;
  std::vector<std::uint8_t> poles;
  desired.reserve(face.local_positions.size());
  poles.reserve(face.local_positions.size());
  constexpr double kPoleTolerance =
      64.0 * std::numeric_limits<float>::epsilon();
  for (const auto position : face.local_positions) {
    const double normalized_x =
        (static_cast<double>(position.x) - bounds.center_x()) / radius_x;
    const double normalized_y =
        (static_cast<double>(position.y) - bounds.center_y()) / radius_y;
    const double normalized_z =
        (static_cast<double>(position.z) - bounds.center_z()) / radius_z;
    double direction_x = normalized_x;
    double direction_y = normalized_y;
    double direction_z = normalized_z;
    double length_squared = direction_x * direction_x +
                            direction_y * direction_y +
                            direction_z * direction_z;
    if (!std::isfinite(length_squared)) {
      return face_error(face, "Sphere projection radius is non-finite.");
    }
    if (length_squared <= 0.0) {
      direction_x = static_cast<double>(face.normal.x);
      direction_y = static_cast<double>(face.normal.y);
      direction_z = static_cast<double>(face.normal.z);
      length_squared = direction_x * direction_x + direction_y * direction_y +
                       direction_z * direction_z;
    }
    const double length = std::sqrt(length_squared);
    const double radial = std::hypot(direction_x, direction_z);
    const bool pole = radial <= kPoleTolerance * length;
    const double u = pole ? 0.0 : longitude_u(direction_x, direction_z);
    const double v = std::acos(std::clamp(direction_y / length, -1.0, 1.0)) /
                     kPi;
    const QVec2 projected{static_cast<float>(u), static_cast<float>(v)};
    if (!finite(projected)) {
      return face_error(
          face, "Sphere projection produced non-finite coordinates.");
    }
    desired.push_back(projected);
    poles.push_back(pole ? 1U : 0U);
  }
  if (!correct_longitude_branch(desired, poles)) {
    return face_error(face, "Sphere longitude correction is invalid.");
  }
  for (std::size_t pole = 0U; pole < poles.size(); ++pole) {
    if (poles[pole] == 0U) {
      continue;
    }
    std::optional<std::size_t> previous;
    std::optional<std::size_t> next;
    for (std::size_t distance = 1U; distance < poles.size(); ++distance) {
      const std::size_t candidate = (pole + poles.size() - distance) % poles.size();
      if (poles[candidate] == 0U) {
        previous = candidate;
        break;
      }
    }
    for (std::size_t distance = 1U; distance < poles.size(); ++distance) {
      const std::size_t candidate = (pole + distance) % poles.size();
      if (poles[candidate] == 0U) {
        next = candidate;
        break;
      }
    }
    if (!previous.has_value() || !next.has_value()) {
      return face_error(face, "Sphere pole has no usable fan neighbors.");
    }
    desired[pole].x =
        (desired[*previous].x + desired[*next].x) * 0.5F;
  }
  return set_effective_uvs(target, face, desired, NativeTextureTransform{});
}

[[nodiscard]] std::optional<Error> apply_projection(
    std::vector<TargetWork> &targets, std::span<const FaceWork> faces,
    FaceTextureAction action) {
  std::vector<ProjectionBounds> bounds(targets.size());
  for (const auto &face : faces) {
    for (const auto position : face.local_positions) {
      bounds[face.target_index].include(position);
    }
  }
  for (const auto &face : faces) {
    auto &target = targets[face.target_index];
    std::optional<Error> error;
    switch (action) {
    case FaceTextureAction::ProjectCube:
      error = apply_cube_projection(target, face, bounds[face.target_index]);
      break;
    case FaceTextureAction::ProjectCylinder:
      error = apply_cylinder_projection(target, face, bounds[face.target_index]);
      break;
    case FaceTextureAction::ProjectSphere:
      error = apply_sphere_projection(target, face, bounds[face.target_index]);
      break;
    default:
      return make_error(ErrorCode::UnsupportedOperation,
                        "The texture action is not a projection.");
    }
    if (error.has_value()) {
      return error;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> apply_face_uv_mirror(
    std::vector<TargetWork> &targets, std::span<const FaceWork> faces,
    const FaceTextureActionOptions &options) {
  const bool horizontal =
      options.action == FaceTextureAction::FlipFaceHorizontal;
  TextureBounds group_bounds;
  if (options.treat_as_one) {
    for (const auto &face : faces) {
      group_bounds.include(face.effective_uvs);
    }
  }
  for (const auto &face : faces) {
    auto bounds = group_bounds;
    if (!options.treat_as_one) {
      bounds.include(face.effective_uvs);
    }
    const double minimum = horizontal ? bounds.min_u : bounds.min_v;
    const double maximum = horizontal ? bounds.max_u : bounds.max_v;
    if (minimum == maximum) {
      continue;
    }
    // Mirror the existing effective chart, including authored residuals and
    // occurrence sampling. Group mode shares only its UV bounds; it must not
    // reproject disconnected or non-affine charts through an active face.
    auto desired = face.effective_uvs;
    for (auto &uv : desired) {
      auto &coordinate = horizontal ? uv.x : uv.y;
      coordinate = static_cast<float>(minimum + maximum - coordinate);
    }
    if (auto error = set_effective_uvs(targets[face.target_index], face,
                                      desired, face.face->texture_transform)) {
      return error;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool bounds_action(FaceTextureAction action) {
  switch (action) {
  case FaceTextureAction::FitBoth:
  case FaceTextureAction::FitX:
  case FaceTextureAction::FitY:
  case FaceTextureAction::JustifyLeft:
  case FaceTextureAction::JustifyRight:
  case FaceTextureAction::JustifyTop:
  case FaceTextureAction::JustifyBottom:
  case FaceTextureAction::JustifyCenter:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool alignment_action(FaceTextureAction action) {
  return action == FaceTextureAction::AlignToGrid ||
         action == FaceTextureAction::AlignToFace ||
         action == FaceTextureAction::AlignToView;
}

[[nodiscard]] bool projection_action(FaceTextureAction action) {
  return action == FaceTextureAction::ProjectCube ||
         action == FaceTextureAction::ProjectCylinder ||
         action == FaceTextureAction::ProjectSphere;
}

[[nodiscard]] bool face_uv_mirror_action(FaceTextureAction action) {
  return action == FaceTextureAction::FlipFaceHorizontal ||
         action == FaceTextureAction::FlipFaceVertical;
}

[[nodiscard]] bool valid_action(FaceTextureAction action) {
  switch (action) {
  case FaceTextureAction::AlignToGrid:
  case FaceTextureAction::AlignToFace:
  case FaceTextureAction::AlignToView:
  case FaceTextureAction::ScaleXUp:
  case FaceTextureAction::ScaleXDown:
  case FaceTextureAction::ScaleYUp:
  case FaceTextureAction::ScaleYDown:
  case FaceTextureAction::ShiftLeft:
  case FaceTextureAction::ShiftRight:
  case FaceTextureAction::ShiftUp:
  case FaceTextureAction::ShiftDown:
  case FaceTextureAction::RotateClockwise:
  case FaceTextureAction::RotateCounterClockwise:
  case FaceTextureAction::FlipTextureHorizontal:
  case FaceTextureAction::FlipTextureVertical:
  case FaceTextureAction::FlipFaceHorizontal:
  case FaceTextureAction::FlipFaceVertical:
  case FaceTextureAction::FitBoth:
  case FaceTextureAction::FitX:
  case FaceTextureAction::FitY:
  case FaceTextureAction::JustifyLeft:
  case FaceTextureAction::JustifyRight:
  case FaceTextureAction::JustifyTop:
  case FaceTextureAction::JustifyBottom:
  case FaceTextureAction::JustifyCenter:
  case FaceTextureAction::ProjectCube:
  case FaceTextureAction::ProjectCylinder:
  case FaceTextureAction::ProjectSphere:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] Result<PreparedFaceTextureAction> prepare_texture_documents(
    std::span<TargetWork> targets, bool changes_topology,
    std::string_view changed_message) {
  PreparedFaceTextureAction result;
  result.documents.reserve(targets.size());
  for (auto &target : targets) {
    PreparedFaceTextureDocument prepared{
        .key = target.target->key, .document = *target.target->document};
    prepared.receipt.message = "Face texture unchanged.";
    if (target.candidate.has_value()) {
      PolygonDocumentNativeAccess::replace_document(prepared.document,
                                                     std::move(*target.candidate));
      ++PolygonDocumentNativeAccess::impl(prepared.document).content_revision;
      const auto validation = changes_topology
          ? prepared.document.validate()
          : PolygonDocumentNativeAccess::validate_authored_topology(prepared.document);
      if (!validation.ok()) {
        return Result<PreparedFaceTextureAction>::failure(validation.error());
      }
      prepared.receipt.changed = true;
      prepared.receipt.message = changed_message;
      prepared.receipt.dirty.geometry = true;
      prepared.receipt.dirty.topology = changes_topology;
      prepared.receipt.dirty.materials = target.materials_changed;
      for (const auto id : target.modified_faces) {
        prepared.receipt.affected.faces.push_back(make_id<FaceTag>(id));
      }
      prepared.receipt.modified = prepared.receipt.affected;
      result.changed = true;
    }
    result.documents.push_back(std::move(prepared));
  }
  return Result<PreparedFaceTextureAction>::success(std::move(result));
}

} // namespace

Result<PreparedFaceTextureAction> prepare_face_texture_action(
    std::span<const FaceTextureActionTarget> requested_targets,
    const FaceTextureActionOptions &options) {
  const auto fail = [](Error error) {
    return Result<PreparedFaceTextureAction>::failure(std::move(error));
  };
  if (!valid_action(options.action)) {
    return fail(make_error(ErrorCode::InvalidArgument, "Unknown face texture action."));
  }
  if ((options.action == FaceTextureAction::RotateClockwise ||
       options.action == FaceTextureAction::RotateCounterClockwise) &&
      (!std::isfinite(options.rotation_degrees) || options.rotation_degrees <= 0.0)) {
    return fail(make_error(ErrorCode::InvalidArgument,
        "Texture rotation requires a finite positive snap angle."));
  }
  std::size_t requested_face_count = 0U;
  for (const auto &target : requested_targets) {
    requested_face_count += target.faces.size();
  }
  const bool capture_world = alignment_action(options.action) ||
      (bounds_action(options.action) && options.treat_as_one && requested_face_count > 1U);
  std::vector<TargetWork> targets;
  std::vector<FaceWork> faces;
  targets.reserve(requested_targets.size());
  std::set<std::uint64_t> keys;
  for (const auto &requested : requested_targets) {
    if (requested.document == nullptr || requested.faces.empty() ||
        !keys.insert(requested.key).second) {
      return fail(make_error(ErrorCode::InvalidArgument,
          "Texture actions require unique targets with selected faces and live documents."));
    }
    TargetWork target;
    target.target = &requested;
    target.original = &PolygonDocumentNativeAccess::document(*requested.document);
    target.world = native_transform(requested.world_from_local);
    target.sampling = native_transform(requested.sampling_from_local);
    const auto inverse = inverse_transform(target.world);
    if (!inverse.has_value() || !inverse_transform(target.sampling).has_value()) {
      return fail(make_error(ErrorCode::InvalidArgument,
          "Texture actions require finite, invertible world and sampling transforms."));
    }
    target.inverse_world = *inverse;
    std::set<FaceId> ids;
    std::vector<FaceId> face_ids;
    face_ids.reserve(requested.faces.size());
    for (const auto &face : requested.faces) {
      if (!ids.insert(face.face).second) {
        return fail(make_error(ErrorCode::InvalidArgument,
            "The texture action contains a duplicate face ID."));
      }
      face_ids.push_back(face.face);
    }
    const auto settings = requested.document->face_texture_settings(face_ids);
    if (!settings.ok()) {
      return fail(settings.error());
    }
    const std::size_t index = targets.size();
    targets.push_back(std::move(target));
    for (const auto &requested_face : requested.faces) {
      FaceWork face;
      if (auto error = capture_face(targets.back(), index, requested_face, face,
                                    capture_world)) {
        return fail(std::move(*error));
      }
      faces.push_back(std::move(face));
    }
  }
  if (faces.empty()) {
    return fail(make_error(ErrorCode::InvalidArgument,
                           "Select one or more faces to edit their texture."));
  }
  const auto active = std::ranges::find_if(faces, [&](const FaceWork &face) {
    return targets[face.target_index].target->key == options.active_target_key &&
           face.requested.face == options.active_face;
  });
  const auto fallback = std::ranges::min_element(faces, [&](const auto &left,
                                                          const auto &right) {
    return std::pair{targets[left.target_index].target->key, left.requested.face} <
           std::pair{targets[right.target_index].target->key, right.requested.face};
  });
  const auto &reference = active == faces.end() ? *fallback : *active;
  if (projection_action(options.action)) {
    if (auto error = apply_projection(targets, faces, options.action)) {
      return fail(std::move(*error));
    }
  } else if (face_uv_mirror_action(options.action)) {
    if (auto error = apply_face_uv_mirror(targets, faces, options)) {
      return fail(std::move(*error));
    }
  } else if (bounds_action(options.action) && options.treat_as_one &&
             faces.size() > 1U) {
    if (auto error = apply_group_bounds(targets, faces, reference, options.action)) {
      return fail(std::move(*error));
    }
  } else {
    for (const auto &face : faces) {
      auto &target = targets[face.target_index];
      std::optional<Error> error;
      if (alignment_action(options.action)) {
        error = apply_alignment(target, face, options);
      } else if (bounds_action(options.action)) {
        error = apply_individual_bounds(target, face, options.action);
      } else {
        error = apply_increment(target, face, options);
      }
      if (error.has_value()) {
        return fail(std::move(*error));
      }
    }
  }
  return prepare_texture_documents(targets, false,
      face_uv_mirror_action(options.action) ? "Face UVs mirrored."
                                          : "Face texture changed.");
}

Result<PreparedFaceTextureAction> prepare_face_texture_transfer(
    const FaceTextureTransferFace &source,
    const FaceTextureTransferFace &target, std::uint32_t target_material_slot) {
  const auto fail = [](Error error) {
    return Result<PreparedFaceTextureAction>::failure(std::move(error));
  };
  const std::array requests{
      FaceTextureActionTarget{.key = source.key, .document = source.document,
          .faces = {{.face = source.face}},
          .world_from_local = source.world_from_local,
          .sampling_from_local = source.sampling_from_local},
      FaceTextureActionTarget{.key = target.key, .document = target.document,
          .faces = {{.face = target.face}},
          .world_from_local = target.world_from_local,
          .sampling_from_local = target.sampling_from_local},
  };
  std::array<TargetWork, 2> targets;
  std::array<FaceWork, 2> faces;
  std::array<TransferPlane, 2> planes;
  for (std::size_t index = 0U; index < requests.size(); ++index) {
    const auto &requested = requests[index];
    if (requested.document == nullptr) {
      return fail(make_error(ErrorCode::InvalidArgument,
          "Texture transfer requires live source and target documents."));
    }
    const std::array ids{requested.faces.front().face};
    const auto settings = requested.document->face_texture_settings(ids);
    if (!settings.ok()) {
      return fail(settings.error());
    }
    auto &work = targets[index];
    work.target = &requested;
    work.original = &PolygonDocumentNativeAccess::document(*requested.document);
    work.world = native_transform(requested.world_from_local);
    work.sampling = native_transform(requested.sampling_from_local);
    const auto inverse = inverse_transform(work.world);
    if (!inverse.has_value() || !inverse_transform(work.sampling).has_value()) {
      return fail(make_error(ErrorCode::InvalidArgument,
          "Texture transfer requires finite, invertible world and sampling transforms."));
    }
    work.inverse_world = *inverse;
    if (auto error = capture_face(work, index, requested.faces.front(), faces[index],
                                  false)) {
      return fail(std::move(*error));
    }
    auto plane = transfer_plane(work, faces[index]);
    if (!plane.has_value()) {
      return fail(face_error(faces[index],
          "Texture transfer requires a nondegenerate face."));
    }
    planes[index] = std::move(*plane);
  }
  const bool same_face = source.key == target.key &&
      targets[0].original == targets[1].original && source.face == target.face &&
      same_uvs(faces[0].effective_uvs, faces[1].effective_uvs) &&
      std::ranges::equal(planes[0].positions, planes[1].positions,
          [](QVec3d left, QVec3d right) {
            return left.x == right.x && left.y == right.y && left.z == right.z;
          });
  if (same_face) {
    if (faces[1].face->material_slot != target_material_slot) {
      quader_poly::find_face(targets[1].edit(), faces[1].face->id)->material_slot =
          target_material_slot;
      targets[1].modified_faces.insert(faces[1].face->id);
      targets[1].materials_changed = true;
    }
    return prepare_texture_documents(std::span<TargetWork>(&targets[1], 1U), false,
                                      "Face texture transferred.");
  }
  const auto continuation = make_continuation_plan(
      planes[0], planes[1], faces[0].face->vertices, faces[1].face->vertices,
      source.key == target.key && targets[0].original == targets[1].original);
  const auto source_chart = fit_continuation_chart(
      targets[0], faces[0], planes[0], continuation.source_corners);
  if (!source_chart.has_value()) {
    return fail(face_error(faces[0],
        "Texture transfer requires an affine source mapping."));
  }
  const auto chart = transport_continuation_chart(*source_chart, continuation);
  std::vector<QVec2> desired;
  std::vector<quader_geometry::QVec2d> ideal_deltas;
  desired.reserve(planes[1].positions.size());
  ideal_deltas.reserve(planes[1].positions.size());
  for (const auto position : planes[1].positions) {
    const auto projected = chart.project(position);
    const QVec2 uv{static_cast<float>(projected.x), static_cast<float>(projected.y)};
    if (!finite(uv)) {
      return fail(face_error(faces[1], "The transferred texture is non-finite."));
    }
    desired.push_back(uv);
    const auto delta = position - planes[1].positions.front();
    ideal_deltas.emplace_back(quader_geometry::dot(chart.u_gradient, delta),
                              quader_geometry::dot(chart.v_gradient, delta));
  }
  if (texture_rank(desired) < texture_rank(ideal_deltas)) {
    return fail(face_error(faces[1],
        "The transferred texture collapses at the target's stored precision."));
  }
  if (auto error = set_effective_uvs(targets[1], faces[1], desired,
                                    faces[0].face->texture_transform, true)) {
    return fail(std::move(*error));
  }
  if (targets[1].candidate.has_value()) {
    TargetWork verification;
    verification.original = &*targets[1].candidate;
    verification.sampling = targets[1].sampling;
    FaceWork verified_face;
    if (auto error = capture_face(verification, 1U, requests[1].faces.front(),
                                  verified_face, false)) {
      return fail(std::move(*error));
    }
    if (texture_rank(verified_face.effective_uvs) < texture_rank(desired) ||
        !quader_poly::face_texture_uvs_reconstruct(
            verified_face.local_positions, verified_face.face->uvs, desired,
            verified_face.normal, verified_face.face->texture_transform,
            verification.sampling)) {
      return fail(face_error(faces[1],
          "The target cannot represent the transferred texture accurately."));
    }
  }
  if (faces[1].face->material_slot != target_material_slot) {
    quader_poly::find_face(targets[1].edit(), faces[1].face->id)->material_slot =
        target_material_slot;
    targets[1].modified_faces.insert(faces[1].face->id);
    targets[1].materials_changed = true;
  }
  return prepare_texture_documents(std::span<TargetWork>(&targets[1], 1U), false,
                                    "Face texture transferred.");
}

} // namespace quader::modeling
