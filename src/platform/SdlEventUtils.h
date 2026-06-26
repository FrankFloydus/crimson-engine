////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <SDL3/SDL_events.h>

#include <optional>

namespace quader {

[[nodiscard]] std::optional<SDL_WindowID> sdlEventWindowId(const SDL_Event& event);
[[nodiscard]] bool sdlEventTargetsWindow(const SDL_Event& event, SDL_WindowID windowId);

} // namespace quader
