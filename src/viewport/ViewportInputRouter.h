////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/Types.h"
#include "editor/modeling/ModelingTypes.h"
#include "viewport/ViewportCameraController.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace quader {

struct ViewportToolContext;
struct ViewportToolEvent;
struct ViewportToolResult;

/**
 * Enumerates ViewportToolId values used by the viewport layer.
 */
enum class ViewportToolId {
    Select,
    Move,
    Extend,
    Extrude,
    Rotate,
    Scale,
    Box,
    InsertEdgeLoop,
    Knife,
    Cut,
    Poly,
    Mirror,
};

/**
 * Enumerates ViewportSelectionMode values used by the viewport layer.
 */
enum class ViewportSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
    Group,
};

/**
 * Enumerates ViewportMirrorAxis values used by the viewport layer.
 */
enum class ViewportMirrorAxis {
    X,
    Y,
    Z,
};

/**
 * Enumerates ViewportKey values used by the viewport layer.
 */
enum class ViewportKey {
    A,
    Q,
    W,
    D,
    H,
    I,
    M,
    R,
    S,
    B,
    C,
    P,
    T,
    V,
    X,
    Y,
    Z,
    Number1,
    Number2,
    Number3,
    Number4,
    Number5,
    Home,
    F,
    Space,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    Escape,
    Delete,
    Backspace,
};

/**
 * Enumerates ViewportShortcutContext values used by the viewport layer.
 */
enum class ViewportShortcutContext {
    Global,
    Viewport,
};

/**
 * Enumerates ViewportShortcutKind values used by the viewport layer.
 */
enum class ViewportShortcutKind {
    None,
    ActivateTool,
    SetSelectionMode,
    FocusDocument,
    FocusOrigin,
    ToggleMaximized,
    SetGridPreset,
    CancelActiveTool,
    DeletePopup,
    DeleteSelection,
    SelectAll,
    InvertSelection,
    Copy,
    Paste,
    Duplicate,
    Undo,
    Redo,
    RepeatLastAction,
    MergeActive,
    MergeCenter,
    MirrorAxis,
    FlipMeshNormals,
    FlipHorizontal,
    FlipVertical,
    ApplyMaterial,
};

/**
 * Represents a Viewport Modifiers value used by the viewport input, layout, and camera layer.
 */
struct ViewportModifiers {
    bool primary = false;
    bool shift = false;
    bool alt = false;
};

/**
 * Represents a Viewport Shortcut value used by the viewport input, layout, and camera layer.
 */
struct ViewportShortcut {
    ViewportShortcutKind kind = ViewportShortcutKind::None;
    ViewportShortcutContext context = ViewportShortcutContext::Viewport;
    ViewportToolId tool = ViewportToolId::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    ViewportMirrorAxis mirrorAxis = ViewportMirrorAxis::X;
    int gridPreset = 0;
};

/**
 * Stores the Viewport Tool State data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportToolState {
    ViewportToolId activeTool = ViewportToolId::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    int gridPreset = 6;
    bool maximized = false;
};

/**
 * Stores the Viewport Input Frame data contract used by the viewport input, layout, and camera layer.
 */
struct ViewportInputFrame {
    bool viewportHovered = false;
    bool viewportFocused = false;
    bool textInputActive = false;
    bool middlePressed = false;
    bool middleDown = false;
    bool middleReleased = false;
    bool rightPressed = false;
    bool rightDown = false;
    bool rightReleased = false;
    float mouseWheel = 0.0f;
    float deltaSeconds = 1.0f / 60.0f;
    Vec2 mouseScreen = {};
    Vec2 mouseDelta = {};
    Vec2 mouseLocal = {};
    Vec2 viewportSize = {};
    ViewportModifiers modifiers = {};
    Vec3 flyLocalDirection = {};
    std::vector<ViewportKey> pressedKeys;
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
    ViewportShortcut shortcut = {};
};

/**
 * Owns Viewport Input Router behavior within the viewport input, layout, and camera layer.
 */
class ViewportInputRouter {
public:
    ViewportInputRouter();
    ~ViewportInputRouter();

    ViewportInputResult handleFrame(const ViewportInputFrame& input, ViewportCameraController& camera);
    void reset();
    void setActiveTool(ViewportToolId tool);
    void setSelectionMode(ViewportSelectionMode mode);
    void setGridPreset(int preset);
    void setMaximized(bool maximized);
    bool activeToolOwnsViewport() const;
    bool activeToolBlocksViewportFocusShortcuts() const;
    bool activeToolAllowsSelectionFallback() const;
    bool hasActiveModalPreview() const;
    const ModelingBoxPreview& boxPreview() const;
    void clearModalToolState();
    void clearModalDragState();
    ViewportToolResult handleActiveToolEvent(
            ViewportToolContext& context,
            const ViewportToolEvent& event);

    const ViewportToolState& state() const { return state_; }
    bool navigating() const { return navigationMode_ != ViewportDragMode::None; }
    bool flyNavigating() const { return navigationMode_ == ViewportDragMode::Fly; }
    bool flyCursorCaptureActive() const { return flyCursorCaptureActive_; }
    bool flyHudActive() const { return flyHudActive_; }
    const class ViewportTool* activeTool() const;

    static bool resolveShortcut(ViewportKey key, ViewportModifiers modifiers, ViewportShortcut& shortcut);

private:
    bool handleShortcut(const ViewportShortcut& shortcut, ViewportCameraController& camera, ViewportInputResult& result);

    ViewportToolState state_;
    ViewportDragMode navigationMode_ = ViewportDragMode::None;
    bool navigationUsesRightButton_ = false;
    bool flyCursorCaptureActive_ = false;
    bool flyHudActive_ = false;
    std::unique_ptr<class ViewportToolRuntime> toolRuntime_;
};

const char* viewportToolId(ViewportToolId tool);
const char* viewportSelectionModeId(ViewportSelectionMode mode);
float viewportGridWorldSizeForPreset(int preset);

} // namespace quader
