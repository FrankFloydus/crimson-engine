#pragma once

#include "quader/math/Types.h"
#include "quader/viewport/ViewportCameraController.h"

#include <cstddef>
#include <vector>

namespace quader {

enum class ViewportTool {
    Select,
    Move,
    Rotate,
    Scale,
    Box,
    InsertEdgeLoop,
    Knife,
    Cut,
    Poly,
    Mirror,
};

enum class ViewportSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
};

enum class ViewportMirrorAxis {
    X,
    Y,
    Z,
};

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

enum class ViewportShortcutContext {
    Global,
    Viewport,
};

enum class ViewportShortcutKind {
    None,
    ActivateTool,
    SetSelectionMode,
    FocusDocument,
    FocusOrigin,
    ToggleMaximized,
    SetGridPreset,
    CancelActiveTool,
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
    FlipHorizontal,
    FlipVertical,
    ApplyMaterial,
};

struct ViewportModifiers {
    bool primary = false;
    bool shift = false;
    bool alt = false;
};

struct ViewportShortcut {
    ViewportShortcutKind kind = ViewportShortcutKind::None;
    ViewportShortcutContext context = ViewportShortcutContext::Viewport;
    ViewportTool tool = ViewportTool::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    ViewportMirrorAxis mirrorAxis = ViewportMirrorAxis::X;
    int gridPreset = 0;
};

struct ViewportToolState {
    ViewportTool activeTool = ViewportTool::Select;
    ViewportSelectionMode selectionMode = ViewportSelectionMode::Object;
    int gridPreset = 6;
    bool maximized = false;
};

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

class ViewportInputRouter {
public:
    ViewportInputRouter();

    ViewportInputResult handleFrame(const ViewportInputFrame& input, ViewportCameraController& camera);
    void reset();
    void setActiveTool(ViewportTool tool);
    void setSelectionMode(ViewportSelectionMode mode);
    void setMaximized(bool maximized);

    const ViewportToolState& state() const { return state_; }
    bool navigating() const { return navigationMode_ != ViewportDragMode::None; }
    bool flyNavigating() const { return navigationMode_ == ViewportDragMode::Fly; }
    bool flyCursorCaptureActive() const { return flyCursorCaptureActive_; }
    bool flyHudActive() const { return flyHudActive_; }

    static bool resolveShortcut(ViewportKey key, ViewportModifiers modifiers, ViewportShortcut& shortcut);

private:
    bool handleShortcut(const ViewportShortcut& shortcut, ViewportCameraController& camera, ViewportInputResult& result);

    ViewportToolState state_;
    ViewportDragMode navigationMode_ = ViewportDragMode::None;
    bool navigationUsesRightButton_ = false;
    bool flyCursorCaptureActive_ = false;
    bool flyHudActive_ = false;
};

const char* viewportToolId(ViewportTool tool);
const char* viewportSelectionModeId(ViewportSelectionMode mode);
float viewportGridWorldSizeForPreset(int preset);

} // namespace quader
