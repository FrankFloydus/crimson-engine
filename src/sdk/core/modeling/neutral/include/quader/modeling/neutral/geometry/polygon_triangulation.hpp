////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/neutral/geometry/geometry.hpp>

namespace quader_geometry {

/// Triangulates one outer ring and its disjoint interior holes. Returned indices
/// address the rings concatenated in input order; every boundary corner is kept.
/// Invalid, intersecting or nested hole rings return no triangles.
[[nodiscard]] std::vector<QTriangleIndices> triangulate_projected_polygon_rings(
    std::span<const std::vector<QVec2d>> rings, double epsilon = 0.000001,
    double coverage_tolerance_ratio = 0.001);

} // namespace quader_geometry
