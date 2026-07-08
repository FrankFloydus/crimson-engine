////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/types.hpp>

#include <cstdint>
#include <vector>

namespace quader::modeling {

/**
 * Defines the viewport rectangle used for host-to-SDK input translation.
 */
struct ViewportRect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 1.0F;
  float height = 1.0F;
};

/**
 * Stores keyboard modifier state for one host input frame.
 */
struct ViewportModifiers {
  bool shift = false;
  bool control = false;
  bool alt = false;
  bool command = false;
};

/**
 * Stores pointer button state for one host input frame.
 */
struct ViewportPointerButtons {
  bool left = false;
  bool middle = false;
  bool right = false;
};

/**
 * Captures toolkit-neutral viewport input for SDK tool and navigation owners.
 */
struct ViewportHostInputSnapshot {
  Vec2 pointer_position{};
  ViewportRect viewport_rect{};
  float wheel_delta = 0.0F;
  ViewportPointerButtons buttons{};
  ViewportModifiers modifiers{};
  bool focused = false;
  bool text_input_active = false;
  bool navigation_capture = false;
  std::vector<std::uint32_t> pressed_chords;
  double frame_time_seconds = 0.0;
};

} // namespace quader::modeling
