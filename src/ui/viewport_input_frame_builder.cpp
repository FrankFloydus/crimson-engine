////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_input_frame_builder.hpp"

#include <algorithm>
#include <array>

namespace quader {
namespace {

void addKey(ImGuiKeyboardInputChords& input, ImGuiKey imguiKey, QuaderKeyCode key) {
    if (ImGui::IsKeyPressed(imguiKey, false)) {
        input.pressed.push_back(keyboardInputChord(key, input.modifiers));
    }
    if (ImGui::IsKeyDown(imguiKey)) {
        input.active.push_back(keyboardInputChord(key, input.modifiers));
    }
}

void addKeyPair(
        ImGuiKeyboardInputChords& input,
        ImGuiKey primaryKey,
        QuaderKeyCode primaryCode,
        ImGuiKey keypadKey,
        QuaderKeyCode keypadCode) {
    addKey(input, primaryKey, primaryCode);
    addKey(input, keypadKey, keypadCode);
}

void addWheelChords(ViewportInputFrame& input) {
    if (input.mouseWheel == 0.0f) {
        return;
    }
    input.wheelInputChords.push_back(wheelInputChord(QuaderInputGesture::Wheel, input.modifiers));
    input.wheelInputChords.push_back(wheelInputChord(
            input.mouseWheel > 0.0f ? QuaderInputGesture::WheelUp : QuaderInputGesture::WheelDown,
            input.modifiers));
}

} // namespace

QuaderInputChord keyboardInputChord(QuaderKeyCode key, ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Keyboard,
            .key = key,
    };
}

QuaderInputChord mouseInputChord(
        QuaderMouseButton button,
        QuaderInputGesture gesture,
        ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Mouse,
            .mouseButton = button,
            .gesture = gesture,
    };
}

QuaderInputChord wheelInputChord(QuaderInputGesture gesture, ViewportModifiers modifiers) {
    return {
            .primary = modifiers.primary,
            .shift = modifiers.shift,
            .alt = modifiers.alt,
            .device = QuaderInputDevice::Mouse,
            .gesture = gesture,
    };
}

