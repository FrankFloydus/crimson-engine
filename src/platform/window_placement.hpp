////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <span>

namespace quader {

inline constexpr int kPlatformWindowMinimumWidth = 800;
inline constexpr int kPlatformWindowMinimumHeight = 600;

/// Toolkit-neutral persisted desktop window rectangle.
struct PlatformWindowChromeFrame {
  int x = 0;
  int y = 0;
  int width = 1;
  int height = 1;
};

/// Logical-pixel insets from a decorated desktop frame to its client area.
struct PlatformWindowFrameMargins {
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

/// Persisted normal window placement plus maximized state.
struct PlatformWindowPlacement {
  PlatformWindowChromeFrame normal_frame{0, 0, 1600, 960};
  bool maximized = false;
};

/// Physical-pixel extent of a native top-level window's client area.
struct PlatformWindowClientExtent {
  int width = 0;
  int height = 0;
};

/**
 * Clamps a persisted client rectangle and its native frame to desktop work areas.
 * The returned rectangle remains client geometry; zero margins preserve callers
 * that already supply an outer frame.
 * The first work area is the fallback for a rectangle that no longer
 * intersects any connected display.
 */
[[nodiscard]] PlatformWindowChromeFrame
platform_window_recover_frame_to_display_work_area(
    PlatformWindowChromeFrame frame,
    std::span<const PlatformWindowChromeFrame> display_work_areas,
    PlatformWindowFrameMargins frame_margins = {});

/** Returns whether the native top-level window has reached its maximized OS state. */
[[nodiscard]] bool platform_window_is_maximized(void *native_window_handle);

/** Returns the native client-area extent in physical pixels, or an empty extent. */
[[nodiscard]] PlatformWindowClientExtent
platform_window_client_extent(void *native_window_handle);

} // namespace quader
