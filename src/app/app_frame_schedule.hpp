////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <algorithm>

namespace quader {

/** Schedules uncapped frames only while submissions make progress. */
class AppFrameSchedule final {
public:
  int next_interval_ms(bool continuous, bool synchronized,
                       int display_interval_ms, bool submitted) noexcept {
    if (!continuous || synchronized) {
      retry_interval_ms_ = 0;
      return continuous ? display_interval_ms : kIdleFrameIntervalMs;
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
