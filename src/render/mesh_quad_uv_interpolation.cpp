////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_quad_uv_interpolation.hpp"

#include <quader/modeling/neutral/geometry/quad_uv_interpolation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace quader {
namespace {

bool same_sampling_chart(const MeshVertex &left,
                         const MeshVertex &right) noexcept {
  return left.uv_carrier == right.uv_carrier &&
         left.uv_geometric_normal.x == right.uv_geometric_normal.x &&
         left.uv_geometric_normal.y == right.uv_geometric_normal.y &&
         left.uv_geometric_normal.z == right.uv_geometric_normal.z;
}

bool quad_boundary(std::span<const std::uint32_t> triangles,
                   std::array<std::uint32_t, 4> &corners) noexcept {
  if (triangles.size() != 6u) {
    return false;
  }
  for (std::size_t triangle = 0u; triangle < 2u; ++triangle) {
    const std::size_t begin = triangle * 3u;
    if (triangles[begin] == triangles[begin + 1u] ||
        triangles[begin] == triangles[begin + 2u] ||
        triangles[begin + 1u] == triangles[begin + 2u]) {
      return false;
    }
  }
  std::size_t unique_first = 3u;
  std::size_t unique_second = 3u;
  std::size_t shared_count = 0u;
  for (std::size_t index = 0u; index < 3u; ++index) {
    const bool shared = std::ranges::find(triangles.subspan(3u),
                                          triangles[index]) != triangles.end();
    if (shared) {
      ++shared_count;
    } else {
      unique_first = index;
    }
    if (std::ranges::find(triangles.first(3u), triangles[3u + index]) ==
        triangles.begin() + 3u) {
      unique_second = index;
    }
  }
  if (shared_count != 2u || unique_first == 3u || unique_second == 3u) {
    return false;
  }
  corners = {triangles[unique_first],
             triangles[(unique_first + 1u) % 3u],
             triangles[3u + unique_second],
             triangles[(unique_first + 2u) % 3u]};
  // Both triangles must traverse the internal diagonal in opposite directions.
  return triangles[3u + (unique_second + 1u) % 3u] == corners[3] &&
         triangles[3u + (unique_second + 2u) % 3u] == corners[1];
}

std::optional<quader_geometry::QuadUvInterpolation>
build_quad(std::span<const MeshVertex> vertices,
           std::span<const QVec3> positions,
           const std::array<std::uint32_t, 4> &corners) noexcept {
  std::array<quader_geometry::QVec3f, 4> points{};
  std::array<quader_geometry::QVec2f, 4> uvs{};
  for (std::size_t index = 0u; index < 4u; ++index) {
    const MeshVertex &vertex = vertices[corners[index]];
    if (vertex.source_vertex_id == 0u || !std::isfinite(vertex.uv.x) ||
        !std::isfinite(vertex.uv.y) ||
        (!std::isfinite(vertex.uv_geometric_normal.x) ||
         !std::isfinite(vertex.uv_geometric_normal.y) ||
         !std::isfinite(vertex.uv_geometric_normal.z)) ||
        !std::ranges::all_of(vertex.uv_carrier,
                            [](float value) { return std::isfinite(value); }) ||
        !same_sampling_chart(vertex, vertices[corners[0]])) {
      return std::nullopt;
    }
    for (std::size_t previous = 0u; previous < index; ++previous) {
      if (vertex.source_vertex_id ==
          vertices[corners[previous]].source_vertex_id) {
        return std::nullopt;
      }
    }
    points[index] = quader_geometry::QVec3f(
        positions.empty() ? vertex.position : positions[corners[index]]);
    uvs[index] = quader_geometry::QVec2f(vertex.uv);
  }

  return quader_geometry::build_quad_uv_interpolation(points, uvs);
}

} // namespace

std::vector<MeshQuadUvInterpolation> build_mesh_quad_uv_interpolations(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives,
    std::span<const QVec3> positions) {
  std::vector<MeshQuadUvInterpolation> result(vertices.size());
  if (vertices.empty() || indices.empty() || primitives.empty() ||
      (!positions.empty() && positions.size() != vertices.size())) {
    return result;
  }
  // A compiled corner belongs to exactly one authored face. Never infer quad
  // constants on shared importer vertices or after material-span coalescing.
  std::vector<std::uint32_t> references(vertices.size(), 0u);
  std::vector<std::size_t> last_primitive(vertices.size(),
                                       std::numeric_limits<std::size_t>::max());
  for (std::size_t primitive_index = 0u; primitive_index < primitives.size();
       ++primitive_index) {
    const MeshPrimitiveSpan primitive = primitives[primitive_index];
    const std::uint64_t end =
        static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count;
    if (end > indices.size()) {
      return result;
    }
    for (std::uint64_t index = primitive.index_offset; index < end; ++index) {
      const std::uint32_t vertex_index = indices[static_cast<std::size_t>(index)];
      if (vertex_index >= vertices.size()) {
        return result;
      }
      if (last_primitive[vertex_index] != primitive_index) {
        last_primitive[vertex_index] = primitive_index;
        ++references[vertex_index];
      }
    }
  }
  for (const MeshPrimitiveSpan primitive : primitives) {
    if (primitive.index_count != 6u) {
      continue;
    }
    std::array<std::uint32_t, 4> corners{};
    if (!quad_boundary(indices.subspan(primitive.index_offset, 6u), corners) ||
        !std::ranges::all_of(corners, [&](std::uint32_t index) {
          return references[index] == 1u;
        })) {
      continue;
    }
    const auto quad = build_quad(vertices, positions, corners);
    if (!quad.has_value()) {
      continue;
    }
    for (std::size_t index = 0u; index < 4u; ++index) {
      result[corners[index]] = {
          .coordinates = {quad->coordinates[index].x,
                          quad->coordinates[index].y,
                          quad->coordinates[index].z},
          .geometry_warp = {quad->geometry_warp.x, quad->geometry_warp.y},
          .uv_correction = {quad->uv_correction.x, quad->uv_correction.y},
          .enabled = true};
    }
  }
  return result;
}

QVec2 evaluate_mesh_quad_uv_interpolation(
    QVec2 triangle_uv,
    const MeshQuadUvInterpolation &interpolated) noexcept {
  if (!interpolated.enabled) {
    return triangle_uv;
  }
  const auto result = quader_geometry::evaluate_quad_uv_interpolation(
      quader_geometry::QVec2f(triangle_uv),
      quader_geometry::QVec3f(interpolated.coordinates),
      quader_geometry::QVec2f(interpolated.geometry_warp),
      quader_geometry::QVec2f(interpolated.uv_correction));
  return {result.x, result.y};
}

} // namespace quader
