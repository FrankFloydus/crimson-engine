////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/raw_mouse_input.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <limits>

namespace quader {

PlatformPointerCapturePostcondition
platform_pointer_capture_postcondition(void *native_window,
                                       bool capture_requested) noexcept {
#if defined(_WIN32)
  if (native_window == nullptr) {
    return {.available = true, .achieved = false};
  }
  const HWND requested_window = static_cast<HWND>(native_window);
  const HWND captured_window = GetCapture();
  return {
      .available = true,
      .achieved = capture_requested ? captured_window == requested_window
                                    : captured_window != requested_window,
  };
#else
  (void)native_window;
  (void)capture_requested;
  return {};
#endif
}

PlatformRawMouseInput::~PlatformRawMouseInput() { (void)stop(); }

bool PlatformRawMouseInput::supported() noexcept {
#if defined(_WIN32)
  return true;
#else
  return false;
#endif
}

bool PlatformRawMouseInput::start(void *native_window) {
  if (active_) {
    return native_window_ == native_window;
  }
  last_native_error_ = 0u;
#if defined(_WIN32)
  if (native_window == nullptr) {
    return false;
  }
  // Retain the target even when raw registration fails. WM_CAPTURECHANGED is
  // independent of WM_INPUT and must remain observable while Qt fallback
  // motion owns the capture stream.
  native_window_ = native_window;
  RAWINPUTDEVICE device{};
  device.usUsagePage = 0x01u;
  device.usUsage = 0x02u;
  // Keep legacy mouse messages enabled so Qt continues to own buttons,
  // focus, hover, and physical local coordinates used by viewport picking.
  device.dwFlags = 0u;
  device.hwndTarget = nullptr;
  if (RegisterRawInputDevices(&device, 1u, sizeof(device)) == FALSE) {
    last_native_error_ = static_cast<std::uint32_t>(GetLastError());
    return false;
  }
  active_ = true;
  return true;
#else
  return false;
#endif
}

bool PlatformRawMouseInput::stop() {
  last_native_error_ = 0u;
#if defined(_WIN32)
  bool stopped = true;
  if (active_) {
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01u;
    device.usUsage = 0x02u;
    device.dwFlags = RIDEV_REMOVE;
    device.hwndTarget = nullptr;
    stopped = RegisterRawInputDevices(&device, 1u, sizeof(device)) != FALSE;
    if (!stopped) {
      last_native_error_ = static_cast<std::uint32_t>(GetLastError());
    }
  }
  // The UI must stop consuming deltas even if the OS registration cleanup
  // fails. Its native event filter is removed independently.
  native_window_ = nullptr;
  active_ = false;
  return stopped;
#else
  native_window_ = nullptr;
  active_ = false;
  return true;
#endif
}

PlatformRawMouseMessage
PlatformRawMouseInput::classify_native_message(void *native_message) const {
#if defined(_WIN32)
  if (native_message == nullptr) {
    return {};
  }
  const auto *message = static_cast<const MSG *>(native_message);
  if (native_window_ != nullptr && message->message == WM_CAPTURECHANGED &&
      reinterpret_cast<void *>(message->hwnd) == native_window_ &&
      reinterpret_cast<void *>(message->lParam) != native_window_) {
    return {.kind = PlatformRawMouseMessageKind::CaptureLost};
  }
  if (!active_ || message->message != WM_INPUT) {
    return {};
  }

  RAWINPUT input{};
  UINT input_size = sizeof(input);
  const UINT bytes = GetRawInputData(
      reinterpret_cast<HRAWINPUT>(message->lParam), RID_INPUT, &input,
      &input_size, sizeof(RAWINPUTHEADER));
  constexpr UINT kMinimumMousePacketSize =
      static_cast<UINT>(sizeof(RAWINPUTHEADER) + sizeof(RAWMOUSE));
  if (bytes == std::numeric_limits<UINT>::max() ||
      bytes < kMinimumMousePacketSize ||
      input.header.dwSize < kMinimumMousePacketSize ||
      input.header.dwType != RIM_TYPEMOUSE) {
    return {};
  }
  if ((input.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0u) {
    return {.kind = PlatformRawMouseMessageKind::AbsoluteMotion};
  }
  return {
      .kind = PlatformRawMouseMessageKind::RelativeMotion,
      .relative_delta_x = static_cast<std::int32_t>(input.data.mouse.lLastX),
      .relative_delta_y = static_cast<std::int32_t>(input.data.mouse.lLastY),
  };
#else
  (void)native_message;
  return {};
#endif
}

} // namespace quader
