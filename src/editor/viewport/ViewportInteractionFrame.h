////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportSettings.h"

namespace quader {

class ViewportCameraController;

/**
 * Stores the editor-owned viewport interaction input for one viewport frame.
 */
struct ViewportInteractionFrame {
    ViewportInputRouter& inputRouter;
    ViewportCameraController& camera;
    const ViewportSettings& settings;
    const ViewportInputFrame& input;
    const ViewportInputResult& inputResult;
    QVec2 leftClickLocal = {};
    int leftClickCount = 1;
    bool leftPressed = false;
    bool leftDown = false;
    bool leftReleased = false;
};

/**
 * Stores pre-tool viewport interaction consumption state.
 */
struct ViewportBeforeToolInteractionResult {
    bool gizmoConsumed = false;
};

} // namespace quader
