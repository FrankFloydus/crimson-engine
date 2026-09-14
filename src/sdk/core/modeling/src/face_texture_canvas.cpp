////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/face_texture_canvas.hpp>

#include "polygon_document_native.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace quader::modeling {
namespace {

using quader::QVec2;
using quader::QVec3;
using quader_geometry::QVec3d;

/// Retains source samples for reconstruction through the canonical texture owner.
struct CanvasWork {
  FaceTextureCanvasChart chart;
  const quader_poly::Face *face = nullptr;
  std::vector<QVec3> positions;
  std::vector<QVec2> effective_uvs;
  QVec3 normal;
  quader_poly::Transform3 sampling;
};

/// Adapts the portable vector without exposing neutral storage in the API.
[[nodiscard]] QVec3 native_vector(Vec3 value) {
  return {value.x, value.y, value.z};
}

/// Adapts occurrence sampling to the canonical evaluator.
[[nodiscard]] quader_poly::Transform3 native_transform(const Transform3 &value) {
  return {.x_axis = native_vector(value.x_axis),
          .y_axis = native_vector(value.y_axis),
          .z_axis = native_vector(value.z_axis),
          .origin = native_vector(value.origin)};
}

/// Rejects invalid placement before using it in geometric or sampling arithmetic.
[[nodiscard]] bool valid_transform(const Transform3 &transform) {
  const auto finite = [](Vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
  };
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return false;
  }
  const QVec3d x{transform.x_axis};
  const QVec3d y{transform.y_axis};
  const QVec3d z{transform.z_axis};
  const double determinant = quader_geometry::dot(x, quader_geometry::cross(y, z));
  const double scale = std::sqrt(quader_geometry::dot(x, x) *
                                quader_geometry::dot(y, y) *
                                quader_geometry::dot(z, z));
  return std::isfinite(determinant) && std::isfinite(scale) && scale > 0.0 &&
         std::abs(determinant) > 1.0e-10 * scale;
}

/// Preserves mirrored charts while rejecting singular and non-finite maps.
[[nodiscard]] bool valid_map(const FaceTextureCanvasMap &map) {
  const auto &matrix = map.uv_from_plane;
  const double u_length = std::hypot(matrix[0], matrix[1]);
  const double v_length = std::hypot(matrix[2], matrix[3]);
  return std::isfinite(map.uv_offset[0]) && std::isfinite(map.uv_offset[1]) &&
         std::isfinite(u_length) && std::isfinite(v_length) &&
         u_length > 0.0 && v_length > 0.0 &&
         std::abs((matrix[0] / u_length) * (matrix[3] / v_length) -
                  (matrix[1] / u_length) * (matrix[2] / v_length)) > 1.0e-10;
}

/// Evaluates normalized UVs in double precision before final authored storage.
[[nodiscard]] std::array<double, 2> project(
    const FaceTextureCanvasMap &map, const std::array<double, 2> &point) {
  const auto &matrix = map.uv_from_plane;
  return {matrix[0] * point[0] + matrix[1] * point[1] + map.uv_offset[0],
          matrix[2] * point[0] + matrix[3] * point[1] + map.uv_offset[1]};
}

/// Calculates chart extents without allowing a large tile offset to hide errors.
[[nodiscard]] std::array<double, 2> texture_span(std::span<const QVec2> uvs) {
  if (uvs.empty()) {
    return {};
  }
  std::array<double, 2> minimum{uvs.front().x, uvs.front().y};
  auto maximum = minimum;
  for (const auto uv : uvs) {
    minimum[0] = std::min(minimum[0], static_cast<double>(uv.x));
    minimum[1] = std::min(minimum[1], static_cast<double>(uv.y));
    maximum[0] = std::max(maximum[0], static_cast<double>(uv.x));
    maximum[1] = std::max(maximum[1], static_cast<double>(uv.y));
  }
  return {maximum[0] - minimum[0], maximum[1] - minimum[1]};
}

