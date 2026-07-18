////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/pivot/pivot_types.hpp"
#include "viewport/viewport_input_types.hpp"

#include <array>
#include <optional>

namespace quader {

class AssetRegistry;
class LinearArrayToolService;
class PivotToolService;
class QuaderEditorApi;
class RadialArrayToolService;
class SceneDocument;
class ViewportCameraController;
class ViewportPointPlacementService;

/**
 * Carries one sampled frame into the editor-native viewport tool owner.
 */
struct ViewportNativeToolInteractionFrame final {
  const ViewportInputFrame &input;
  const ViewportInputResult &input_result;
  bool navigation_gizmo_consumed = false;
  bool suppress_hover_work = false;
  bool left_pressed = false;
  bool left_down = false;
  bool left_released = false;
  QVec2 left_click_local = {};
  bool grid_snap_enabled = false;
  double *linear_array_wheel_remainder = nullptr;
  double *radial_array_wheel_remainder = nullptr;
};

/**
 * Reports consumed input and retained-presentation invalidation decisions.
 */
struct ViewportNativeToolInteractionResult final {
  bool radial_array_consumed = false;
  bool linear_array_consumed = false;
  bool pivot_consumed = false;
  bool pivot_blocks_gizmo = false;
  bool claim_interaction_pane = false;
  bool sync_active_tool = false;
  bool invalidate_hover = false;
  bool mark_overlay_dirty = false;
};

/**
 * Owns camera-space hit and drag decisions for editor-native viewport tools.
 */
class ViewportNativeToolInteraction final {
public:
  [[nodiscard]] ViewportNativeToolInteractionResult handle_frame(
      QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
      PivotToolService *pivot, LinearArrayToolService *linear_array,
      RadialArrayToolService *radial_array,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame);
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
                     const ViewportInputFrame &input, float grid_size,
                     bool snap_enabled);
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
  bool pivot_handle_dragging_ = false;
  bool linear_array_handle_dragging_ = false;
};

} // namespace quader
