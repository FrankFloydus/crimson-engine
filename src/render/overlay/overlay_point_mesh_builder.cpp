////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_point_mesh_builder.hpp"

#include "core/color_space.hpp"

#include <algorithm>
#include <cstdint>

namespace quader {

OverlayPointMesh OverlayPointMeshBuilder::build(
    const std::vector<const ViewportOverlayPoint *> &points,
    const ViewportOverlayCamera &camera, float size_pixels, float depth_bias,
    std::uint32_t override_color, bool use_override_color) {
  OverlayPointMesh mesh;
  mesh.vertices.reserve(points.size() * 4u);
  mesh.indices.reserve(points.size() * 6u);
  const OverlayCameraProjector projector(camera);
  for (const ViewportOverlayPoint *point : points) {
    const std::uint32_t color =
        use_override_color ? override_color : point->color;
    (void)append_point_quad(mesh, point->position, color, projector,
                            size_pixels, depth_bias);
  }
  return mesh;
}

bool OverlayPointMeshBuilder::append_point_quad(
    OverlayPointMesh &mesh, QVec3 position, std::uint32_t color,
    const OverlayCameraProjector &projector, float size_pixels,
    float depth_bias) {
  OverlayViewPoint center = projector.view_point_for(position);
  if (projector.near_plane_distance(center) < 0.0f) {
    return false;
  }

  OverlayProjectedPoint projected = projector.project(center);
  if (!projected.valid) {
    return false;
  }
  projector.apply_device_depth_bias(projected, depth_bias);

  const float half_size = std::max(size_pixels, 1.0f) * 0.5f;
  const std::uint32_t linear_color = rgba8_srgb_to_linear(color);
  const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(
      {projector.with_pixel_offset(projected.device, {-half_size, -half_size}),
       {0.0f, 0.0f},
       linear_color});
  mesh.vertices.push_back(
      {projector.with_pixel_offset(projected.device, {half_size, -half_size}),
       {1.0f, 0.0f},
       linear_color});
  mesh.vertices.push_back(
      {projector.with_pixel_offset(projected.device, {half_size, half_size}),
       {1.0f, 1.0f},
       linear_color});
  mesh.vertices.push_back(
      {projector.with_pixel_offset(projected.device, {-half_size, half_size}),
       {0.0f, 1.0f},
       linear_color});
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 1u);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset + 3u);
  return true;
}

} // namespace quader
