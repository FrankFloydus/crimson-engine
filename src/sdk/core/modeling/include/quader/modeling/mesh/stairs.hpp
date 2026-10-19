////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/mesh/polygon_document.hpp>
#include <quader/modeling/result.hpp>

#include <cstdint>
#include <vector>

namespace quader::modeling {

inline constexpr double kStairsMinimumDimension = 0.0001;
inline constexpr std::uint32_t kStairsMinimumFlightCount = 2U;
inline constexpr std::uint32_t kStairsMaximumFlightCount = 16U;
inline constexpr std::uint32_t kStairsMinimumStepCount = 1U;
inline constexpr std::uint32_t kStairsMaximumStepCount = 1024U;
inline constexpr std::uint32_t kStairsMinimumCurvedSegments = 3U;
inline constexpr std::uint32_t kStairsMaximumCurvedSegments = 256U;
inline constexpr double kStairsMaximumTurns = 8.0;

enum class StairsLayout { Straight, L, U, Switchback, Arc, Spiral };
enum class StairsSurface { Steps, Ramp };
enum class StairsConstruction { Solid, Slab };
enum class StairsSwitchbackArrangement { Spread, Stacked };

/** Creates a preset in local X/Z, with starting walking elevation zero. */
struct StairsSpec final {
  StairsLayout layout = StairsLayout::Straight;
  StairsSurface surface = StairsSurface::Steps;
  StairsConstruction construction = StairsConstruction::Solid;
  StairsSwitchbackArrangement switchback_arrangement =
      StairsSwitchbackArrangement::Spread;
  double footprint_width = 2.0;
  double footprint_depth = 4.0;
  double height = 2.0;
  double flight_width = 1.0;
  double landing_depth = 1.0;
  double gap = 0.25;
  double inner_radius = 1.0;
  double sweep_degrees = 90.0;
  double turns = 1.0;
  double slab_thickness = 0.2;
  double preferred_riser = 0.2;
  std::uint32_t flight_count = 3U;
  std::uint32_t step_count = 10U;
  std::uint32_t curved_segments = 32U;
  bool automatic_step_count = true;
  bool clockwise = false;
};

/** Owns one editable mesh and the solved dimensions of its rising flights. */
struct StairsMesh final {
  PolygonDocument document;
  std::uint32_t effective_step_count = 0U;
  std::vector<std::uint32_t> flight_step_counts;
  std::vector<double> flight_tread_depths;
  double riser_height = 0.0;
  /// Minimum walking-line tread depth, or zero for ramps.
  double tread_depth = 0.0;
  /// Steepest flight's continuous grade in degrees.
  double slope_degrees = 0.0;
  double footprint_width = 0.0;
  double footprint_depth = 0.0;
  /// Generated treads, step risers, ramp decks and landing tops; excludes body caps.
  std::vector<FaceId> walking_faces;
};

/**
 * Generates one closed boundary mesh without overlapping component solids.
 *
 * X/Z are centered on the preset footprint. Straight uses the drawn width;
 * L uses drawn width/depth and its independent flight width. U/stacked widths
 * are twice flight_width plus gap; spread widths contain all lanes and gaps.
 * Circular presets use inner_radius and flight_width, with a full-diameter
 * placement footprint. Slabs extend slab_thickness below walking elevation.
 * Solid fills to Y=0 and rejects repeated footprints. Slabs reject body contact
 * between nonadjacent route sections. This function retains no generator state.
 */
[[nodiscard]] Result<StairsMesh> build_stairs_mesh(const StairsSpec &spec);

/**
 * Detaches the connected walking strip from the body within one document.
 * Preserves all faces, face IDs, corner order, positions and attributes; only
 * shared boundary vertices receive duplicates through the normal Detach Faces
 * operation. Adds no faces or caps and leaves the input unchanged. Requires the
 * generated walking-face provenance and retains the detached face selection.
 */
[[nodiscard]] Result<PolygonDocument> detach_stairs_walking_surface(const StairsMesh &mesh);

} // namespace quader::modeling