/// Captures a stable physical plane, then validates the effective affine UV chart.
[[nodiscard]] Result<CanvasWork> capture(const FaceTextureCanvasInput &input) {
  const auto fail = [](ErrorCode code, std::string message) {
    return Result<CanvasWork>::failure(make_error(code, std::move(message)));
  };
  if (input.document == nullptr) {
    return fail(ErrorCode::InvalidArgument, "The canvas document is unavailable.");
  }
  const auto &document = PolygonDocumentNativeAccess::document(*input.document);
  CanvasWork work;
  work.face = quader_poly::find_face(document, input.face.index);
  if (work.face == nullptr) {
    return fail(ErrorCode::StaleId, "The canvas face is unavailable.");
  }
  if (!valid_transform(input.world_from_local)) {
    return fail(ErrorCode::InvalidArgument, "The face occurrence transform is invalid.");
  }
  const auto disable = [&work](std::string reason) {
    work.chart.disabled_reason = std::move(reason);
    return Result<CanvasWork>::success(std::move(work));
  };
  work.positions.reserve(work.face->vertices.size());
  for (const auto id : work.face->vertices) {
    const auto *vertex = quader_poly::find_vertex(document, id);
    if (vertex == nullptr || !std::isfinite(vertex->position.x) ||
        !std::isfinite(vertex->position.y) || !std::isfinite(vertex->position.z)) {
      return fail(ErrorCode::ValidationFailed, "The canvas face has invalid vertices.");
    }
    work.positions.push_back(vertex->position);
  }
  if (work.positions.size() < 3U) {
    return disable("The face has no usable geometric plane.");
  }

  // Transform offsets directly: a distant occurrence origin must not quantize
  // short edges or change the physical outline when its texture is edited.
  const auto &world = input.world_from_local;
  const QVec3d local_origin{work.positions.front()};
  std::vector<QVec3d> physical;
  physical.reserve(work.positions.size());
  double radius_squared = 0.0;
  QVec3d area{};
  for (const auto position : work.positions) {
    const auto delta = QVec3d{position} - local_origin;
    const auto point = QVec3d{world.x_axis} * delta.x +
                       QVec3d{world.y_axis} * delta.y +
                       QVec3d{world.z_axis} * delta.z;
    if (!physical.empty()) {
      area = area + quader_geometry::cross(physical.back(), point);
    }
    physical.push_back(point);
    radius_squared = std::max(radius_squared, quader_geometry::dot(point, point));
  }
  const double area_length = std::sqrt(quader_geometry::dot(area, area));
  if (!std::isfinite(area_length) || radius_squared == 0.0 ||
      area_length <= 1.0e-12 * radius_squared) {
    return disable("The face has no usable geometric plane.");
  }
  const auto normal = area / area_length;
  // Keep the face upright in world Y, independently of texture orientation,
  // boundary lengths, and the first corner. Horizontal faces use world X.
  auto x_axis = quader_geometry::cross(QVec3d{0.0, 1.0, 0.0}, normal);
  if (quader_geometry::dot(x_axis, x_axis) <= 1.0e-12) {
    x_axis = QVec3d{1.0, 0.0, 0.0} - normal * normal.x;
  }
  x_axis = x_axis / std::sqrt(quader_geometry::dot(x_axis, x_axis));
  const auto y_axis = quader_geometry::cross(normal, x_axis);
  bool planar = true;
  const double plane_tolerance = 32.0 * std::numeric_limits<float>::epsilon() *
                                 std::sqrt(radius_squared);
  work.chart.outline.reserve(physical.size());
  for (const auto point : physical) {
    work.chart.outline.push_back({quader_geometry::dot(point, x_axis),
                                  quader_geometry::dot(point, y_axis)});
    planar = planar && std::abs(quader_geometry::dot(point, normal)) <= plane_tolerance;
  }
  if (!planar) {
    return disable("Canvas manipulation requires a planar face.");
  }
  if (!valid_transform(input.sampling_from_local) ||
      !quader_poly::valid_face_texture_transform(work.face->texture_transform) ||
      (!work.face->uvs.empty() && work.face->uvs.size() != work.positions.size())) {
    return disable("The face texture mapping is invalid.");
  }
  work.normal = quader_poly::face_normal(document, *work.face);
  work.sampling = native_transform(input.sampling_from_local);
  const auto carrier = quader_poly::face_texture_carrier(
      work.positions, work.face->uvs, work.normal, work.face->texture_transform);
  work.effective_uvs.reserve(work.positions.size());
  for (std::size_t corner = 0U; corner < work.positions.size(); ++corner) {
    const auto position = work.positions[corner];
    const auto base = work.face->uvs.empty()
        ? quader_poly::generated_texture_uv(position, work.normal)
        : work.face->uvs[corner];
    const auto uv = quader_poly::evaluate_texture_mapping(
        quader_poly::apply_face_texture_transform(base, work.face->texture_transform),
        position, work.normal, carrier, work.sampling);
    if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
      return disable("The face texture mapping is non-finite.");
    }
    work.effective_uvs.push_back(uv);
  }
  const auto affine = quader_poly::fit_face_texture_affine_chart(
      work.chart.outline, work.positions, work.face->uvs, work.normal,
      work.face->texture_transform, work.sampling);
  if (!affine.has_value()) {
    return disable("The face has no usable affine texture projection.");
  }
  work.chart.map = {.uv_from_plane = affine->carrier, .uv_offset = affine->offset};
  if (!valid_map(work.chart.map)) {
    return disable("The face texture projection is singular.");
  }
  const auto precision = quader_poly::face_texture_precision_bounds(
      work.positions, work.face->uvs, work.normal, work.face->texture_transform,
      work.sampling, quader_poly::TextureUvPrecision::AffineResidual);
  if (precision.size() != work.positions.size()) {
    return disable("The face has no usable affine texture projection.");
  }
  const auto span = texture_span(work.effective_uvs);
  for (std::size_t corner = 0U; corner < work.positions.size(); ++corner) {
    const auto fitted = project(work.chart.map, work.chart.outline[corner]);
    const std::array stored{work.effective_uvs[corner].x, work.effective_uvs[corner].y};
    for (std::size_t axis = 0U; axis < 2U; ++axis) {
      const double error = fitted[axis] - stored[axis];
      const double uncertainty = precision[corner][axis];
      if (!std::isfinite(uncertainty) ||
          (std::abs(error) > uncertainty &&
           !quader_poly::texture_uv_coordinate_close(
               fitted[axis] - std::copysign(uncertainty, error), stored[axis], span[axis]))) {
        return disable("Canvas manipulation requires an affine texture projection.");
      }
    }
  }
  work.chart.mapping_usable = true;
  return Result<CanvasWork>::success(std::move(work));
}

} // namespace

