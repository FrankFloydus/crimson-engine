////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <gtest/gtest-spi.h>

namespace {

bool code_after_failed_prerequisite_was_reached = false;

void run_deliberately_failing_prerequisite()
{
    ASSERT_TRUE(require_test(false, "deliberate prerequisite failure"));
    code_after_failed_prerequisite_was_reached = true;
}

} // namespace

TEST(QdrTestSupportPrerequisiteTests, CallerStopsAtFailedPrerequisite)
{
    code_after_failed_prerequisite_was_reached = false;

    EXPECT_FATAL_FAILURE(
        run_deliberately_failing_prerequisite(),
        "deliberate prerequisite failure");

    EXPECT_FALSE(code_after_failed_prerequisite_was_reached);
}

TEST(QdrTestSupportPrerequisiteTests, SubsequentIndependentTestStillRuns)
{
    EXPECT_FALSE(code_after_failed_prerequisite_was_reached);
}
