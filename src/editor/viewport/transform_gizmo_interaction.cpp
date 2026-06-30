////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/viewport/transform_gizmo_interaction.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <editor/interactions/selection_box.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace quader {
namespace {

QuaderTransformAxis apiAxisFor(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return QuaderTransformAxis::X;
    case quader_editor_core::TransformAxis::y:
        return QuaderTransformAxis::Y;
    case quader_editor_core::TransformAxis::z:
        return QuaderTransformAxis::Z;
    case quader_editor_core::TransformAxis::xy:
        return QuaderTransformAxis::XY;
    case quader_editor_core::TransformAxis::xz:
        return QuaderTransformAxis::XZ;
    case quader_editor_core::TransformAxis::yz:
        return QuaderTransformAxis::YZ;
    case quader_editor_core::TransformAxis::all:
        return QuaderTransformAxis::All;
    case quader_editor_core::TransformAxis::none:
        return QuaderTransformAxis::None;
    }
    return QuaderTransformAxis::None;
}

QuaderTransformTool apiTransformToolFor(ViewportToolId tool) {
    switch (viewportToolTransformTool(tool)) {
    case ModelingTransformTool::Move:
        return QuaderTransformTool::Move;
    case ModelingTransformTool::Rotate:
        return QuaderTransformTool::Rotate;
    case ModelingTransformTool::Scale:
        return QuaderTransformTool::Scale;
    }
    return QuaderTransformTool::Move;
}

quader_editor_core::TransformGizmoFrameInput makeGizmoInput(
        const ViewportInteractionFrame& frame,
        ViewportToolId tool,
        QVec3 pivot,
        quader_editor_core::TransformAxis activeAxis,
        quader_editor_core::TransformAxis hoverAxis) {
    quader_editor_core::TransformGizmoFrameInput input;
    input.tool = viewportToolInteractionTool(tool);
    input.active_axis = activeAxis;
    input.hover_axis = hoverAxis;
    input.has_selection = true;
    input.has_pivot = true;
    input.pivot = pivot;
    input.camera = ViewportCameraAdapter::runtimeCamera(frame.camera, frame.input.viewportSize);
    input.grid_size = viewportGridWorldSizeForPreset(frame.input.gridPreset);
    input.gizmo_size_pixels = kOverlayTransformGizmoSizePixels;
    input.pick_radius_pixels = kOverlayTransformGizmoPickRadiusPixels;
    input.gizmo_scale = std::max(frame.settings.floatValue(ViewportFloatSettingId::GizmoSize), 0.1f);
    input.ring_segments = 64;
    return input;
}

QuaderTransformOperation transformOperationFor(
        ViewportToolId tool,
        QuaderSelectionMode mode,
        ViewportModifiers modifiers) {
    if (!modifiers.shift || !isComponentSelectionMode(toModelingSelectionMode(mode))) {
        return QuaderTransformOperation::Transform;
    }
    if (tool == ViewportToolId::Move) {
        return QuaderTransformOperation::Extrude;
    }
    if (tool == ViewportToolId::Scale) {
        return QuaderTransformOperation::Inset;
    }
    return QuaderTransformOperation::Transform;
}

QVec3 projectedOntoPlane(QVec3 value, QVec3 normal) {
    return value - normal * dot(value, normal);
}

std::optional<QVec3> intersectRayPlane(const ViewportRay& ray, QVec3 planePoint, QVec3 planeNormal) {
    const float normalLength = length(planeNormal);
    if (normalLength <= 0.000001f) {
        return std::nullopt;
    }
    const QVec3 normal = planeNormal / normalLength;
    const float denominator = dot(normal, ray.direction);
    if (std::abs(denominator) <= 0.000001f) {
        return std::nullopt;
    }
    const float distance = dot(normal, planePoint - ray.origin) / denominator;
    if (!std::isfinite(distance) || distance < 0.0f) {
        return std::nullopt;
    }
    return ray.origin + ray.direction * distance;
}

QVec3 fallbackRotationPlaneDirection(const ViewportCameraController& camera, QVec3 planeNormal) {
    const ViewportCameraBasis cameraBasis = ViewportCameraAdapter::basis(camera);
    QVec3 direction = projectedOntoPlane(cameraBasis.right, planeNormal);
    if (length(direction) <= 0.000001f) {
        direction = projectedOntoPlane(cameraBasis.up, planeNormal);
    }
    if (length(direction) <= 0.000001f) {
        direction = cross(planeNormal, QVec3{0.0f, 1.0f, 0.0f});
    }
    if (length(direction) <= 0.000001f) {
        direction = cross(planeNormal, QVec3{1.0f, 0.0f, 0.0f});
    }
    const float directionLength = length(direction);
    return directionLength <= 0.000001f ? QVec3{1.0f, 0.0f, 0.0f} : direction / directionLength;
}

