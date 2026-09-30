////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_surface_grid_projection.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace quader {
namespace {

constexpr float kNormalEpsilonSquared = 0.000000000001f;
constexpr float kFlatNormalAlignment = 0.9999f;

} // namespace

MeshSurfaceGridProjectionPlan prepare_mesh_surface_grid_projection(
    std::size_t vertex_count, std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives) {
  MeshSurfaceGridProjectionPlan plan{.vertex_count = vertex_count};
  std::vector<std::uint32_t> references(vertex_count, 0u);
  std::vector<std::size_t> stamps(vertex_count, std::numeric_limits<std::size_t>::max());
  for (std::size_t face = 0u; face < primitives.size(); ++face) {
    const auto primitive = primitives[face];
    const std::uint64_t end = static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count;
    if (primitive.index_count == 0u || primitive.index_offset >= indices.size() || end > indices.size()) continue;
    for (std::uint64_t index = primitive.index_offset; index < end; ++index) {
      const auto vertex = indices[static_cast<std::size_t>(index)];
      if (vertex < vertex_count && stamps[vertex] != face) {
        stamps[vertex] = face;
        ++references[vertex];
      }
    }
  }
  std::ranges::fill(stamps, std::numeric_limits<std::size_t>::max());
  plan.faces.reserve(primitives.size());
  for (std::size_t face = 0u; face < primitives.size(); ++face) {
    const auto primitive = primitives[face];
    const std::uint64_t end = static_cast<std::uint64_t>(primitive.index_offset) + primitive.index_count;
    if (primitive.index_count < 3u || primitive.index_offset >= indices.size() || end > indices.size()) continue;
    MeshSurfaceGridProjectionPlan::Face binding;
    for (std::uint64_t index = primitive.index_offset; index + 2u < end; index += 3u) {
      const std::array triangle{indices[static_cast<std::size_t>(index)],
          indices[static_cast<std::size_t>(index + 1u)], indices[static_cast<std::size_t>(index + 2u)]};
      if (std::ranges::any_of(triangle, [&](auto vertex) { return vertex >= vertex_count; })) continue;
      binding.triangles.push_back(triangle);
      for (const auto vertex : triangle) {
        if (stamps[vertex] != face) {
          stamps[vertex] = face;
          binding.vertices.push_back(vertex);
        }
      }
    }
    binding.exclusive = std::ranges::all_of(binding.vertices,
        [&](auto vertex) { return references[vertex] == 1u; });
    plan.faces.push_back(std::move(binding));
  }
  return plan;
}

std::vector<MeshSurfaceGridVertexProjection>
build_mesh_surface_grid_vertex_projections(
    std::span<const MeshVertex> vertices, const MeshSurfaceGridProjectionPlan &plan,
    std::span<const QVec3> positions) {
  std::vector<MeshSurfaceGridVertexProjection> projections;
  projections.reserve(vertices.size());
  for (const auto &vertex : vertices) {
    const QVec3 normal = normalize(vertex.normal);
    projections.push_back({.local_normal = length_squared(normal) > kNormalEpsilonSquared
        ? normal : QVec3{0.0f, 1.0f, 0.0f}, .face_projected = false});
  }
  if (plan.vertex_count != vertices.size() ||
      (!positions.empty() && positions.size() != vertices.size())) return projections;
  const auto position = [&](std::uint32_t index) {
    return positions.empty() ? vertices[index].position : positions[index];
  };
  for (const auto &face : plan.faces) {
    if (face.vertices.size() < 3u || !face.exclusive) continue;
    QVec3 area_normal;
    for (const auto triangle : face.triangles) {
      area_normal += cross(position(triangle[1]) - position(triangle[0]),
                           position(triangle[2]) - position(triangle[0]));
    }
    if (length_squared(area_normal) <= kNormalEpsilonSquared) continue;
    const auto normal = normalize(area_normal);
    if (face.vertices.size() != 3u && !std::ranges::all_of(face.vertices, [&](auto index) {
      const auto vertex_normal = normalize(vertices[index].normal);
      return length_squared(vertex_normal) > kNormalEpsilonSquared &&
          dot(vertex_normal, normal) >= kFlatNormalAlignment;
    })) continue;
    for (const auto vertex : face.vertices) projections[vertex] = {normal, true};
  }
  return projections;
}

std::vector<MeshSurfaceGridVertexProjection>
build_mesh_surface_grid_vertex_projections(
    std::span<const MeshVertex> vertices, std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives, std::span<const QVec3> positions) {
  return build_mesh_surface_grid_vertex_projections(vertices,
      prepare_mesh_surface_grid_projection(vertices.size(), indices, primitives), positions);
}

} // namespace quader
