////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

/**
 * Classifies one native mouse message without exposing platform message types.
 */
enum class PlatformRawMouseMessageKind : std::uint8_t {
  None,
  RelativeMotion,
  AbsoluteMotion,
  CaptureLost,
};

/**
 * Carries the renderer-neutral result of classifying one native mouse message.
 */
struct PlatformRawMouseMessage {
  PlatformRawMouseMessageKind kind = PlatformRawMouseMessageKind::None;
  std::int32_t relative_delta_x = 0;
  std::int32_t relative_delta_y = 0;
};

/**
 * Describes whether a native mouse-capture postcondition can be inspected.
 */
struct PlatformPointerCapturePostcondition {
  bool available = false;
  bool achieved = false;
};

/**
 * Resolves a toolkit capture operation against an inspectable native
 * postcondition when one is available.
 */
[[nodiscard]] constexpr bool platform_pointer_capture_transition_achieved(
    bool toolkit_transition_succeeded,
    PlatformPointerCapturePostcondition postcondition) noexcept {
  return postcondition.available
             ? postcondition.achieved
             : toolkit_transition_succeeded;
}

[[nodiscard]] PlatformPointerCapturePostcondition
platform_pointer_capture_postcondition(void *native_window,
                                       bool capture_requested) noexcept;

/**
 * Owns foreground raw mouse registration for one active native viewport.
 * Native messages remain opaque at this boundary so Win32 types never leak
 * into the Qt input adapter or the toolkit-neutral viewport runtime.
 */
class PlatformRawMouseInput final {
public:
  PlatformRawMouseInput() = default;
  ~PlatformRawMouseInput();

  PlatformRawMouseInput(const PlatformRawMouseInput &) = delete;
  PlatformRawMouseInput &operator=(const PlatformRawMouseInput &) = delete;

  [[nodiscard]] static bool supported() noexcept;
  [[nodiscard]] bool start(void *native_window);
  [[nodiscard]] bool stop();
  [[nodiscard]] bool active() const noexcept { return active_; }
  [[nodiscard]] std::uint32_t last_native_error() const noexcept {
    return last_native_error_;
  }

  [[nodiscard]] PlatformRawMouseMessage
  classify_native_message(void *native_message) const;

private:
  void *native_window_ = nullptr;
  bool active_ = false;
  std::uint32_t last_native_error_ = 0u;
};

} // namespace quader
