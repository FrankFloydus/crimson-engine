////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/ViewportSelectionInteraction.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderEditorApi.h"
#include "core/Logger.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "editor/tools/ViewportTool.h"
#include "viewport/ViewportCameraController.h"

#include <algorithm>

namespace quader {
namespace {

bool componentSelectionMode(QuaderSelectionMode mode) {
    return mode == QuaderSelectionMode::Vertex ||
            mode == QuaderSelectionMode::Edge ||
            mode == QuaderSelectionMode::Face;
}

bool hoverTargetValid(const QuaderHoverTarget& target) {
    return target.kind != QuaderActiveSelectionKind::None && target.objectId > 0;
}

bool hoverTargetsMatch(const QuaderHoverTarget& left, const QuaderHoverTarget& right) {
    if (left.kind != right.kind || left.objectId != right.objectId) {
        return false;
    }
    switch (left.kind) {
    case QuaderActiveSelectionKind::Object:
        return true;
    case QuaderActiveSelectionKind::Vertex:
        return left.vertexId == right.vertexId;
    case QuaderActiveSelectionKind::Edge:
        return (left.edgeA == right.edgeA && left.edgeB == right.edgeB) ||
                (left.edgeA == right.edgeB && left.edgeB == right.edgeA);
    case QuaderActiveSelectionKind::Face:
        return left.faceId == right.faceId;
    case QuaderActiveSelectionKind::None:
    case QuaderActiveSelectionKind::Light:
        break;
    }
    return false;
}

QuaderPickSelectRequest makePickSelectRequest(
        const ViewportCameraController& camera,
        const ViewportSettings& settings,
        QVec2 localMouse,
        ViewportModifiers modifiers,
        int clickCount,
        bool preserveSelectionOnMiss,
        bool preserveSelectionOnSelectedHit) {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    return {
            .ray = {
                    .origin = worldRay.origin,
                    .direction = worldRay.direction,
            },
            .clickCount = clickCount,
            .controlPressed = modifiers.primary,
            .shiftPressed = modifiers.shift,
            .commandPressed = modifiers.primary,
            .altPressed = modifiers.alt,
            .preserveSelectionOnMiss = preserveSelectionOnMiss,
            .preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit,
            .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
            .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
    };
}

QuaderHoverRequest makeHoverRequest(
        const ViewportCameraController& camera,
        const ViewportSettings& settings,
        QVec2 localMouse,
        ViewportModifiers modifiers) {
    const ViewportRay worldRay = camera.rayFromViewportPoint(localMouse);
    return {
            .ray = {
                    .origin = worldRay.origin,
                    .direction = worldRay.direction,
            },
            .selectedModifierPressed = modifiers.primary,
            .vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
            .edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius),
    };
}

QuaderSelectionSummary pickSelection(
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        const ViewportInteractionFrame& frame,
        QVec2 localMouse,
        int clickCount,
        bool preserveSelectionOnMiss = false,
        bool preserveSelectionOnSelectedHit = false) {
    const QuaderApiResult<QuaderSelectionSummary> result =
            editor.selection().pickSelect(makePickSelectRequest(
                    frame.camera,
                    frame.settings,
                    localMouse,
                    frame.input.modifiers,
                    clickCount,
                    preserveSelectionOnMiss,
                    preserveSelectionOnSelectedHit));
    if (!result.ok) {
        LOG_ERROR("Viewport selection failed: {}", result.message.empty() ? "Selection failed." : result.message);
    }
    const QuaderSelectionSummary summary = result.ok ? result.value() : editor.selection().summary();
    inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(summary.mode)));
    return summary;
}

} // namespace

void ViewportSelectionInteraction::reset() {
    paintSelectionActive_ = false;
    hoverClearIssued_ = false;
    hoverSuppressedTarget_.reset();
}

