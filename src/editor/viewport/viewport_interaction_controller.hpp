////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/viewport/transform_gizmo_interaction.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"

namespace quader {

class QuaderEditorApi;
class ViewportToolHoverPort;
struct ViewportToolResult;

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

private:
  TransformGizmoInteraction transform_gizmo_;
  ViewportSelectionInteraction selection_;
};

} // namespace quader
