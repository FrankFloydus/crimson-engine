#include "ui/ViewportContextMenuPolicy.h"

namespace quader {

bool isViewportContextMenuClickGesture(const ViewportContextMenuOpenRequest& request) {
    if (request.modalToolOwnsViewport ||
            !request.rightPressTracked ||
            !request.rightReleased ||
            !request.releaseInsideViewport) {
        return false;
    }

    const Vec2 delta = request.releaseScreen - request.pressScreen;
    const float releaseDistanceSquared = delta.x * delta.x + delta.y * delta.y;
    const float dragDistanceSquared =
            request.accumulatedDragDelta.x * request.accumulatedDragDelta.x +
            request.accumulatedDragDelta.y * request.accumulatedDragDelta.y;
    return releaseDistanceSquared <= request.maxClickDistanceSquared &&
            dragDistanceSquared <= request.maxClickDistanceSquared;
}

bool shouldOpenViewportContextMenu(const ViewportContextMenuOpenRequest& request) {
    return isViewportContextMenuClickGesture(request) && request.targetHit && request.hasActions;
}

bool shouldPreserveViewportContextMenuHover(const ViewportContextMenuHoverRequest& request) {
    if (!request.rightPressTracked || request.flyHudActive) {
        return false;
    }

    const float dragDistanceSquared =
            request.accumulatedDragDelta.x * request.accumulatedDragDelta.x +
            request.accumulatedDragDelta.y * request.accumulatedDragDelta.y;
    return dragDistanceSquared <= request.maxClickDistanceSquared;
}

} // namespace quader
