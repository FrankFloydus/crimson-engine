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
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
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

constexpr float kTextureDensity =
    quader_poly::document_internal::kGeneratedUvTilesPerWorldUnit;

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

[[nodiscard]] bool near(float left, float right) {
  constexpr float kTolerance =
      8.0F * std::numeric_limits<float>::epsilon();
  return std::isfinite(left) && std::isfinite(right) &&
         std::abs(left - right) <=
             kTolerance * std::max({1.0F, std::abs(left), std::abs(right)});
}

[[nodiscard]] bool same_uvs(std::span<const QVec2> left,
                            std::span<const QVec2> right) {
  if (left.size() != right.size()) {
    return false;
  }
  for (std::size_t index = 0U; index < left.size(); ++index) {
    if (!near(left[index].x, right[index].x) ||
        !near(left[index].y, right[index].y)) {
      return false;
    }
  }
  return true;
}

/// Holds inverse affine rows in double precision until destination storage.
struct InverseTransform {
  quader_geometry::QVec3d x_row;
  quader_geometry::QVec3d y_row;
  quader_geometry::QVec3d z_row;
  QVec3 origin;

  [[nodiscard]] QVec3 point(QVec3 world) const {
    const quader_geometry::QVec3d delta{
        static_cast<double>(world.x) - origin.x,
        static_cast<double>(world.y) - origin.y,
        static_cast<double>(world.z) - origin.z};
    return {static_cast<float>(quader_geometry::dot(x_row, delta)),
            static_cast<float>(quader_geometry::dot(y_row, delta)),
            static_cast<float>(quader_geometry::dot(z_row, delta))};
  }
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
                           z_row / determinant, transform.origin};
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
  std::set<quader_poly::ElementId> modified_vertices;

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
    const FaceTextureActionFace &requested, FaceWork &output) {
  output.target_index = target_index;
  output.requested = requested;
  output.face = quader_poly::find_face(*target.original, requested.face.index);
  if (output.face == nullptr) {
    return make_error(ErrorCode::StaleId, "The selected face is unavailable.");
  }
  const NativeFace &face = *output.face;
  if (!quader_poly::valid_face_texture_transform(face.texture_transform) ||
      (!face.uvs.empty() && face.uvs.size() != face.vertices.size())) {
    return face_error(output, "The source texture mapping is invalid.");
  }
  output.local_positions.reserve(face.vertices.size());
  output.world_positions.reserve(face.vertices.size());
  for (const auto id : face.vertices) {
    const auto *vertex = quader_poly::find_vertex(*target.original, id);
    if (vertex == nullptr || !finite(vertex->position)) {
      return face_error(output, "The face contains an invalid vertex.");
    }
    const auto world = quader_poly::document_internal::transform_point(
        target.world, vertex->position);
    if (!finite(world)) {
      return face_error(output, "The world-space face is non-finite.");
    }
    output.local_positions.push_back(vertex->position);
    output.world_positions.push_back(world);
  }
  output.normal = quader_poly::face_normal(*target.original, face);
  output.world_normal = geometric_normal(output.world_positions);
  if (!finite(output.normal) || !finite(output.world_normal) ||
      quader::length_squared(output.world_normal) == 0.0F) {
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

[[nodiscard]] FaceBasis geometric_basis(const TargetWork &target,
                                        const FaceWork &face) {
  const auto generated = quader_poly::document_internal::generated_face_uv_basis(
      *target.original, *face.face, face.world_normal);
  // Axis projection must not lose a unit vector when added to a distant
  // world-space anchor. Project at zero, then attach the chart's real origin.
  auto basis = quader_poly::document_internal::project_uv_basis_to_face_plane(
      generated, face.world_normal, {});
  basis.origin = face.world_positions.front();
  return basis;
}

/// One planar, world-space affine chart inherited from the active face.
struct SharedChart {
  FaceBasis plane;
  std::array<float, 4> carrier{};
  QVec2 offset;

  [[nodiscard]] QVec2 project(QVec3 point) const {
    const auto q = quader_poly::document_internal::generated_face_uv(point,
                                                                   plane);
    return {carrier[0] * q.x + carrier[1] * q.y + offset.x,
            carrier[2] * q.x + carrier[3] * q.y + offset.y};
  }
};

[[nodiscard]] bool full_rank(const std::array<float, 4> &matrix) {
  const double scale = std::max({std::abs(static_cast<double>(matrix[0])),
                                 std::abs(static_cast<double>(matrix[1])),
                                 std::abs(static_cast<double>(matrix[2])),
                                 std::abs(static_cast<double>(matrix[3]))});
  if (!std::isfinite(scale) || scale == 0.0) {
    return false;
  }
  return std::abs((matrix[0] / scale) * (matrix[3] / scale) -
                  (matrix[1] / scale) * (matrix[2] / scale)) > 1.0e-10;
}

[[nodiscard]] std::optional<SharedChart> shared_chart(
    const TargetWork &target, const FaceWork &reference) {
  SharedChart chart;
  chart.plane = geometric_basis(target, reference);
  if (!chart.plane.valid) {
    return std::nullopt;
  }
  std::vector<QVec3> plane_positions;
  plane_positions.reserve(reference.world_positions.size());
  double mean_qx = 0.0;
  double mean_qy = 0.0;
  double mean_u = 0.0;
  double mean_v = 0.0;
  for (std::size_t corner = 0U; corner < reference.world_positions.size();
       ++corner) {
    const auto q = quader_poly::document_internal::generated_face_uv(
        reference.world_positions[corner], chart.plane);
    // The canonical +Z chart is (x/2,-y/2). These synthetic positions let
    // the existing carrier fit an orthonormal face-plane chart unchanged.
    plane_positions.push_back({q.x / kTextureDensity,
                                -q.y / kTextureDensity, 0.0F});
    mean_qx += q.x;
    mean_qy += q.y;
    mean_u += reference.effective_uvs[corner].x;
    mean_v += reference.effective_uvs[corner].y;
  }
  chart.carrier = quader_poly::face_texture_carrier(
      plane_positions, reference.effective_uvs, {0.0F, 0.0F, 1.0F}, {});
  if (!full_rank(chart.carrier)) {
    return std::nullopt;
  }
  const double count = static_cast<double>(plane_positions.size());
  chart.offset = {
      static_cast<float>((mean_u - chart.carrier[0] * mean_qx -
                          chart.carrier[1] * mean_qy) / count),
      static_cast<float>((mean_v - chart.carrier[2] * mean_qx -
                          chart.carrier[3] * mean_qy) / count)};
  return finite(chart.offset) ? std::optional{chart} : std::nullopt;
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
  const double trace = xx + yy;
  return std::isfinite(trace) && trace > 0.0 &&
         xx * yy - xy * xy > 1.0e-12 * trace * trace;
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
    const NativeTextureTransform &desired_transform) {
  if (desired_transform == face.face->texture_transform &&
      same_uvs(desired_uvs, face.effective_uvs)) {
    return std::nullopt;
  }
  auto base = quader_poly::solve_face_texture_base_uvs(
      face.local_positions, desired_uvs, face.normal, desired_transform,
      target.sampling);
  if (!base.has_value()) {
    return face_error(face,
        "This projection cannot preserve the occurrence texture mapping.");
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
  const auto scale_step = [&](float &scale, bool up) -> bool {
    constexpr float kStep = 0.1F;
    const float magnitude = std::abs(scale) + (up ? kStep : -kStep);
    if (!std::isfinite(magnitude) ||
        magnitude <= 8.0F * std::numeric_limits<float>::epsilon() * kStep) {
      return false;
    }
    scale = std::copysign(magnitude, scale);
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
    desired.rotation_degrees = static_cast<float>(
        desired.rotation_degrees + options.rotation_degrees *
            (options.action == FaceTextureAction::RotateClockwise ? 1.0 : -1.0));
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
    desired_transform = {};
  }
  for (const auto position : face.world_positions) {
    const auto base = options.action == FaceTextureAction::AlignToGrid
        ? quader_poly::generated_texture_uv(position, face.world_normal)
        : quader_poly::document_internal::generated_face_uv(position, basis);
    desired.push_back(quader_poly::apply_face_texture_transform(
        base, desired_transform));
  }
  return set_effective_uvs(target, face, desired, desired_transform);
}

[[nodiscard]] bool self_intersects(const NativeDocument &document,
                                  const NativeFace &face) {
  const auto normal = quader_poly::face_normal(document, face);
  std::vector<quader_geometry::QVec2d> projected;
  projected.reserve(face.vertices.size());
  for (const auto id : face.vertices) {
    const auto *vertex = quader_poly::find_vertex(document, id);
    if (vertex == nullptr) {
      return true;
    }
    projected.emplace_back(quader_poly::generated_texture_uv(vertex->position,
                                                              normal));
  }
  for (std::size_t first = 0U; first < projected.size(); ++first) {
    const std::size_t first_next = (first + 1U) % projected.size();
    for (std::size_t second = first + 1U; second < projected.size(); ++second) {
      const std::size_t second_next = (second + 1U) % projected.size();
      if (first_next == second || second_next == first) {
        continue;
      }
      if (quader_geometry::segments_intersect_2d(
              projected[first], projected[first_next], projected[second],
              projected[second_next], 1.0e-10)) {
        return true;
      }
    }
  }
  return false;
}

[[nodiscard]] std::optional<Error> validate_mirror_topology(
    const TargetWork &target) {
  if (!target.candidate.has_value()) {
    return std::nullopt;
  }
  using Edge = std::pair<quader_poly::ElementId, quader_poly::ElementId>;
  std::map<Edge, std::vector<std::pair<quader_poly::ElementId, bool>>> edges;
  for (const auto &face : target.candidate->faces) {
    for (std::size_t corner = 0U; corner < face.vertices.size(); ++corner) {
      const auto a = face.vertices[corner];
      const auto b = face.vertices[(corner + 1U) % face.vertices.size()];
      edges[{std::min(a, b), std::max(a, b)}].emplace_back(face.id, a < b);
    }
    if (target.modified_faces.contains(face.id) &&
        self_intersects(*target.candidate, face)) {
      return make_error(ErrorCode::ValidationFailed,
          "The face reflection creates a self-intersecting face loop.");
    }
  }
  for (const auto &[edge, incidence] : edges) {
    static_cast<void>(edge);
    if (incidence.size() == 2U &&
        (target.modified_faces.contains(incidence[0].first) ||
         target.modified_faces.contains(incidence[1].first)) &&
        incidence[0].second == incidence[1].second) {
      return make_error(ErrorCode::ValidationFailed,
          "The face reflection conflicts with an adjacent face's winding. "
          "Select the complete connected surface and use Treat as One.");
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error> apply_geometry_mirror(
    std::vector<TargetWork> &targets, std::span<const FaceWork> faces,
    const FaceWork &reference, const FaceTextureActionOptions &options) {
  const bool horizontal =
      options.action == FaceTextureAction::FlipFaceHorizontal;
  const auto reference_basis = geometric_basis(
      targets[reference.target_index], reference);
  if (!reference_basis.valid) {
    return face_error(reference, "The face reflection has no geometric axis.");
  }
  const auto reference_axis = horizontal ? reference_basis.u_axis
                                         : reference_basis.v_axis;
  double group_min = std::numeric_limits<double>::infinity();
  double group_max = -std::numeric_limits<double>::infinity();
  if (options.treat_as_one) {
    for (const auto &face : faces) {
      for (const auto point : face.world_positions) {
        const double coordinate = quader::dot(point, reference_axis);
        group_min = std::min(group_min, coordinate);
        group_max = std::max(group_max, coordinate);
      }
    }
  }
  std::vector<std::map<quader_poly::ElementId, QVec3>> destinations(targets.size());
  std::map<std::pair<std::size_t, quader_poly::ElementId>, QVec3> expected_normals;
  for (const auto &face : faces) {
    auto &target = targets[face.target_index];
    const auto basis = options.treat_as_one ? reference_basis
                                          : geometric_basis(target, face);
    if (!basis.valid) {
      return face_error(face, "The face reflection has no geometric axis.");
    }
    const auto axis = horizontal ? basis.u_axis : basis.v_axis;
    double minimum = group_min;
    double maximum = group_max;
    if (!options.treat_as_one) {
      minimum = std::numeric_limits<double>::infinity();
      maximum = -std::numeric_limits<double>::infinity();
      for (const auto position : face.world_positions) {
        const double coordinate = quader::dot(position, axis);
        minimum = std::min(minimum, coordinate);
        maximum = std::max(maximum, coordinate);
      }
    }
    const double center = (minimum + maximum) * 0.5;
    auto &target_destinations = destinations[face.target_index];
    for (std::size_t corner = 0U; corner < face.world_positions.size(); ++corner) {
      const auto original = face.world_positions[corner];
      const float distance = static_cast<float>(
          2.0 * (quader::dot(original, axis) - center));
      const auto destination = distance == 0.0F
          ? face.local_positions[corner]
          : target.inverse_world.point(original - axis * distance);
      if (!finite(destination)) {
        return face_error(face, "The reflected vertex is non-finite.");
      }
      const auto [entry, inserted] = target_destinations.emplace(
          face.face->vertices[corner], destination);
      // Coordinate-magnitude-relative tolerances can accept different mirrors
      // on a small face far from the origin. One shared vertex must have one
      // destination, within the polygon owner's fixed geometric tolerance.
      constexpr float kPositionTolerance =
          quader_poly::document_internal::kEpsilon;
      if (!inserted &&
          (std::abs(entry->second.x - destination.x) > kPositionTolerance ||
           std::abs(entry->second.y - destination.y) > kPositionTolerance ||
           std::abs(entry->second.z - destination.z) > kPositionTolerance)) {
        return face_error(face,
            "Selected faces require conflicting positions for a shared vertex. "
            "Use Treat as One or change the selection.");
      }
    }
    expected_normals[{face.target_index, face.face->id}] =
        face.world_normal - axis * (2.0F * quader::dot(face.world_normal, axis));
  }
  // Capture every incident face before moving anything. This includes
  // unselected neighbors, whose corner UVs must remain attached to vertices.
  std::vector<FaceWork> affected;
  for (std::size_t index = 0U; index < targets.size(); ++index) {
    auto &target = targets[index];
    for (const auto &[id, destination] : destinations[index]) {
      const auto *vertex = quader_poly::find_vertex(*target.original, id);
      if (vertex->position.x != destination.x ||
          vertex->position.y != destination.y ||
          vertex->position.z != destination.z) {
        target.modified_vertices.insert(id);
      }
    }
    for (const auto &face : target.original->faces) {
      if (!expected_normals.contains({index, face.id}) &&
          !std::ranges::any_of(face.vertices, [&](auto id) {
            return target.modified_vertices.contains(id);
          })) {
        continue;
      }
      FaceWork captured;
      if (auto error = capture_face(target, index,
              FaceTextureActionFace{.face = make_id<FaceTag>(face.id)}, captured)) {
        return error;
      }
      affected.push_back(std::move(captured));
      target.modified_faces.insert(face.id);
    }
    auto &candidate = target.edit();
    for (const auto &[id, destination] : destinations[index]) {
      if (target.modified_vertices.contains(id)) {
        auto *vertex = quader_poly::find_vertex(candidate, id);
        vertex->position = destination;
      }
    }
  }
  for (const auto &face : faces) {
    auto &candidate = targets[face.target_index].edit();
    quader_poly::document_internal::reverse_face_winding(
        *quader_poly::find_face(candidate, face.face->id));
  }
  for (const auto &face : affected) {
    auto &target = targets[face.target_index];
    auto &candidate = *target.candidate;
    auto *edited_face = quader_poly::find_face(candidate, face.face->id);
    std::vector<QVec3> positions;
    std::vector<QVec3> world_positions;
    std::vector<QVec2> desired;
    for (const auto id : edited_face->vertices) {
      const auto *vertex = quader_poly::find_vertex(candidate, id);
      positions.push_back(vertex->position);
      world_positions.push_back(quader_poly::document_internal::transform_point(
          target.world, vertex->position));
      const auto original = std::ranges::find(face.face->vertices, id);
      desired.push_back(face.effective_uvs[static_cast<std::size_t>(
          original - face.face->vertices.begin())]);
    }
    const auto normal = quader_poly::face_normal(candidate, *edited_face);
    const auto expected = expected_normals.find({face.target_index, face.face->id});
    if (expected != expected_normals.end() &&
        quader::dot(geometric_normal(world_positions), expected->second) < 0.999F) {
      return face_error(face, "The reflected face lost its intended facing.");
    }
    auto base = quader_poly::solve_face_texture_base_uvs(
        positions, desired, normal, edited_face->texture_transform, target.sampling);
    if (!base.has_value()) {
      return face_error(face,
          "The reflection cannot preserve this face's effective texture mapping.");
    }
    edited_face->uvs = std::move(*base);
  }
  for (const auto &target : targets) {
    if (auto error = validate_mirror_topology(target)) {
      return error;
    }
  }
  return std::nullopt;
}

[[nodiscard]] bool bounds_action(FaceTextureAction action) {
  return action >= FaceTextureAction::FitBoth &&
         action <= FaceTextureAction::JustifyCenter;
}

[[nodiscard]] bool geometry_action(FaceTextureAction action) {
  return action == FaceTextureAction::FlipFaceHorizontal ||
         action == FaceTextureAction::FlipFaceVertical;
}

} // namespace

Result<PreparedFaceTextureAction> prepare_face_texture_action(
    std::span<const FaceTextureActionTarget> requested_targets,
    const FaceTextureActionOptions &options) {
  const auto fail = [](Error error) {
    return Result<PreparedFaceTextureAction>::failure(std::move(error));
  };
  if (options.action < FaceTextureAction::AlignToGrid ||
      options.action > FaceTextureAction::JustifyCenter) {
    return fail(make_error(ErrorCode::InvalidArgument, "Unknown face texture action."));
  }
  if ((options.action == FaceTextureAction::RotateClockwise ||
       options.action == FaceTextureAction::RotateCounterClockwise) &&
      (!std::isfinite(options.rotation_degrees) || options.rotation_degrees <= 0.0)) {
    return fail(make_error(ErrorCode::InvalidArgument,
        "Texture rotation requires a finite positive snap angle."));
  }
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
      if (auto error = capture_face(targets.back(), index, requested_face, face)) {
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
  if (geometry_action(options.action)) {
    if (auto error = apply_geometry_mirror(targets, faces, reference, options)) {
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
      if (options.action <= FaceTextureAction::AlignToView) {
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
      const auto validation = prepared.document.validate();
      if (!validation.ok()) {
        return fail(validation.error());
      }
      prepared.receipt.changed = true;
      prepared.receipt.message = geometry_action(options.action)
                                    ? "Selected faces reflected."
                                    : "Face texture changed.";
      prepared.receipt.dirty.geometry = true;
      prepared.receipt.dirty.topology = geometry_action(options.action);
      for (const auto id : target.modified_faces) {
        prepared.receipt.affected.faces.push_back(make_id<FaceTag>(id));
      }
      for (const auto id : target.modified_vertices) {
        prepared.receipt.affected.vertices.push_back(make_id<VertexTag>(id));
      }
      prepared.receipt.modified = prepared.receipt.affected;
      result.changed = true;
    }
    result.documents.push_back(std::move(prepared));
  }
  return Result<PreparedFaceTextureAction>::success(std::move(result));
}

} // namespace quader::modeling