ImGuiKeyboardInputChords collectImGuiKeyboardInputChords() {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiKeyboardInputChords input;
    input.textInputActive = io.WantTextInput;
    input.modifiers = {
            .primary = io.KeyCtrl || io.KeySuper,
            .shift = io.KeyShift,
            .alt = io.KeyAlt,
    };

    addKey(input, ImGuiKey_A, QuaderKeyCode::A);
    addKey(input, ImGuiKey_B, QuaderKeyCode::B);
    addKey(input, ImGuiKey_C, QuaderKeyCode::C);
    addKey(input, ImGuiKey_D, QuaderKeyCode::D);
    addKey(input, ImGuiKey_E, QuaderKeyCode::E);
    addKey(input, ImGuiKey_F, QuaderKeyCode::F);
    addKey(input, ImGuiKey_G, QuaderKeyCode::G);
    addKey(input, ImGuiKey_H, QuaderKeyCode::H);
    addKey(input, ImGuiKey_I, QuaderKeyCode::I);
    addKey(input, ImGuiKey_J, QuaderKeyCode::J);
    addKey(input, ImGuiKey_K, QuaderKeyCode::K);
    addKey(input, ImGuiKey_L, QuaderKeyCode::L);
    addKey(input, ImGuiKey_M, QuaderKeyCode::M);
    addKey(input, ImGuiKey_N, QuaderKeyCode::N);
    addKey(input, ImGuiKey_O, QuaderKeyCode::O);
    addKey(input, ImGuiKey_P, QuaderKeyCode::P);
    addKey(input, ImGuiKey_Q, QuaderKeyCode::Q);
    addKey(input, ImGuiKey_R, QuaderKeyCode::R);
    addKey(input, ImGuiKey_S, QuaderKeyCode::S);
    addKey(input, ImGuiKey_T, QuaderKeyCode::T);
    addKey(input, ImGuiKey_U, QuaderKeyCode::U);
    addKey(input, ImGuiKey_V, QuaderKeyCode::V);
    addKey(input, ImGuiKey_W, QuaderKeyCode::W);
    addKey(input, ImGuiKey_X, QuaderKeyCode::X);
    addKey(input, ImGuiKey_Y, QuaderKeyCode::Y);
    addKey(input, ImGuiKey_Z, QuaderKeyCode::Z);
    addKeyPair(input, ImGuiKey_0, QuaderKeyCode::Digit0, ImGuiKey_Keypad0, QuaderKeyCode::Numpad0);
    addKeyPair(input, ImGuiKey_1, QuaderKeyCode::Digit1, ImGuiKey_Keypad1, QuaderKeyCode::Numpad1);
    addKeyPair(input, ImGuiKey_2, QuaderKeyCode::Digit2, ImGuiKey_Keypad2, QuaderKeyCode::Numpad2);
    addKeyPair(input, ImGuiKey_3, QuaderKeyCode::Digit3, ImGuiKey_Keypad3, QuaderKeyCode::Numpad3);
    addKeyPair(input, ImGuiKey_4, QuaderKeyCode::Digit4, ImGuiKey_Keypad4, QuaderKeyCode::Numpad4);
    addKeyPair(input, ImGuiKey_5, QuaderKeyCode::Digit5, ImGuiKey_Keypad5, QuaderKeyCode::Numpad5);
    addKeyPair(input, ImGuiKey_6, QuaderKeyCode::Digit6, ImGuiKey_Keypad6, QuaderKeyCode::Numpad6);
    addKeyPair(input, ImGuiKey_7, QuaderKeyCode::Digit7, ImGuiKey_Keypad7, QuaderKeyCode::Numpad7);
    addKeyPair(input, ImGuiKey_8, QuaderKeyCode::Digit8, ImGuiKey_Keypad8, QuaderKeyCode::Numpad8);
    addKeyPair(input, ImGuiKey_9, QuaderKeyCode::Digit9, ImGuiKey_Keypad9, QuaderKeyCode::Numpad9);
    addKey(input, ImGuiKey_KeypadAdd, QuaderKeyCode::NumpadAdd);
    addKey(input, ImGuiKey_KeypadSubtract, QuaderKeyCode::NumpadSubtract);
    addKey(input, ImGuiKey_KeypadMultiply, QuaderKeyCode::NumpadMultiply);
    addKey(input, ImGuiKey_KeypadDivide, QuaderKeyCode::NumpadDivide);
    addKey(input, ImGuiKey_KeypadDecimal, QuaderKeyCode::NumpadDecimal);
    addKey(input, ImGuiKey_Comma, QuaderKeyCode::Comma);
    addKey(input, ImGuiKey_Period, QuaderKeyCode::Period);
    addKey(input, ImGuiKey_Slash, QuaderKeyCode::Slash);
    addKey(input, ImGuiKey_Backslash, QuaderKeyCode::Backslash);
    addKey(input, ImGuiKey_Semicolon, QuaderKeyCode::Semicolon);
    addKey(input, ImGuiKey_Apostrophe, QuaderKeyCode::Apostrophe);
    addKey(input, ImGuiKey_LeftBracket, QuaderKeyCode::LeftBracket);
    addKey(input, ImGuiKey_RightBracket, QuaderKeyCode::RightBracket);
    addKey(input, ImGuiKey_Minus, QuaderKeyCode::Minus);
    addKey(input, ImGuiKey_Equal, QuaderKeyCode::Equal);
    addKey(input, ImGuiKey_GraveAccent, QuaderKeyCode::Grave);
    addKey(input, ImGuiKey_Home, QuaderKeyCode::Home);
    addKey(input, ImGuiKey_End, QuaderKeyCode::End);
    addKey(input, ImGuiKey_PageUp, QuaderKeyCode::PageUp);
    addKey(input, ImGuiKey_PageDown, QuaderKeyCode::PageDown);
    addKey(input, ImGuiKey_LeftArrow, QuaderKeyCode::Left);
    addKey(input, ImGuiKey_RightArrow, QuaderKeyCode::Right);
    addKey(input, ImGuiKey_UpArrow, QuaderKeyCode::Up);
    addKey(input, ImGuiKey_DownArrow, QuaderKeyCode::Down);
    addKey(input, ImGuiKey_Space, QuaderKeyCode::Space);
    addKey(input, ImGuiKey_Enter, QuaderKeyCode::Enter);
    addKey(input, ImGuiKey_KeypadEnter, QuaderKeyCode::NumpadEnter);
    addKey(input, ImGuiKey_Tab, QuaderKeyCode::Tab);
    addKey(input, ImGuiKey_F1, QuaderKeyCode::F1);
    addKey(input, ImGuiKey_F2, QuaderKeyCode::F2);
    addKey(input, ImGuiKey_F3, QuaderKeyCode::F3);
    addKey(input, ImGuiKey_F4, QuaderKeyCode::F4);
    addKey(input, ImGuiKey_F5, QuaderKeyCode::F5);
    addKey(input, ImGuiKey_F6, QuaderKeyCode::F6);
    addKey(input, ImGuiKey_F7, QuaderKeyCode::F7);
    addKey(input, ImGuiKey_F8, QuaderKeyCode::F8);
    addKey(input, ImGuiKey_F9, QuaderKeyCode::F9);
    addKey(input, ImGuiKey_F10, QuaderKeyCode::F10);
    addKey(input, ImGuiKey_F11, QuaderKeyCode::F11);
    addKey(input, ImGuiKey_F12, QuaderKeyCode::F12);
    addKey(input, ImGuiKey_Escape, QuaderKeyCode::Escape);
    addKey(input, ImGuiKey_Delete, QuaderKeyCode::Delete);
    addKey(input, ImGuiKey_Backspace, QuaderKeyCode::Backspace);

    return input;
}

