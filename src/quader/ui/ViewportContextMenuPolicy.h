#pragma once

#include "quader/math/Types.h"

namespace quader {

struct ViewportContextMenuOpenRequest {
    bool modalToolOwnsViewport = false;
    bool rightPressTracked = false;
    bool rightReleased = false;
    bool releaseInsideViewport = false;
    bool targetHit = false;
    bool hasActions = false;
    Vec2 pressScreen = {};
    Vec2 releaseScreen = {};
    Vec2 accumulatedDragDelta = {};
    float maxClickDistanceSquared = 16.0f;
};

struct ViewportContextMenuHoverRequest {
    bool rightPressTracked = false;
    bool flyHudActive = false;
    Vec2 accumulatedDragDelta = {};
    float maxClickDistanceSquared = 16.0f;
};

[[nodiscard]] bool isViewportContextMenuClickGesture(const ViewportContextMenuOpenRequest& request);
[[nodiscard]] bool shouldOpenViewportContextMenu(const ViewportContextMenuOpenRequest& request);
[[nodiscard]] bool shouldPreserveViewportContextMenuHover(const ViewportContextMenuHoverRequest& request);

} // namespace quader
