#include "viewport/tools/base/PointSequenceTool.h"

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

void finishPointSequenceTool(ViewportToolContext& context, ViewportToolId nextTool) {
    if (context.setActiveTool) {
        context.setActiveTool(nextTool);
    }
    if (context.setSelectionMode) {
        context.setSelectionMode(toViewportSelectionMode(context.modeling.selectionSummary().mode));
    }
}

} // namespace

ViewportToolResult PointSequenceTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            context.modalState == nullptr) {
        return ModalViewportTool::handleEvent(context, event);
    }

    const ViewportPointerToolEvent& pointer = event.pointer;
    switch (modelingTool()) {
    case ModelingViewportTool::Knife:
        if (!context.makeToolEventAt) {
            break;
        }
        if (pointer.canInteract &&
                !pointer.suppressHoverWork &&
                (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
            handleResult(context,
                    "Knife hover",
                    context.modeling.updateKnifeHover(context.makeToolEventAt(pointer.mouseLocal, false)));
        }
        if (pointer.canInteract && pointer.leftClicked) {
            handleResult(context,
                    "Knife point",
                    context.modeling.acceptKnifePoint(context.makeToolEventAt(pointer.mouseLocal, false)));
        }
        if (pointer.finalizePressed) {
            const ModelingCommandResult command = context.modeling.commitKnifeTool();
            handleResult(context, "Knife commit", command);
            if (command.ok) {
                finishPointSequenceTool(context, ViewportToolId::Move);
            }
        }
        return {
                .handled = true,
                .activeToolChanged = pointer.finalizePressed,
        };
    case ModelingViewportTool::Poly:
        if (pointer.canInteract &&
                !pointer.suppressHoverWork &&
                context.makeToolEventAt &&
                (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
            handleResult(context,
                    "Poly hover",
                    context.modeling.updatePolyHover(context.makeToolEventAt(pointer.mouseLocal, false)));
        }
        if (pointer.canInteract && pointer.leftClicked) {
            handleResult(context, "Poly point", context.modeling.acceptPolyPoint());
        }
        if (pointer.finalizePressed) {
            const ModelingCommandResult command = context.modeling.commitPolyTool();
            handleResult(context, "Poly commit", command);
            if (command.ok) {
                finishPointSequenceTool(context, ViewportToolId::Move);
            }
        }
        return {
                .handled = true,
                .activeToolChanged = pointer.finalizePressed,
        };
    case ModelingViewportTool::Select:
    case ModelingViewportTool::Move:
    case ModelingViewportTool::Extend:
    case ModelingViewportTool::Extrude:
    case ModelingViewportTool::Rotate:
    case ModelingViewportTool::Scale:
    case ModelingViewportTool::Box:
    case ModelingViewportTool::InsertEdgeLoop:
    case ModelingViewportTool::Cut:
    case ModelingViewportTool::Mirror:
        break;
    }
    return ModalViewportTool::handleEvent(context, event);
}

} // namespace quader
