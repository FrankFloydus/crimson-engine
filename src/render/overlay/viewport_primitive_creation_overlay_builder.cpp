////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_primitive_creation_overlay_builder.hpp"

#include "render/overlay/overlay_camera_projector.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/primitive_creation_controls.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace quader {
namespace {

constexpr std::size_t kDiscSegments = 32u;
constexpr float kTau = 6.2831853071795864769f;
constexpr float kFaceInnerRadius = 0.72f;
constexpr float kPlusExtent = 0.52f;
constexpr float kPlusHalfWidth = 0.12f;

const std::array<QVec2, kDiscSegments> &disc_directions() {
  static const std::array<QVec2, kDiscSegments> kDirections = [] {
    std::array<QVec2, kDiscSegments> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
      const float angle = kTau * static_cast<float>(index) /
                          static_cast<float>(result.size());
      result[index] = {std::cos(angle), std::sin(angle)};
    }
    return result;
  }();
  return kDirections;
}

void append_triangle(ViewportOverlayFrame &frame, QVec3 a, QVec3 b, QVec3 c,
                     std::uint32_t color) {
  frame.triangles.push_back({
      .layer = ViewportOverlayLayer::ToolHandle,
      .a = a,
      .b = b,
      .c = c,
      .color = color,
      .style_role = ViewportOverlayStyleRole::ExplicitColor,
      .source = ViewportOverlaySource::Tool,
  });
}

void append_rectangle(ViewportOverlayFrame &frame, QVec3 center, QVec3 right,
                      QVec3 down, QVec2 min, QVec2 max,
                      std::uint32_t color) {
  const QVec3 a = center + right * min.x + down * min.y;
  const QVec3 b = center + right * max.x + down * min.y;
  const QVec3 c = center + right * max.x + down * max.y;
  const QVec3 d = center + right * min.x + down * max.y;
  append_triangle(frame, a, b, c, color);
  append_triangle(frame, a, c, d, color);
}

bool drawable(const PrimitiveCreationControl &control) {
  return control.visible && std::isfinite(control.screen_position.x) &&
         std::isfinite(control.screen_position.y) &&
         std::isfinite(control.depth) && control.depth > 0.0f &&
         std::isfinite(control.radius_pixels) && control.radius_pixels > 0.0f;
}

void append_control(ViewportOverlayFrame &frame,
                    const ViewportOverlayCamera &camera,
                    const PrimitiveCreationControlsFrame &controls,
                    std::size_t index, float perspective_span_per_depth) {
  const PrimitiveCreationControl &control = controls.controls[index];
  if (!drawable(control)) {
    return;
  }

  const bool face = index < 6u;
  const bool highlighted = control.enabled &&
                           (controls.hovered == static_cast<int>(index) ||
                            controls.active == static_cast<int>(index));
  const std::uint32_t fill =
      face ? (highlighted ? kOverlayPrimitiveCubeFaceHighlightColor
                          : kOverlayPrimitiveCubeFaceColor)
           : (!control.enabled ? kOverlayPrimitiveCubeSubdivisionDisabledColor
              : highlighted ? kOverlayPrimitiveCubeSubdivisionHighlightColor
                            : kOverlayPrimitiveCubeSubdivisionColor);

  const float world_per_pixel =
      (camera.projection == ViewportOverlayProjection::Orthographic
           ? camera.orthographic_size
           : control.depth * perspective_span_per_depth) /
      camera.viewport_size.y;
  const QVec3 pixel_right = camera.right * world_per_pixel;
  const QVec3 pixel_down = camera.up * -world_per_pixel;
  // Reconstruct the published screen target, rather than independently
  // projecting its world anchor. This also preserves frozen button placement.
  const QVec3 center =
      camera.eye + camera.forward * control.depth +
      pixel_right * (control.screen_position.x - camera.viewport_size.x * 0.5f) +
      pixel_down * (control.screen_position.y - camera.viewport_size.y * 0.5f);
  const QVec3 right = pixel_right * control.radius_pixels;
  const QVec3 down = pixel_down * control.radius_pixels;

  const auto &directions = disc_directions();
  for (std::size_t segment = 0u; segment < directions.size(); ++segment) {
    const QVec2 a = directions[segment];
    const QVec2 b = directions[(segment + 1u) % directions.size()];
    const QVec3 outer_a = right * a.x + down * a.y;
    const QVec3 outer_b = right * b.x + down * b.y;
    if (face) {
      const QVec3 inner_a = center + outer_a * kFaceInnerRadius;
      const QVec3 inner_b = center + outer_b * kFaceInnerRadius;
      append_triangle(frame, center, inner_a, inner_b, fill);
      append_triangle(frame, inner_a, center + outer_a, center + outer_b,
                      kOverlayPrimitiveCubeFaceOutlineColor);
      append_triangle(frame, inner_a, center + outer_b, inner_b,
                      kOverlayPrimitiveCubeFaceOutlineColor);
    } else {
      append_triangle(frame, center, center + outer_a, center + outer_b, fill);
    }
  }
  if (!face) {
    // Keep the glyph in the same triangle batch as its yellow background.
    // Unlike point/line sizes, this physical-pixel geometry is not rescaled by
    // the overlay style resolver. Plus arms avoid glyph self-overdraw; minus
    // uses just the same horizontal bar.
    append_rectangle(frame, center, right, down,
                     {-kPlusExtent, -kPlusHalfWidth},
                     {kPlusExtent, kPlusHalfWidth},
                     kOverlayPrimitiveCubeSubdivisionGlyphColor);
    if (!control.subdivision_decrement) {
      append_rectangle(frame, center, right, down,
                       {-kPlusHalfWidth, -kPlusExtent},
                       {kPlusHalfWidth, -kPlusHalfWidth},
                       kOverlayPrimitiveCubeSubdivisionGlyphColor);
      append_rectangle(frame, center, right, down,
                       {-kPlusHalfWidth, kPlusHalfWidth},
                       {kPlusHalfWidth, kPlusExtent},
                       kOverlayPrimitiveCubeSubdivisionGlyphColor);
    }
  }
}

} // namespace