QVec3 rotationIndicatorStartDirectionFromMouse(
        const ViewportCameraController& camera,
        QVec2 localMouse,
        QVec3 worldPivot,
        QVec3 planeNormal) {
    const float normalLength = length(planeNormal);
    if (normalLength <= 0.000001f) {
        return {};
    }
    planeNormal = planeNormal / normalLength;

    const ViewportRay ray = camera.rayFromViewportPoint(localMouse);
    std::optional<QVec3> hit = intersectRayPlane(ray, worldPivot, planeNormal);
    if (!hit) {
        const ViewportCameraBasis cameraBasis = ViewportCameraAdapter::basis(camera);
        hit = intersectRayPlane(ray, worldPivot, cameraBasis.forward);
    }
    if (hit) {
        const QVec3 direction = projectedOntoPlane(*hit - worldPivot, planeNormal);
        if (length(direction) > 0.000001f) {
            return normalize(direction);
        }
    }

    return fallbackRotationPlaneDirection(camera, planeNormal);
}

std::optional<QVec3> rotationIndicatorMousePositionFromMouse(
        const ViewportCameraController& camera,
        QVec2 localMouse,
        QVec3 worldPivot,
        QVec3 planeNormal) {
    const ViewportRay ray = camera.rayFromViewportPoint(localMouse);
    const ViewportCameraBasis cameraBasis = ViewportCameraAdapter::basis(camera);
    std::optional<QVec3> hit = intersectRayPlane(ray, worldPivot, cameraBasis.forward);
    if (!hit) {
        const float normalLength = length(planeNormal);
        if (normalLength > 0.000001f) {
            hit = intersectRayPlane(ray, worldPivot, planeNormal / normalLength);
        }
    }
    return hit;
}

double screenDistanceSquared(QVec2 a, QVec2 b) {
    const double dx = static_cast<double>(a.x) - static_cast<double>(b.x);
    const double dy = static_cast<double>(a.y) - static_cast<double>(b.y);
    return dx * dx + dy * dy;
}

} // namespace

bool TransformGizmoInteraction::pendingDragThresholdReached(QVec2 current) const {
    if (!pendingDrag_.has_value()) {
        return false;
    }
    const double threshold = quader_editor_core::selection_box_default_drag_threshold;
    return screenDistanceSquared(current, pendingDrag_->beginRequest.screenPosition) >= threshold * threshold;
}

bool TransformGizmoInteraction::beginPendingDrag(QuaderEditorApi& editor, const ViewportInteractionFrame& frame) {
    QDR_PROFILE_FUNCTION();
    if (!pendingDrag_.has_value()) {
        return false;
    }

    const PendingGizmoDrag pending = *pendingDrag_;
    QuaderTransformDragBeginRequest request = pending.beginRequest;
    request.vertexSnapRadius = 0.0;
    const QuaderApiResult<QuaderTransformDragPreview> begin = editor.modeling().beginTransformDrag(request);
    if (!begin.ok) {
        LOG_ERROR("Transform drag begin failed: {}", begin.message.empty() ? "Begin failed." : begin.message);
        resetPendingDrag();
        return false;
    }

    dragging_ = begin.value().active;
    activeAxis_ = dragging_ ? pending.axis : quader_editor_core::TransformAxis::none;
    activeWorldDirection_ = dragging_ ? pending.beginRequest.worldDirection : QVec3{};
    rotationIndicatorStartDirection_ =
            dragging_ && viewportToolInteractionTool(pending.tool) == quader_editor_core::ViewportInteractionTool::rotate
            ? std::optional<QVec3>(rotationIndicatorStartDirectionFromMouse(
                    frame.camera,
                    pending.beginRequest.screenPosition,
                    pending.beginRequest.pivot,
                    pending.beginRequest.worldDirection))
            : std::nullopt;
    resetPendingDrag();
    return dragging_;
}

ViewportBeforeToolInteractionResult TransformGizmoInteraction::releasePendingClick() {
    ViewportBeforeToolInteractionResult result;
    if (pendingDrag_.has_value()) {
        result.selectionClickRequested = true;
        result.selectionClickLocal = pendingDrag_->clickLocal;
        result.selectionClickCount = pendingDrag_->clickCount;
    }
    resetPendingDrag();
    return result;
}

