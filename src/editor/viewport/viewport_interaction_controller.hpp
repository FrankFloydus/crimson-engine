////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/tools/viewport_tool_runtime.hpp"
#include "editor/viewport/transform_gizmo_interaction.hpp"
#include "editor/viewport/viewport_native_tool_interaction.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"

namespace quader {

class QuaderEditorApi;
class ViewportToolHoverPort;

/**
 * Coordinates editor-owned viewport interactions around active viewport tools.
 */
class ViewportInteractionController {
public:
  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_before_active_tool(QuaderEditorApi &editor,
                            const ViewportInteractionFrame &frame);
  void handle_after_active_tool(
      QuaderEditorApi &editor, const ViewportInteractionFrame &frame,
      const ViewportToolResult &tool_result,
      const ViewportBeforeToolInteractionResult &before_tool,
      ViewportToolHoverPort *tool_hover = nullptr);
  [[nodiscard]] QuaderSelectionSummary
  pick_context_menu_target(QuaderEditorApi &editor,
                           const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor);
  [[nodiscard]] bool dragging() const;
  void reset(QuaderEditorApi *editor = nullptr);

  [[nodiscard]] const quader_editor_core::TransformGizmoFrame *
  transform_gizmo_frame() const;
  [[nodiscard]] const TransformGizmoSelectionCacheStats &
  transform_gizmo_selection_cache_stats() const;
  [[nodiscard]] ViewportSelectionMarquee selection_marquee() const;

  [[nodiscard]] bool active_tool_owns_viewport(ViewportToolId tool);
  [[nodiscard]] bool active_tool_blocks_viewport_focus_shortcuts(
      ViewportToolId tool);
  [[nodiscard]] bool active_tool_allows_selection_fallback(
      ViewportToolId tool) const;
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
      PivotToolService *pivot, LinearArrayToolService *linear_array,
      RadialArrayToolService *radial_array,
      ViewportPointPlacementService *point_placement,
      ViewportCameraController &camera,
      const ViewportNativeToolInteractionFrame &frame);
  void clear_native_tool_drag_state();
  [[nodiscard]] bool native_tool_dragging() const;

private:
  void sync_tool_runtime(ViewportToolId tool);

  ViewportToolRuntime tool_runtime_;
  ViewportNativeToolInteraction native_tools_;
  TransformGizmoInteraction transform_gizmo_;
  ViewportSelectionInteraction selection_;
};

} // namespace quader
