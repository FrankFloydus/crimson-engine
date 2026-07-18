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

/// Stores one primitive's validated vertices and projection orientation.
struct PrimitiveProjectionCandidate final {
  std::vector<std::uint32_t> vertices;
  QVec3 local_normal;
  bool valid = false;
  bool flat = false;
};

PrimitiveProjectionCandidate primitive_projection_candidate(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices,
    MeshPrimitiveSpan primitive,
    std::vector<std::uint32_t> &vertex_stamps,
    std::uint32_t stamp) {
  PrimitiveProjectionCandidate candidate;
  const std::uint64_t begin = primitive.index_offset;
  const std::uint64_t end = begin + primitive.index_count;
  if (primitive.index_count < 3u || begin >= indices.size() ||
      end > indices.size()) {
    return candidate;
  }

  QVec3 area_normal;
  for (std::uint64_t index = begin; index + 2u < end; index += 3u) {
    const std::uint32_t i0 = indices[static_cast<std::size_t>(index)];
    const std::uint32_t i1 = indices[static_cast<std::size_t>(index + 1u)];
    const std::uint32_t i2 = indices[static_cast<std::size_t>(index + 2u)];
    if (i0 >= vertices.size() || i1 >= vertices.size() ||
        i2 >= vertices.size()) {
      continue;
    }
    area_normal += cross(vertices[i1].position - vertices[i0].position,
                         vertices[i2].position - vertices[i0].position);
    for (const std::uint32_t vertex_index : {i0, i1, i2}) {
      if (vertex_stamps[vertex_index] == stamp) {
        continue;
      }
      vertex_stamps[vertex_index] = stamp;
      candidate.vertices.push_back(vertex_index);
    }
  }

  if (candidate.vertices.size() < 3u ||
      length_squared(area_normal) <= kNormalEpsilonSquared) {
    return candidate;
  }

  candidate.local_normal = normalize(area_normal);
  candidate.valid = true;
  candidate.flat = std::ranges::all_of(
      candidate.vertices, [&](const std::uint32_t vertex_index) {
        const QVec3 vertex_normal = normalize(vertices[vertex_index].normal);
        return length_squared(vertex_normal) > kNormalEpsilonSquared &&
               dot(vertex_normal, candidate.local_normal) >=
                   kFlatNormalAlignment;
      });
  return candidate;
}

} // namespace

std::vector<MeshSurfaceGridVertexProjection>
build_mesh_surface_grid_vertex_projections(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives) {
  std::vector<MeshSurfaceGridVertexProjection> projections;
  projections.reserve(vertices.size());
  for (const MeshVertex &vertex : vertices) {
    const QVec3 normal = normalize(vertex.normal);
    projections.push_back({
        .local_normal = length_squared(normal) > kNormalEpsilonSquared
                            ? normal
                            : QVec3{0.0f, 1.0f, 0.0f},
        .face_projected = false,
    });
  }
  if (vertices.empty() || indices.empty() || primitives.empty()) {
    return projections;
  }

  std::vector<std::uint32_t> primitive_reference_counts(vertices.size(), 0u);
  std::vector<std::uint32_t> vertex_stamps(vertices.size(), 0u);
  std::uint32_t stamp = 1u;
  for (const MeshPrimitiveSpan primitive : primitives) {
    const std::uint64_t begin = primitive.index_offset;
    const std::uint64_t end = begin + primitive.index_count;
    if (primitive.index_count == 0u || begin >= indices.size() ||
        end > indices.size()) {
      continue;
    }
    for (std::uint64_t index = begin; index < end; ++index) {
      const std::uint32_t vertex_index =
          indices[static_cast<std::size_t>(index)];
      if (vertex_index >= vertices.size() ||
          vertex_stamps[vertex_index] == stamp) {
        continue;
      }
      vertex_stamps[vertex_index] = stamp;
      ++primitive_reference_counts[vertex_index];
    }
    if (stamp == std::numeric_limits<std::uint32_t>::max()) {
      std::ranges::fill(vertex_stamps, 0u);
      stamp = 1u;
    } else {
      ++stamp;
    }
  }

  std::ranges::fill(vertex_stamps, 0u);
  stamp = 1u;
  for (const MeshPrimitiveSpan primitive : primitives) {
    PrimitiveProjectionCandidate candidate =
        primitive_projection_candidate(vertices, indices, primitive,
                                       vertex_stamps, stamp);
    if (stamp == std::numeric_limits<std::uint32_t>::max()) {
      std::ranges::fill(vertex_stamps, 0u);
      stamp = 1u;
    } else {
      ++stamp;
    }
    if (!candidate.valid ||
        (candidate.vertices.size() != 3u && !candidate.flat) ||
        !std::ranges::all_of(candidate.vertices,
                            [&](const std::uint32_t vertex_index) {
                              return primitive_reference_counts[vertex_index] ==
                                     1u;
                            })) {
      continue;
    }
    for (const std::uint32_t vertex_index : candidate.vertices) {
      projections[vertex_index] = {
          .local_normal = candidate.local_normal,
          .face_projected = true,
      };
    }
  }
  return projections;
}

} // namespace quader
