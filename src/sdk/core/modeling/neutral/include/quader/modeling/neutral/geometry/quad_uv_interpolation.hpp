////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <array>
#include <optional>

namespace quader_geometry {

/// Derived inverse-bilinear coefficients. Coordinates retain input corner order;
/// interpolate them with the same weights as the ordinary triangle UV values.
struct QuadUvInterpolation final {
  std::array<QVec3f, 4> coordinates{};
  QVec2f geometry_warp{};
  QVec2f uv_correction{};
};

// Only well-conditioned planar convex quads with convex, nonaffine UVs qualify.
// Other charts retain their existing triangle interpolation. Callers own
// topology/identity eligibility and supply the coordinates actually rendered.
[[nodiscard]] std::optional<QuadUvInterpolation> build_quad_uv_interpolation(
    std::array<QVec3f, 4> positions,
    std::array<QVec2f, 4> uvs) noexcept;

// Numerical counterpart of resources/materials/mesh_quad_uv.glsl. Additional
// affine placement corrections already in triangle_uv stay intact.
[[nodiscard]] QVec2f evaluate_quad_uv_interpolation(
    QVec2f triangle_uv, QVec3f coordinate,
    QVec2f geometry_warp, QVec2f uv_correction) noexcept;

} // namespace quader_geometry
