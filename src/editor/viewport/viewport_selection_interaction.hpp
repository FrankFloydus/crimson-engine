////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_selection_api.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"

#include <editor/interactions/selection_box.hpp>

#include <optional>

namespace quader {

class QuaderEditorApi;
class ViewportToolHoverPort;
struct ViewportToolResult;

/**
 * Stores the active viewport selection marquee for overlay presentation.
 */
struct ViewportSelectionMarquee {
    bool active = false;
    QVec2 start = {};
    QVec2 end = {};
    ModelingBoxSelectionRule rule = ModelingBoxSelectionRule::Contain;
};

/**
 * Owns selection fallback, paint selection, and hover sequencing for viewport frames.
 */
class ViewportSelectionInteraction {
public:
    void reset();
    void handle_frame(QuaderEditorApi &editor,
                      const ViewportInteractionFrame &frame,
                      const ViewportToolResult &tool_result,
                      const ViewportBeforeToolInteractionResult &before_tool,
                      ViewportToolHoverPort *tool_hover);
    [[nodiscard]] QuaderSelectionSummary
    pick_context_menu_target(QuaderEditorApi &editor,
                             const ViewportInteractionFrame &frame);
    [[nodiscard]] ViewportSelectionMarquee marquee() const;

private:
  bool hover_clear_issued_ = false;
  quader_editor_core::SelectionBoxState box_state_;
  QuaderSelectionEdit box_edit_ = QuaderSelectionEdit::Replace;
  std::optional<QuaderHoverTarget> box_hovered_before_selection_;
  std::optional<QuaderSelectionBoxBaseline> box_baseline_;
  std::optional<ViewportSelectionMarquee> committed_marquee_;
  std::optional<QuaderHoverTarget> hover_suppressed_target_;
  bool paint_selection_active_ = false;
};

} // namespace quader
