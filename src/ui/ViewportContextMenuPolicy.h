////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"

namespace quader {

/**
 * Stores the Viewport Context Menu Open Request data contract used by the editor presentation layer.
 */
struct ViewportContextMenuOpenRequest {
    bool modalToolOwnsViewport = false;
    bool rightPressTracked = false;
    bool rightReleased = false;
    bool releaseInsideViewport = false;
    bool targetHit = false;
    bool hasActions = false;
    QVec2 pressScreen = {};
    QVec2 releaseScreen = {};
    QVec2 accumulatedDragDelta = {};
    float maxClickDistanceSquared = 16.0f;
};

/**
 * Stores the Viewport Context Menu Hover Request data contract used by the editor presentation layer.
 */
struct ViewportContextMenuHoverRequest {
    bool rightPressTracked = false;
    bool flyHudActive = false;
    QVec2 accumulatedDragDelta = {};
    float maxClickDistanceSquared = 16.0f;
};

[[nodiscard]] bool isViewportContextMenuClickGesture(const ViewportContextMenuOpenRequest& request);
[[nodiscard]] bool shouldOpenViewportContextMenu(const ViewportContextMenuOpenRequest& request);
[[nodiscard]] bool shouldPreserveViewportContextMenuHover(const ViewportContextMenuHoverRequest& request);

} // namespace quader
