////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <cstdint>
#include <array>
#include <span>
#include <vector>

namespace quader {

/**
 * Selects how one compiled vertex participates in the world-space surface grid.
 */
struct MeshSurfaceGridVertexProjection final {
  QVec3 local_normal{0.0f, 1.0f, 0.0f};
  bool face_projected = false;
};

/// Immutable authored-face membership, independent from deforming coordinates.
struct MeshSurfaceGridProjectionPlan final {
  /// Preserves canonical triangle accumulation and first-seen corner order.
  struct Face final {
    std::vector<std::array<std::uint32_t, 3u>> triangles;
    std::vector<std::uint32_t> vertices;
    bool exclusive = false;
  };
  std::size_t vertex_count = 0u;
  std::vector<Face> faces;
};

[[nodiscard]] MeshSurfaceGridProjectionPlan prepare_mesh_surface_grid_projection(
    std::size_t vertex_count, std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives);

[[nodiscard]] std::vector<MeshSurfaceGridVertexProjection>
build_mesh_surface_grid_vertex_projections(
    std::span<const MeshVertex> vertices,
    const MeshSurfaceGridProjectionPlan &plan,
    std::span<const QVec3> positions = {});

/**
 * Builds renderer-owned projection hints from original, uncoalesced face spans.
 *
 * Face-local triangles and flat faces receive one constant area-weighted normal.
 * Smooth multi-vertex faces retain their interpolated-normal fallback so curved
 * surfaces can blend projection planes without hard internal switches.
 */
[[nodiscard]] std::vector<MeshSurfaceGridVertexProjection>
build_mesh_surface_grid_vertex_projections(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives,
    std::span<const QVec3> positions = {});

} // namespace quader
