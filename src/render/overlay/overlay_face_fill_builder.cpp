////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_face_fill_builder.hpp"

#include "core/color_space.hpp"

#include <cstdint>

namespace quader {

OverlayFaceFillMesh
OverlayFaceFillBuilder::build_layer(const ViewportOverlayFrame &frame,
                                    ViewportOverlayLayer layer) {
  OverlayFaceFillMesh mesh;
  mesh.vertices.reserve(frame.triangles.size() * 3u);
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    if (triangle.layer == layer) {
      append_triangle(mesh, triangle);
    }
  }
  mesh.indices.reserve(mesh.vertices.size());
  for (std::uint32_t index = 0; index < mesh.vertices.size(); ++index) {
    mesh.indices.push_back(index);
  }
  return mesh;
}

void OverlayFaceFillBuilder::append_triangle(
    OverlayFaceFillMesh &mesh, const ViewportOverlayTriangle &triangle) {
  const std::uint32_t linear_color = rgba8_srgb_to_linear(triangle.color);
  mesh.vertices.push_back(
      {{triangle.a.x, triangle.a.y, triangle.a.z}, {}, linear_color});
  mesh.vertices.push_back(
      {{triangle.b.x, triangle.b.y, triangle.b.z}, {}, linear_color});
  mesh.vertices.push_back(
      {{triangle.c.x, triangle.c.y, triangle.c.z}, {}, linear_color});
}

} // namespace quader
