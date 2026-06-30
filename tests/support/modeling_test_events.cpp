////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "modeling_test_events.hpp"

namespace quader::test {

ModelingViewportCamera orthographicCamera(CameraSpec spec) {
    return {
            .orthographic = true,
            .origin = spec.origin,
            .xAxis = {1.0f, 0.0f, 0.0f},
            .yAxis = {0.0f, 1.0f, 0.0f},
            .zAxis = {0.0f, 0.0f, 1.0f},
            .viewportSize = spec.viewportSize,
            .orthographicSize = spec.orthographicSize,
            .fovDegrees = 60.0,
    };
}

ModelingRay forwardRay(QVec3 origin) {
    return {
            .origin = origin,
            .direction = {0.0f, 0.0f, -1.0f},
    };
}

ModelingPickSelectRequest pickAt(QVec3 origin, PickOptions options) {
    return {
            .ray = forwardRay(origin),
            .clickCount = options.clickCount,
            .controlPressed = options.controlPressed,
            .shiftPressed = options.shiftPressed,
            .commandPressed = options.commandPressed,
            .altPressed = options.altPressed,
            .preserveSelectionOnMiss = options.preserveSelectionOnMiss,
            .preserveSelectionOnSelectedHit = options.preserveSelectionOnSelectedHit,
    };
}

ModelingHoverRequest hoverAt(QVec3 origin, HoverOptions options) {
    return {
            .ray = forwardRay(origin),
            .selectedModifierPressed = options.selectedModifierPressed,
    };
}

ModelingToolRayEvent edgeLoopEvent(QVec3 origin) {
    return {
            .screenPosition = {},
            .ray = forwardRay(origin),
            .camera = orthographicCamera(),
            .gridSize = 1.0,
            .vertexRadius = 0.3,
            .edgeRadius = 0.2,
            .snapEnabled = false,
    };
}

ModelingToolRayEvent drawnPlaneEvent(float x, bool dragging) {
    ModelingViewportCamera camera = orthographicCamera();
    camera.orthographic = false;
    return {
            .screenPosition = {},
            .ray = {
                    .origin = {x, 2.0f, 0.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .camera = camera,
            .gridSize = 0.5,
            .snapEnabled = true,
            .dragging = dragging,
    };
}

ModelingTransformDragBeginRequest transformDragBegin(DragBeginOptions options) {
    return {
            .tool = options.tool,
            .operation = options.operation,
            .axis = options.axis,
            .pivot = options.pivot,
            .screenPosition = options.screenPosition,
            .screenPivot = options.screenPivot,
            .screenTip = options.screenTip,
            .hasWorldDirection = options.hasWorldDirection,
            .worldDirection = options.worldDirection,
            .ray = forwardRay(),
            .camera = orthographicCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = options.gridSnapEnabled,
            .duplicateSelected = options.duplicateSelected,
    };
}

ModelingTransformDragUpdateRequest transformDragUpdate(QVec2 screenPosition) {
    return {
            .screenPosition = screenPosition,
            .ray = forwardRay(),
            .camera = orthographicCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = true,
            .precision = false,
    };
}

ModelingTransformDragUpdateRequest transformDragUpdate(float screenX) {
    return transformDragUpdate(QVec2{screenX, 0.0f});
}

ModelingTransformDragBeginRequest xMoveDragBegin(DragBeginOptions options) {
    return transformDragBegin(options);
}

ModelingTransformDragUpdateRequest xMoveDragUpdate(float screenX) {
    return transformDragUpdate(screenX);
}

} // namespace quader::test
