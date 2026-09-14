////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/modeling_face_texture_canvas.hpp"

#include <cmath>
#include <numbers>

namespace quader {
namespace {
using Map = modeling::FaceTextureCanvasMap;

Map compose(const Map &a, const Map &b) {
  const auto &m = a.uv_from_plane;
  const auto &n = b.uv_from_plane;
  return {.uv_from_plane = {m[0] * n[0] + m[1] * n[2],
                            m[0] * n[1] + m[1] * n[3],
                            m[2] * n[0] + m[3] * n[2],
                            m[2] * n[1] + m[3] * n[3]},
          .uv_offset = {m[0] * b.uv_offset[0] + m[1] * b.uv_offset[1] + a.uv_offset[0],
                        m[2] * b.uv_offset[0] + m[3] * b.uv_offset[1] + a.uv_offset[1]}};
}

Map inverse_map(const Map &map) {
  const auto &m = map.uv_from_plane;
  const double determinant = m[0] * m[3] - m[1] * m[2];
  Map result{.uv_from_plane = {m[3] / determinant, -m[1] / determinant,
                               -m[2] / determinant, m[0] / determinant}};
  result.uv_offset = {
      -result.uv_from_plane[0] * map.uv_offset[0] - result.uv_from_plane[1] * map.uv_offset[1],
      -result.uv_from_plane[2] * map.uv_offset[0] - result.uv_from_plane[3] * map.uv_offset[1]};
  return result;
}

bool usable(const Map &map) {
  for (const double value : map.uv_from_plane) if (!std::isfinite(value)) return false;
  for (const double value : map.uv_offset) if (!std::isfinite(value)) return false;
  const auto &m = map.uv_from_plane;
  const double magnitude = std::hypot(m[0], m[1]) * std::hypot(m[2], m[3]);
  return magnitude > 0.0 && std::isfinite(magnitude) &&
         std::abs(m[0] * m[3] - m[1] * m[2]) / magnitude > 1.0e-10;
}

} // namespace

modeling::FaceTextureCanvasMap transform_face_texture_canvas_map(
    const Map &map, const StandardMaterialTextureTransform &transform,
    bool inverse) {
  const double angle = transform.rotation_degrees * std::numbers::pi / 180.0;
  const double c = std::cos(angle), s = std::sin(angle);
  Map material{.uv_from_plane = {c * transform.scale[0], -s * transform.scale[1],
                                  s * transform.scale[0], c * transform.scale[1]}};
  const double x = transform.center[0], y = transform.center[1];
  material.uv_offset = {x + transform.offset[0] - material.uv_from_plane[0] * x - material.uv_from_plane[1] * y,
                        y + transform.offset[1] - material.uv_from_plane[2] * x - material.uv_from_plane[3] * y};
  // Filament flipUV and the explicit fragment V flip cancel each other. The
  // uploaded CPU effective UV enters the material transform unchanged.
  return compose(inverse ? inverse_map(material) : material, map);
}

void measure_face_texture_canvas(ModelingFaceTextureCanvas &canvas) {
  if (!usable(canvas.chart.map)) {
    canvas.editable = false;
    if (canvas.disabled_reason.empty()) {
      canvas.disabled_reason = "The face has no usable affine texture mapping.";
    }
    canvas.repeat_width = canvas.repeat_height = 0.0;
    return;
  }
  const auto &m = inverse_map(canvas.chart.map).uv_from_plane;
  canvas.repeat_width = std::hypot(m[0], m[2]);
  canvas.repeat_height = std::hypot(m[1], m[3]);
  const double sign_x = std::signbit(canvas.texture_transform.scale.x) ? -1.0 : 1.0;
  canvas.rotation_degrees = std::atan2(sign_x * m[2], sign_x * m[0]) *
      180.0 / std::numbers::pi;
  if (canvas.rotation_degrees >= 180.0) canvas.rotation_degrees -= 360.0;
}

modeling::Result<Map> prepare_face_texture_gesture_map(
    const ModelingFaceTextureCanvas &baseline,
    const ModelingFaceTextureGestureBegin &begin,
    const ModelingFaceTextureGestureUpdate &update) {
  const auto failure = [](const char *message) {
    return modeling::Result<Map>::failure(
        {.code = modeling::ErrorCode::InvalidArgument, .message = message});
  };
  if (!std::isfinite(update.current.x) || !std::isfinite(update.current.y) ||
      !std::isfinite(begin.start.x) || !std::isfinite(begin.start.y) ||
      !std::isfinite(begin.pivot.x) || !std::isfinite(begin.pivot.y) ||
      !usable(baseline.chart.map)) return failure("Texture gesture has no usable affine mapping.");
  Map spatial;
  const double dx = update.current.x - begin.start.x;
  const double dy = update.current.y - begin.start.y;
  const double px = begin.pivot.x, py = begin.pivot.y;
  if (begin.kind == ModelingFaceTextureGestureKind::Translate) {
    spatial.uv_offset = {-dx, -dy};
    Map candidate = compose(baseline.chart.map, spatial);
    if (update.precision && !update.snap) {
      for (std::size_t axis = 0; axis != 2; ++axis) {
        const double extent = axis == 0 ? baseline.texture_extent.x : baseline.texture_extent.y;
        if (!std::isfinite(extent) || extent <= 0.0)
          return failure("Texture precision requires usable image dimensions.");
        const double delta = candidate.uv_offset[axis] - baseline.chart.map.uv_offset[axis];
        candidate.uv_offset[axis] = baseline.chart.map.uv_offset[axis] +
            std::round(delta * extent) / extent;
      }
    }
    return usable(candidate) ? modeling::Result<Map>::success(candidate)
                             : failure("Texture translation is not finite.");
  }
  if (begin.kind == ModelingFaceTextureGestureKind::Rotate) {
    const double ax = begin.start.x - px, ay = begin.start.y - py;
    const double bx = update.current.x - px, by = update.current.y - py;
    if (std::hypot(ax, ay) < 1.0e-8 || std::hypot(bx, by) < 1.0e-8)
      return failure("Move away from the rotation pivot.");
    double angle = std::atan2(ax * by - ay * bx, ax * bx + ay * by);
    const double step = update.snap ? update.rotation_step : update.precision ? 1.0 : 0.0;
    if (step > 0.0 && std::isfinite(step))
      angle = std::round(angle * 180.0 / std::numbers::pi / step) * step * std::numbers::pi / 180.0;
    const double c = std::cos(angle), s = std::sin(angle);
    spatial.uv_from_plane = {c, s, -s, c};
  } else {
    const bool width = begin.kind == ModelingFaceTextureGestureKind::ScaleWidth;
    const std::size_t row = width ? 0 : 2;
    const auto &matrix = baseline.chart.map.uv_from_plane;
    const double row_length = std::hypot(matrix[row], matrix[row + 1]);
    const double axis_x = matrix[row] / row_length;
    const double axis_y = matrix[row + 1] / row_length;
    // The selected UV row ignores movement along the other image axis, even
    // for a sheared chart. Normalize it so pivot tolerance stays in map units.
    const double initial = axis_x * (begin.start.x - px) +
                           axis_y * (begin.start.y - py);
    const double current = axis_x * (update.current.x - px) +
                           axis_y * (update.current.y - py);
    if (std::abs(initial) < 1.0e-8) return failure("Texture scale pivot coincides with its handle.");
    if (current == initial) return modeling::Result<Map>::success(baseline.chart.map);
    double factor = current / initial;
    const double step = update.snap ? update.grid_step : update.precision ? 1.0 : 0.0;
    if (step > 0.0 && std::isfinite(step)) {
      const double repeat = width ? baseline.repeat_width : baseline.repeat_height;
      if (!std::isfinite(repeat) || repeat <= 0.0)
        return failure("Texture scale requires a usable repeat dimension.");
      factor = std::round(factor * repeat / step) * step / repeat;
    }
    // Crossing the opposite edge would introduce an accidental mirror. Existing
    // mirrored maps remain mirrored because the spatial scale stays positive.
    if (!std::isfinite(factor) || factor <= 1.0e-8)
      return failure("Texture scale must remain nonzero.");
    // Inverting the chart can place an exact snapped repeat a few double ULPs
    // from its baseline. Preserve the original phase and no-op history exactly.
    if (std::abs(factor - 1.0) <= 8.0 * std::numeric_limits<double>::epsilon())
      return modeling::Result<Map>::success(baseline.chart.map);
    // The normal pointer motion drives an image-axis dimension. Scale the UV
    // row rather than shearing the rotated chart in physical plane coordinates.
    Map candidate = baseline.chart.map;
    const std::size_t axis = width ? 0 : 1;
    const double pivot_uv = candidate.uv_from_plane[row] * px +
        candidate.uv_from_plane[row + 1] * py + candidate.uv_offset[axis];
    candidate.uv_from_plane[row] /= factor;
    candidate.uv_from_plane[row + 1] /= factor;
    candidate.uv_offset[axis] = pivot_uv +
        (candidate.uv_offset[axis] - pivot_uv) / factor;
    return usable(candidate) ? modeling::Result<Map>::success(candidate)
                             : failure("Texture scale would create a singular mapping.");
  }
  const auto &m = spatial.uv_from_plane;
  spatial.uv_offset = {px - m[0] * px - m[1] * py,
                        py - m[2] * px - m[3] * py};
  const Map candidate = compose(baseline.chart.map, spatial);
  return usable(candidate) ? modeling::Result<Map>::success(candidate)
                           : failure("Texture gesture would create a singular mapping.");
}

} // namespace quader
