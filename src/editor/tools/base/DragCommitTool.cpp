////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/DragCommitTool.h"

#include "editor/modeling/services/ModelingSessionService.h"
#include "editor/modeling/services/SelectionModeAdapter.h"

namespace quader {
namespace {

void handleResult(ViewportToolContext& context, std::string_view label, const ModelingCommandResult& command) {
    if (context.handleToolCommandResult) {
        context.handleToolCommandResult(label, command);
    }
}

} // namespace

ViewportToolResult DragCommitTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            modelingTool() != ModelingViewportTool::InsertEdgeLoop ||
            context.modalState == nullptr ||
            context.point == nullptr ||
            !context.point->makeEventAt) {
        return ModalViewportTool::handleEvent(context, event);
    }

    ViewportPointToolContext& pointContext = *context.point;
    const ViewportPointerToolEvent& pointer = event.pointer;
    const bool dragging =
            context.modalState->insertEdgeLoopDragActive ||
            context.modeling.toolStatus().insertEdgeLoopDragging;

    if (pointer.canInteract &&
            !pointer.leftDown &&
            !pointer.leftReleased &&
            !dragging &&
            !pointer.suppressHoverWork &&
            (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
        handleResult(context,
                "Insert edge loop hover",
                context.modeling.updateInsertEdgeLoopHover(pointContext.makeEventAt(pointer.mouseLocal, false)));
    }

    if (pointer.canInteract && pointer.leftClicked) {
        const ModelingCommandResult command =
                context.modeling.beginInsertEdgeLoopDrag(pointContext.makeEventAt(pointer.mouseLocal, true));
        handleResult(context, "Insert edge loop drag begin", command);
        context.modalState->insertEdgeLoopDragActive = command.ok && context.modeling.toolStatus().insertEdgeLoopDragging;
    } else if (pointer.canInteract && pointer.leftDown && dragging) {
        handleResult(context,
                "Insert edge loop drag update",
                context.modeling.updateInsertEdgeLoopDrag(pointContext.makeEventAt(pointer.mouseLocal, true)));
    }

    if (pointer.leftReleased && dragging) {
        const ModelingCommandResult command = context.modeling.commitInsertEdgeLoopDrag();
        handleResult(context, "Insert edge loop commit", command);
        context.modalState->insertEdgeLoopDragActive = false;
        if (context.setSelectionMode) {
            context.setSelectionMode(appSelectionModeToViewport(context.modeling.selectionSummary().mode));
        }
    }

    return {
            .handled = true,
            .modalStateChanged = true,
    };
}

} // namespace quader
