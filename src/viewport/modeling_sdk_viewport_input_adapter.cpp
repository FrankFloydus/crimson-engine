////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/modeling_sdk_viewport_input_adapter.hpp"

#include <algorithm>

namespace quader {
namespace {

constexpr std::uint32_t kPrimaryModifierBit = 1U << 24U;
constexpr std::uint32_t kShiftModifierBit = 1U << 25U;
constexpr std::uint32_t kAltModifierBit = 1U << 26U;

[[nodiscard]] std::uint32_t enum_index(QuaderInputDevice value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t enum_index(QuaderInputGesture value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t enum_index(QuaderKeyCode value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t enum_index(QuaderMouseButton value) {
  return static_cast<std::uint32_t>(value);
}

[[nodiscard]] quader::modeling::Vec2 sdk_vec2(QVec2 value) {
  return {.x = value.x, .y = value.y};
}

[[nodiscard]] bool chord_matches_button(const QuaderInputChord &chord,
                                        QuaderMouseButton button) {
  return chord.device == QuaderInputDevice::Mouse &&
         chord.mouse_button == button &&
         (chord.gesture == QuaderInputGesture::Press ||
          chord.gesture == QuaderInputGesture::Drag);
}

[[nodiscard]] bool button_active(const ViewportInputFrame &frame,
                                 QuaderMouseButton button) {
  auto matches = [button](const QuaderInputChord &chord) {
    return chord_matches_button(chord, button);
  };
  return std::ranges::any_of(frame.active_input_chords, matches) ||
         std::ranges::any_of(frame.pressed_input_chords, matches);
}

} // namespace

std::uint32_t modeling_sdk_encode_input_chord(
    const QuaderInputChord &chord) {
  std::uint32_t token = enum_index(chord.device);
  token |= enum_index(chord.gesture) << 4U;
  token |= enum_index(chord.key) << 8U;
  token |= enum_index(chord.mouse_button) << 16U;
  if (chord.primary) {
    token |= kPrimaryModifierBit;
  }
  if (chord.shift) {
    token |= kShiftModifierBit;
  }
  if (chord.alt) {
    token |= kAltModifierBit;
  }
  return token;
}

quader::modeling::ViewportHostInputSnapshot
modeling_sdk_viewport_input_snapshot(
    const ViewportInputFrame &frame,
    ModelingSdkViewportInputOrigin origin) {
  quader::modeling::ViewportHostInputSnapshot snapshot;
  snapshot.pointer_position = sdk_vec2(frame.mouse_local);
  snapshot.viewport_rect = {
      .x = origin.x,
      .y = origin.y,
      .width = std::max(frame.viewport_size.x, 1.0F),
      .height = std::max(frame.viewport_size.y, 1.0F),
  };
  snapshot.wheel_delta = frame.mouse_wheel;
  snapshot.buttons = {
      .left = button_active(frame, QuaderMouseButton::Left),
      .middle = frame.middle_down || frame.middle_pressed ||
                button_active(frame, QuaderMouseButton::Middle),
      .right = frame.right_down || frame.right_pressed ||
               button_active(frame, QuaderMouseButton::Right),
  };
  snapshot.modifiers = {
      .shift = frame.modifiers.shift,
      .control = frame.modifiers.primary,
      .alt = frame.modifiers.alt,
      .command = false,
  };
  snapshot.focused = frame.viewport_focused;
  snapshot.text_input_active = frame.text_input_active;
  snapshot.navigation_capture =
      frame.navigation_active || frame.fly_navigation_active ||
      frame.orbit_navigation_active || frame.pan_navigation_active ||
      frame.active_tool_owns_viewport;
  snapshot.frame_time_seconds = frame.delta_seconds;
  snapshot.pressed_chords.reserve(frame.pressed_input_chords.size());
  for (const QuaderInputChord &chord : frame.pressed_input_chords) {
    if (!chord.empty()) {
      snapshot.pressed_chords.push_back(
          modeling_sdk_encode_input_chord(chord));
    }
  }
  return snapshot;
}

} // namespace quader
