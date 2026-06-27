////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/quader_input_binding.hpp"

#include <SDL3/SDL_keycode.h>

#include <cstdint>
#include <optional>

namespace quader {

[[nodiscard]] std::optional<QuaderKeyCode> quaderKeyCodeFromSdlKey(SDL_Keycode key);
[[nodiscard]] std::optional<QuaderMouseButton> quaderMouseButtonFromSdlButton(std::uint8_t button);

} // namespace quader
