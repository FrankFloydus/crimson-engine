////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/viewport_selection_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>

namespace quader {
namespace {

quader_editor_core::SelectionEditOperation selectionBoxOperation(QuaderSelectionEdit edit) {
    switch (edit) {
    case QuaderSelectionEdit::Add:
        return quader_editor_core::SelectionEditOperation::add;
    case QuaderSelectionEdit::Remove:
        return quader_editor_core::SelectionEditOperation::remove;
    case QuaderSelectionEdit::Toggle:
        return quader_editor_core::SelectionEditOperation::toggle;
    case QuaderSelectionEdit::Replace:
        return quader_editor_core::SelectionEditOperation::replace;
    }
    return quader_editor_core::SelectionEditOperation::replace;
}

QuaderSelectionEdit selectionEditFromModifiers(ViewportModifiers modifiers) {
    if (modifiers.primary) {
        return QuaderSelectionEdit::Remove;
    }
    if (modifiers.shift) {
        return QuaderSelectionEdit::Add;
    }
    return QuaderSelectionEdit::Replace;
}

ViewportSelectionMarquee boxMarquee(QVec2 start, QVec2 end) {
    return {
            .active = true,
            .start = start,
            .end = end,
            .rule = resolveBoxSelectionRule(ModelingBoxSelectionRule::DragDirection, start, end),
    };
}

bool sameMarquee(const ViewportSelectionMarquee& left, const ViewportSelectionMarquee& right) {
    return left.active == right.active &&
            left.rule == right.rule &&
            left.start.x == right.start.x &&
            left.start.y == right.start.y &&
            left.end.x == right.end.x &&
            left.end.y == right.end.y;
}

bool liveBoxSelectionAllowed(QuaderSelectionEdit edit, QuaderSelectionMode mode) {
    (void)edit;
    (void)mode;
    return true;
}

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
        bool preserveSelectionOnSelectedHit,
        bool preserveExistingComponentSelectionOnHit = false) {
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
            .preserveExistingComponentSelectionOnHit = preserveExistingComponentSelectionOnHit,
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

QuaderBoxSelectRequest makeBoxSelectRequest(
        const ViewportCameraController& camera,
        QVec2 viewportSize,
        QVec2 start,
        QVec2 end,
        QuaderSelectionEdit edit) {
    return {
            .camera = toApiViewportCamera(ViewportCameraAdapter::modelingCamera(camera, viewportSize)),
            .start = start,
            .end = end,
            .edit = edit,
            .rule = QuaderBoxSelectionRule::DragDirection,
    };
}

QuaderSelectionSummary pickSelection(
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        const ViewportInteractionFrame& frame,
        QVec2 localMouse,
        int clickCount,
        bool preserveSelectionOnMiss = false,
        bool preserveSelectionOnSelectedHit = false,
        bool preserveExistingComponentSelectionOnHit = false) {
    const QuaderApiResult<QuaderSelectionSummary> result =
            editor.selection().pickSelect(makePickSelectRequest(
                    frame.camera,
                    frame.settings,
                    localMouse,
                    frame.input.modifiers,
                    clickCount,
                    preserveSelectionOnMiss,
                    preserveSelectionOnSelectedHit,
                    preserveExistingComponentSelectionOnHit));
    if (!result.ok) {
        LOG_ERROR("Viewport selection failed: {}", result.message.empty() ? "Selection failed." : result.message);
    }
    const QuaderSelectionSummary summary = result.ok ? result.value() : editor.selection().summary();
    inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(summary.mode)));
    return summary;
}

QuaderSelectionSummary boxSelection(
        QuaderEditorApi& editor,
        ViewportInputRouter& inputRouter,
        const ViewportInteractionFrame& frame,
        QVec2 start,
        QVec2 end,
        QuaderSelectionEdit edit,
        const QuaderSelectionBoxBaseline* baseline = nullptr) {
    const QuaderBoxSelectRequest request = makeBoxSelectRequest(
            frame.camera,
            frame.input.viewportSize,
            start,
            end,
            edit);
    const QuaderApiResult<QuaderSelectionSummary> result = baseline != nullptr && baseline->valid()
            ? editor.selection().boxSelect(request, *baseline)
            : editor.selection().boxSelect(request);
    if (!result.ok) {
        LOG_ERROR("Viewport box selection failed: {}", result.message.empty() ? "Box selection failed." : result.message);
    }
    const QuaderSelectionSummary summary = result.ok ? result.value() : editor.selection().summary();
    inputRouter.setSelectionMode(appSelectionModeToViewport(toModelingSelectionMode(summary.mode)));
    return summary;
}

} // namespace

void ViewportSelectionInteraction::reset() {
    hoverClearIssued_ = false;
    quader_editor_core::clear_selection_box(boxState_);
    boxEdit_ = QuaderSelectionEdit::Replace;
    boxHoveredBeforeSelection_.reset();
    boxBaseline_.reset();
    committedMarquee_.reset();
    liveBoxSelection_.reset();
    hoverSuppressedTarget_.reset();
    paintSelectionActive_ = false;
}

ViewportSelectionMarquee ViewportSelectionInteraction::marquee() const {
    if (!boxState_.active || !boxState_.dragging) {
        return committedMarquee_.value_or(ViewportSelectionMarquee{});
    }
    return boxMarquee(boxState_.start, boxState_.end);
}

