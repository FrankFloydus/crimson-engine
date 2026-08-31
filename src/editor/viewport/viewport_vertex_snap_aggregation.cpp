////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_vertex_snap_aggregation.hpp"

#include "editor/viewport/viewport_surface_occlusion_policy.hpp"

#include <cmath>

namespace quader {
namespace {

constexpr double kVertexSnapTieEpsilon = 1.0e-8;

bool valid_metrics(QVec3 position, double ray_distance,
                   double pick_distance,
                   std::optional<double> render_surface_ray_distance) {
  if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
      !std::isfinite(position.z) || !std::isfinite(ray_distance) ||
      !std::isfinite(pick_distance) || ray_distance < 0.0 ||
      pick_distance < 0.0) {
    return false;
  }
  return !render_surface_ray_distance.has_value() ||
         (std::isfinite(*render_surface_ray_distance) &&
          *render_surface_ray_distance >= 0.0 &&
          ray_distance <=
              *render_surface_ray_distance +
                  kViewportSurfaceOcclusionTolerance);
}

bool curve_precedes_mesh(const ViewportCurveAnchorSnapTarget &curve,
                         const ViewportMeshVertexSnapTarget &mesh) {
  if (std::abs(curve.ray_distance - mesh.ray_distance) >
      kVertexSnapTieEpsilon) {
    return curve.ray_distance < mesh.ray_distance;
  }
  if (std::abs(curve.pick_distance - mesh.pick_distance) >
      kVertexSnapTieEpsilon) {
    return curve.pick_distance < mesh.pick_distance;
  }
  return false;
}

} // namespace

std::optional<ViewportVertexSnapResolution>
aggregate_viewport_vertex_snap_targets(
    const std::optional<ViewportMeshVertexSnapTarget> &mesh,
    const std::optional<ViewportCurveAnchorSnapTarget> &curve,
    std::optional<double> render_surface_ray_distance) {
  const bool mesh_valid =
      mesh.has_value() &&
      valid_metrics(mesh->world_position, mesh->ray_distance,
                    mesh->pick_distance, render_surface_ray_distance);
  const bool curve_valid =
      curve.has_value() &&
      valid_metrics(curve->world_position, curve->ray_distance,
                    curve->pick_distance, render_surface_ray_distance);
  if (!mesh_valid && !curve_valid) {
    return std::nullopt;
  }
  if (curve_valid &&
      (!mesh_valid || curve_precedes_mesh(*curve, *mesh))) {
    return ViewportVertexSnapResolution{
        .domain = ViewportVertexSnapDomain::CurveAnchor,
        .source = *curve,
        .world_position = curve->world_position,
        .ray_distance = curve->ray_distance,
        .pick_distance = curve->pick_distance,
    };
  }
  return ViewportVertexSnapResolution{
      .domain = ViewportVertexSnapDomain::Mesh,
      .source = *mesh,
      .world_position = mesh->world_position,
      .ray_distance = mesh->ray_distance,
      .pick_distance = mesh->pick_distance,
  };
}

} // namespace quader