void ViewportSelectionInteraction::handleFrame(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame,
        const ViewportToolResult& toolResult,
        bool gizmoConsumed) {
    const bool selectionFallbackActive = frame.inputRouter.activeToolAllowsSelectionFallback();
    const bool suppressHoverWork =
            frame.inputResult.navigationActive ||
            frame.inputResult.cameraChanged ||
            frame.input.textInputActive ||
            frame.input.modalToolActive ||
            frame.input.activeToolOwnsViewport ||
            gizmoConsumed;
    const bool canPick =
            selectionFallbackActive &&
            frame.input.viewportHovered &&
            !frame.input.textInputActive &&
            !frame.inputResult.navigationActive &&
            !frame.inputResult.cameraChanged &&
            !gizmoConsumed &&
            !toolResult.handled;

    auto clearHover = [&]() {
        const QuaderApiVoidResult clearHoverResult = editor.selection().clearHover();
        if (!clearHoverResult.ok) {
            LOG_ERROR("Viewport hover clear failed: {}", clearHoverResult.message.empty() ? "Hover clear failed." : clearHoverResult.message);
        }
        hoverClearIssued_ = true;
    };

    auto suppressHoverAfterSelection = [&](QVec2 localMouse, QuaderHoverTarget hoveredBeforeSelection) {
        if (hoverTargetValid(hoveredBeforeSelection)) {
            hoverSuppressedTarget_ = hoveredBeforeSelection;
        } else {
            const QuaderHoverSummary hover =
                    editor.selection().previewHover(makeHoverRequest(
                            frame.camera,
                            frame.settings,
                            localMouse,
                            frame.input.modifiers));
            if (hoverTargetValid(hover.target)) {
                hoverSuppressedTarget_ = hover.target;
            } else {
                hoverSuppressedTarget_.reset();
            }
        }
        clearHover();
    };

    if (canPick && frame.leftPressed) {
        const QuaderHoverTarget hoveredBeforeSelection = editor.selection().hoverSummary().target;
        const QuaderSelectionSummary summary = pickSelection(
                editor,
                frame.inputRouter,
                frame,
                frame.leftClickLocal,
                std::max(1, frame.leftClickCount));
        paintSelectionActive_ = componentSelectionMode(summary.mode);
        suppressHoverAfterSelection(frame.leftClickLocal, hoveredBeforeSelection);
    } else if (canPick && paintSelectionActive_ && frame.leftDown && !frame.leftPressed) {
        const QuaderHoverTarget hoveredBeforeSelection = editor.selection().hoverSummary().target;
        const QuaderSelectionSummary summary = pickSelection(
                editor,
                frame.inputRouter,
                frame,
                frame.input.mouseLocal,
                0);
        paintSelectionActive_ = componentSelectionMode(summary.mode);
        suppressHoverAfterSelection(frame.input.mouseLocal, hoveredBeforeSelection);
    }

    if (frame.leftReleased || !frame.leftDown) {
        paintSelectionActive_ = false;
    }

    const bool groupSelectionMode = editor.selection().summary().mode == QuaderSelectionMode::Group;
    if (groupSelectionMode) {
        hoverSuppressedTarget_.reset();
        if (!hoverClearIssued_) {
            clearHover();
        }
        return;
    }

    const bool canHover =
            selectionFallbackActive &&
            frame.input.viewportHovered &&
            !suppressHoverWork &&
            !paintSelectionActive_ &&
            !frame.leftDown &&
            !toolResult.handled;

    bool hoverSuppressedThisFrame = false;
    if (canHover && hoverSuppressedTarget_.has_value()) {
        const QuaderHoverSummary hover =
                editor.selection().previewHover(makeHoverRequest(
                        frame.camera,
                        frame.settings,
                        frame.input.mouseLocal,
                        frame.input.modifiers));
        if (hoverTargetValid(hover.target) && hoverTargetsMatch(*hoverSuppressedTarget_, hover.target)) {
            if (!frame.input.modifiers.primary) {
                clearHover();
                hoverSuppressedThisFrame = true;
            }
        } else {
            hoverSuppressedTarget_.reset();
        }
    }

    if (canHover && !hoverSuppressedThisFrame) {
        const QuaderApiResult<QuaderHoverSummary> hover =
                editor.selection().hover(makeHoverRequest(
                        frame.camera,
                        frame.settings,
                        frame.input.mouseLocal,
                        frame.input.modifiers));
        if (!hover.ok) {
            LOG_ERROR("Viewport hover failed: {}", hover.message.empty() ? "Hover failed." : hover.message);
        }
        hoverClearIssued_ = false;
        return;
    }

    if (!hoverClearIssued_ && (!frame.input.viewportHovered || !selectionFallbackActive || suppressHoverWork)) {
        hoverSuppressedTarget_.reset();
        clearHover();
    }
}

} // namespace quader
