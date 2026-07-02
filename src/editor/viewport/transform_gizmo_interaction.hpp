////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "editor/viewport/viewport_interaction_frame.hpp"

#include <editor/interactions/transform_gizmo.hpp>

#include <optional>

namespace quader {

class QuaderEditorApi;

/**
 * Owns transform gizmo hit testing and transform drag lifecycle routing for viewport frames.
 */
class TransformGizmoInteraction {
public:
  [[nodiscard]] ViewportBeforeToolInteractionResult
  handle_frame(QuaderEditorApi &editor, const ViewportInteractionFrame &frame);
  [[nodiscard]] bool cancel_active_drag(QuaderEditorApi &editor);
  void reset(QuaderEditorApi *editor = nullptr);

  [[nodiscard]] bool dragging() const { return dragging_; }
  [[nodiscard]] const quader_editor_core::TransformGizmoFrame *frame() const;

private:
    /**
     * Stores a gizmo hit that is waiting for drag-threshold confirmation.
     */
    struct PendingGizmoDrag {
      QuaderTransformDragBeginRequest begin_request;
      ViewportToolId tool = ViewportToolId::Select;
      quader_editor_core::TransformAxis axis =
          quader_editor_core::TransformAxis::None;
      quader::QVec2 click_local = {};
      int click_count = 1;
    };

    [[nodiscard]] bool
    pending_drag_threshold_reached(quader::QVec2 current) const;
    [[nodiscard]] bool
    begin_pending_drag(QuaderEditorApi &editor,
                       const ViewportInteractionFrame &frame);
    [[nodiscard]] ViewportBeforeToolInteractionResult release_pending_click();
    void reset_drag_state();
    void reset_pending_drag();
    void clear_frame();

    bool dragging_ = false;
    std::optional<PendingGizmoDrag> pending_drag_;
    quader_editor_core::TransformAxis active_axis_ =
        quader_editor_core::TransformAxis::None;
    quader_editor_core::TransformAxis hover_axis_ =
        quader_editor_core::TransformAxis::None;
    quader::QVec3 active_drag_pivot_ = {};
    quader::QVec3 active_world_direction_ = {};
    std::optional<quader::QVec3> rotation_indicator_start_direction_;
    std::optional<quader_editor_core::TransformGizmoFrame> current_frame_;
};

} // namespace quader
