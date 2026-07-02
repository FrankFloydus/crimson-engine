////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_clock.hpp"

#include <gtest/gtest.h>

#include <chrono>

using namespace quader;

TEST(AppFrameClockTests, ProducesClampedFrameTimingAndMonotonicFrameIndex) {
    using Clock = AppFrameClock::Clock;

    AppFrameClock clock;
    const Clock::time_point start{};
    clock.reset(start);

    const FrameTiming first = clock.tick(start + std::chrono::milliseconds(16));
    EXPECT_EQ(first.frame_index, 0u);
    EXPECT_NEAR(first.raw_delta_seconds, 0.016, 0.000001);
    EXPECT_NEAR(first.delta_seconds, 0.016, 0.000001);
    EXPECT_NEAR(first.elapsed_seconds, 0.016, 0.000001);

    const FrameTiming clamped = clock.tick(start + std::chrono::seconds(2));
    EXPECT_EQ(clamped.frame_index, 1u);
    EXPECT_NEAR(clamped.raw_delta_seconds, 1.984, 0.000001);
    EXPECT_NEAR(clamped.delta_seconds, AppFrameClock::kMaxDeltaSeconds,
                0.000001);
    EXPECT_NEAR(clamped.elapsed_seconds, 2.0, 0.000001);
}
