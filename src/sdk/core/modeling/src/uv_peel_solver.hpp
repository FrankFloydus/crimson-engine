////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/result.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace quader::modeling::detail {

/// Maximum nodes in one independently solved, already cut UV chart.
inline constexpr std::size_t kUvPeelMaxChartVertices = 16384U;

/// Maximum canonical triangles in one independently solved UV chart.
inline constexpr std::size_t kUvPeelMaxChartTriangles = 32768U;

/// Physical chart position, before the solver's internal normalization.
struct PeelPoint3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

/// Texture chart coordinate, in the same distance units as its positions.
struct PeelPoint2 {
  double u = 0.0;
  double v = 0.0;
};

/// Canonical oriented triangle using chart-local, potentially split vertices.
struct PeelTriangle {
  std::array<std::size_t, 3> vertices{};
};

/// Actual UV samples in the corner order of one canonical chart triangle.
using PeelTriangleCoordinates = std::array<PeelPoint2, 3>;

/// An opened, ordered flow guide; exactly one guide must be the reference.
struct PeelGuide {
  std::vector<std::size_t> vertices;
  bool reference = false;
};

/**
 * Owned numerical input. Chart topology and any transverse UV cuts have already
 * been prepared by the polygon owner; these indices never identify mesh edits.
 */
struct GuidedPeelChart {
  std::vector<PeelPoint3> positions;
  std::vector<PeelTriangle> triangles;
  /// Original authored quad corners in winding order, after any chart-only cut.
  std::vector<std::array<std::size_t, 4>> quads;
  std::vector<PeelGuide> guides;
};

/**
 * Recognized complete quad strips share exact reference-arclength U stations
 * and solve transverse physical-width equations with straight guide constraints.
 * Other charts retain area-weighted conformal least squares. Reference V is zero
 * and other selected guides share a V unknown in either formulation.
 * Normalization, column-scaled LSQR, and validation are private numerical work;
 * no document, selection, transform, or occurrence state is changed.
 */
[[nodiscard]] Result<std::vector<PeelPoint2>>
solve_guided_uv_peel(const GuidedPeelChart &chart);

/**
 * Checks finite, noncollapsed, consistently oriented triangles, chart overlaps,
 * and straight, monotonic flow guides. Either consistent global orientation is
 * accepted. Set guide_direction_is_u=false after rotating the chart's flow to V.
 * The tolerance admits stored float UVs, but is independent of their offset.
 * Returns no error for a valid chart; does not require absolute reference pins.
 * Optional triangle samples check actual stored face corners for folds and
 * overlaps while shared-node representatives retain the guide constraints.
 * A nonzero stored_coordinate_tolerance is the caller's existing span-relative
 * shared-corner budget. It permits only measured edge or vertex contact between
 * triangles sharing chart nodes; the default double-coordinate solve is strict.
 */
[[nodiscard]] std::optional<Error> validate_uv_peel_chart(
    const GuidedPeelChart &chart, std::span<const PeelPoint2> coordinates,
    bool guide_direction_is_u = true,
    std::span<const PeelTriangleCoordinates> triangle_coordinates = {},
    double stored_coordinate_tolerance = 0.0);

} // namespace quader::modeling::detail
