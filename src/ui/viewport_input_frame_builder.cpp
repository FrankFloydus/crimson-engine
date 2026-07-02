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

void add_key(ImGuiKeyboardInputChords &input, ImGuiKey imgui_key,
             QuaderKeyCode key) {
  if (ImGui::IsKeyPressed(imgui_key, false)) {
    input.pressed.push_back(keyboard_input_chord(key, input.modifiers));
  }
  if (ImGui::IsKeyDown(imgui_key)) {
    input.active.push_back(keyboard_input_chord(key, input.modifiers));
  }
}

void add_key_pair(ImGuiKeyboardInputChords &input, ImGuiKey primary_key,
                  QuaderKeyCode primary_code, ImGuiKey keypad_key,
                  QuaderKeyCode keypad_code) {
  add_key(input, primary_key, primary_code);
  add_key(input, keypad_key, keypad_code);
}

void add_wheel_chords(ViewportInputFrame &input) {
  if (input.mouse_wheel == 0.0f) {
    return;
  }
  input.wheel_input_chords.push_back(
      wheel_input_chord(QuaderInputGesture::Wheel, input.modifiers));
  input.wheel_input_chords.push_back(wheel_input_chord(
      input.mouse_wheel > 0.0f ? QuaderInputGesture::WheelUp
                               : QuaderInputGesture::WheelDown,
      input.modifiers));
}

} // namespace

QuaderInputChord keyboard_input_chord(QuaderKeyCode key,
                                      ViewportModifiers modifiers) {
  return {
      .primary = modifiers.primary,
      .shift = modifiers.shift,
      .alt = modifiers.alt,
      .device = QuaderInputDevice::Keyboard,
      .key = key,
  };
}

QuaderInputChord mouse_input_chord(QuaderMouseButton button,
                                   QuaderInputGesture gesture,
                                   ViewportModifiers modifiers) {
  return {
      .primary = modifiers.primary,
      .shift = modifiers.shift,
      .alt = modifiers.alt,
      .device = QuaderInputDevice::Mouse,
      .mouse_button = button,
      .gesture = gesture,
  };
}

QuaderInputChord wheel_input_chord(QuaderInputGesture gesture,
                                   ViewportModifiers modifiers) {
  return {
      .primary = modifiers.primary,
      .shift = modifiers.shift,
      .alt = modifiers.alt,
      .device = QuaderInputDevice::Mouse,
      .gesture = gesture,
  };
}

