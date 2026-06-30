////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "editor/modeling/modeling_types.hpp"

namespace quader::test {

/**
 * Represents a Camera Spec value used by the behavior test DSL.
 */
struct CameraSpec {
    QVec3 origin = {0.0f, 0.0f, 5.0f};
    QVec2 viewportSize = {1280.0f, 720.0f};
    double orthographicSize = 12.0;
};

/**
 * Stores the Pick Options data contract used by the behavior test DSL.
 */
struct PickOptions {
    int clickCount = 1;
    bool controlPressed = false;
    bool shiftPressed = false;
    bool commandPressed = false;
    bool altPressed = false;
    bool preserveSelectionOnMiss = false;
    bool preserveSelectionOnSelectedHit = false;
};

/**
 * Stores the Hover Options data contract used by the behavior test DSL.
 */
struct HoverOptions {
    bool selectedModifierPressed = false;
};

/**
 * Stores the Drag Begin Options data contract used by the behavior test DSL.
 */
struct DragBeginOptions {
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformAxis axis = ModelingTransformAxis::X;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    QVec3 pivot = {};
    bool useSelectionCenter = true;
    QVec2 screenPosition = {0.0f, 0.0f};
    QVec2 screenPivot = {0.0f, 0.0f};
    QVec2 screenTip = {10.0f, 0.0f};
    bool hasWorldDirection = true;
    QVec3 worldDirection = {1.0f, 0.0f, 0.0f};
    bool gridSnapEnabled = true;
    bool duplicateSelected = false;
};

[[nodiscard]] ModelingViewportCamera orthographicCamera(CameraSpec spec = {});
[[nodiscard]] ModelingRay forwardRay(QVec3 origin = {0.0f, 0.0f, 5.0f});
[[nodiscard]] ModelingPickSelectRequest pickAt(QVec3 origin, PickOptions options = {});
[[nodiscard]] ModelingHoverRequest hoverAt(QVec3 origin, HoverOptions options = {});
[[nodiscard]] ModelingToolRayEvent edgeLoopEvent(QVec3 origin = {1.0f, 0.0f, 5.0f});
[[nodiscard]] ModelingToolRayEvent drawnPlaneEvent(float x, bool dragging = false);
[[nodiscard]] ModelingTransformDragBeginRequest transformDragBegin(DragBeginOptions options = {});
[[nodiscard]] ModelingTransformDragUpdateRequest transformDragUpdate(QVec2 screenPosition);
[[nodiscard]] ModelingTransformDragUpdateRequest transformDragUpdate(float screenX);
[[nodiscard]] ModelingTransformDragBeginRequest xMoveDragBegin(DragBeginOptions options = {});
[[nodiscard]] ModelingTransformDragUpdateRequest xMoveDragUpdate(float screenX);

} // namespace quader::test
