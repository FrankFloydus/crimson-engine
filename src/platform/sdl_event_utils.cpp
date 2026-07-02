////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/sdl_event_utils.hpp"

namespace quader {

std::optional<SDL_WindowID> sdl_event_window_id(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_WINDOW_SHOWN:
  case SDL_EVENT_WINDOW_HIDDEN:
  case SDL_EVENT_WINDOW_EXPOSED:
  case SDL_EVENT_WINDOW_MOVED:
  case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_RESIZED:
  case SDL_EVENT_WINDOW_MINIMIZED:
  case SDL_EVENT_WINDOW_MAXIMIZED:
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_MOUSE_ENTER:
  case SDL_EVENT_WINDOW_MOUSE_LEAVE:
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WINDOW_HIT_TEST:
  case SDL_EVENT_WINDOW_ICCPROF_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
  case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
  case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
  case SDL_EVENT_WINDOW_OCCLUDED:
  case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
  case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
  case SDL_EVENT_WINDOW_DESTROYED:
  case SDL_EVENT_WINDOW_HDR_STATE_CHANGED:
    return event.window.windowID;
  case SDL_EVENT_MOUSE_MOTION:
    return event.motion.windowID;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    return event.button.windowID;
  case SDL_EVENT_MOUSE_WHEEL:
    return event.wheel.windowID;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    return event.key.windowID;
  case SDL_EVENT_TEXT_INPUT:
    return event.text.windowID;
  default:
    return std::nullopt;
  }
}

bool sdl_event_targets_window(const SDL_Event &event, SDL_WindowID window_id) {
  const std::optional<SDL_WindowID> event_window = sdl_event_window_id(event);
  return event_window.has_value() && *event_window == window_id;
}

} // namespace quader
