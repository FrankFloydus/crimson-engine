////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/window_placement.hpp"

#include <algorithm>
#include <cstdint>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace quader {
namespace {

std::int64_t intersection_area(PlatformWindowChromeFrame frame,
                               PlatformWindowChromeFrame area) {
  const int left = std::max(frame.x, area.x);
  const int top = std::max(frame.y, area.y);
  const int right = std::min(frame.x + std::max(1, frame.width),
                             area.x + std::max(1, area.width));
  const int bottom = std::min(frame.y + std::max(1, frame.height),
                              area.y + std::max(1, area.height));
  if (right <= left || bottom <= top) {
    return 0;
  }
  return static_cast<std::int64_t>(right - left) *
         static_cast<std::int64_t>(bottom - top);
}

int clamp_origin(int origin, int size, int area_origin, int area_size) {
  if (size >= area_size) {
    return area_origin;
  }
  return std::clamp(origin, area_origin, area_origin + area_size - size);
}

} // namespace

PlatformWindowChromeFrame platform_window_recover_frame_to_display_work_area(
    PlatformWindowChromeFrame frame,
    std::span<const PlatformWindowChromeFrame> display_work_areas,
    PlatformWindowFrameMargins frame_margins) {
  frame.width = std::max(kPlatformWindowMinimumWidth, frame.width);
  frame.height = std::max(kPlatformWindowMinimumHeight, frame.height);
  if (display_work_areas.empty()) {
    return frame;
  }

  const PlatformWindowChromeFrame *target_area = &display_work_areas.front();
  std::int64_t best_intersection = 0;
  for (const PlatformWindowChromeFrame &work_area : display_work_areas) {
    if (work_area.width <= 0 || work_area.height <= 0) {
      continue;
    }
    const std::int64_t intersection = intersection_area(frame, work_area);
    if (intersection > best_intersection) {
      best_intersection = intersection;
      target_area = &work_area;
    }
  }

  const int left = std::max(0, frame_margins.left);
  const int top = std::max(0, frame_margins.top);
  const PlatformWindowChromeFrame client_work_area{
      .x = target_area->x + left,
      .y = target_area->y + top,
      .width = std::max(
          1, target_area->width - left - std::max(0, frame_margins.right)),
      .height = std::max(
          1, target_area->height - top - std::max(0, frame_margins.bottom)),
  };
  const int maximum_width =
      std::max(kPlatformWindowMinimumWidth, client_work_area.width);
  const int maximum_height =
      std::max(kPlatformWindowMinimumHeight, client_work_area.height);
  frame.width = std::clamp(frame.width, kPlatformWindowMinimumWidth,
                           maximum_width);
  frame.height = std::clamp(frame.height, kPlatformWindowMinimumHeight,
                            maximum_height);
  frame.x = clamp_origin(frame.x, frame.width, client_work_area.x,
                         client_work_area.width);
  frame.y = clamp_origin(frame.y, frame.height, client_work_area.y,
                         client_work_area.height);
  return frame;
}

bool platform_window_is_maximized(void *native_window_handle) {
#if defined(_WIN32)
  if (native_window_handle == nullptr) {
    return false;
  }
  return IsZoomed(static_cast<HWND>(native_window_handle)) != FALSE;
#else
  (void)native_window_handle;
  return false;
#endif
}

PlatformWindowClientExtent
platform_window_client_extent(void *native_window_handle) {
#if defined(_WIN32)
  if (native_window_handle == nullptr) {
    return {};
  }
  RECT client{};
  if (GetClientRect(static_cast<HWND>(native_window_handle), &client) == FALSE) {
    return {};
  }
  return {
      .width = static_cast<int>(std::max<LONG>(0, client.right - client.left)),
      .height =
          static_cast<int>(std::max<LONG>(0, client.bottom - client.top)),
  };
#else
  (void)native_window_handle;
  return {};
#endif
}

PlatformWindowClientBounds platform_window_client_bounds_in_window(
    void *native_window_handle, void *reference_window_handle) {
#if defined(_WIN32)
  if (native_window_handle == nullptr || reference_window_handle == nullptr) {
    return {};
  }
  RECT client{};
  const HWND window = static_cast<HWND>(native_window_handle);
  if (GetClientRect(window, &client) == FALSE) {
    return {};
  }
  // Zero is a valid mapping when the client origins already coincide.
  SetLastError(ERROR_SUCCESS);
  if (MapWindowPoints(window, static_cast<HWND>(reference_window_handle),
                      reinterpret_cast<POINT *>(&client), 2) == 0 &&
      GetLastError() != ERROR_SUCCESS) {
    return {};
  }
  return {
      .x = static_cast<int>(client.left),
      .y = static_cast<int>(client.top),
      .width = static_cast<int>(std::max<LONG>(0, client.right - client.left)),
      .height = static_cast<int>(std::max<LONG>(0, client.bottom - client.top)),
  };
#else
  (void)native_window_handle;
  (void)reference_window_handle;
  return {};
#endif
}

} // namespace quader
