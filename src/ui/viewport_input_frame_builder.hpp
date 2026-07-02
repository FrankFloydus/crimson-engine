////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"
#include "viewport/viewport_input_router.hpp"

#include <imgui.h>

#include <optional>
#include <vector>

namespace quader {

/**
 * Stores canonical keyboard input collected from the current ImGui frame.
 */
struct ImGuiKeyboardInputChords {
  bool text_input_active = false;
  ViewportModifiers modifiers = {};
  std::vector<QuaderInputChord> pressed;
  std::vector<QuaderInputChord> active;
};

[[nodiscard]] QuaderInputChord
keyboard_input_chord(QuaderKeyCode key, ViewportModifiers modifiers);
[[nodiscard]] QuaderInputChord mouse_input_chord(QuaderMouseButton button,
                                                 QuaderInputGesture gesture,
                                                 ViewportModifiers modifiers);
[[nodiscard]] QuaderInputChord wheel_input_chord(QuaderInputGesture gesture,
                                                 ViewportModifiers modifiers);
[[nodiscard]] ImGuiKeyboardInputChords collect_im_gui_keyboard_input_chords();

/**
 * Stores the Viewport Input Frame Build Request data contract used by the ImGui editor presentation layer.
 */
struct ViewportInputFrameBuildRequest {
    bool hovered = false;
    bool focused = false;
    ImVec2 item_min = {};
    ImVec2 item_size = {};
    bool route_active_navigation_input = false;
    bool use_captured_mouse_delta = false;
    QVec2 captured_mouse_delta = {};
    float delta_seconds = 1.0f / 60.0f;
};

/**
 * Builds Viewport Input Frame Builder data for the ImGui editor presentation layer.
 */
class ViewportInputFrameBuilder {
public:
    [[nodiscard]] ViewportInputFrame build(const ViewportInputFrameBuildRequest& request) const;

private:
  mutable std::optional<QuaderInputChord> middle_drag_chord_down_;
  mutable std::optional<QuaderInputChord> right_drag_chord_down_;
};

} // namespace quader