void ViewportPrimitiveCreationOverlayBuilder::append(
    ViewportOverlayFrame &frame, const ViewportOverlayCamera &camera,
    const PrimitiveCreationControlsFrame &controls) {
  if (!controls.visible) {
    return;
  }
  const std::size_t highlight_count =
      std::min(controls.highlight_point_count, controls.highlight_points.size());
  if (highlight_count > 1u) {
    frame.lines.reserve(frame.lines.size() + highlight_count - 1u);
    for (std::size_t index = 1u; index < highlight_count; ++index) {
      frame.lines.push_back({
          .layer = ViewportOverlayLayer::ToolPreviewLine,
          .start = controls.highlight_points[index - 1u],
          .end = controls.highlight_points[index],
          .color = kOverlayPrimitiveCubeSubdivisionHighlightColor,
          .width_pixels = kOverlayToolHandleLineWidthPixels,
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::Tool,
      });
    }
  }
  std::size_t triangle_count = 0u;
  for (std::size_t index = 0u; index < controls.controls.size(); ++index) {
    const PrimitiveCreationControl &control = controls.controls[index];
    if (drawable(control)) {
      triangle_count += index < 6u
                            ? kDiscSegments * 3u
                            : kDiscSegments + (control.subdivision_decrement ? 2u : 6u);
    }
  }
  if (triangle_count == 0u) {
    return;
  }
  frame.triangles.reserve(frame.triangles.size() + triangle_count);
  const ViewportOverlayCamera sanitized =
      OverlayCameraProjector::sanitized_camera(camera);
  const float perspective_span_per_depth =
      2.0f * std::tan(sanitized.fov_degrees * kTau / 720.0f);
  // Paint the resolved hover/drag target last when controls overlap, without
  // changing its radius or making a second picking decision in the renderer.
  for (std::size_t index = 0u; index < controls.controls.size(); ++index) {
    if (static_cast<int>(index) != controls.hovered &&
        static_cast<int>(index) != controls.active) {
      append_control(frame, sanitized, controls, index,
                     perspective_span_per_depth);
    }
  }
  if (controls.hovered >= 0 &&
      controls.hovered < static_cast<int>(controls.controls.size()) &&
      controls.hovered != controls.active) {
    append_control(frame, sanitized, controls,
                   static_cast<std::size_t>(controls.hovered),
                   perspective_span_per_depth);
  }
  if (controls.active >= 0 &&
      controls.active < static_cast<int>(controls.controls.size())) {
    append_control(frame, sanitized, controls,
                   static_cast<std::size_t>(controls.active),
                   perspective_span_per_depth);
  }
}

} // namespace quader
