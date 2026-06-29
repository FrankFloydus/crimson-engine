////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/base/drawn_plane_tool.hpp"

#include "core/logger.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace quader {
namespace {

void handleResult(ViewportToolContext& context, std::string_view label, const ModelingCommandResult& command) {
    if (context.handleToolCommandResult) {
        context.handleToolCommandResult(label, command);
    }
}

void finishDrawnPlaneTool(ViewportToolContext& context) {
    if (context.setSelectionMode) {
        context.setSelectionMode(appSelectionModeToViewport(context.modeling.selectionSummary().mode));
    }
}

/**
 * Distinguishes drawn-plane tools that share the same viewport lifecycle.
 */
enum class DrawnPlaneToolKind {
    Cut,
    Mirror,
};

DrawnPlaneToolKind drawnPlaneToolKind(ModelingViewportTool tool) {
    return tool == ModelingViewportTool::Cut ? DrawnPlaneToolKind::Cut : DrawnPlaneToolKind::Mirror;
}

std::string_view drawnPlaneToolName(DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? "Cut" : "Mirror";
}

bool& drawnPlaneDragActive(ViewportModalToolState& state, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? state.cutDragActive : state.mirrorDragActive;
}

bool drawnPlaneDragging(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutDragging : status.mirrorDragging;
}

bool drawnPlaneCanPreview(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutCanPreview : status.mirrorCanPreview;
}

bool drawnPlanePreviewValid(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutPreviewValid : status.mirrorPreviewValid;
}

const std::array<QVec3, 3>& drawnPlanePoints(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutPoints : status.mirrorPoints;
}

int drawnPlaneCapturedPointCount(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutCapturedPointCount : status.mirrorCapturedPointCount;
}

bool drawnPlaneHasLine(const ModelingToolStatus& status, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? status.cutHasLine : status.mirrorHasLine;
}

ModelingCommandResult updateDrawnPlaneHover(
        ModelingSessionService& modeling,
        DrawnPlaneToolKind kind,
        const ModelingToolRayEvent& event) {
    return kind == DrawnPlaneToolKind::Cut
            ? modeling.updateCutHover(event)
            : modeling.updateMirrorHover(event);
}

ModelingCommandResult beginDrawnPlanePointDrag(
        ModelingSessionService& modeling,
        DrawnPlaneToolKind kind,
        int pointIndex) {
    return kind == DrawnPlaneToolKind::Cut
            ? modeling.beginCutPointDrag(pointIndex)
            : modeling.beginMirrorPointDrag(pointIndex);
}

ModelingCommandResult beginDrawnPlaneLineDrag(
        ModelingSessionService& modeling,
        DrawnPlaneToolKind kind,
        const ModelingToolRayEvent& event) {
    return kind == DrawnPlaneToolKind::Cut
            ? modeling.beginCutLineDrag(event)
            : modeling.beginMirrorLineDrag(event);
}

ModelingCommandResult clickDrawnPlanePoint(
        ModelingSessionService& modeling,
        DrawnPlaneToolKind kind,
        const ModelingToolRayEvent& event) {
    return kind == DrawnPlaneToolKind::Cut
            ? modeling.clickCut(event)
            : modeling.clickMirror(event);
}

ModelingCommandResult endDrawnPlaneDrag(ModelingSessionService& modeling, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? modeling.endCutDrag() : modeling.endMirrorDrag();
}

ModelingCommandResult finalizeDrawnPlaneTool(ModelingSessionService& modeling, DrawnPlaneToolKind kind) {
    return kind == DrawnPlaneToolKind::Cut ? modeling.finalizeCutTool() : modeling.finalizeMirrorTool();
}

void logDrawnPlaneCommitBlocked(DrawnPlaneToolKind kind, bool dragging) {
    if (kind == DrawnPlaneToolKind::Cut) {
        if (dragging) {
            LOG_ERROR("Cut commit failed: finish the current drag before committing.");
        } else {
            LOG_ERROR("Cut commit failed: place a valid cut line first.");
        }
        return;
    }
    if (dragging) {
        LOG_ERROR("Mirror commit failed: finish the current drag before committing.");
    } else {
        LOG_ERROR("Mirror commit failed: place a valid mirror line first.");
    }
}

ViewportToolResult handleDrawnPlaneLifecycle(
        ViewportToolContext& context,
        const ViewportPointerToolEvent& pointer,
        ViewportPointToolContext& pointContext,
        DrawnPlaneToolKind kind) {
    bool& dragActive = drawnPlaneDragActive(*context.modalState, kind);
    const std::string_view toolName = drawnPlaneToolName(kind);

    if (pointer.canInteract &&
            !pointer.suppressHoverWork &&
            !(pointer.leftDown && dragActive) &&
            (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
        handleResult(context,
                std::string(toolName) + " hover",
                updateDrawnPlaneHover(context.modeling, kind, pointContext.makeEventAt(pointer.mouseLocal, false)));
    }
    if (pointer.canInteract && pointer.leftClicked) {
        const ModelingToolStatus before = context.modeling.toolStatus();
        ModelingCommandResult command;
        if (pointContext.hitDrawnPlaneCapturedPoint) {
            if (const std::optional<int> pointIndex =
                        pointContext.hitDrawnPlaneCapturedPoint(
                                drawnPlanePoints(before, kind),
                                drawnPlaneCapturedPointCount(before, kind))) {
                command = beginDrawnPlanePointDrag(context.modeling, kind, *pointIndex);
                handleResult(context, std::string(toolName) + " point drag begin", command);
            }
        }
        if (!command.ok && command.message.empty() && pointContext.hitDrawnPlaneLine &&
                pointContext.hitDrawnPlaneLine(
                        drawnPlanePoints(before, kind),
                        drawnPlaneCapturedPointCount(before, kind),
                        drawnPlaneHasLine(before, kind))) {
            command = beginDrawnPlaneLineDrag(
                    context.modeling,
                    kind,
                    pointContext.makeEventAt(pointer.leftClickLocal, true));
            handleResult(context, std::string(toolName) + " line drag begin", command);
        } else if (!command.ok && command.message.empty()) {
            command = clickDrawnPlanePoint(
                    context.modeling,
                    kind,
                    pointContext.makeEventAt(pointer.leftClickLocal, false));
            handleResult(context, std::string(toolName) + " point", command);
        }
        dragActive = command.ok && drawnPlaneDragging(context.modeling.toolStatus(), kind);
    } else if (pointer.canInteract &&
            pointer.leftDown &&
            (dragActive || drawnPlaneDragging(context.modeling.toolStatus(), kind))) {
        handleResult(context,
                std::string(toolName) + " drag update",
                updateDrawnPlaneHover(context.modeling, kind, pointContext.makeEventAt(pointer.mouseLocal, true)));
        dragActive = drawnPlaneDragging(context.modeling.toolStatus(), kind);
    }
    if (pointer.leftReleased && (dragActive || drawnPlaneDragging(context.modeling.toolStatus(), kind))) {
        const ModelingCommandResult command = endDrawnPlaneDrag(context.modeling, kind);
        handleResult(context, std::string(toolName) + " drag end", command);
        dragActive = false;
    }
    if (kind == DrawnPlaneToolKind::Cut && pointer.modeCyclePressed) {
        handleResult(context, "Cut mode", context.modeling.cycleCutMode());
    }

    bool committed = false;
    if (pointer.finalizePressed) {
        const ModelingToolStatus status = context.modeling.toolStatus();
        if (drawnPlaneDragging(status, kind)) {
            logDrawnPlaneCommitBlocked(kind, true);
        } else if (!drawnPlaneCanPreview(status, kind) || !drawnPlanePreviewValid(status, kind)) {
            logDrawnPlaneCommitBlocked(kind, false);
        } else {
            const ModelingCommandResult command = finalizeDrawnPlaneTool(context.modeling, kind);
            handleResult(context, std::string(toolName) + " commit", command);
            if (command.ok) {
                finishDrawnPlaneTool(context);
                committed = true;
            }
        }
    }
    return {
            .handled = true,
            .modalStateChanged = true,
            .activeToolChanged = committed,
            .syncActiveToolFromModeling = committed,
    };
}

} // namespace

ViewportToolResult DrawnPlaneTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            context.modalState == nullptr ||
            context.point == nullptr ||
            !context.point->makeEventAt) {
        return ModalViewportTool::handleEvent(context, event);
    }

    ViewportPointToolContext& pointContext = *context.point;
    const ViewportPointerToolEvent& pointer = event.pointer;
    if (modelingTool() == ModelingViewportTool::Cut || modelingTool() == ModelingViewportTool::Mirror) {
        return handleDrawnPlaneLifecycle(
                context,
                pointer,
                pointContext,
                drawnPlaneToolKind(modelingTool()));
    }

    return ModalViewportTool::handleEvent(context, event);
}

} // namespace quader
