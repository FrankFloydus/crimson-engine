////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/frame_timing.hpp"

#include <chrono>
#include <cstdint>

namespace quader {

/**
 * Owns App Frame Clock behavior within the native application shell.
 */
class AppFrameClock {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr double kDefaultFirstDeltaSeconds = kFrameTimingDefaultDeltaSeconds;
    static constexpr double kMaxDeltaSeconds = kFrameTimingMaxDeltaSeconds;

    void reset(Clock::time_point now = Clock::now());
    [[nodiscard]] FrameTiming tick(Clock::time_point now = Clock::now());

private:
    Clock::time_point start_ = {};
    Clock::time_point last_ = {};
    std::uint64_t frame_index_ = 0;
    bool initialized_ = false;
};

} // namespace quader
