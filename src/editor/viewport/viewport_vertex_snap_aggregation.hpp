////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"

#include <optional>
#include <variant>

namespace quader {

/** Identifies the geometry domain that supplied a vertex-snap candidate. */
enum class ViewportVertexSnapDomain : std::uint8_t {
  Mesh,
  CurveAnchor,
};

using ViewportVertexSnapSource =
    std::variant<ViewportMeshVertexSnapTarget,
                 ViewportCurveAnchorSnapTarget>;

/** Carries the canonical winner from independently cached vertex domains. */
struct ViewportVertexSnapResolution final {
  ViewportVertexSnapDomain domain = ViewportVertexSnapDomain::Mesh;
  ViewportVertexSnapSource source = ViewportMeshVertexSnapTarget{};
  QVec3 world_position{};
  double ray_distance = 0.0;
  double pick_distance = 0.0;
};

/**
 * Resolves the already stable mesh and Curve-domain winners by ray depth,
 * perpendicular pick distance, then domain. The optional render-surface depth
 * applies one occlusion policy to both domains before arbitration. Mesh wins
 * an exact cross-domain tie for compatibility with the existing Move tool
 * policy.
 */
[[nodiscard]] std::optional<ViewportVertexSnapResolution>
aggregate_viewport_vertex_snap_targets(
    const std::optional<ViewportMeshVertexSnapTarget> &mesh,
    const std::optional<ViewportCurveAnchorSnapTarget> &curve,
    std::optional<double> render_surface_ray_distance = std::nullopt);

} // namespace quader
