////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "viewport/curve_tool_preview.hpp"
#include "viewport/viewport_input_types.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace quader {

class AssetRegistry;
enum class CurveCreationKind;
class CurveToolService;
class EditorSelectionService;
class LinearArrayToolService;
class PathArrayToolService;
class PivotToolService;
class RadialArrayToolService;
class SceneDocument;
class ViewportCameraController;
class ViewportPointPlacementService;
struct ViewportRay;

/** Commits the coordinator-owned native tool without depending on public API. */
class ViewportNativeToolLifecyclePort {
public:
  virtual ~ViewportNativeToolLifecyclePort() = default;
  [[nodiscard]] virtual CommandResult commit_active() = 0;
};

/** Resolves Curve controls through the viewport spatial-query owner. */
class ViewportCurveControlPickingPort {
public:
  virtual ~ViewportCurveControlPickingPort() = default;
  [[nodiscard]] virtual std::optional<CurveControlTarget>
  pick(const CurveToolPreviewState &preview, QVec2 mouse_local,
       bool anchors_only) const = 0;
};

/** Resolves Path Array source candidates through the viewport query owner. */
class ViewportPathArraySourcePickingPort {
public:
  virtual ~ViewportPathArraySourcePickingPort() = default;
  [[nodiscard]] virtual std::optional<EntityId> pick(
      QVec2 mouse_local, std::span<const EntityId> excluded_entities,
      std::span<const SceneSurfaceOccurrenceExclusion> excluded_occurrences,
      bool select_backfaces) const = 0;
};

/**
 * Carries one sampled frame into the editor-native viewport tool owner.
 */
struct ViewportNativeToolInteractionFrame final {
  const ViewportInputFrame &input;
  const ViewportInputResult &input_result;
  bool navigation_gizmo_consumed = false;
  bool transform_gizmo_consumed = false;
  bool suppress_hover_work = false;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  QVec2 left_click_local = {};
  float pointer_drag_threshold_pixels = 4.0f;
  bool scene_snap_enabled = false;
  bool grid_snap_enabled = false;
  const ViewportCurveControlPickingPort *curve_control_picker = nullptr;
  const ViewportPathArraySourcePickingPort *path_array_source_picker = nullptr;
  bool select_backfaces = true;
  std::optional<QVec3> selection_transform_pivot;
  double *linear_array_wheel_remainder = nullptr;
  double *radial_array_wheel_remainder = nullptr;
};

/**
 * Reports consumed input and retained-presentation invalidation decisions.
 */
struct ViewportNativeToolInteractionResult final {
  /// Common lifecycle outcome consumed by the interaction controller. The
  /// tool-specific bits below remain diagnostic compatibility facts.
  bool consumed = false;
  bool blocks_transform_gizmo = false;
  bool radial_array_consumed = false;
  bool linear_array_consumed = false;
  bool path_array_consumed = false;
  bool pivot_consumed = false;
  bool pivot_blocks_gizmo = false;
  bool claim_interaction_pane = false;
  bool sync_active_tool = false;
  bool invalidate_hover = false;
  bool mark_overlay_dirty = false;
  bool curve_consumed = false;
};

/**
 * Owns camera-space hit and drag decisions for editor-native viewport tools.
 */
class ViewportNativeToolInteraction final {
public:
  [[nodiscard]] ViewportNativeToolInteractionResult handle_frame(
      ViewportNativeToolLifecyclePort &lifecycle, SceneDocument &document,
      AssetRegistry &assets,
      PivotToolService *pivot, LinearArrayToolService *linear_array,
      RadialArrayToolService *radial_array,
      PathArrayToolService *path_array,
      EditorSelectionService *selection,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame,
      CurveToolService *curve = nullptr);
  void clear_drag_state();
  [[nodiscard]] bool dragging() const;

  [[nodiscard]] static std::optional<int> hit_drawn_plane_captured_point(
      const std::array<QVec3, 3> &points, int captured_point_count,
      QVec2 mouse_local, const ViewportCameraController &camera);
  [[nodiscard]] static bool hit_drawn_plane_line(
      const std::array<QVec3, 3> &points, int captured_point_count,
      bool has_line, QVec2 mouse_local,
      const ViewportCameraController &camera);
  [[nodiscard]] static ModelingBoxToolEvent
  modeling_box_event(const ViewportCameraController &camera,
                     QVec2 viewport_size, QVec2 mouse_local,
                     float grid_size, bool snap_enabled);
  [[nodiscard]] static QVec3 resolved_pivot_drag_position(
      const ViewportCameraController &camera, QVec2 mouse_local,
      QVec3 current_position, const SceneDocument &document,
      const AssetRegistry &assets, ViewportPointPlacementService &placement,
      PivotSnapSettings settings, float grid_size);
  [[nodiscard]] static bool pivot_handle_hit(
      const ViewportCameraController &camera, const PivotHandleState &handle,
      QVec2 mouse_local);
  [[nodiscard]] static bool linear_array_handle_hit(
      const ViewportCameraController &camera,
      const LinearArrayHandleState &handle, QVec2 mouse_local);
  [[nodiscard]] static QVec3 linear_array_drag_point(
      const ViewportCameraController &camera, QVec2 mouse_local,
      const LinearArrayHandleState &handle);
private:
  /** Retains press-time state for one deferred curve-point construction gesture. */
  struct CurvePointConstructionPress final {
    ViewportPointerGesture gesture;
    QVec2 pointer_offset = {};
    ViewportPointPlacementPlane plane;
  };

  /** Pins one empty preset's exact press-time plane placement until release. */
  struct CurvePresetPlacementPress final {
    ViewportPointerGesture gesture;
    QVec3 center = {};
    QVec3 axis_u = {1.0f, 0.0f, 0.0f};
    QVec3 axis_v = {0.0f, 1.0f, 0.0f};
    CurveCreationKind creation_kind{};
  };

  /** Retains press-time state for one linear-array handle gesture. */
  struct LinearArrayHandlePress final {
    ViewportPointerGesture gesture;
    QVec3 endpoint_before = {};
  };

  /** Retains press-time state for one pivot-handle gesture. */
  struct PivotHandlePress final {
    ViewportPointerGesture gesture;
    QVec3 position_before = {};
    bool persistent = false;
  };

  /** Retains one exact stationary source-selection gesture. */
  struct PathArraySourcePress final {
    ViewportPointerGesture gesture;
    EntityId candidate = kInvalidEntityId;
    EntityId path_entity = kInvalidEntityId;
  };

  /// Pins a deferred conversion click to the anchor and document it pressed.
  struct CurveConversionPress {
    ViewportPointerGesture gesture;
    std::optional<CurveControlTarget> target;
    modeling::CurveContentIdentity content;
    EntityId entity = kInvalidEntityId;
  };

  std::optional<PivotHandlePress> pivot_handle_press_;
  std::optional<LinearArrayHandlePress> linear_array_handle_press_;
  std::optional<CurvePointConstructionPress> curve_point_construction_press_;
  std::optional<CurvePresetPlacementPress> curve_preset_placement_press_;
  std::optional<CurveConversionPress> curve_conversion_press_;
  std::optional<PathArraySourcePress> path_array_source_press_;
  const PathArrayToolService *path_array_exclusion_owner_ = nullptr;
  std::uint64_t path_array_exclusion_revision_ = 0u;
  EntityId path_array_exclusion_source_ = kInvalidEntityId;
  std::vector<EntityId> path_array_excluded_entities_;
  std::vector<SceneSurfaceOccurrenceExclusion>
      path_array_excluded_occurrences_;
  ViewportPointerReleaseSuppression left_release_suppression_;
};

} // namespace quader
