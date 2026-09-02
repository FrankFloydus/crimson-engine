////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/picking/curve_picking_cache.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "viewport/curve_tool_preview.hpp"

#include <cstddef>
#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace quader {

class ViewportCameraController;
class SceneSurfaceQuerySnapshot;
struct ViewportRay;

/**
 * Adapts the canonical viewport Curve picker to the native-tool input port.
 */
class ViewportCurveControlPickingAdapter final
    : public ViewportCurveControlPickingPort {
public:
  ViewportCurveControlPickingAdapter(
      const ViewportCameraController &camera,
      const SceneSurfaceQuerySnapshot *scene_surfaces)
      : camera_(&camera), scene_surfaces_(scene_surfaces) {}

  [[nodiscard]] std::optional<CurveControlTarget>
  pick(const CurveToolPreviewState &preview, QVec2 mouse_local,
       bool anchors_only) const override;

private:
  const ViewportCameraController *camera_ = nullptr;
  const SceneSurfaceQuerySnapshot *scene_surfaces_ = nullptr;
};

/// Reports one authored Curve control and its screen/ray-space pick metrics.
struct ViewportCurveControlPickHit {
  CurveControlTarget target;
  QVec3 world_position;
  double ray_distance = 0.0;
  double distance_pixels = 0.0;
};

/**
 * Picks one visible authored Curve control endpoint. Screen distance wins,
 * followed by ray depth and stable target identity. An optional mesh-surface
 * depth excludes controls hidden behind foreground geometry.
 */
[[nodiscard]] std::optional<ViewportCurveControlPickHit>
pick_viewport_curve_control(
    const ViewportCameraController &camera,
    const CurveToolPreviewState &preview, QVec2 local_mouse,
    bool anchors_only = false, double radius_pixels = 9.0,
    std::optional<double> maximum_ray_distance = std::nullopt);

/** Uses the session's render-visible surface index for Curve occlusion. */
[[nodiscard]] std::optional<ViewportCurveControlPickHit>
pick_unoccluded_viewport_curve_control(
    const ViewportCameraController &camera,
    const CurveToolPreviewState &preview, QVec2 local_mouse,
    const SceneSurfaceQuerySnapshot &scene_surfaces,
    bool anchors_only = false, double radius_pixels = 9.0);

/**
 * Returns visible authored Curve control endpoints inside the normalized
 * selection rectangle. Tangent lines are presentation only and never hits.
 */
[[nodiscard]] std::vector<CurveControlTarget>
box_pick_viewport_curve_controls(const ViewportCameraController &camera,
                                 const CurveToolPreviewState &preview,
                                 QVec2 start, QVec2 end);

/// Reports one whole-curve hit without changing editor selection or geometry.
struct ViewportCurvePickHit {
  EntityId entity = kInvalidEntityId;
  QVec3 world_position;
  double ray_distance = 0.0;
  double distance_pixels = 0.0;
};

/// Freezes one authored world-space anchor inside its owning Curve group.
struct ViewportCurveAnchorSnapPointCandidate {
  modeling::CurvePointId point{};
  QVec3 world_position;
};

/** Distinguishes persisted scene curves from anchors in the active draft. */
enum class ViewportCurveAnchorSnapGroupKind : std::uint8_t {
  SceneEntity,
  ActiveDraft,
};

/// Identifies a scene Curve or the single active uncommitted Curve draft.
struct ViewportCurveAnchorSnapGroupIdentity {
  ViewportCurveAnchorSnapGroupKind kind =
      ViewportCurveAnchorSnapGroupKind::SceneEntity;
  EntityId scene_entity = kInvalidEntityId;

  [[nodiscard]] static constexpr ViewportCurveAnchorSnapGroupIdentity
  scene(EntityId entity) {
    return {ViewportCurveAnchorSnapGroupKind::SceneEntity, entity};
  }
  [[nodiscard]] static constexpr ViewportCurveAnchorSnapGroupIdentity
  active_draft() {
    return {ViewportCurveAnchorSnapGroupKind::ActiveDraft, kInvalidEntityId};
  }
  [[nodiscard]] constexpr bool valid() const {
    return kind == ViewportCurveAnchorSnapGroupKind::ActiveDraft ||
           scene_entity != kInvalidEntityId;
  }
  [[nodiscard]] auto operator<=>(
      const ViewportCurveAnchorSnapGroupIdentity &) const = default;
};

