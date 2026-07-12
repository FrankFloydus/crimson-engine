////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "platform/window_placement.hpp"

#include <algorithm>
#include <cstdint>

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
    std::span<const PlatformWindowChromeFrame> display_work_areas) {
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

  const int maximum_width =
      std::max(kPlatformWindowMinimumWidth, target_area->width);
  const int maximum_height =
      std::max(kPlatformWindowMinimumHeight, target_area->height);
  frame.width = std::clamp(frame.width, kPlatformWindowMinimumWidth,
                           maximum_width);
  frame.height = std::clamp(frame.height, kPlatformWindowMinimumHeight,
                            maximum_height);
  frame.x = clamp_origin(frame.x, frame.width, target_area->x,
                         std::max(1, target_area->width));
  frame.y = clamp_origin(frame.y, frame.height, target_area->y,
                         std::max(1, target_area->height));
  return frame;
}

} // namespace quader
