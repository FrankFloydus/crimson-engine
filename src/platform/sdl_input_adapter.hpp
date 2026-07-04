////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/quader_input_binding.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keycode.h>

#include <cstdint>
#include <optional>

namespace quader {

[[nodiscard]] std::optional<QuaderKeyCode>
quader_key_code_from_sdl_key(SDL_Keycode key);
[[nodiscard]] QuaderInputChord
quader_input_chord_from_sdl_key(SDL_Keycode key, SDL_Keymod modifiers);
[[nodiscard]] std::optional<QuaderInputChord>
quader_pressed_input_chord_from_sdl_event(const SDL_Event &event);
[[nodiscard]] std::optional<QuaderMouseButton>
quader_mouse_button_from_sdl_button(std::uint8_t button);

} // namespace quader
