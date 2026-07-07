////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

namespace quader {

/**
 * Enumerates viewport tools exposed by the public command and tool APIs.
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
    Pivot,
};

/**
 * Enumerates viewport selection modes exposed by public keybinding dispatch APIs.
 */
enum class ViewportSelectionMode {
    Vertex,
    Edge,
    Face,
    Object,
    Group,
};

/**
 * Enumerates viewport mirror axes exposed by public keybinding dispatch APIs.
 */
enum class ViewportMirrorAxis {
    X,
    Y,
    Z,
};

/**
 * Enumerates viewport keys exposed by public keybinding dispatch APIs.
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
    U,
    V,
    X,
    Y,
    Z,
    E,
    G,
    J,
    K,
    L,
    N,
    O,
    Number0,
    Number1,
    Number2,
    Number3,
    Number4,
    Number5,
    Number6,
    Number7,
    Number8,
    Number9,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
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
    F11,
    F12,
    Escape,
    Delete,
    Backspace,
    Enter,
    Tab,
};

/**
 * Enumerates public viewport shortcut dispatch contexts.
 */
enum class ViewportShortcutContext {
    Global,
    Viewport,
};

/**
 * Enumerates public viewport shortcut dispatch results.
 */
enum class ViewportShortcutKind {
    None,
    ActivateTool,
    SetSelectionMode,
    FocusDocument,
    FocusOrigin,
    ToggleMaximized,
    SetGridPreset,
    SetView,
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
 * Enumerates semantic viewport input actions resolved from public input bindings.
 */
enum class ViewportInputActionKind {
    None,
    Orbit,
    OrbitSnap,
    Pan,
    Fly,
    Zoom,
    ZoomToCursor,
    ResetFlySpeed,
    CancelNavigation,
    FlyForward,
    FlyBackward,
    FlyRight,
    FlyLeft,
    ModalToolFinalize,
    ModalToolModeCycle,
    QuickPivot,
};

/**
 * Represents viewport modifier keys used by public command dispatch APIs.
 */
struct ViewportModifiers {
    bool primary = false;
    bool shift = false;
    bool alt = false;
};

/**
 * Represents a public viewport shortcut dispatch result.
 */
struct ViewportShortcut {
    ViewportShortcutKind kind = ViewportShortcutKind::None;
    ViewportShortcutContext context = ViewportShortcutContext::Viewport;
    ViewportToolId tool = ViewportToolId::Select;
    ViewportSelectionMode selection_mode = ViewportSelectionMode::Object;
    ViewportMirrorAxis mirror_axis = ViewportMirrorAxis::X;
    int grid_preset = 0;
    ViewportView view = ViewportView::Perspective;
};

/**
 * Stores public viewport tool state needed for keybinding dispatch.
 */
struct ViewportToolState {
  ViewportToolId active_tool = ViewportToolId::Select;
  ViewportSelectionMode selection_mode = ViewportSelectionMode::Object;
  int grid_preset = 6;
};

} // namespace quader