ViewportBeforeToolInteractionResult TransformGizmoInteraction::handleFrame(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame) {
    QDR_PROFILE_FUNCTION();
    ViewportBeforeToolInteractionResult result;

    const ViewportToolId tool = frame.inputRouter.state().activeTool;
    const QuaderSelectionSummary selection = editor.query().selection();
    const std::optional<QVec3> pivot = editor.query().selectionCenter();
    if (!frame.settings.boolValue(ViewportBoolSettingId::ShowGizmos) ||
            !viewportToolSupportsTransformGizmo(tool) ||
            !selection.hasSelection ||
            !pivot.has_value()) {
        clearFrame();
        resetPendingDrag();
        if (dragging_) {
            result.gizmoConsumed = cancelActiveDrag(editor);
        }
        return result;
    }

    const bool suppressHover =
            frame.inputResult.navigationActive ||
            frame.inputResult.cameraChanged ||
            frame.input.textInputActive ||
            frame.input.modalToolActive ||
            frame.input.activeToolOwnsViewport;

    quader_editor_core::TransformGizmoFrameInput gizmoInput =
            makeGizmoInput(
                    frame,
                    tool,
                    *pivot,
                    dragging_
                            ? activeAxis_
                            : (pendingDrag_.has_value() ? pendingDrag_->axis : quader_editor_core::TransformAxis::none),
                    hoverAxis_);

    if (!dragging_ && !pendingDrag_.has_value() && frame.input.viewportHovered && !suppressHover) {
        QDR_PROFILE_SCOPE("Transform gizmo hover pick");
        const quader_editor_core::TransformGizmoPickHit hover =
                quader_editor_core::pick_transform_gizmo(gizmoInput, frame.input.mouseLocal);
        hoverAxis_ = hover.hit ? hover.axis : quader_editor_core::TransformAxis::none;
        gizmoInput.hover_axis = hoverAxis_;
    } else if (!dragging_ && !pendingDrag_.has_value()) {
        hoverAxis_ = quader_editor_core::TransformAxis::none;
        gizmoInput.hover_axis = hoverAxis_;
    }

    {
        QDR_PROFILE_SCOPE("Transform gizmo build frame");
        currentFrame_ = quader_editor_core::build_transform_gizmo_frame(gizmoInput);
    }

    if (pendingDrag_.has_value()) {
        result.gizmoConsumed = true;
        if (frame.leftReleased || !frame.leftDown) {
            return releasePendingClick();
        }
        if (!frame.input.viewportHovered || suppressHover) {
            resetPendingDrag();
            return result;
        }
        if (!pendingDragThresholdReached(frame.input.mouseLocal)) {
            return result;
        }
        if (!beginPendingDrag(editor, frame)) {
            return result;
        }
        result.gizmoConsumed = true;
    }

    if (dragging_) {
        result.gizmoConsumed = true;
        if (frame.leftReleased || !frame.leftDown) {
            const QuaderApiResult<QuaderTransformDragPreview> commit = editor.modeling().commitTransformDrag();
            if (!commit.ok) {
                LOG_ERROR("Transform drag commit failed: {}", commit.message.empty() ? "Commit failed." : commit.message);
            }
            resetDragState();
            return result;
        }

        const ViewportRay dragRay = frame.camera.rayFromViewportPoint(frame.input.mouseLocal);
        const QuaderApiResult<QuaderTransformDragPreview> preview = [&]() {
            QDR_PROFILE_SCOPE("Transform gizmo update drag");
            return editor.modeling().updateTransformDrag({
                    .screenPosition = frame.input.mouseLocal,
                    .ray = {
                            .origin = dragRay.origin,
                            .direction = dragRay.direction,
                    },
                    .camera = toApiViewportCamera(ViewportCameraAdapter::modelingCamera(frame.camera, frame.input.viewportSize)),
                    .gridSize = viewportGridWorldSizeForPreset(frame.input.gridPreset),
                    .vertexSnapRadius = viewportVertexSnapRadius(frame.settings),
                    .gridSnapEnabled = viewportGridSnapActive(frame.settings, frame.input.modifiers.primary),
                    .precision = frame.input.modifiers.primary,
            });
        }();
        if (!preview.ok) {
            LOG_ERROR("Transform drag update failed: {}", preview.message.empty() ? "Update failed." : preview.message);
        }
        if (preview.ok &&
                preview.value().active &&
                viewportToolInteractionTool(tool) == quader_editor_core::ViewportInteractionTool::rotate &&
                currentFrame_.has_value() &&
                rotationIndicatorStartDirection_.has_value()) {
            const std::optional<QVec3> mousePosition = rotationIndicatorMousePositionFromMouse(
                    frame.camera,
                    frame.input.mouseLocal,
                    currentFrame_->info.pivot,
                    activeWorldDirection_);
            currentFrame_->info.rotate_indicator = {
                    .active = true,
                    .axis = activeAxis_,
                    .world_axis = activeWorldDirection_,
                    .start_direction = *rotationIndicatorStartDirection_,
                    .has_mouse_position = mousePosition.has_value(),
                    .mouse_position = mousePosition.value_or(QVec3{}),
                    .angle_degrees = preview.value().rotateAngleDegrees,
            };
        }
        return result;
    }

    if (!frame.leftPressed || !frame.input.viewportHovered || suppressHover) {
        return result;
    }

    const quader_editor_core::TransformGizmoPickHit hit =
            [&]() {
                QDR_PROFILE_SCOPE("Transform gizmo click pick");
                return quader_editor_core::pick_transform_gizmo(gizmoInput, frame.leftClickLocal);
            }();
    if (!hit.hit || hit.axis == quader_editor_core::TransformAxis::none) {
        return result;
    }

    const ViewportRay startRay = frame.camera.rayFromViewportPoint(frame.leftClickLocal);
    const bool duplicateSelected = !isComponentSelectionMode(toModelingSelectionMode(selection.mode)) &&
            frame.input.modifiers.shift;
    pendingDrag_ = PendingGizmoDrag{
            .beginRequest = {
                    .tool = apiTransformToolFor(tool),
                    .operation = transformOperationFor(tool, selection.mode, frame.input.modifiers),
                    .axis = apiAxisFor(hit.axis),
                    .pivot = *pivot,
                    .screenPosition = frame.leftClickLocal,
                    .screenPivot = currentFrame_->info.screen_pivot,
                    .screenTip = hit.screen_tip,
                    .hasWorldDirection = true,
                    .worldDirection = hit.world_direction,
                    .ray = {
                            .origin = startRay.origin,
                            .direction = startRay.direction,
                    },
                    .camera = toApiViewportCamera(ViewportCameraAdapter::modelingCamera(frame.camera, frame.input.viewportSize)),
                    .gridSize = viewportGridWorldSizeForPreset(frame.input.gridPreset),
                    .vertexSnapRadius = 0.0,
                    .gridSnapEnabled = viewportGridSnapActive(frame.settings, frame.input.modifiers.primary),
                    .precision = frame.input.modifiers.primary,
                    .duplicateSelected = duplicateSelected,
            },
            .tool = tool,
            .axis = hit.axis,
            .clickLocal = frame.leftClickLocal,
            .clickCount = std::max(1, frame.leftClickCount),
    };
    result.gizmoConsumed = true;
    return result;
}