ImGuiKeyboardInputChords collect_im_gui_keyboard_input_chords() {
  ImGuiIO &io = ImGui::GetIO();
  ImGuiKeyboardInputChords input;
  input.text_input_active = io.WantTextInput;
  input.modifiers = {
      .primary = io.KeyCtrl || io.KeySuper,
      .shift = io.KeyShift,
      .alt = io.KeyAlt,
  };

  add_key(input, ImGuiKey_A, QuaderKeyCode::A);
  add_key(input, ImGuiKey_B, QuaderKeyCode::B);
  add_key(input, ImGuiKey_C, QuaderKeyCode::C);
  add_key(input, ImGuiKey_D, QuaderKeyCode::D);
  add_key(input, ImGuiKey_E, QuaderKeyCode::E);
  add_key(input, ImGuiKey_F, QuaderKeyCode::F);
  add_key(input, ImGuiKey_G, QuaderKeyCode::G);
  add_key(input, ImGuiKey_H, QuaderKeyCode::H);
  add_key(input, ImGuiKey_I, QuaderKeyCode::I);
  add_key(input, ImGuiKey_J, QuaderKeyCode::J);
  add_key(input, ImGuiKey_K, QuaderKeyCode::K);
  add_key(input, ImGuiKey_L, QuaderKeyCode::L);
  add_key(input, ImGuiKey_M, QuaderKeyCode::M);
  add_key(input, ImGuiKey_N, QuaderKeyCode::N);
  add_key(input, ImGuiKey_O, QuaderKeyCode::O);
  add_key(input, ImGuiKey_P, QuaderKeyCode::P);
  add_key(input, ImGuiKey_Q, QuaderKeyCode::Q);
  add_key(input, ImGuiKey_R, QuaderKeyCode::R);
  add_key(input, ImGuiKey_S, QuaderKeyCode::S);
  add_key(input, ImGuiKey_T, QuaderKeyCode::T);
  add_key(input, ImGuiKey_U, QuaderKeyCode::U);
  add_key(input, ImGuiKey_V, QuaderKeyCode::V);
  add_key(input, ImGuiKey_W, QuaderKeyCode::W);
  add_key(input, ImGuiKey_X, QuaderKeyCode::X);
  add_key(input, ImGuiKey_Y, QuaderKeyCode::Y);
  add_key(input, ImGuiKey_Z, QuaderKeyCode::Z);
  add_key_pair(input, ImGuiKey_0, QuaderKeyCode::Digit0, ImGuiKey_Keypad0,
               QuaderKeyCode::Numpad0);
  add_key_pair(input, ImGuiKey_1, QuaderKeyCode::Digit1, ImGuiKey_Keypad1,
               QuaderKeyCode::Numpad1);
  add_key_pair(input, ImGuiKey_2, QuaderKeyCode::Digit2, ImGuiKey_Keypad2,
               QuaderKeyCode::Numpad2);
  add_key_pair(input, ImGuiKey_3, QuaderKeyCode::Digit3, ImGuiKey_Keypad3,
               QuaderKeyCode::Numpad3);
  add_key_pair(input, ImGuiKey_4, QuaderKeyCode::Digit4, ImGuiKey_Keypad4,
               QuaderKeyCode::Numpad4);
  add_key_pair(input, ImGuiKey_5, QuaderKeyCode::Digit5, ImGuiKey_Keypad5,
               QuaderKeyCode::Numpad5);
  add_key_pair(input, ImGuiKey_6, QuaderKeyCode::Digit6, ImGuiKey_Keypad6,
               QuaderKeyCode::Numpad6);
  add_key_pair(input, ImGuiKey_7, QuaderKeyCode::Digit7, ImGuiKey_Keypad7,
               QuaderKeyCode::Numpad7);
  add_key_pair(input, ImGuiKey_8, QuaderKeyCode::Digit8, ImGuiKey_Keypad8,
               QuaderKeyCode::Numpad8);
  add_key_pair(input, ImGuiKey_9, QuaderKeyCode::Digit9, ImGuiKey_Keypad9,
               QuaderKeyCode::Numpad9);
  add_key(input, ImGuiKey_KeypadAdd, QuaderKeyCode::NumpadAdd);
  add_key(input, ImGuiKey_KeypadSubtract, QuaderKeyCode::NumpadSubtract);
  add_key(input, ImGuiKey_KeypadMultiply, QuaderKeyCode::NumpadMultiply);
  add_key(input, ImGuiKey_KeypadDivide, QuaderKeyCode::NumpadDivide);
  add_key(input, ImGuiKey_KeypadDecimal, QuaderKeyCode::NumpadDecimal);
  add_key(input, ImGuiKey_Comma, QuaderKeyCode::Comma);
  add_key(input, ImGuiKey_Period, QuaderKeyCode::Period);
  add_key(input, ImGuiKey_Slash, QuaderKeyCode::Slash);
  add_key(input, ImGuiKey_Backslash, QuaderKeyCode::Backslash);
  add_key(input, ImGuiKey_Semicolon, QuaderKeyCode::Semicolon);
  add_key(input, ImGuiKey_Apostrophe, QuaderKeyCode::Apostrophe);
  add_key(input, ImGuiKey_LeftBracket, QuaderKeyCode::LeftBracket);
  add_key(input, ImGuiKey_RightBracket, QuaderKeyCode::RightBracket);
  add_key(input, ImGuiKey_Minus, QuaderKeyCode::Minus);
  add_key(input, ImGuiKey_Equal, QuaderKeyCode::Equal);
  add_key(input, ImGuiKey_GraveAccent, QuaderKeyCode::Grave);
  add_key(input, ImGuiKey_Home, QuaderKeyCode::Home);
  add_key(input, ImGuiKey_End, QuaderKeyCode::End);
  add_key(input, ImGuiKey_PageUp, QuaderKeyCode::PageUp);
  add_key(input, ImGuiKey_PageDown, QuaderKeyCode::PageDown);
  add_key(input, ImGuiKey_LeftArrow, QuaderKeyCode::Left);
  add_key(input, ImGuiKey_RightArrow, QuaderKeyCode::Right);
  add_key(input, ImGuiKey_UpArrow, QuaderKeyCode::Up);
  add_key(input, ImGuiKey_DownArrow, QuaderKeyCode::Down);
  add_key(input, ImGuiKey_Space, QuaderKeyCode::Space);
  add_key(input, ImGuiKey_Enter, QuaderKeyCode::Enter);
  add_key(input, ImGuiKey_KeypadEnter, QuaderKeyCode::NumpadEnter);
  add_key(input, ImGuiKey_Tab, QuaderKeyCode::Tab);
  add_key(input, ImGuiKey_F1, QuaderKeyCode::F1);
  add_key(input, ImGuiKey_F2, QuaderKeyCode::F2);
  add_key(input, ImGuiKey_F3, QuaderKeyCode::F3);
  add_key(input, ImGuiKey_F4, QuaderKeyCode::F4);
  add_key(input, ImGuiKey_F5, QuaderKeyCode::F5);
  add_key(input, ImGuiKey_F6, QuaderKeyCode::F6);
  add_key(input, ImGuiKey_F7, QuaderKeyCode::F7);
  add_key(input, ImGuiKey_F8, QuaderKeyCode::F8);
  add_key(input, ImGuiKey_F9, QuaderKeyCode::F9);
  add_key(input, ImGuiKey_F10, QuaderKeyCode::F10);
  add_key(input, ImGuiKey_F11, QuaderKeyCode::F11);
  add_key(input, ImGuiKey_F12, QuaderKeyCode::F12);
  add_key(input, ImGuiKey_Escape, QuaderKeyCode::Escape);
  add_key(input, ImGuiKey_Delete, QuaderKeyCode::Delete);
  add_key(input, ImGuiKey_Backspace, QuaderKeyCode::Backspace);

  return input;
}

