////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_line_mesh_builder.hpp"

#include "core/color_space.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace quader {

OverlayLineMesh OverlayLineMeshBuilder::build(
    const std::vector<const ViewportOverlayLine *> &lines,
    const ViewportOverlayCamera &camera, float width_pixels, float depth_bias) {
  OverlayLineMesh mesh;
  mesh.vertices.reserve(lines.size() * 4u);
  mesh.indices.reserve(lines.size() * 6u);
  const OverlayCameraProjector projector(camera);
  for (const ViewportOverlayLine *line : lines) {
    (void)append_line_quad(mesh, *line, projector, width_pixels, depth_bias);
  }
  return mesh;
}

bool OverlayLineMeshBuilder::append_line_quad(
    OverlayLineMesh &mesh, const ViewportOverlayLine &line,
    const OverlayCameraProjector &projector, float width_pixels,
    float depth_bias) {
  const std::optional<OverlayClippedLine> clipped =
      projector.clip_line_to_near_plane(line.start, line.end);
  if (!clipped.has_value()) {
    return false;
  }

  OverlayProjectedPoint projected_a = projector.project(clipped->start);
  OverlayProjectedPoint projected_b = projector.project(clipped->end);
  if (!projected_a.valid || !projected_b.valid) {
    return false;
  }
  projector.apply_device_depth_bias(projected_a, depth_bias);
  projector.apply_device_depth_bias(projected_b, depth_bias);

  QVec2 pixel_direction = projected_b.pixel - projected_a.pixel;
  const float pixel_length = std::sqrt(pixel_direction.x * pixel_direction.x +
                                       pixel_direction.y * pixel_direction.y);
  if (pixel_length > 0.000001f) {
    pixel_direction = {pixel_direction.x / pixel_length,
                       pixel_direction.y / pixel_length};
  } else {
    pixel_direction = {1.0f, 0.0f};
  }
  const QVec2 pixel_normal{-pixel_direction.y, pixel_direction.x};
  const float side_distance =
      (std::max(width_pixels, 1.0f) + kOverlayLineFeatherPixels) * 0.5f;
  const QVec2 start_cap_offset = pixel_direction * -side_distance;
  const QVec2 end_cap_offset = pixel_direction * side_distance;
  const std::uint32_t linear_color = rgba8_srgb_to_linear(line.color);
  const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());

  mesh.vertices.push_back({
      projector.with_pixel_offset(
          projected_a.device, start_cap_offset + pixel_normal * -side_distance),
      {-side_distance, 0.0f, 0.0f, 0.0f},
      linear_color,
  });
  mesh.vertices.push_back({
      projector.with_pixel_offset(
          projected_b.device, end_cap_offset + pixel_normal * -side_distance),
      {-side_distance, 0.0f, 0.0f, 0.0f},
      linear_color,
  });
  mesh.vertices.push_back({
      projector.with_pixel_offset(
          projected_b.device, end_cap_offset + pixel_normal * side_distance),
      {side_distance, 0.0f, 0.0f, 0.0f},
      linear_color,
  });
  mesh.vertices.push_back({
      projector.with_pixel_offset(
          projected_a.device, start_cap_offset + pixel_normal * side_distance),
      {side_distance, 0.0f, 0.0f, 0.0f},
      linear_color,
  });
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 1u);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset + 3u);
  return true;
}

} // namespace quader
