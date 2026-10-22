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
#include "editor/interactions/move_tool.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "editor/placement/viewport_point_placement_service.hpp"
#include "editor/tools/pivot_handle_layout.hpp"
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
struct QuaderOperationInputContext;

/** Commits the coordinator-owned native tool without depending on public API. */
class ViewportNativeToolLifecyclePort {
public:
  virtual ~ViewportNativeToolLifecyclePort() = default;
  [[nodiscard]] virtual CommandResult commit_active() = 0;
  [[nodiscard]] virtual bool light_creation_active() const { return false; }
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
  bool transform_gizmo_consumed = false;
  bool suppress_hover_work = false;
  bool handles_visible = true;
  std::uint64_t scene_surface_revision = 0u;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  QVec2 left_click_local = {};
  int left_click_count = 1;
  float pointer_drag_threshold_pixels = 4.0f;
  float viewport_pixel_scale = 1.0f;
  bool scene_snap_enabled = false;
  bool grid_snap_enabled = false;
  double scene_vertex_snap_radius = 0.0;
  const ViewportCurveControlPickingPort *curve_control_picker = nullptr;
  const ViewportPathArraySourcePickingPort *path_array_source_picker = nullptr;
  bool select_backfaces = true;
  std::optional<QVec3> selection_transform_pivot;
  std::optional<PivotHandleLayoutOptions> pivot_layout_options;
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
  std::optional<QVec3> light_creation_world_position;
  bool command_attempted = false;
  CommandResult command_result;
  /// Releases a deferred native face click to the existing selection owner.
  bool selection_click_requested = false;
  QVec2 selection_click_local = {};
  ViewportModifiers selection_click_modifiers = {};
  int selection_click_count = 1;
  bool selection_cycle_depth = false;
  ViewportModifiers selection_cycle_depth_modifiers = {};
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
  [[nodiscard]] ViewportPointerCursor pointer_cursor() const {
    return pointer_cursor_;
  }
  [[nodiscard]] PivotHandlePart pivot_hovered_part() const {
    return pivot_handle_hovered_ ? pivot_hover_part_ : PivotHandlePart::None;
  }
  [[nodiscard]] PivotHandlePart pivot_active_part() const {
    return pivot_handle_press_ ? pivot_handle_press_->part : PivotHandlePart::None;
  }
  [[nodiscard]] std::uint64_t pivot_axis_sample_count() const {
    return pivot_axis_sample_count_;
  }
  [[nodiscard]] std::optional<PivotHandleLayoutOptions>
  pivot_drag_layout_options() const {
    return pivot_handle_press_ ? std::optional{pivot_handle_press_->layout_options}
                               : std::nullopt;
  }
  /// Projects only the eligible hover accepted by the most recent input frame.
  void populate_operation_input_context(
      QuaderOperationInputContext &context) const;

  [[nodiscard]] static std::optional<int> hit_drawn_plane_captured_point(
      const std::array<QVec3, 3> &points, int captured_point_count,
      QVec2 mouse_local, const ViewportCameraController &camera,
      float viewport_pixel_scale = 1.0f);
  [[nodiscard]] static bool hit_drawn_plane_line(
      const std::array<QVec3, 3> &points, int captured_point_count,
      bool has_line, QVec2 mouse_local,
      const ViewportCameraController &camera,
      float viewport_pixel_scale = 1.0f);
  [[nodiscard]] static ModelingBoxToolEvent
  modeling_box_event(const ViewportCameraController &camera,
                     QVec2 viewport_size, QVec2 mouse_local,
                     float grid_size, bool snap_enabled);
  [[nodiscard]] static QVec3 resolved_pivot_drag_position(
      const ViewportCameraController &camera, QVec2 mouse_local,
      QVec3 current_position, const SceneDocument &document,
      const AssetRegistry &assets, ViewportPointPlacementService &placement,
      PivotSnapSettings settings, float grid_size,
      float viewport_pixel_scale = 1.0f);
  [[nodiscard]] static bool pivot_handle_hit(
      const ViewportCameraController &camera, const PivotHandleState &handle,
      QVec2 mouse_local, float viewport_pixel_scale = 1.0f,
      const PivotHandleLayoutOptions &options = {});
  [[nodiscard]] static bool linear_array_handle_hit(
      const ViewportCameraController &camera,
      const LinearArrayHandleState &handle, QVec2 mouse_local,
      float viewport_pixel_scale = 1.0f);
private:
  /// Reuses projected handle hover until the camera, pointer, or handle moves.
  struct HandleHoverKey final {
    std::array<float, 25> pointer_camera{};
    std::array<float, 3> position{};
    std::array<float, 10> layout{};
    bool persistent = false;
    bool operator==(const HandleHoverKey &) const = default;
  };

