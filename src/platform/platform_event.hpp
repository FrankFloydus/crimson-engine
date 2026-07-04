////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/quader_input_binding.hpp"

#include <memory>
#include <optional>
#include <string>

namespace quader {

class PlatformWindow;

/**
 * Stores app-facing platform event classification without exposing the backend
 * event enum to app orchestration.
 */
struct PlatformEvent {
  const void *native_event = nullptr;
  std::optional<QuaderInputChord> pressed_input_chord;
  bool quit_requested = false;
  bool mouse_motion = false;
  bool window_pixel_size_changed = false;
  bool window_chrome_state_changed = false;
  bool live_resize_exposed_frame = false;
};

class PlatformEventPumpImpl;

/**
 * Polls backend events and projects them into app-facing platform events.
 */
class PlatformEventPump {
public:
  explicit PlatformEventPump(const PlatformWindow &main_window);
  ~PlatformEventPump();

  PlatformEventPump(const PlatformEventPump &) = delete;
  PlatformEventPump &operator=(const PlatformEventPump &) = delete;

  [[nodiscard]] bool poll(PlatformEvent &event);

private:
  std::unique_ptr<PlatformEventPumpImpl> impl_;
};

using PlatformEventWatchCallback = bool (*)(void *userdata,
                                            const PlatformEvent &event);

class PlatformEventWatchImpl;

/**
 * Owns a backend event watch and translates callbacks to PlatformEvent.
 */
class PlatformEventWatch {
public:
  PlatformEventWatch();
  ~PlatformEventWatch();

  PlatformEventWatch(const PlatformEventWatch &) = delete;
  PlatformEventWatch &operator=(const PlatformEventWatch &) = delete;

  [[nodiscard]] bool install(const PlatformWindow &main_window,
                             PlatformEventWatchCallback callback,
                             void *userdata);
  void uninstall();
  [[nodiscard]] const std::string &last_error() const;

private:
  std::unique_ptr<PlatformEventWatchImpl> impl_;
};

[[nodiscard]] bool platform_wait_for_event_timeout(int timeout_ms);

} // namespace quader
