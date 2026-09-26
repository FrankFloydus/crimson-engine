////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_runtime_binding_token.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/tools/viewport_tool_runtime.hpp"
#include "editor/viewport/transform_gizmo_interaction.hpp"
#include "editor/viewport/point_light_range_interaction.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "viewport/viewport_pointer_gesture.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace quader {

class QuaderEditorApi;
struct QuaderPrimitiveCreationState;
class ViewportToolHoverPort;
struct QuaderOperationInputContext;

/**
 * Carries the host facts needed to resolve Curve-control gizmo priority,
 * native-tool pointer handling, and ordinary transform-gizmo fallback as one
 * editor-owned lifecycle decision.
 */
struct ViewportToolPointerLifecycleFrame final {
  const ViewportInteractionFrame &interaction;
  const ViewportNativeToolInteractionFrame &native_tool;
  bool cancel_consumed = false;
  ViewportRuntimeBindingToken runtime_binding;
};

/** Reports every policy decision made during one native tool pointer frame. */
struct ViewportToolPointerLifecycleResult final {
  ViewportBeforeToolInteractionResult before_tool;
  ViewportNativeToolInteractionResult native_tool;
  ViewportInteractionSelectionState interaction_selection;
  bool curve_gizmo_prepass = false;
};

/** Completed toolkit-neutral right-button gesture owned by the controller. */
struct ViewportContextMenuPointerResult final {
  bool completed = false;
  bool release_inside_viewport = false;
  QVec2 press_screen = {};
  QVec2 release_screen = {};
  QVec2 release_local = {};
  QVec2 accumulated_drag_delta = {};
  float drag_threshold_pixels = 4.0f;
};

/**
 * Coordinates editor-owned viewport interactions around active viewport tools.
 */
class ViewportInteractionController {
public:
  /// Cheap counters proving idle frames avoid material interaction work.
  struct MaterialInteractionWorkStats final {
    std::uint64_t face_pick_count = 0;
    std::uint64_t apply_face_to_mesh_request_count = 0;
    std::uint64_t transfer_face_texture_request_count = 0;
    std::uint64_t copy_face_material_request_count = 0;
  };

  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_before_active_tool(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame);
  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_before_active_tool(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame,
                            NativeViewportToolCoordinator &native_tools);
  void handle_after_active_tool(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      ViewportToolHoverPort *tool_hover = nullptr,
      ViewportRuntimeBindingToken runtime_binding = {});
  void handle_after_active_tool(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      NativeViewportToolCoordinator &native_tools,
      ViewportToolHoverPort *tool_hover = nullptr,
      ViewportRuntimeBindingToken runtime_binding = {});
  [[nodiscard]] QuaderSelectionSummary
  pick_context_menu_target(QuaderEditorApi &editor,
                           const ViewportInteractionFrame &frame);
  [[nodiscard]] std::optional<QuaderHoverTarget>
  pick_material_face_target(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor);
  [[nodiscard]] bool
  cancel_active_drag(QuaderEditorApi &editor,
                     NativeViewportToolCoordinator &native_tools);
  [[nodiscard]] bool dragging() const;
  /// Copies owner-accepted state after dispatch; never performs hover picking.
  void populate_operation_input_context(
      QuaderOperationInputContext &context,
      const ViewportInteractionFrame &frame) const;
  [[nodiscard]] bool transform_gizmo_pointer_active() const;
  [[nodiscard]] bool point_light_range_pointer_active() const {
    return point_light_range_.pointer_active();
  }
  [[nodiscard]] std::uint64_t point_light_range_hover_pick_count() const {
    return point_light_range_.hover_pick_count();
  }
  [[nodiscard]] bool primitive_creation_pointer_active() const {
    return tool_runtime_.primitive_creation_interaction().active() >= 0;
  }
  [[nodiscard]] const PrimitiveCreationControlsFrame &primitive_creation_controls(
      const std::optional<QuaderPrimitiveCreationState> &cube,
      const ViewportCameraController &camera, float raster_scale);
  /// Synchronizes live glyph presentation even when no pane receives tool input.
  void set_primitive_creation_decrement(bool decrement);
  /// Keeps a material press and its release in the initiating pane.
  [[nodiscard]] bool material_transfer_pointer_active() const {
    return material_transfer_pointer_.has_value();
  }
  [[nodiscard]] ViewportContextMenuPointerResult
  handle_context_menu_pointer(const ViewportInteractionFrame &frame,
                              bool modal_tool_owns_viewport);
  [[nodiscard]] bool context_menu_pointer_active() const {
    return context_menu_pointer_.has_value();
  }
  void cancel_context_menu_pointer();
  void cancel_pending_material_interaction();
  void cancel_pending_selection_click();
  void reset(QuaderEditorApi *editor = nullptr);
  void reset(QuaderEditorApi *editor,
             NativeViewportToolCoordinator &native_tools);

