////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/drag_commit_tool.hpp"

#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

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
        const ModelingCommandResult beginCommand =
                context.modeling.beginInsertEdgeLoopDrag(pointContext.makeEventAt(pointer.mouseLocal, true));
        handleResult(context, "Insert edge loop begin", beginCommand);
        const bool canCommit = beginCommand.ok && context.modeling.toolStatus().insertEdgeLoopDragging;
        context.modalState->insertEdgeLoopDragActive = false;
        if (canCommit) {
            const ModelingCommandResult commitCommand = context.modeling.commitInsertEdgeLoopDrag();
            handleResult(context, "Insert edge loop commit", commitCommand);
            if (context.setSelectionMode) {
                context.setSelectionMode(appSelectionModeToViewport(context.modeling.selectionSummary().mode));
            }
            return {
                    .handled = true,
                    .modalStateChanged = true,
                    .activeToolChanged = commitCommand.ok,
                    .syncActiveToolFromModeling = commitCommand.ok,
            };
        }
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
