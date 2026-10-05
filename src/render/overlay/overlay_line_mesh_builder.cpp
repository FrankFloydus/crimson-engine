////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_line_mesh_builder.hpp"

#include "core/color_space.hpp"

#include <cstdint>

namespace quader {

OverlayLineMesh OverlayLineMeshBuilder::build(
    const std::vector<const ViewportOverlayLine *> &lines,
    const ViewportOverlayCamera &camera, float width_pixels, float depth_bias) {
  // Width, projection, near clipping and depth bias are shader state. Keeping
  // the CPU mesh camera-independent lets orbit, pan, zoom and fly reuse it.
  (void)camera;
  (void)width_pixels;
  (void)depth_bias;
  OverlayLineMesh mesh;
  mesh.vertices.reserve(lines.size() * 4u);
  mesh.indices.reserve(lines.size() * 6u);
  for (const ViewportOverlayLine *line : lines) {
    append_line_quad(mesh, *line);
  }
  return mesh;
}

void OverlayLineMeshBuilder::append_line_quad(
    OverlayLineMesh &mesh, const ViewportOverlayLine &line) {
  const std::uint32_t linear_color = rgba8_srgb_to_linear(line.color);
  const std::uint32_t offset = static_cast<std::uint32_t>(mesh.vertices.size());
  const OverlayFloat4 origin{0.0f, 0.0f, 0.0f, 1.0f};
  const OverlayFloat4 start{line.start.x, line.start.y, line.start.z, 1.0f};
  const OverlayFloat4 end{line.end.x, line.end.y, line.end.z, 1.0f};
  const float active = line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire
                           ? 1.0f : 0.0f;
  const auto support_position = [&line, start](std::size_t index) {
    const auto &support = line.surface_support[index];
    return support.vertex_id >= 0
               ? OverlayFloat4{support.position.x, support.position.y,
                               support.position.z, 1.0f}
               : start;
  };
  const auto support0 = support_position(0u);
  const auto support1 = support_position(1u);
  mesh.vertices.push_back(
      {origin, start, end, {-1.0f, -1.0f, 0.0f, active}, support0, support1, linear_color});
  mesh.vertices.push_back(
      {origin, start, end, {-1.0f, 1.0f, 1.0f, active}, support0, support1, linear_color});
  mesh.vertices.push_back(
      {origin, start, end, {1.0f, 1.0f, 1.0f, active}, support0, support1, linear_color});
  mesh.vertices.push_back(
      {origin, start, end, {1.0f, -1.0f, 0.0f, active}, support0, support1, linear_color});
  if (line.element.object_id >= 0 && line.element.edge_a >= 0) {
    mesh.position_stream_bindings.push_back({
        .object_id = line.element.object_id,
        .vertex_id = line.element.edge_a,
        .first_vertex = offset,
        .vertex_count = 4u,
        .stream = OverlayPositionStream::Primary,
        .base_position = start,
    });
  }
  if (line.element.object_id >= 0 && line.element.edge_b >= 0) {
    mesh.position_stream_bindings.push_back({
        .object_id = line.element.object_id,
        .vertex_id = line.element.edge_b,
        .first_vertex = offset,
        .vertex_count = 4u,
        .stream = OverlayPositionStream::Secondary,
        .base_position = end,
    });
  }
  if (line.element.object_id >= 0) {
    for (std::size_t index = 0u; index < line.surface_support.size(); ++index) {
      const auto &support = line.surface_support[index];
      const auto vertex_id = support.vertex_id >= 0 ? support.vertex_id : line.element.edge_a;
      if (vertex_id < 0) continue;
      mesh.position_stream_bindings.push_back({
          .object_id = line.element.object_id,
          .vertex_id = vertex_id,
          .first_vertex = offset,
          .vertex_count = 4u,
          .stream = index == 0u ? OverlayPositionStream::Support0 : OverlayPositionStream::Support1,
          .base_position = index == 0u ? support0 : support1,
      });
    }
  }
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 1u);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset);
  mesh.indices.push_back(offset + 2u);
  mesh.indices.push_back(offset + 3u);
}

} // namespace quader
