#pragma once

#include "math/Types.h"
#include "modeling/ModelingSessionService.h"

namespace quader::test {

struct CameraSpec {
    Vec3 origin = {0.0f, 0.0f, 5.0f};
    Vec2 viewportSize = {1280.0f, 720.0f};
    double orthographicSize = 12.0;
};

struct PickOptions {
    int clickCount = 1;
    bool controlPressed = false;
    bool shiftPressed = false;
    bool commandPressed = false;
    bool altPressed = false;
    bool preserveSelectionOnMiss = false;
    bool preserveSelectionOnSelectedHit = false;
};

struct HoverOptions {
    bool selectedModifierPressed = false;
};

struct DragBeginOptions {
    ModelingTransformTool tool = ModelingTransformTool::Move;
    ModelingTransformAxis axis = ModelingTransformAxis::X;
    ModelingTransformOperation operation = ModelingTransformOperation::Transform;
    Vec3 pivot = {};
    bool duplicateSelected = false;
};

[[nodiscard]] ModelingViewportCamera orthographicCamera(CameraSpec spec = {});
[[nodiscard]] ModelingRay forwardRay(Vec3 origin = {0.0f, 0.0f, 5.0f});
[[nodiscard]] ModelingPickSelectRequest pickAt(Vec3 origin, PickOptions options = {});
[[nodiscard]] ModelingHoverRequest hoverAt(Vec3 origin, HoverOptions options = {});
[[nodiscard]] ModelingToolRayEvent edgeLoopEvent(Vec3 origin = {1.0f, 0.0f, 5.0f});
[[nodiscard]] ModelingToolRayEvent drawnPlaneEvent(float x, bool dragging = false);
[[nodiscard]] ModelingTransformDragBeginRequest xMoveDragBegin(DragBeginOptions options = {});
[[nodiscard]] ModelingTransformDragUpdateRequest xMoveDragUpdate(float screenX);

} // namespace quader::test
