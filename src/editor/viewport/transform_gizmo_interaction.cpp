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
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <algorithm>

namespace quader {
namespace {

bool componentSelectionMode(QuaderSelectionMode mode) {
    return mode == QuaderSelectionMode::Vertex ||
            mode == QuaderSelectionMode::Edge ||
            mode == QuaderSelectionMode::Face;
}

quader_editor_core::ViewportInteractionTool runtimeToolFor(ViewportToolId tool) {
    switch (tool) {
    case ViewportToolId::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ViewportToolId::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ViewportToolId::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    case ViewportToolId::Select:
    case ViewportToolId::Extend:
    case ViewportToolId::Extrude:
    case ViewportToolId::Box:
    case ViewportToolId::InsertEdgeLoop:
    case ViewportToolId::Knife:
    case ViewportToolId::Cut:
    case ViewportToolId::Poly:
    case ViewportToolId::Mirror:
        return quader_editor_core::ViewportInteractionTool::select;
    }
    return quader_editor_core::ViewportInteractionTool::select;
}

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
    input.tool = runtimeToolFor(tool);
    input.active_axis = activeAxis;
    input.hover_axis = hoverAxis;
    input.has_selection = true;
    input.has_pivot = true;
    input.pivot = pivot;
    input.camera = ViewportCameraAdapter::runtimeCamera(frame.camera, frame.input.viewportSize);
    input.grid_size = viewportGridWorldSizeForPreset(frame.inputRouter.state().gridPreset);
    input.gizmo_size_pixels = 68.0;
    input.pick_radius_pixels = 12.0;
    input.gizmo_scale = std::max(frame.settings.floatValue(ViewportFloatSettingId::GizmoSize), 0.1f);
    input.ring_segments = 64;
    return input;
}

QuaderTransformOperation transformOperationFor(
        ViewportToolId tool,
        QuaderSelectionMode mode,
        ViewportModifiers modifiers) {
    if (!modifiers.shift || !componentSelectionMode(mode)) {
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

} // namespace

ViewportBeforeToolInteractionResult TransformGizmoInteraction::handleFrame(
        QuaderEditorApi& editor,
        const ViewportInteractionFrame& frame) {
    ViewportBeforeToolInteractionResult result;

    const ViewportToolId tool = frame.inputRouter.state().activeTool;
    const QuaderSelectionSummary selection = editor.query().selection();
    const std::optional<QVec3> pivot = editor.query().selectionCenter();
    if (!viewportToolSupportsTransformGizmo(tool) || !selection.hasSelection || !pivot.has_value()) {
        clearFrame();
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
            makeGizmoInput(frame, tool, *pivot, dragging_ ? activeAxis_ : quader_editor_core::TransformAxis::none, hoverAxis_);

    if (!dragging_ && frame.input.viewportHovered && !suppressHover) {
        const quader_editor_core::TransformGizmoPickHit hover =
                quader_editor_core::pick_transform_gizmo(gizmoInput, frame.input.mouseLocal);
        hoverAxis_ = hover.hit ? hover.axis : quader_editor_core::TransformAxis::none;
        gizmoInput.hover_axis = hoverAxis_;
    } else if (!dragging_) {
        hoverAxis_ = quader_editor_core::TransformAxis::none;
        gizmoInput.hover_axis = hoverAxis_;
    }

    currentFrame_ = quader_editor_core::build_transform_gizmo_frame(gizmoInput);

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
        const QuaderApiResult<QuaderTransformDragPreview> preview =
                editor.modeling().updateTransformDrag({
                        .screenPosition = frame.input.mouseLocal,
                        .ray = {
                                .origin = dragRay.origin,
                                .direction = dragRay.direction,
                        },
                        .camera = toApiViewportCamera(ViewportCameraAdapter::modelingCamera(frame.camera, frame.input.viewportSize)),
                        .gridSize = viewportGridWorldSizeForPreset(frame.inputRouter.state().gridPreset),
                        .vertexSnapRadius = frame.settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                        .precision = frame.input.modifiers.primary,
                });
        if (!preview.ok) {
            LOG_ERROR("Transform drag update failed: {}", preview.message.empty() ? "Update failed." : preview.message);
        }
        return result;
    }

    if (!frame.leftPressed || !frame.input.viewportHovered || suppressHover) {
        return result;
    }

    const quader_editor_core::TransformGizmoPickHit hit =
            quader_editor_core::pick_transform_gizmo(gizmoInput, frame.leftClickLocal);
    if (!hit.hit || hit.axis == quader_editor_core::TransformAxis::none) {
        return result;
    }

    const ViewportRay startRay = frame.camera.rayFromViewportPoint(frame.leftClickLocal);
    const bool duplicateSelected = !componentSelectionMode(selection.mode) && frame.input.modifiers.shift;
    const QuaderApiResult<QuaderTransformDragPreview> begin =
            editor.modeling().beginTransformDrag({
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
                    .gridSize = viewportGridWorldSizeForPreset(frame.inputRouter.state().gridPreset),
                    .vertexSnapRadius = frame.settings.floatValue(ViewportFloatSettingId::PickVertexRadius),
                    .duplicateSelected = duplicateSelected,
            });
    if (!begin.ok) {
        LOG_ERROR("Transform drag begin failed: {}", begin.message.empty() ? "Begin failed." : begin.message);
        return result;
    }

    dragging_ = begin.value().active;
    activeAxis_ = dragging_ ? hit.axis : quader_editor_core::TransformAxis::none;
    result.gizmoConsumed = dragging_;
    return result;
}

bool TransformGizmoInteraction::cancelActiveDrag(QuaderEditorApi& editor) {
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

const quader_editor_core::TransformGizmoFrame* TransformGizmoInteraction::frame() const {
    if (!currentFrame_.has_value() || !currentFrame_->info.ok) {
        return nullptr;
    }
    return &*currentFrame_;
}

void TransformGizmoInteraction::resetDragState() {
    dragging_ = false;
    activeAxis_ = quader_editor_core::TransformAxis::none;
}

void TransformGizmoInteraction::clearFrame() {
    currentFrame_.reset();
    hoverAxis_ = quader_editor_core::TransformAxis::none;
}

} // namespace quader