/// Freezes one Curve's anchors and bounds for allocation-free pointer queries.
struct ViewportCurveAnchorSnapCandidate {
  ViewportCurveAnchorSnapGroupIdentity group;
  QVec3 world_bounds_min;
  QVec3 world_bounds_max;
  std::vector<ViewportCurveAnchorSnapPointCandidate> anchors;
};

/// Reports one eligible authored anchor and its relation to the current ray.
struct ViewportCurveAnchorSnapTarget {
  ViewportCurveAnchorSnapGroupIdentity group;
  /// Convenience projection; invalid for ActiveDraft targets.
  EntityId entity = kInvalidEntityId;
  modeling::CurvePointId point{};
  QVec3 world_position;
  double ray_distance = 0.0;
  double pick_distance = 0.0;
};

/** Counts bounded candidate work performed by one curve-anchor snap query. */
struct ViewportCurveAnchorSnapWorkCounters {
  std::size_t entity_bounds_tested = 0u;
  std::size_t anchors_tested = 0u;
};

using ViewportCurveAnchorSnapExclusion =
    std::function<bool(EntityId, modeling::CurvePointId)>;

/**
 * Expands pinned Curve documents once so pointer updates do no asset work.
 * Results are grouped by entity for allocation-free per-Curve hit resolution.
 */
[[nodiscard]] std::vector<ViewportCurveAnchorSnapCandidate>
collect_viewport_curve_anchor_snap_candidates(
    std::span<const ViewportSceneCurvePickCandidate> candidates,
    const ViewportCurveAnchorSnapExclusion &is_excluded = {});

using ViewportCurveDraftAnchorSnapExclusion =
    std::function<bool(modeling::CurvePointId)>;

/** Freezes the active uncommitted draft without inventing a scene entity. */
[[nodiscard]] std::optional<ViewportCurveAnchorSnapCandidate>
collect_viewport_curve_draft_anchor_snap_candidate(
    const CurveToolPreviewState &preview,
    const ViewportCurveDraftAnchorSnapExclusion &is_excluded = {});

/**
 * Resolves authored anchors inside a world-space radius of a normalized ray.
 * Within each Curve entity, pick distance wins before ray depth. Entity winners
 * are then ranked by ray depth, pick distance and stable identity, matching the
 * mesh vertex picker. Handles are never considered. An optional mesh-surface
 * depth culls occluded anchors using the mesh component tolerance.
 */
[[nodiscard]] std::optional<ViewportCurveAnchorSnapTarget>
resolve_viewport_curve_anchor_snap_target(
    const ViewportRay &normalized_world_ray,
    std::span<const ViewportCurveAnchorSnapCandidate> candidates,
    double world_radius,
    const ViewportCurveAnchorSnapExclusion &is_excluded = {},
    std::optional<double> maximum_ray_distance = std::nullopt,
    ViewportCurveAnchorSnapWorkCounters *work_counters = nullptr);

/** Uses clipped SDK samples and perspective-correct depth, never curve bounds alone. */
[[nodiscard]] std::optional<ViewportCurvePickHit> pick_viewport_curve(
    const ViewportCameraController &camera, QVec2 local_mouse,
    std::span<const ViewportSceneCurvePickCandidate> candidates,
    double radius_pixels = 8.0,
    std::optional<double> maximum_ray_distance = std::nullopt,
    bool include_backfaces = true);

/** Picks sampled Curves behind no render-visible mesh surface. */
[[nodiscard]] std::optional<ViewportCurvePickHit>
pick_unoccluded_viewport_curve(
    const ViewportCameraController &camera, QVec2 local_mouse,
    std::span<const ViewportSceneCurvePickCandidate> candidates,
    const SceneSurfaceQuerySnapshot &scene_surfaces,
    double radius_pixels = 8.0);

} // namespace quader