  [[nodiscard]] const quader_editor_core::TransformGizmoFrame *
  transform_gizmo_frame() const;
  [[nodiscard]] const std::vector<quader_editor_core::SelectionOverlayPoint> &
  transform_gizmo_snap_overlay_points() const;
  [[nodiscard]] const TransformGizmoSelectionCacheStats &
  transform_gizmo_selection_cache_stats() const;
  [[nodiscard]] const ViewportSelectionMarquee &selection_marquee() const;
  [[nodiscard]] const MaterialInteractionWorkStats &
  material_interaction_work_stats() const;

  [[nodiscard]] bool active_tool_owns_viewport(ViewportToolId tool);
  [[nodiscard]] bool active_tool_blocks_viewport_focus_shortcuts(
      ViewportToolId tool);
  [[nodiscard]] bool active_tool_allows_selection_fallback(
      ViewportToolId tool) const;
  [[nodiscard]] bool
  active_tool_uses_editor_native_lifecycle(ViewportToolId tool) const;
  [[nodiscard]] ViewportPointerCursor
  active_tool_pointer_cursor(ViewportToolId tool) const;
  [[nodiscard]] bool
  has_active_modal_preview(const ViewportToolPreviewPort &preview,
                           bool snap_enabled = true) const;
  [[nodiscard]] ModelingBoxPreview
  box_preview(const ViewportToolPreviewPort &preview,
              bool snap_enabled = true) const;
  [[nodiscard]] const ViewportTool *active_tool(ViewportToolId tool);
  bool set_active_tool(ViewportToolState &state, ViewportToolId tool,
                       ViewportInputResult *result = nullptr);
  void clear_modal_tool_state();
  void clear_modal_drag_state();
  ViewportToolResult handle_active_tool_event(ViewportToolState &state,
                                              ViewportToolContext &context,
                                              const ViewportToolEvent &event);
  [[nodiscard]] ViewportNativeToolInteractionResult handle_native_tools(
      QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
      NativeViewportToolCoordinator &native_tools,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame);
  [[nodiscard]] ViewportToolPointerLifecycleResult
  handle_tool_pointer_lifecycle(
      QuaderEditorApi &editor, SceneDocument &document, AssetRegistry &assets,
      NativeViewportToolCoordinator &native_tools,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportToolPointerLifecycleFrame &frame);

private:
  /// Keeps the original scene and selected source for a reserved mouse gesture.
  struct MaterialTransferPointer final {
    ViewportPointerGesture pointer;
    QuaderInputChord semantic_chord;
    ViewportInputActionKind action = ViewportInputActionKind::TransferFaceTexture;
    SceneSessionId scene_session_id;
    std::uint64_t selection_revision = 0;
    std::uint64_t keybindings_revision = 0;
  };

  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_before_active_tool_impl(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      ViewportCurveInteractionPort *curve,
      NativeViewportToolCoordinator *native_tools);
  void handle_after_active_tool_impl(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      ViewportToolHoverPort *tool_hover,
      ViewportCurveInteractionPort *curve,
      ViewportRuntimeBindingToken runtime_binding);

  void handle_material_interactions(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      ViewportRuntimeBindingToken runtime_binding);
  [[nodiscard]] std::optional<ViewportBeforeToolInteractionResult>
  reserve_material_transfer(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      NativeViewportToolCoordinator *native_tools);
  [[nodiscard]] std::optional<ViewportBeforeToolInteractionResult>
  reserve_material_pointer_edge(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      NativeViewportToolCoordinator *native_tools);
  [[nodiscard]] std::optional<QuaderHoverTarget> pick_material_face_target(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      QVec2 local_mouse);
  void sync_tool_runtime(ViewportToolId tool);

  ViewportToolRuntime tool_runtime_;
  TransformGizmoInteraction transform_gizmo_;
  PointLightRangeInteraction point_light_range_;
  ViewportSelectionInteraction selection_;
  // Ownership only: transfer happens on the semantic trigger, never while held.
  std::optional<MaterialTransferPointer> material_transfer_pointer_;
  ViewportInputActionKind accepted_material_hint_action_ = ViewportInputActionKind::None;
  // A reset drops pane ownership, but the old release must not become a click
  // in the new scene. The shared pointer primitive retires it on release or a
  // fresh press.
  ViewportPointerReleaseSuppression transfer_release_suppression_;
  std::optional<ViewportPointerGesture> context_menu_pointer_;
  ViewportPointerReleaseSuppression context_menu_release_suppression_;
  MaterialInteractionWorkStats material_interaction_work_stats_;
  ViewportPointerCursor interaction_cursor_ = ViewportPointerCursor::Default;
};

} // namespace quader
