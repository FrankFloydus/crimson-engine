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

/// Derived, face-constant coefficients and one corner's bilinear coordinates.
/// Only coordinates are interpolated; the remaining fields agree across a face.
struct MeshQuadUvInterpolation final {
  QVec3 coordinates{};
  QVec2 geometry_warp{};
  QVec2 uv_correction{};
  bool enabled = false;
};

/// Retains exclusive authored quad corner bindings independently of coordinates.
struct MeshQuadUvInterpolationPlan final {
  std::size_t vertex_count = 0u;
  std::vector<std::array<std::uint32_t, 4u>> corners;
};

[[nodiscard]] MeshQuadUvInterpolationPlan prepare_mesh_quad_uv_interpolation(
    std::size_t vertex_count, std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives);

[[nodiscard]] std::vector<MeshQuadUvInterpolation> build_mesh_quad_uv_interpolations(
    std::span<const MeshVertex> vertices, const MeshQuadUvInterpolationPlan &plan,
    std::span<const QVec3> positions = {});

/// Builds optional inverse-bilinear UV correction from original, uncoalesced
/// compiler face spans. Nonzero distinct source vertex IDs and exclusive corner
/// indices distinguish authored faces from anonymous triangle-mesh primitives.
/// Only well-conditioned planar convex quads with convex, non-affine authored
/// UVs qualify. Unsupported faces retain their exact existing triangle mapping.
/// Optional positions are the actual uploaded positions of a dynamic preview.
[[nodiscard]] std::vector<MeshQuadUvInterpolation>
build_mesh_quad_uv_interpolations(
    std::span<const MeshVertex> vertices,
    std::span<const std::uint32_t> indices,
    std::span<const MeshPrimitiveSpan> primitives,
    std::span<const QVec3> positions = {});

/// Numerical counterpart of resources/materials/mesh_quad_uv.glsl. Accepts the
/// existing perspective-correct triangle UV and interpolated coordinate payload;
/// affine occurrence corrections already included in triangle_uv stay intact.
[[nodiscard]] QVec2 evaluate_mesh_quad_uv_interpolation(
    QVec2 triangle_uv,
    const MeshQuadUvInterpolation &interpolated) noexcept;

} // namespace quader