ViewportInputFrame ViewportInputFrameBuilder::build(const ViewportInputFrameBuildRequest& request) const {
    ImGuiIO& io = ImGui::GetIO();
    const ImGuiKeyboardInputChords keyboard =
        collect_im_gui_keyboard_input_chords();
    ViewportInputFrame input;
    input.viewport_hovered = request.hovered;
    input.viewport_focused = request.focused;
    input.text_input_active = keyboard.text_input_active;
    input.middle_pressed =
        (request.hovered || request.route_active_navigation_input) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
    input.middle_down = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    input.middle_released = ImGui::IsMouseReleased(ImGuiMouseButton_Middle);
    input.right_pressed =
        request.hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    input.right_down = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    input.right_released = ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    input.mouse_wheel =
        (request.hovered || request.route_active_navigation_input)
            ? io.MouseWheel
            : 0.0f;
    input.delta_seconds = std::max(request.delta_seconds, 0.0f);
    input.mouse_screen = {io.MousePos.x, io.MousePos.y};
    input.mouse_delta = request.use_captured_mouse_delta
                            ? request.captured_mouse_delta
                            : QVec2{io.MouseDelta.x, io.MouseDelta.y};
    input.mouse_local = {io.MousePos.x - request.item_min.x,
                         io.MousePos.y - request.item_min.y};
    input.viewport_size = {request.item_size.x, request.item_size.y};
    input.modifiers = keyboard.modifiers;

    if (input.middle_pressed) {
      middle_drag_chord_down_ = mouse_input_chord(
          QuaderMouseButton::Middle, QuaderInputGesture::Drag, input.modifiers);
      input.pressed_input_chords.push_back(
          mouse_input_chord(QuaderMouseButton::Middle,
                            QuaderInputGesture::Press, input.modifiers));
      input.pressed_input_chords.push_back(*middle_drag_chord_down_);
    }
    if (input.right_pressed) {
      right_drag_chord_down_ = mouse_input_chord(
          QuaderMouseButton::Right, QuaderInputGesture::Drag, input.modifiers);
      input.pressed_input_chords.push_back(
          mouse_input_chord(QuaderMouseButton::Right, QuaderInputGesture::Press,
                            input.modifiers));
      input.pressed_input_chords.push_back(*right_drag_chord_down_);
    }
    if (input.middle_down && middle_drag_chord_down_.has_value()) {
      input.active_input_chords.push_back(*middle_drag_chord_down_);
    }
    if (input.right_down && right_drag_chord_down_.has_value()) {
      input.active_input_chords.push_back(*right_drag_chord_down_);
    }
    if (input.middle_released) {
      if (middle_drag_chord_down_.has_value()) {
        input.released_input_chords.push_back(*middle_drag_chord_down_);
      }
      middle_drag_chord_down_.reset();
    }
    if (input.right_released) {
      if (right_drag_chord_down_.has_value()) {
        input.released_input_chords.push_back(*right_drag_chord_down_);
      }
      right_drag_chord_down_.reset();
    }
    if (!input.middle_down && !input.middle_released) {
      middle_drag_chord_down_.reset();
    }
    if (!input.right_down && !input.right_released) {
      right_drag_chord_down_.reset();
    }
    add_wheel_chords(input);

    input.pressed_input_chords.insert(input.pressed_input_chords.end(),
                                      keyboard.pressed.begin(),
                                      keyboard.pressed.end());
    input.active_input_chords.insert(input.active_input_chords.end(),
                                     keyboard.active.begin(),
                                     keyboard.active.end());

    return input;
}

} // namespace quader
