////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/native_window_input.hpp"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace quader {

std::optional<std::intptr_t>
platform_transparent_overlay_input_result(const void *native_message) noexcept {
#if defined(_WIN32)
  if (native_message != nullptr &&
      static_cast<const MSG *>(native_message)->message == WM_NCHITTEST) {
    // Windows continues hit testing beneath this HWND within the UI thread,
    // including the separate native QWindow used by a viewport surface.
    return static_cast<std::intptr_t>(HTTRANSPARENT);
  }
#else
  (void)native_message;
#endif
  return std::nullopt;
}

bool platform_native_ancestor_geometry_changes(
    const void *native_message, std::uintptr_t native_child_window) noexcept {
#if defined(_WIN32)
  if (native_message == nullptr || native_child_window == 0U) {
    return false;
  }
  const auto &message = *static_cast<const MSG *>(native_message);
  const HWND child = reinterpret_cast<HWND>(native_child_window);
  if (message.message != WM_WINDOWPOSCHANGING || message.lParam == 0 ||
      message.hwnd == child || !IsChild(message.hwnd, child)) {
    return false;
  }
  const auto &position =
      *reinterpret_cast<const WINDOWPOS *>(message.lParam);
  RECT current{};
  if (!GetWindowRect(message.hwnd, &current)) {
    return false;
  }
  if ((GetWindowLongPtrW(message.hwnd, GWL_STYLE) & WS_CHILD) != 0) {
    // WINDOWPOS child coordinates belong to the native parent's client area,
    // whereas GetWindowRect always reports screen coordinates.
    MapWindowPoints(HWND_DESKTOP, GetParent(message.hwnd),
                    reinterpret_cast<POINT *>(&current), 2);
  }
  return ((position.flags & SWP_NOMOVE) == 0 &&
          (position.x != current.left || position.y != current.top)) ||
         ((position.flags & SWP_NOSIZE) == 0 &&
          (position.cx != current.right - current.left ||
           position.cy != current.bottom - current.top));
#else
  (void)native_message;
  (void)native_child_window;
  return false;
#endif
}

} // namespace quader
