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
    void handleFrame(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame,
            const ViewportToolResult& toolResult,
            bool gizmoConsumed);
    [[nodiscard]] QuaderSelectionSummary pickContextMenuTarget(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame);
    [[nodiscard]] ViewportSelectionMarquee marquee() const;

private:
    bool hoverClearIssued_ = false;
    quader_editor_core::SelectionBoxState boxState_;
    QuaderSelectionEdit boxEdit_ = QuaderSelectionEdit::Replace;
    std::optional<QuaderHoverTarget> boxHoveredBeforeSelection_;
    std::optional<QuaderSelectionBoxBaseline> boxBaseline_;
    std::optional<ViewportSelectionMarquee> committedMarquee_;
    std::optional<ViewportSelectionMarquee> liveBoxSelection_;
    std::optional<QuaderHoverTarget> hoverSuppressedTarget_;
    bool paintSelectionActive_ = false;
};

} // namespace quader
