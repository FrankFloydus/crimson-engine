////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

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
    [[nodiscard]] ViewportBeforeToolInteractionResult handleFrame(
            QuaderEditorApi& editor,
            const ViewportInteractionFrame& frame);
    [[nodiscard]] bool cancelActiveDrag(QuaderEditorApi& editor);
    void reset(QuaderEditorApi* editor = nullptr);

    [[nodiscard]] bool dragging() const { return dragging_; }
    [[nodiscard]] const quader_editor_core::TransformGizmoFrame* frame() const;

private:
    void resetDragState();
    void clearFrame();

    bool dragging_ = false;
    quader_editor_core::TransformAxis activeAxis_ = quader_editor_core::TransformAxis::none;
    quader_editor_core::TransformAxis hoverAxis_ = quader_editor_core::TransformAxis::none;
    std::optional<quader_editor_core::TransformGizmoFrame> currentFrame_;
};

} // namespace quader
