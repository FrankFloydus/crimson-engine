////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"

#include <cstdint>
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
    std::span<const MeshPrimitiveSpan> primitives);

} // namespace quader
