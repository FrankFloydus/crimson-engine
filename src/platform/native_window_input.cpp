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

} // namespace quader
