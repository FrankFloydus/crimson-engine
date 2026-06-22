////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/modes/base/DrawnPlaneTool.h"

#include "core/Logger.h"
#include "modeling/services/ModelingSessionService.h"
#include "modeling/services/SelectionModeAdapter.h"

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

} // namespace

ViewportToolResult DrawnPlaneTool::handleEvent(
        ViewportToolContext& context,
        const ViewportToolEvent& event) const {
    if (event.kind != ViewportToolEventKind::Frame ||
            event.tool != viewportTool() ||
            context.modalState == nullptr ||
            !context.makeToolEventAt) {
        return ModalViewportTool::handleEvent(context, event);
    }

    const ViewportPointerToolEvent& pointer = event.pointer;
    if (modelingTool() == ModelingViewportTool::Cut) {
        if (pointer.canInteract &&
                !pointer.suppressHoverWork &&
                !(pointer.leftDown && context.modalState->cutDragActive) &&
                (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
            handleResult(context,
                    "Cut hover",
                    context.modeling.updateCutHover(context.makeToolEventAt(pointer.mouseLocal, false)));
        }
        if (pointer.canInteract && pointer.leftClicked) {
            const ModelingToolStatus before = context.modeling.toolStatus();
            ModelingCommandResult command;
            if (context.hitDrawnPlaneCapturedPoint) {
                if (const std::optional<int> pointIndex =
                            context.hitDrawnPlaneCapturedPoint(before.cutPoints, before.cutCapturedPointCount)) {
                    command = context.modeling.beginCutPointDrag(*pointIndex);
                    handleResult(context, "Cut point drag begin", command);
                }
            }
            if (!command.ok && command.message.empty() && context.hitDrawnPlaneLine &&
                    context.hitDrawnPlaneLine(before.cutPoints, before.cutCapturedPointCount, before.cutHasLine)) {
                command = context.modeling.beginCutLineDrag(context.makeToolEventAt(pointer.leftClickLocal, true));
                handleResult(context, "Cut line drag begin", command);
            } else if (!command.ok && command.message.empty()) {
                command = context.modeling.clickCut(context.makeToolEventAt(pointer.leftClickLocal, false));
                handleResult(context, "Cut point", command);
            }
            context.modalState->cutDragActive = command.ok && context.modeling.toolStatus().cutDragging;
        } else if (pointer.canInteract &&
                pointer.leftDown &&
                (context.modalState->cutDragActive || context.modeling.toolStatus().cutDragging)) {
            handleResult(context,
                    "Cut drag update",
                    context.modeling.updateCutHover(context.makeToolEventAt(pointer.mouseLocal, true)));
            context.modalState->cutDragActive = context.modeling.toolStatus().cutDragging;
        }
        if (pointer.leftReleased && (context.modalState->cutDragActive || context.modeling.toolStatus().cutDragging)) {
            const ModelingCommandResult command = context.modeling.endCutDrag();
            handleResult(context, "Cut drag end", command);
            context.modalState->cutDragActive = false;
        }
        if (pointer.modeCyclePressed) {
            handleResult(context, "Cut mode", context.modeling.cycleCutMode());
        }
        bool committed = false;
        if (pointer.finalizePressed) {
            const ModelingToolStatus status = context.modeling.toolStatus();
            if (status.cutDragging) {
                LOG_ERROR("Cut commit failed: finish the current drag before committing.");
            } else if (!status.cutCanPreview || !status.cutPreviewValid) {
                LOG_ERROR("Cut commit failed: place a valid cut line first.");
            } else {
                const ModelingCommandResult command = context.modeling.finalizeCutTool();
                handleResult(context, "Cut commit", command);
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

    if (modelingTool() == ModelingViewportTool::Mirror) {
        if (pointer.canInteract &&
                !pointer.suppressHoverWork &&
                !(pointer.leftDown && context.modalState->mirrorDragActive) &&
                (!context.claimHover || context.claimHover(ModelingBoxDragState::Idle))) {
            handleResult(context,
                    "Mirror hover",
                    context.modeling.updateMirrorHover(context.makeToolEventAt(pointer.mouseLocal, false)));
        }
        if (pointer.canInteract && pointer.leftClicked) {
            const ModelingToolStatus before = context.modeling.toolStatus();
            ModelingCommandResult command;
            if (context.hitDrawnPlaneCapturedPoint) {
                if (const std::optional<int> pointIndex =
                            context.hitDrawnPlaneCapturedPoint(before.mirrorPoints, before.mirrorCapturedPointCount)) {
                    command = context.modeling.beginMirrorPointDrag(*pointIndex);
                    handleResult(context, "Mirror point drag begin", command);
                }
            }
            if (!command.ok && command.message.empty() && context.hitDrawnPlaneLine &&
                    context.hitDrawnPlaneLine(before.mirrorPoints, before.mirrorCapturedPointCount, before.mirrorHasLine)) {
                command = context.modeling.beginMirrorLineDrag(context.makeToolEventAt(pointer.leftClickLocal, true));
                handleResult(context, "Mirror line drag begin", command);
            } else if (!command.ok && command.message.empty()) {
                command = context.modeling.clickMirror(context.makeToolEventAt(pointer.leftClickLocal, false));
                handleResult(context, "Mirror point", command);
            }
            context.modalState->mirrorDragActive = command.ok && context.modeling.toolStatus().mirrorDragging;
        } else if (pointer.canInteract &&
                pointer.leftDown &&
                (context.modalState->mirrorDragActive || context.modeling.toolStatus().mirrorDragging)) {
            handleResult(context,
                    "Mirror drag update",
                    context.modeling.updateMirrorHover(context.makeToolEventAt(pointer.mouseLocal, true)));
            context.modalState->mirrorDragActive = context.modeling.toolStatus().mirrorDragging;
        }
        if (pointer.leftReleased &&
                (context.modalState->mirrorDragActive || context.modeling.toolStatus().mirrorDragging)) {
            const ModelingCommandResult command = context.modeling.endMirrorDrag();
            handleResult(context, "Mirror drag end", command);
            context.modalState->mirrorDragActive = false;
        }
        bool committed = false;
        if (pointer.finalizePressed) {
            const ModelingToolStatus status = context.modeling.toolStatus();
            if (status.mirrorDragging) {
                LOG_ERROR("Mirror commit failed: finish the current drag before committing.");
            } else if (!status.mirrorCanPreview || !status.mirrorPreviewValid) {
                LOG_ERROR("Mirror commit failed: place a valid mirror line first.");
            } else {
                const ModelingCommandResult command = context.modeling.finalizeMirrorTool();
                handleResult(context, "Mirror commit", command);
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

    return ModalViewportTool::handleEvent(context, event);
}

} // namespace quader
