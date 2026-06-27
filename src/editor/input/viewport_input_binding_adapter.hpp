////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/quader_input_binding.hpp"
#include "viewport/viewport_input_types.hpp"

#include <optional>

namespace quader {

inline std::optional<QuaderKeyCode> quaderKeyCodeForViewportKey(ViewportKey key) {
    switch (key) {
    case ViewportKey::A:
        return QuaderKeyCode::A;
    case ViewportKey::B:
        return QuaderKeyCode::B;
    case ViewportKey::C:
        return QuaderKeyCode::C;
    case ViewportKey::D:
        return QuaderKeyCode::D;
    case ViewportKey::E:
        return QuaderKeyCode::E;
    case ViewportKey::F:
        return QuaderKeyCode::F;
    case ViewportKey::G:
        return QuaderKeyCode::G;
    case ViewportKey::H:
        return QuaderKeyCode::H;
    case ViewportKey::I:
        return QuaderKeyCode::I;
    case ViewportKey::J:
        return QuaderKeyCode::J;
    case ViewportKey::K:
        return QuaderKeyCode::K;
    case ViewportKey::L:
        return QuaderKeyCode::L;
    case ViewportKey::M:
        return QuaderKeyCode::M;
    case ViewportKey::N:
        return QuaderKeyCode::N;
    case ViewportKey::O:
        return QuaderKeyCode::O;
    case ViewportKey::P:
        return QuaderKeyCode::P;
    case ViewportKey::Q:
        return QuaderKeyCode::Q;
    case ViewportKey::R:
        return QuaderKeyCode::R;
    case ViewportKey::S:
        return QuaderKeyCode::S;
    case ViewportKey::T:
        return QuaderKeyCode::T;
    case ViewportKey::U:
        return QuaderKeyCode::U;
    case ViewportKey::V:
        return QuaderKeyCode::V;
    case ViewportKey::W:
        return QuaderKeyCode::W;
    case ViewportKey::X:
        return QuaderKeyCode::X;
    case ViewportKey::Y:
        return QuaderKeyCode::Y;
    case ViewportKey::Z:
        return QuaderKeyCode::Z;
    case ViewportKey::Number0:
        return QuaderKeyCode::Digit0;
    case ViewportKey::Number1:
        return QuaderKeyCode::Digit1;
    case ViewportKey::Number2:
        return QuaderKeyCode::Digit2;
    case ViewportKey::Number3:
        return QuaderKeyCode::Digit3;
    case ViewportKey::Number4:
        return QuaderKeyCode::Digit4;
    case ViewportKey::Number5:
        return QuaderKeyCode::Digit5;
    case ViewportKey::Number6:
        return QuaderKeyCode::Digit6;
    case ViewportKey::Number7:
        return QuaderKeyCode::Digit7;
    case ViewportKey::Number8:
        return QuaderKeyCode::Digit8;
    case ViewportKey::Number9:
        return QuaderKeyCode::Digit9;
    case ViewportKey::Home:
        return QuaderKeyCode::Home;
    case ViewportKey::End:
        return QuaderKeyCode::End;
    case ViewportKey::PageUp:
        return QuaderKeyCode::PageUp;
    case ViewportKey::PageDown:
        return QuaderKeyCode::PageDown;
    case ViewportKey::Left:
        return QuaderKeyCode::Left;
    case ViewportKey::Right:
        return QuaderKeyCode::Right;
    case ViewportKey::Up:
        return QuaderKeyCode::Up;
    case ViewportKey::Down:
        return QuaderKeyCode::Down;
    case ViewportKey::Space:
        return QuaderKeyCode::Space;
    case ViewportKey::F1:
        return QuaderKeyCode::F1;
    case ViewportKey::F2:
        return QuaderKeyCode::F2;
    case ViewportKey::F3:
        return QuaderKeyCode::F3;
    case ViewportKey::F4:
        return QuaderKeyCode::F4;
    case ViewportKey::F5:
        return QuaderKeyCode::F5;
    case ViewportKey::F6:
        return QuaderKeyCode::F6;
    case ViewportKey::F7:
        return QuaderKeyCode::F7;
    case ViewportKey::F8:
        return QuaderKeyCode::F8;
    case ViewportKey::F9:
        return QuaderKeyCode::F9;
    case ViewportKey::F10:
        return QuaderKeyCode::F10;
    case ViewportKey::F11:
        return QuaderKeyCode::F11;
    case ViewportKey::F12:
        return QuaderKeyCode::F12;
    case ViewportKey::Escape:
        return QuaderKeyCode::Escape;
    case ViewportKey::Delete:
        return QuaderKeyCode::Delete;
    case ViewportKey::Backspace:
        return QuaderKeyCode::Backspace;
    case ViewportKey::Enter:
        return QuaderKeyCode::Enter;
    case ViewportKey::Tab:
        return QuaderKeyCode::Tab;
    }
    return std::nullopt;
}

inline std::optional<ViewportKey> viewportKeyForQuaderKeyCode(QuaderKeyCode key) {
    switch (key) {
    case QuaderKeyCode::A:
        return ViewportKey::A;
    case QuaderKeyCode::B:
        return ViewportKey::B;
    case QuaderKeyCode::C:
        return ViewportKey::C;
    case QuaderKeyCode::D:
        return ViewportKey::D;
    case QuaderKeyCode::E:
        return ViewportKey::E;
    case QuaderKeyCode::F:
        return ViewportKey::F;
    case QuaderKeyCode::G:
        return ViewportKey::G;
    case QuaderKeyCode::H:
        return ViewportKey::H;
    case QuaderKeyCode::I:
        return ViewportKey::I;
    case QuaderKeyCode::J:
        return ViewportKey::J;
    case QuaderKeyCode::K:
        return ViewportKey::K;
    case QuaderKeyCode::L:
        return ViewportKey::L;
    case QuaderKeyCode::M:
        return ViewportKey::M;
    case QuaderKeyCode::N:
        return ViewportKey::N;
    case QuaderKeyCode::O:
        return ViewportKey::O;
    case QuaderKeyCode::P:
        return ViewportKey::P;
    case QuaderKeyCode::Q:
        return ViewportKey::Q;
    case QuaderKeyCode::R:
        return ViewportKey::R;
    case QuaderKeyCode::S:
        return ViewportKey::S;
    case QuaderKeyCode::T:
        return ViewportKey::T;
    case QuaderKeyCode::U:
        return ViewportKey::U;
    case QuaderKeyCode::V:
        return ViewportKey::V;
    case QuaderKeyCode::W:
        return ViewportKey::W;
    case QuaderKeyCode::X:
        return ViewportKey::X;
    case QuaderKeyCode::Y:
        return ViewportKey::Y;
    case QuaderKeyCode::Z:
        return ViewportKey::Z;
    case QuaderKeyCode::Digit0:
    case QuaderKeyCode::Numpad0:
        return ViewportKey::Number0;
    case QuaderKeyCode::Digit1:
    case QuaderKeyCode::Numpad1:
        return ViewportKey::Number1;
    case QuaderKeyCode::Digit2:
    case QuaderKeyCode::Numpad2:
        return ViewportKey::Number2;
    case QuaderKeyCode::Digit3:
    case QuaderKeyCode::Numpad3:
        return ViewportKey::Number3;
    case QuaderKeyCode::Digit4:
    case QuaderKeyCode::Numpad4:
        return ViewportKey::Number4;
    case QuaderKeyCode::Digit5:
    case QuaderKeyCode::Numpad5:
        return ViewportKey::Number5;
    case QuaderKeyCode::Digit6:
    case QuaderKeyCode::Numpad6:
        return ViewportKey::Number6;
    case QuaderKeyCode::Digit7:
    case QuaderKeyCode::Numpad7:
        return ViewportKey::Number7;
    case QuaderKeyCode::Digit8:
    case QuaderKeyCode::Numpad8:
        return ViewportKey::Number8;
    case QuaderKeyCode::Digit9:
    case QuaderKeyCode::Numpad9:
        return ViewportKey::Number9;
    case QuaderKeyCode::Home:
        return ViewportKey::Home;
    case QuaderKeyCode::End:
        return ViewportKey::End;
    case QuaderKeyCode::PageUp:
        return ViewportKey::PageUp;
    case QuaderKeyCode::PageDown:
        return ViewportKey::PageDown;
    case QuaderKeyCode::Left:
        return ViewportKey::Left;
    case QuaderKeyCode::Right:
        return ViewportKey::Right;
    case QuaderKeyCode::Up:
        return ViewportKey::Up;
    case QuaderKeyCode::Down:
        return ViewportKey::Down;
    case QuaderKeyCode::Space:
        return ViewportKey::Space;
    case QuaderKeyCode::F1:
        return ViewportKey::F1;
    case QuaderKeyCode::F2:
        return ViewportKey::F2;
    case QuaderKeyCode::F3:
        return ViewportKey::F3;
    case QuaderKeyCode::F4:
        return ViewportKey::F4;
    case QuaderKeyCode::F5:
        return ViewportKey::F5;
    case QuaderKeyCode::F6:
        return ViewportKey::F6;
    case QuaderKeyCode::F7:
        return ViewportKey::F7;
    case QuaderKeyCode::F8:
        return ViewportKey::F8;
    case QuaderKeyCode::F9:
        return ViewportKey::F9;
    case QuaderKeyCode::F10:
        return ViewportKey::F10;
    case QuaderKeyCode::F11:
        return ViewportKey::F11;
    case QuaderKeyCode::F12:
        return ViewportKey::F12;
    case QuaderKeyCode::Escape:
        return ViewportKey::Escape;
    case QuaderKeyCode::Delete:
        return ViewportKey::Delete;
    case QuaderKeyCode::Backspace:
        return ViewportKey::Backspace;
    case QuaderKeyCode::Enter:
    case QuaderKeyCode::NumpadEnter:
        return ViewportKey::Enter;
    case QuaderKeyCode::Tab:
        return ViewportKey::Tab;
    default:
        return std::nullopt;
    }
}

inline std::optional<QuaderInputChord> quaderInputChordForViewportKey(
        ViewportKey key,
        ViewportModifiers modifiers) {
    const std::optional<QuaderKeyCode> keyCode = quaderKeyCodeForViewportKey(key);
    if (!keyCode) {
        return std::nullopt;
    }
    return QuaderInputChord{
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Keyboard,
            .key = *keyCode,
    };
}

} // namespace quader