bool TransformGizmoInteraction::cancelActiveDrag(QuaderEditorApi& editor) {
    if (pendingDrag_.has_value()) {
        resetPendingDrag();
        return true;
    }
    if (!dragging_) {
        return false;
    }
    const QuaderApiVoidResult cancel = editor.modeling().cancelTransformDrag();
    if (!cancel.ok) {
        LOG_ERROR("Transform drag cancel failed: {}", cancel.message.empty() ? "Cancel failed." : cancel.message);
    }
    resetDragState();
    return true;
}

void TransformGizmoInteraction::reset(QuaderEditorApi* editor) {
    if (editor != nullptr && dragging_) {
        (void)cancelActiveDrag(*editor);
    } else {
        resetDragState();
    }
    resetPendingDrag();
    clearFrame();
}

const quader_editor_core::TransformGizmoFrame* TransformGizmoInteraction::frame() const {
    if (!currentFrame_.has_value() || !currentFrame_->info.ok) {
        return nullptr;
    }
    return &*currentFrame_;
}

void TransformGizmoInteraction::resetDragState() {
    dragging_ = false;
    activeAxis_ = quader_editor_core::TransformAxis::none;
    activeWorldDirection_ = {};
    rotationIndicatorStartDirection_.reset();
}

void TransformGizmoInteraction::resetPendingDrag() {
    pendingDrag_.reset();
}

void TransformGizmoInteraction::clearFrame() {
    currentFrame_.reset();
    hoverAxis_ = quader_editor_core::TransformAxis::none;
}

} // namespace quader