Result<FaceTextureCanvasChart> query_face_texture_canvas(
    const FaceTextureCanvasInput &input) {
  auto captured = capture(input);
  if (!captured.ok()) {
    return Result<FaceTextureCanvasChart>::failure(captured.error());
  }
  return Result<FaceTextureCanvasChart>::success(std::move(captured.value().chart));
}

Result<PreparedFaceTextureDocument> prepare_face_texture_canvas(
    const FaceTextureCanvasInput &input, const FaceTextureCanvasMap &map) {
  const auto fail = [](ErrorCode code, std::string message) {
    return Result<PreparedFaceTextureDocument>::failure(make_error(code, std::move(message)));
  };
  if (!valid_map(map)) {
    return fail(ErrorCode::InvalidArgument, "The desired texture projection is singular or non-finite.");
  }
  const auto captured = capture(input);
  if (!captured.ok()) {
    return Result<PreparedFaceTextureDocument>::failure(captured.error());
  }
  const auto &work = captured.value();
  if (!work.chart.mapping_usable) {
    return fail(ErrorCode::UnsupportedOperation, work.chart.disabled_reason);
  }
  PreparedFaceTextureDocument result{.key = input.key, .document = *input.document};
  result.receipt.message = "Face texture unchanged.";
  if (map == work.chart.map) {
    return Result<PreparedFaceTextureDocument>::success(std::move(result));
  }
  std::vector<QVec2> desired;
  desired.reserve(work.positions.size());
  for (const auto point : work.chart.outline) {
    const auto uv = project(map, point);
    const QVec2 stored{static_cast<float>(uv[0]), static_cast<float>(uv[1])};
    if (!std::isfinite(stored.x) || !std::isfinite(stored.y)) {
      return fail(ErrorCode::ValidationFailed, "The desired texture coordinates cannot be stored.");
    }
    desired.push_back(stored);
  }
  const auto original_span = texture_span(work.effective_uvs);
  const auto desired_span = texture_span(desired);
  bool same = true;
  for (std::size_t corner = 0U; corner < desired.size(); ++corner) {
    same = same && quader_poly::texture_uv_coordinate_close(
        desired[corner].x, work.effective_uvs[corner].x,
        std::max(original_span[0], desired_span[0])) &&
        quader_poly::texture_uv_coordinate_close(
            desired[corner].y, work.effective_uvs[corner].y,
            std::max(original_span[1], desired_span[1]));
  }
  if (same) {
    return Result<PreparedFaceTextureDocument>::success(std::move(result));
  }
  auto base = quader_poly::solve_face_texture_base_uvs(
      work.positions, desired, work.normal, work.face->texture_transform, work.sampling);
  if (!base.has_value() || !quader_poly::face_texture_uvs_reconstruct(
      work.positions, *base, desired, work.normal, work.face->texture_transform,
      work.sampling)) {
    return fail(ErrorCode::ValidationFailed,
                "The current texture-lock mapping cannot represent this UV edit accurately.");
  }
  auto replacement = PolygonDocumentNativeAccess::document(*input.document);
  auto *face = quader_poly::find_face(replacement, input.face.index);
  face->uvs = std::move(*base);
  PolygonDocumentNativeAccess::replace_document(result.document, std::move(replacement));
  ++PolygonDocumentNativeAccess::impl(result.document).content_revision;
  const auto validation = PolygonDocumentNativeAccess::validate_authored_topology(result.document);
  if (!validation.ok()) {
    return Result<PreparedFaceTextureDocument>::failure(validation.error());
  }
  result.receipt.changed = true;
  result.receipt.message = "Updated face texture.";
  result.receipt.dirty.geometry = true;
  result.receipt.affected.faces.push_back(input.face);
  result.receipt.modified = result.receipt.affected;
  return Result<PreparedFaceTextureDocument>::success(std::move(result));
}

} // namespace quader::modeling
