////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace quader {

/**
 * Stores completed-frame timing data exposed by the renderer to editor UI surfaces.
 */
struct RenderFramePerformanceStats {
  bool frames_per_second_available = false;
  double frames_per_second = 0.0;
  bool cpu_frame_ms_available = false;
  double cpu_frame_ms = 0.0;
  bool gpu_frame_ms_available = false;
  double gpu_frame_ms = 0.0;
};

/**
 * Aggregates one presentation frame across all swapchain submissions made for
 * that frame. The submission count is intentionally treated as availability,
 * not as an FPS multiplier.
 */
class RenderPresentationFrameTracker {
public:
  using Clock = std::chrono::steady_clock;

  void reset() {
    presentation_begin_ = {};
    sample_begin_ = {};
    sample_count_ = 0u;
    presentation_active_ = false;
    sample_active_ = false;
  }

  void begin_presentation_frame(Clock::time_point now) {
    presentation_begin_ = now;
    presentation_active_ = true;
  }

  [[nodiscard]] bool complete_presentation_frame(
      Clock::time_point now, std::size_t swap_chain_submissions,
      RenderFramePerformanceStats &stats) {
    if (!presentation_active_) {
      return false;
    }
    presentation_active_ = false;
    if (swap_chain_submissions == 0u) {
      return false;
    }

    stats.cpu_frame_ms =
        std::chrono::duration<double, std::milli>(now - presentation_begin_)
            .count();
    stats.cpu_frame_ms_available = true;

    if (!sample_active_) {
      sample_begin_ = presentation_begin_;
      sample_active_ = true;
    }
    ++sample_count_;
    const double sample_seconds =
        std::chrono::duration<double>(now - sample_begin_).count();
    if (sample_seconds >= kFrameRateSampleSeconds) {
      stats.frames_per_second =
          static_cast<double>(sample_count_) / sample_seconds;
      stats.frames_per_second_available = true;
      sample_begin_ = now;
      sample_count_ = 0u;
    }
    return true;
  }

private:
  static constexpr double kFrameRateSampleSeconds = 0.5;

  Clock::time_point presentation_begin_{};
  Clock::time_point sample_begin_{};
  std::uint32_t sample_count_ = 0u;
  bool presentation_active_ = false;
  bool sample_active_ = false;
};

} // namespace quader
