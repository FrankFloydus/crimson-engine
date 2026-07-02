////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_clock.hpp"

#include <algorithm>
#include <chrono>

namespace quader {
namespace {

double seconds_between(AppFrameClock::Clock::time_point first,
                       AppFrameClock::Clock::time_point second) {
  return std::chrono::duration<double>(second - first).count();
}

} // namespace

void AppFrameClock::reset(Clock::time_point now) {
    start_ = now;
    last_ = now;
    frame_index_ = 0;
    initialized_ = true;
}

FrameTiming AppFrameClock::tick(Clock::time_point now) {
    if (!initialized_) {
        reset(now);
    }

    double raw_delta_seconds = std::max(0.0, seconds_between(last_, now));
    if (frame_index_ == 0u && raw_delta_seconds <= 0.0) {
      raw_delta_seconds = kDefaultFirstDeltaSeconds;
    }

    const FrameTiming timing{
        .raw_delta_seconds = raw_delta_seconds,
        .delta_seconds = std::clamp(raw_delta_seconds, 0.0, kMaxDeltaSeconds),
        .elapsed_seconds = std::max(0.0, seconds_between(start_, now)),
        .frame_index = frame_index_,
    };

    last_ = now;
    ++frame_index_;
    return timing;
}

} // namespace quader
