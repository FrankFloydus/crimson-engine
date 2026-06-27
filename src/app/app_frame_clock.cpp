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

double secondsBetween(AppFrameClock::Clock::time_point first, AppFrameClock::Clock::time_point second) {
    return std::chrono::duration<double>(second - first).count();
}

} // namespace

void AppFrameClock::reset(Clock::time_point now) {
    start_ = now;
    last_ = now;
    frameIndex_ = 0;
    initialized_ = true;
}

FrameTiming AppFrameClock::tick(Clock::time_point now) {
    if (!initialized_) {
        reset(now);
    }

    double rawDeltaSeconds = std::max(0.0, secondsBetween(last_, now));
    if (frameIndex_ == 0u && rawDeltaSeconds <= 0.0) {
        rawDeltaSeconds = kDefaultFirstDeltaSeconds;
    }

    const FrameTiming timing{
            .rawDeltaSeconds = rawDeltaSeconds,
            .deltaSeconds = std::clamp(rawDeltaSeconds, 0.0, kMaxDeltaSeconds),
            .elapsedSeconds = std::max(0.0, secondsBetween(start_, now)),
            .frameIndex = frameIndex_,
    };

    last_ = now;
    ++frameIndex_;
    return timing;
}

} // namespace quader
