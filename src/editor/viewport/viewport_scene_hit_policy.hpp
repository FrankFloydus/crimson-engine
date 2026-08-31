////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/placement/scene_surface_query.hpp"
#include "editor/modeling/picking/curve_picking_cache.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/interactions/viewport_projection.hpp"
#include "viewport/curve_tool_preview.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace quader {

struct ViewportSceneBillboardPickCandidate;

/**
 * Freezes the projection facts needed by marquee arbitration. Viewport input
 * and public API callers therefore use the same projection and occlusion
 * implementation without constructing a live camera controller.
 */
class ViewportSceneProjectionCamera final {
public:
  ViewportSceneProjectionCamera(const ViewportCameraController &camera);
  ViewportSceneProjectionCamera(const ModelingViewportCamera &camera);

  [[nodiscard]] ViewportRay ray_from_viewport_point(QVec2 local_mouse) const;
  [[nodiscard]] ViewportScreenPoint project_world_point(QVec3 world) const;
  [[nodiscard]] std::optional<ViewportScreenSegment>
  project_visible_world_segment(QVec3 start, QVec3 end,
                                bool clip_to_viewport = true) const;
  [[nodiscard]] QVec2 viewport_size() const noexcept {
    return runtime_camera_.viewport_size;
  }
  [[nodiscard]] bool orthographic() const noexcept {
    return runtime_camera_.projection ==
           quader_editor_core::CameraProjection::Orthographic;
  }
  [[nodiscard]] double near_plane() const noexcept { return near_plane_; }
  [[nodiscard]] double far_plane() const noexcept { return far_plane_; }
  [[nodiscard]] const quader_editor_core::ViewportCameraState &
  runtime_camera() const noexcept {
    return runtime_camera_;
  }

private:
  quader_editor_core::ViewportCameraState runtime_camera_;
  double near_plane_ = 0.01;
  double far_plane_ = 1000000.0;
};

/// Identifies the authored provider behind one viewport scene hit.
enum class ViewportSceneHitKind : std::uint8_t {
  CurveControl,
  ModelingComponent,
  Billboard,
  CurveEntity,
  ModelingObject,
  MeshSurface,
  FormalInstance,
};

/** Identifies the scene-surface participant and primitive selected by a mesh hit. */
struct ViewportSceneMeshHit final {
  SceneSurfaceParticipantId participant;
  std::size_t primitive_index = 0u;
};

struct ViewportSceneCurveEntityHit final {};

/** Identifies the authored curve control selected by viewport arbitration. */
struct ViewportSceneCurveControlHit final {
  CurveControlTarget control;
};

struct ViewportSceneBillboardHit final {};

/** Pairs a provider-neutral selection hit with its source category. */
struct ViewportResolvedSelectionHit final {
  ResolvedSelectionTarget target;
  std::array<std::uint64_t, 4> stable_key{};
};

using ViewportSceneOccurrenceExclusion = SceneSurfaceOccurrenceExclusion;

using ViewportSceneHitSource =
    std::variant<ViewportSceneCurveControlHit, ViewportSceneBillboardHit,
                 ViewportSceneCurveEntityHit, ViewportSceneMeshHit,
                 ViewportResolvedSelectionHit>;

/**
 * Carries one provider-neutral viewport hit. Canonical ray-depth buckets win
 * before semantic priority, pointer-distance buckets, and stable identity.
 */
struct ViewportSceneHitCandidate final {
  ViewportSceneHitKind kind = ViewportSceneHitKind::MeshSurface;
  EntityId entity = kInvalidEntityId;
  QVec3 world_position{};
  double ray_distance = 0.0;
  double pick_distance_pixels = 0.0;
  ViewportSceneHitSource source = ViewportSceneMeshHit{};
};

