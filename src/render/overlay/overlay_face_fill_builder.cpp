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
  const std::uint32_t first_vertex =
      static_cast<std::uint32_t>(mesh.vertices.size());
  mesh.vertices.push_back(
      {{triangle.a.x, triangle.a.y, triangle.a.z}, {}, linear_color});
  mesh.vertices.push_back(
      {{triangle.b.x, triangle.b.y, triangle.b.z}, {}, linear_color});
  mesh.vertices.push_back(
      {{triangle.c.x, triangle.c.y, triangle.c.z}, {}, linear_color});
  const auto append_binding = [&](int vertex_id, std::uint32_t offset,
                                  QVec3 position) {
    if (triangle.element.object_id < 0 || vertex_id < 0) {
      return;
    }
    mesh.position_stream_bindings.push_back({
        .object_id = triangle.element.object_id,
        .vertex_id = vertex_id,
        .first_vertex = first_vertex + offset,
        .vertex_count = 1u,
        .stream = OverlayPositionStream::Primary,
        .base_position = {position.x, position.y, position.z, 1.0f},
    });
  };
  append_binding(triangle.element.vertex_id, 0u, triangle.a);
  append_binding(triangle.element.edge_a, 1u, triangle.b);
  append_binding(triangle.element.edge_b, 2u, triangle.c);
}

} // namespace quader
