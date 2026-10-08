////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace quader::modeling::detail {

/** Local position or column of a world-space linear transformation. */
struct StructuredPeelPosition {
  double x{};
  double y{};
  double z{};
};

/** Authored face loop, with vertex indices into the input position span. */
struct StructuredPeelFace {
  std::vector<std::size_t> vertices;
};

/** Immutable authored connectivity owned by one target's preparation request. */
struct StructuredPeelTopology;

/** Validated connectivity index or a construction failure. */
struct StructuredPeelTopologyResult {
  bool success{};
  std::string error_message;
  std::shared_ptr<const StructuredPeelTopology> topology;
};

/** Own and index authored face loops once for all guides of the same target. */
[[nodiscard]] StructuredPeelTopologyResult make_structured_peel_topology(
    std::vector<StructuredPeelFace> faces, std::size_t vertex_count);

/** Desired texture coordinates before the host's storage reconstruction. */
struct StructuredPeelUv {
  double u{};
  double v{};
};

/** Complete coordinates for one affected quad, in authored corner order. */
struct StructuredPeelFaceCoordinates {
  std::size_t face_index{};
  std::array<StructuredPeelUv, 4> corners{};
};

/** Guide-axis selection and the affine placement of the resulting UV chart. */
struct StructuredPeelOptions {
  bool axis_u{true};
  bool world_space{true};
  double u_repeat{1.0};
  double v_repeat{1.0};
  double u_offset{};
  double v_offset{};
};

/** Validated authored topology and one ordered guide; closed guides repeat start. */
struct StructuredPeelInput {
  std::span<const StructuredPeelPosition> positions;
  std::shared_ptr<const StructuredPeelTopology> topology;
  std::span<const std::size_t> guide;
  bool closed{};
  std::array<StructuredPeelPosition, 3> world_basis{
      StructuredPeelPosition{1.0, 0.0, 0.0},
      StructuredPeelPosition{0.0, 1.0, 0.0},
      StructuredPeelPosition{0.0, 0.0, 1.0}};
  StructuredPeelOptions options;
  std::size_t corner_budget_cap{};
};

/** Atomic result, ordered by input face index; an empty quad set permits fallback. */
struct StructuredPeelResult {
  bool success{};
  bool no_quad_incident_sides{};
  std::string error_message;
  std::vector<StructuredPeelFaceCoordinates> faces;
};

/** Integrate quad profiles and resolve corner writes; charge budget only on success. */
[[nodiscard]] StructuredPeelResult compute_structured_uv_peel(
    const StructuredPeelInput &input, std::size_t &remaining_corner_budget);

} // namespace quader::modeling::detail