/// Inputs for one frozen cross-provider point pick.
struct ViewportScenePointHitQuery final {
  const ViewportCameraController &camera;
  QVec2 local_mouse{};
  std::span<const ViewportSceneBillboardPickCandidate> billboards;
  std::span<const ViewportSceneCurvePickCandidate> curves;
  const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
  const CurveToolPreviewState *curve_controls = nullptr;
  std::span<const ResolvedSelectionHit> resolved_selection_hits;
  // Both exclusion spans must be sorted, unique, and remain valid for the
  // query. They remove providers before render-surface occlusion and canonical
  // hit arbitration, so a transiently suppressed front object cannot mask a
  // selectable object behind it.
  std::span<const EntityId> excluded_entities;
  std::span<const ViewportSceneOccurrenceExclusion> excluded_occurrences;
  bool include_mesh_surfaces = true;
  bool select_backfaces = true;
  bool include_curve_controls = false;
  bool anchors_only = false;
  /// Depth cycling traverses selectable providers behind the ordinary visible
  /// surface while still excluding locked/non-selectable targets.
  bool include_occluded = false;
  bool collect_all_mesh_surfaces = false;
  double curve_radius_pixels = 8.0;
  double curve_control_radius_pixels = 9.0;
  /// Reuses traversal storage across repeated pointer queries when supplied.
  SceneSurfaceQueryScratch *surface_scratch = nullptr;
};

/// Camera-independent inputs for public/API ray picking through the same hit
/// candidate and comparator policy used by the viewport.
struct ViewportSceneRayHitQuery final {
  ModelingRay ray;
  const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
  std::span<const ResolvedSelectionHit> resolved_selection_hits;
  bool include_mesh_surfaces = true;
  bool select_backfaces = true;
  bool include_occluded = false;
  bool collect_all_mesh_surfaces = false;
  SceneSurfaceQueryScratch *surface_scratch = nullptr;
};

/// Inputs for provider-neutral scene candidates inside one marquee rectangle.
struct ViewportSceneMarqueeHitQuery final {
  ViewportSceneProjectionCamera camera;
  QVec2 start{};
  QVec2 end{};
  ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::DragDirection;
  std::span<const ViewportSceneBillboardPickCandidate> billboards;
  std::span<const ViewportSceneCurvePickCandidate> curves;
  const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
  const CurveToolPreviewState *curve_controls = nullptr;
  std::span<const ResolvedSelectionHit> resolved_selection_hits;
  bool include_curve_controls = false;
  /// Backface marquee selection keeps owner-resolved modeling hits even when a
  /// render-visible mesh surface is nearer. Curves and billboards stay culled.
  bool include_occluded_resolved_modeling_hits = false;
  /// Shared by every occlusion ray emitted for this marquee projection.
  SceneSurfaceQueryScratch *surface_scratch = nullptr;
};

/** Adapts an owner-resolved modeling hit into the shared viewport policy. */
[[nodiscard]] ViewportSceneHitCandidate
viewport_scene_hit_candidate(const ResolvedSelectionHit &hit);

/** Converts a winning provider hit into the canonical selection identity. */
[[nodiscard]] std::optional<ResolvedSelectionTarget>
resolved_selection_target(const ViewportSceneHitCandidate &candidate);

[[nodiscard]] bool viewport_scene_hit_candidate_valid(
    const ViewportSceneHitCandidate &candidate) noexcept;

/** Strict canonical depth, semantic, pointer, and stable-ID comparator. */
[[nodiscard]] bool viewport_scene_hit_precedes(
    const ViewportSceneHitCandidate &candidate,
    const ViewportSceneHitCandidate &best) noexcept;

[[nodiscard]] std::optional<ViewportSceneHitCandidate>
resolve_viewport_scene_hit(
    std::span<const ViewportSceneHitCandidate> candidates);

/**
 * Collects billboard, Curve entity/control, mesh, and formal-occurrence hits
 * against one frozen render-visible surface snapshot.
 */
[[nodiscard]] std::vector<ViewportSceneHitCandidate>
collect_viewport_scene_point_hits(const ViewportScenePointHitQuery &query);

[[nodiscard]] std::optional<ViewportSceneHitCandidate>
resolve_viewport_scene_point_hit(const ViewportScenePointHitQuery &query);

[[nodiscard]] std::vector<ViewportSceneHitCandidate>
collect_viewport_scene_ray_hits(const ViewportSceneRayHitQuery &query);

[[nodiscard]] std::optional<ViewportSceneHitCandidate>
resolve_viewport_scene_ray_hit(const ViewportSceneRayHitQuery &query);

/** Collects every provider using one box rule and render-surface occlusion. */
[[nodiscard]] std::vector<ViewportSceneHitCandidate>
collect_viewport_scene_marquee_hits(
    const ViewportSceneMarqueeHitQuery &query);

/** Sorts provider hits by the canonical policy and removes duplicate targets. */
[[nodiscard]] std::vector<ViewportSceneHitCandidate>
resolve_viewport_scene_marquee_hits(
    std::span<const ViewportSceneHitCandidate> candidates);

} // namespace quader
