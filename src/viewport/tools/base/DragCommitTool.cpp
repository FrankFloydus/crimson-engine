#include "viewport/tools/base/DragCommitTool.h"

namespace quader {
namespace {

void handleResult(ViewportToolContext& context, std::string_view label, const ModelingCommandResult& command) {
    if (context.handleToolCommandResult) {
        context.handleToolCommandResult(label, command);
    }
}

ViewportSelectionMode toViewportSelectionMode(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return ViewportSelectionMode::Vertex;
    case ModelingSelectionMode::Edge:
        return ViewportSelectionMode::Edge;
    case ModelingSelectionMode::Face:
        return ViewportSelectionMode::Face;
    case ModelingSelectionMode::Object:
        return ViewportSelectionMode::Object;
    case ModelingSelectionMode::Group:
        return ViewportSelectionMode::Group;
    }
    return ViewportSelectionMode::Object;
}

} // namespace

ViewportToolResult DragCommitTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            modelingTool() != ModelingViewportTool::InsertEdgeLoop ||
            context.modalState == nullptr ||
            !context.makeToolEventAt) {
        return ModalViewportTool::handleEvent(context, event);
    }

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
                context.modeling.updateInsertEdgeLoopHover(context.makeToolEventAt(pointer.mouseLocal, false)));
    }

    if (pointer.canInteract && pointer.leftClicked) {
        const ModelingCommandResult command =
                context.modeling.beginInsertEdgeLoopDrag(context.makeToolEventAt(pointer.mouseLocal, true));
        handleResult(context, "Insert edge loop drag begin", command);
        context.modalState->insertEdgeLoopDragActive = command.ok && context.modeling.toolStatus().insertEdgeLoopDragging;
    } else if (pointer.canInteract && pointer.leftDown && dragging) {
        handleResult(context,
                "Insert edge loop drag update",
                context.modeling.updateInsertEdgeLoopDrag(context.makeToolEventAt(pointer.mouseLocal, true)));
    }

    if (pointer.leftReleased && dragging) {
        const ModelingCommandResult command = context.modeling.commitInsertEdgeLoopDrag();
        handleResult(context, "Insert edge loop commit", command);
        context.modalState->insertEdgeLoopDragActive = false;
        if (context.setSelectionMode) {
            context.setSelectionMode(toViewportSelectionMode(context.modeling.selectionSummary().mode));
        }
    }

    return {
            .handled = true,
            .modalStateChanged = true,
    };
}

} // namespace quader
