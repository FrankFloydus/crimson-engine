////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>

namespace quader {

inline constexpr double kFrameTimingDefaultDeltaSeconds = 1.0 / 60.0;
inline constexpr double kFrameTimingMaxDeltaSeconds = 0.25;

/**
 * Stores the Frame Timing data contract shared by runtime frame-driven systems.
 */
struct FrameTiming {
  double raw_delta_seconds = kFrameTimingDefaultDeltaSeconds;
  double delta_seconds = kFrameTimingDefaultDeltaSeconds;
  double elapsed_seconds = 0.0;
  std::uint64_t frame_index = 0;
};

} // namespace quader