void ViewportSelectionInteraction::handleFrame(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame,
        const ViewportToolResult& toolResult,
        bool gizmoConsumed) {
    committedMarquee_.reset();
    const bool selectionFallbackActive = frame.inputRouter.activeToolAllowsSelectionFallback();
    const bool suppressHoverWork =
            frame.inputResult.navigationActive ||
            frame.inputResult.cameraChanged ||
            frame.input.textInputActive ||
            frame.input.modalToolActive ||
            frame.input.activeToolOwnsViewport ||
            gizmoConsumed;
    const bool canUseSelectionFallback =
            selectionFallbackActive &&
            !frame.input.textInputActive &&
            !frame.inputResult.navigationActive &&
            !frame.inputResult.cameraChanged &&
            !gizmoConsumed &&
            !toolResult.handled;
    const bool canPick = canUseSelectionFallback && frame.input.viewportHovered;

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

    if (!boxState_.active && canUseSelectionFallback && frame.leftPressed) {
        boxEdit_ = selectionEditFromModifiers(frame.input.modifiers);
        boxHoveredBeforeSelection_ = editor.selection().hoverSummary().target;
        boxBaseline_ = editor.selection().captureBoxSelectionBaseline();
        liveBoxSelection_.reset();
        quader_editor_core::begin_selection_box(
                boxState_,
                frame.leftClickLocal,
                selectionBoxOperation(boxEdit_));
    }

    if (boxState_.active && frame.leftDown) {
        quader_editor_core::update_selection_box(boxState_, frame.input.mouseLocal);
        if (boxState_.dragging) {
            const ViewportSelectionMarquee liveMarquee = boxMarquee(boxState_.start, boxState_.end);
            if (liveBoxSelectionAllowed(boxEdit_, editor.selection().summary().mode) &&
                    (!liveBoxSelection_.has_value() || !sameMarquee(*liveBoxSelection_, liveMarquee))) {
                (void)boxSelection(
                        editor,
                        frame.inputRouter,
                        frame,
                        liveMarquee.start,
                        liveMarquee.end,
                        boxEdit_,
                        boxBaseline_ ? &*boxBaseline_ : nullptr);
                liveBoxSelection_ = liveMarquee;
            }
            clearHover();
        }
    }

    if (boxState_.active && (frame.leftReleased || !frame.leftDown)) {
        const quader_editor_core::SelectionBoxCommit commit =
                quader_editor_core::finish_selection_box(
                        boxState_,
                        frame.input.mouseLocal,
                        selectionBoxOperation(boxEdit_));
        const QuaderHoverTarget hoveredBeforeSelection =
                boxHoveredBeforeSelection_.value_or(editor.selection().hoverSummary().target);
        if (commit.dragged) {
            committedMarquee_ = boxMarquee(commit.start, commit.end);
            if (!liveBoxSelection_.has_value() || !sameMarquee(*liveBoxSelection_, *committedMarquee_)) {
                (void)boxSelection(
                        editor,
                        frame.inputRouter,
                        frame,
                        commit.start,
                        commit.end,
                        boxEdit_,
                        boxBaseline_ ? &*boxBaseline_ : nullptr);
            }
            suppressHoverAfterSelection(commit.end, hoveredBeforeSelection);
        } else if (canPick) {
            const QuaderSelectionSummary summary = pickSelection(
                    editor,
                    frame.inputRouter,
                    frame,
                    frame.leftClickLocal,
                    std::max(1, frame.leftClickCount));
            paintSelectionActive_ = componentSelectionMode(summary.mode);
            suppressHoverAfterSelection(frame.leftClickLocal, hoveredBeforeSelection);
        }
        boxEdit_ = QuaderSelectionEdit::Replace;
        boxHoveredBeforeSelection_.reset();
        boxBaseline_.reset();
        liveBoxSelection_.reset();
    }

    if (frame.leftReleased || !frame.leftDown) {
        if (boxState_.active) {
            quader_editor_core::clear_selection_box(boxState_);
        }
        paintSelectionActive_ = false;
    }

    if (!boxState_.active && canPick && paintSelectionActive_ && frame.leftDown) {
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

    const bool boxDragging = boxState_.active && boxState_.dragging;
    const bool groupSelectionMode = editor.selection().summary().mode == QuaderSelectionMode::Group;
    if (groupSelectionMode && !boxDragging && !frame.leftDown) {
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
            !boxDragging &&
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

    const bool shouldClearHover =
            !frame.input.viewportHovered ||
            !selectionFallbackActive ||
            frame.input.textInputActive ||
            frame.input.modalToolActive ||
            frame.input.activeToolOwnsViewport;
    if (!hoverClearIssued_ && shouldClearHover) {
        hoverSuppressedTarget_.reset();
        clearHover();
    }
}

QuaderSelectionSummary ViewportSelectionInteraction::pickContextMenuTarget(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame) {
    const QuaderSelectionSummary summary = pickSelection(
            editor,
            frame.inputRouter,
            frame,
            frame.input.mouseLocal,
            1,
            true,
            true,
            true);
    paintSelectionActive_ = componentSelectionMode(summary.mode);
    hoverClearIssued_ = false;
    hoverSuppressedTarget_.reset();
    return summary;
}

} // namespace quader
