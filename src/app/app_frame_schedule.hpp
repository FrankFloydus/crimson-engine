////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>

namespace quader {

/// Coalesces native presentation changes without consuming active-frame requests.
class AppFramePresentationRequest final {
public:
  [[nodiscard]] bool request() noexcept {
    requested_ = true;
    if (queued_) {
      return false;
    }
    queued_ = true;
    return true;
  }

  void deliver() noexcept { queued_ = false; }

  [[nodiscard]] bool take(bool rendering_frame) noexcept {
    if (!requested_ || queued_ || rendering_frame) {
      return false;
    }
    requested_ = false;
    return true;
  }

private:
  bool requested_ = false;
  bool queued_ = false;
};

/** Schedules uncapped frames only while submissions make progress. */
class AppFrameSchedule final {
public:
  int next_interval_ms(bool continuous, bool synchronized,
                       int display_interval_ms, bool submitted,
                       bool composition_driven = false) noexcept {
    if (!continuous || synchronized) {
      retry_interval_ms_ = 0;
      if (!continuous) {
        return kIdleFrameIntervalMs;
      }
      // A synchronized Qt root requests its next frame after composition.
      // Leave time for that callback instead of racing it with a second clock;
      // retry if a requested composition never arrives.
      return composition_driven ? display_interval_ms * 2
                                : display_interval_ms;
    }
    if (submitted) {
      retry_interval_ms_ = 0;
      return 0;
    }
    // A rejected frame must yield the Qt event loop. Only the cheap renderer
    // readiness query is retried; retained UI and scene work wait for capacity.
    retry_interval_ms_ = std::clamp(retry_interval_ms_ * 2, 1, 4);
    return retry_interval_ms_;
  }

private:
  static constexpr int kIdleFrameIntervalMs = 16;
  int retry_interval_ms_ = 0;
};

} // namespace quader