ViewportInputFrame ViewportInputFrameBuilder::build(const ViewportInputFrameBuildRequest& request) const {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiKeyboardInputChords keyboard = collectImGuiKeyboardInputChords();
    ViewportInputFrame input;
    input.viewportHovered = request.hovered;
    input.viewportFocused = request.focused;
    input.textInputActive = keyboard.textInputActive;
    input.middlePressed = (request.hovered || request.routeActiveNavigationInput) && ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    input.middleDown = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.middleReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    input.rightPressed = request.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.rightDown = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.rightReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    input.mouseWheel = (request.hovered || request.routeActiveNavigationInput) ? io.MouseWheel : 0.0f;
    input.deltaSeconds = std::max(request.deltaSeconds, 0.0f);
    input.mouseScreen = {io.MousePos.x, io.MousePos.y};
    input.mouseDelta = request.useCapturedMouseDelta ? request.capturedMouseDelta : QVec2{io.MouseDelta.x, io.MouseDelta.y};
    input.mouseLocal = {io.MousePos.x - request.itemMin.x, io.MousePos.y - request.itemMin.y};
    input.viewportSize = {request.itemSize.x, request.itemSize.y};
    input.modifiers = keyboard.modifiers;

    if (input.middlePressed) {
        middleDragChordDown_ = mouseInputChord(QuaderMouseButton::Middle, QuaderInputGesture::Drag, input.modifiers);
        input.pressedInputChords.push_back(mouseInputChord(QuaderMouseButton::Middle, QuaderInputGesture::Press, input.modifiers));
        input.pressedInputChords.push_back(*middleDragChordDown_);
    }
    if (input.rightPressed) {
        rightDragChordDown_ = mouseInputChord(QuaderMouseButton::Right, QuaderInputGesture::Drag, input.modifiers);
        input.pressedInputChords.push_back(mouseInputChord(QuaderMouseButton::Right, QuaderInputGesture::Press, input.modifiers));
        input.pressedInputChords.push_back(*rightDragChordDown_);
    }
    if (input.middleDown && middleDragChordDown_.has_value()) {
        input.activeInputChords.push_back(*middleDragChordDown_);
    }
    if (input.rightDown && rightDragChordDown_.has_value()) {
        input.activeInputChords.push_back(*rightDragChordDown_);
    }
    if (input.middleReleased) {
        if (middleDragChordDown_.has_value()) {
            input.releasedInputChords.push_back(*middleDragChordDown_);
        }
        middleDragChordDown_.reset();
    }
    if (input.rightReleased) {
        if (rightDragChordDown_.has_value()) {
            input.releasedInputChords.push_back(*rightDragChordDown_);
        }
        rightDragChordDown_.reset();
    }
    if (!input.middleDown && !input.middleReleased) {
        middleDragChordDown_.reset();
    }
    if (!input.rightDown && !input.rightReleased) {
        rightDragChordDown_.reset();
    }
    addWheelChords(input);

    input.pressedInputChords.insert(
            input.pressedInputChords.end(),
            keyboard.pressed.begin(),
            keyboard.pressed.end());
    input.activeInputChords.insert(
            input.activeInputChords.end(),
            keyboard.active.begin(),
            keyboard.active.end());

    return input;
}

} // namespace quader
