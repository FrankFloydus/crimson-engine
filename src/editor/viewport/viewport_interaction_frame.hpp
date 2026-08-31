////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "core/types.hpp"
#include "editor/modeling/picking/curve_picking_cache.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <span>

#include <quader/modeling/neutral/polygon/document_model.hpp>

namespace quader {

class ViewportCameraController;
class SceneSurfaceQuerySnapshot;
struct ViewportRay;

/**
 * Stores a selectable scene billboard projected by the viewport interaction layer.
 */
struct ViewportSceneBillboardPickCandidate {
  EntityId entity = kInvalidEntityId;
  QVec3 position = {};
  float size_pixels = 1.0f;
  bool selectable = true;
  bool effectively_locked = false;
};

/// Carries one depth-ranked stationary mesh vertex into viewport interaction.
struct ViewportMeshVertexSnapTarget {
  int modeling_object_id = -1;
  quader_poly::ElementId vertex_id = quader_poly::kInvalidElementId;
  QVec3 world_position = {};
  double ray_distance = 0.0;
  double pick_distance = 0.0;
};

/// Carries one prepared mesh Move snap query into viewport interaction.
struct ViewportMeshSnapQueryResult {
  std::optional<ViewportMeshVertexSnapTarget> vertex_target;
};

/**
 * Captures camera-independent selection inputs used by transform gizmos.
 * Revisions let the interaction layer retain an expensive world-space pivot
 * while camera-only frames continue to rebuild projected gizmo geometry.
 */
struct ViewportInteractionSelectionState {
  QuaderSelectionSummary summary;
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t pivot_revision = 0;
};

/**
 * Stores the editor-owned viewport interaction input for one viewport frame.
 */
struct ViewportInteractionFrame {
  ViewportInputRouter &input_router;
  ViewportCameraController &camera;
  const ViewportSettings &settings;
  const ViewportInputFrame &input;
  const ViewportInputResult &input_result;
  QVec2 left_click_local = {};
  int left_click_count = 1;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  QVec2 right_click_local = {};
  int right_click_count = 1;
  bool right_pressed = false;
  bool right_down = false;
  bool right_released = false;
  float pointer_drag_threshold_pixels = 4.0f;
  /// True when native pointer capture recenters the cursor this frame.
  bool pointer_capture_recentered = false;
  double double_click_interval_seconds = 0.5;
  double monotonic_time_seconds = 0.0;
  bool active_tool_allows_selection_fallback = false;
  std::span<const ViewportSceneBillboardPickCandidate>
      scene_billboard_pick_candidates;
  std::span<const ViewportSceneCurvePickCandidate> scene_curve_pick_candidates;
  /// One frozen render-visible surface index shared by every Curve hit in the
  /// frame. The host owns its lifetime until frame dispatch returns.
  const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
  /// The modeling owner retains mesh picking, occlusion depth, and
  /// moving-selection exclusion in one prepared query.
  std::function<ViewportMeshSnapQueryResult(const ViewportRay &,
                                            double vertex_radius,
                                            std::optional<double>
                                                maximum_ray_distance)>
      scene_mesh_snap_query;
  ViewportInteractionSelectionState selection_state;
};

/**
 * Stores pre-tool viewport interaction consumption state.
 */
struct ViewportBeforeToolInteractionResult {
  /// A resolved earlier single click can change the selection before gizmo work.
  std::optional<ViewportInteractionSelectionState> selection_after_pending_pick;
  bool gizmo_consumed = false;
  /// Distinguishes a texture transfer's pointer claim from a transform gizmo.
  bool transfer_face_texture_consumed = false;
  /// Only an eligible semantic action may dispatch; cancelled releases cannot.
  bool transfer_face_texture_requested = false;
  /// Exact initiating press or matching release position for the transfer.
  QVec2 transfer_face_texture_local = {};
  bool selection_click_requested = false;
  QVec2 selection_click_local = {};
  int selection_click_count = 1;
  bool selection_cycle_depth = false;
  ViewportModifiers selection_cycle_depth_modifiers = {};
};

} // namespace quader
