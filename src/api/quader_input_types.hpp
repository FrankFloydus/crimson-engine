////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

namespace quader {

/**
 * Enumerates input device families that can participate in public editor bindings.
 */
enum class QuaderInputDevice {
    None,
    Keyboard,
    Mouse,
};

/**
 * Enumerates canonical keyboard keys understood by public editor input bindings.
 */
enum class QuaderKeyCode {
    None,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Digit0,
    Digit1,
    Digit2,
    Digit3,
    Digit4,
    Digit5,
    Digit6,
    Digit7,
    Digit8,
    Digit9,
    Comma,
    Period,
    Slash,
    Backslash,
    Semicolon,
    Apostrophe,
    LeftBracket,
    RightBracket,
    Minus,
    Equal,
    Grave,
    Escape,
    Tab,
    Backspace,
    Enter,
    Space,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
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
    F13,
    F14,
    F15,
    F16,
    F17,
    F18,
    F19,
    F20,
    F21,
    F22,
    F23,
    F24,
    Numpad0,
    Numpad1,
    Numpad2,
    Numpad3,
    Numpad4,
    Numpad5,
    Numpad6,
    Numpad7,
    Numpad8,
    Numpad9,
    NumpadAdd,
    NumpadSubtract,
    NumpadMultiply,
    NumpadDivide,
    NumpadDecimal,
    NumpadEnter,
};

/**
 * Enumerates canonical mouse buttons understood by public editor input bindings.
 */
enum class QuaderMouseButton {
    None,
    Left,
    Middle,
    Right,
    Back,
    Forward,
    Button6,
    Button7,
    Button8,
};

/**
 * Enumerates pointer gesture shapes that can be bound to public editor actions.
 */
enum class QuaderInputGesture {
    Press,
    DoubleClick,
    Drag,
    Wheel,
    WheelUp,
    WheelDown,
    WheelLeft,
    WheelRight,
};

/**
 * Represents one normalized keyboard or mouse chord used by public editor input bindings.
 */
struct QuaderInputChord {
    bool primary = false;
    bool shift = false;
    bool alt = false;
    QuaderInputDevice device = QuaderInputDevice::None;
    QuaderKeyCode key = QuaderKeyCode::None;
    QuaderMouseButton mouseButton = QuaderMouseButton::None;
    QuaderInputGesture gesture = QuaderInputGesture::Press;

    [[nodiscard]] bool empty() const {
        if (device == QuaderInputDevice::Keyboard) {
            return key == QuaderKeyCode::None;
        }
        if (device == QuaderInputDevice::Mouse) {
            return gesture == QuaderInputGesture::Wheel ||
                    gesture == QuaderInputGesture::WheelUp ||
                    gesture == QuaderInputGesture::WheelDown ||
                    gesture == QuaderInputGesture::WheelLeft ||
                    gesture == QuaderInputGesture::WheelRight
                    ? false
                    : mouseButton == QuaderMouseButton::None;
        }
        return true;
    }
};

inline bool operator==(const QuaderInputChord& left, const QuaderInputChord& right) {
    return left.primary == right.primary &&
            left.shift == right.shift &&
            left.alt == right.alt &&
            left.device == right.device &&
            left.key == right.key &&
            left.mouseButton == right.mouseButton &&
            left.gesture == right.gesture;
}

} // namespace quader
