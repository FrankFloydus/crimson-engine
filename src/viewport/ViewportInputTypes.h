////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderViewportInputTypes.h"
#include "editor/input/QuaderInputBinding.h"
#include "math/Types.h"

#include <vector>

namespace quader {

/**
 * Stores per-frame semantic viewport navigation input resolved from raw bindings.
 */
struct ViewportNavigationInput {
    bool orbitPressed = false;
    bool orbitDown = false;
    bool orbitReleased = false;
    bool orbitSnapPressed = false;
    bool orbitSnapDown = false;
    bool orbitSnapReleased = false;
    bool panPressed = false;
    bool panDown = false;
    bool panReleased = false;
    bool flyPressed = false;
    bool flyDown = false;
    bool flyReleased = false;
    bool zoomWheel = false;
    bool zoomToCursorWheel = false;
    bool resetFlySpeedPressed = false;
    bool flyForwardDown = false;
    bool flyBackwardDown = false;
    bool flyRightDown = false;
    bool flyLeftDown = false;
};

/**
 * Stores the Viewport Input Frame data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportInputFrame {
    bool viewportHovered = false;
    bool viewportFocused = false;
    bool textInputActive = false;
    bool navigationActive = false;
    bool flyNavigationActive = false;
    bool orbitNavigationActive = false;
    bool panNavigationActive = false;
    bool modalToolActive = false;
    bool activeToolOwnsViewport = false;
    bool modalToolFinalizePressed = false;
    bool modalToolModeCyclePressed = false;
    bool middlePressed = false;
    bool middleDown = false;
    bool middleReleased = false;
    bool rightPressed = false;
    bool rightDown = false;
    bool rightReleased = false;
    float mouseWheel = 0.0f;
    float deltaSeconds = 1.0f / 60.0f;
    QVec2 mouseScreen = {};
    QVec2 mouseDelta = {};
    QVec2 mouseLocal = {};
    QVec2 viewportSize = {};
    ViewportModifiers modifiers = {};
    QVec3 flyLocalDirection = {};
    std::vector<ViewportKey> pressedKeys;
    std::vector<QuaderInputChord> pressedInputChords;
    std::vector<QuaderInputChord> activeInputChords;
    std::vector<QuaderInputChord> releasedInputChords;
    std::vector<QuaderInputChord> wheelInputChords;
    std::vector<ViewportShortcut> resolvedShortcuts;
    ViewportNavigationInput navigationBindings = {};
    bool shortcutResolverActive = false;
};

/**
 * Stores the Viewport Input Result data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportInputResult {
    bool consumed = false;
    bool cameraChanged = false;
    bool shortcutHandled = false;
    bool toolChanged = false;
    bool selectionModeChanged = false;
    bool gridPresetChanged = false;
    bool navigationActive = false;
    bool flyNavigationActive = false;
    bool flyCursorCaptureActive = false;
    bool flyHudActive = false;
    bool flySpeedChanged = false;
    bool flySpeedReset = false;
    float flySpeed = 0.0f;
    bool zoomedToCursor = false;
    bool cancelRequested = false;
    bool focusDocumentRequested = false;
    bool maximizedToggleRequested = false;
    ViewportShortcut shortcut = {};
};

const char* viewportToolId(ViewportToolId tool);
const char* viewportSelectionModeId(ViewportSelectionMode mode);
float viewportGridWorldSizeForPreset(int preset);

} // namespace quader