  /// Invalidates control picking only when its authored or spatial inputs change.
  struct CurveControlHoverKey final {
    modeling::CurveContentIdentity content;
    EntityId entity = kInvalidEntityId;
    std::uint64_t scene_revision = 0u;
    std::uint64_t asset_revision = 0u;
    std::uint64_t scene_surface_revision = 0u;
    std::array<float, 16> world_transform{};
    std::array<float, 25> pointer_camera{};
    bool drawing = false;
    bool active = false;
    bool operator==(const CurveControlHoverKey &) const = default;
  };

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
    LinearArrayAxisConstraint constraint_before = LinearArrayAxisConstraint::Free;
    QVec3 motion_endpoint = {};
    QVec2 motion_pointer = {};
    LinearArrayAxisConstraint motion_constraint = LinearArrayAxisConstraint::Free;
    bool vertical = false;
    bool screen_fallback = false;
    bool motion_started = false;
  };

  static void rebase_linear_array_drag(
      LinearArrayHandlePress &press, const ViewportCameraController &camera,
      QVec2 pointer, const LinearArrayHandleState &handle, bool vertical);
  [[nodiscard]] static QVec3 linear_array_drag_point(
      const ViewportCameraController &camera, QVec2 mouse_local,
      const LinearArrayHandlePress &press);

  /// Rejects unchanged axis samples before scene landmark picking.
  struct PivotAxisSampleKey final {
    std::array<float, 25> pointer_camera{};
    std::uint64_t scene_revision = 0;
    std::uint64_t asset_revision = 0;
    std::uint64_t surface_revision = 0;
    std::uint64_t settings_revision = 0;
    int grid_preset = 0;
    bool precision = false;
    bool operator==(const PivotAxisSampleKey &) const = default;
  };

  /** Retains press-time state for one pivot-handle gesture. */
  struct PivotHandlePress final {
    ViewportPointerGesture gesture;
    QVec3 position_before = {};
    bool persistent = false;
    EntityId target = kInvalidEntityId;
    PivotHandlePart part = PivotHandlePart::Center;
    PivotHandleLayoutOptions layout_options;
    quader_editor_core::MoveToolState axis_drag;
    bool axis_motion_started = false;
    std::optional<PivotAxisSampleKey> axis_sample_key;
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
  ViewportPointerCursor pointer_cursor_ = ViewportPointerCursor::Default;
  std::optional<CurveControlHoverKey> curve_control_hover_key_;
  std::optional<CurveControlTarget> curve_control_hover_target_;
  std::optional<HandleHoverKey> linear_array_hover_key_;
  std::optional<HandleHoverKey> pivot_hover_key_;
  bool linear_array_hover_hit_ = false;
  PivotHandlePart pivot_hover_part_ = PivotHandlePart::None;
  bool curve_control_hover_accepted_ = false;
  bool path_source_hovered_ = false;
  bool linear_array_endpoint_hovered_ = false;
  bool pivot_handle_hovered_ = false;
  std::uint64_t pivot_axis_sample_count_ = 0;
  // The next adjustment family owns the pending physical wheel fraction.
  std::optional<ViewportRadialArrayInputEventKind> radial_array_wheel_kind_;
};

} // namespace quader
