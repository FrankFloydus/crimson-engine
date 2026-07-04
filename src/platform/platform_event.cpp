////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/platform_event.hpp"

#include "platform/platform_window.hpp"
#include "platform/sdl_event_utils.hpp"
#include "platform/sdl_input_adapter.hpp"

#include <SDL3/SDL.h>

namespace quader {
namespace {

[[nodiscard]] bool platform_event_targets_window(const SDL_Event &event,
                                                 SDL_WindowID window_id) {
  return window_id != 0u && sdl_event_targets_window(event, window_id);
}

[[nodiscard]] bool is_window_pixel_size_change_event(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
  case SDL_EVENT_WINDOW_RESIZED:
  case SDL_EVENT_WINDOW_MAXIMIZED:
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_SHOWN:
  case SDL_EVENT_WINDOW_EXPOSED:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] bool is_window_chrome_state_event(const SDL_Event &event) {
  switch (event.type) {
  case SDL_EVENT_WINDOW_FOCUS_GAINED:
  case SDL_EVENT_WINDOW_FOCUS_LOST:
  case SDL_EVENT_WINDOW_MAXIMIZED:
  case SDL_EVENT_WINDOW_RESTORED:
  case SDL_EVENT_WINDOW_SHOWN:
  case SDL_EVENT_WINDOW_HIDDEN:
  case SDL_EVENT_WINDOW_MINIMIZED:
  case SDL_EVENT_WINDOW_OCCLUDED:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] PlatformEvent classify_sdl_event(const SDL_Event &event,
                                               SDL_WindowID window_id) {
  const bool targets_main_window =
      platform_event_targets_window(event, window_id);
  PlatformEvent platform_event;
  platform_event.pressed_input_chord =
      quader_pressed_input_chord_from_sdl_event(event);
  platform_event.mouse_motion = event.type == SDL_EVENT_MOUSE_MOTION;
  platform_event.quit_requested =
      event.type == SDL_EVENT_QUIT ||
      (targets_main_window && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED);
  platform_event.window_pixel_size_changed =
      targets_main_window && is_window_pixel_size_change_event(event);
  platform_event.window_chrome_state_changed =
      targets_main_window && is_window_chrome_state_event(event);
  platform_event.live_resize_exposed_frame =
      targets_main_window && event.type == SDL_EVENT_WINDOW_EXPOSED &&
      event.window.data1 == 1;
  return platform_event;
}

[[nodiscard]] SDL_WindowID window_id_from_window(
    const PlatformWindow &window) {
  SDL_Window *sdl_window = window.sdl_window();
  return sdl_window == nullptr ? 0u : SDL_GetWindowID(sdl_window);
}

} // namespace

/**
 * Stores SDL polling state for one app-facing platform event pump.
 */
class PlatformEventPumpImpl {
public:
  explicit PlatformEventPumpImpl(const PlatformWindow &main_window)
      : window_id_(window_id_from_window(main_window)) {}

  [[nodiscard]] bool poll(PlatformEvent &event) {
    if (!SDL_PollEvent(&sdl_event_)) {
      event = {};
      return false;
    }

    event = classify_sdl_event(sdl_event_, window_id_);
    event.native_event = &sdl_event_;
    return true;
  }

private:
  SDL_WindowID window_id_ = 0u;
  SDL_Event sdl_event_{};
};

PlatformEventPump::PlatformEventPump(const PlatformWindow &main_window)
    : impl_(std::make_unique<PlatformEventPumpImpl>(main_window)) {}

PlatformEventPump::~PlatformEventPump() = default;

bool PlatformEventPump::poll(PlatformEvent &event) { return impl_->poll(event); }

/**
 * Owns one SDL event watch registration and callback translation state.
 */
class PlatformEventWatchImpl {
public:
  [[nodiscard]] bool install(const PlatformWindow &main_window,
                             PlatformEventWatchCallback callback,
                             void *userdata) {
    uninstall();
    window_id_ = window_id_from_window(main_window);
    callback_ = callback;
    callback_userdata_ = userdata;
    last_error_.clear();

    if (callback_ == nullptr || window_id_ == 0u) {
      last_error_ = "invalid platform event watch target";
      callback_ = nullptr;
      callback_userdata_ = nullptr;
      window_id_ = 0u;
      return false;
    }

    if (SDL_AddEventWatch(&PlatformEventWatchImpl::dispatch, this)) {
      installed_ = true;
      return true;
    }

    last_error_ = SDL_GetError();
    callback_ = nullptr;
    callback_userdata_ = nullptr;
    window_id_ = 0u;
    return false;
  }

  void uninstall() {
    if (installed_) {
      SDL_RemoveEventWatch(&PlatformEventWatchImpl::dispatch, this);
      installed_ = false;
    }
    callback_ = nullptr;
    callback_userdata_ = nullptr;
    window_id_ = 0u;
  }

  [[nodiscard]] const std::string &last_error() const { return last_error_; }

private:
  static bool SDLCALL dispatch(void *userdata, SDL_Event *event) {
    auto *self = static_cast<PlatformEventWatchImpl *>(userdata);
    if (self == nullptr || event == nullptr || self->callback_ == nullptr) {
      return true;
    }

    self->sdl_event_ = *event;
    PlatformEvent platform_event =
        classify_sdl_event(self->sdl_event_, self->window_id_);
    platform_event.native_event = &self->sdl_event_;
    return self->callback_(self->callback_userdata_, platform_event);
  }

  SDL_WindowID window_id_ = 0u;
  PlatformEventWatchCallback callback_ = nullptr;
  void *callback_userdata_ = nullptr;
  SDL_Event sdl_event_{};
  bool installed_ = false;
  std::string last_error_;
};

PlatformEventWatch::PlatformEventWatch()
    : impl_(std::make_unique<PlatformEventWatchImpl>()) {}

PlatformEventWatch::~PlatformEventWatch() { uninstall(); }

bool PlatformEventWatch::install(const PlatformWindow &main_window,
                                 PlatformEventWatchCallback callback,
                                 void *userdata) {
  return impl_->install(main_window, callback, userdata);
}

void PlatformEventWatch::uninstall() { impl_->uninstall(); }

const std::string &PlatformEventWatch::last_error() const {
  return impl_->last_error();
}

bool platform_wait_for_event_timeout(int timeout_ms) {
  return SDL_WaitEventTimeout(nullptr, timeout_ms);
}

} // namespace quader
