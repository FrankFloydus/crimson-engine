////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_relative_pointer_input.hpp"

namespace {

using quader::QVec2;
using quader::QtPointerCaptureFailure;
using quader::QtPointerCapturePolicy;
using quader::QtPointerCaptureTransition;
using quader::QtPointerCaptureWarningGate;
using quader::QtRelativePointerDeltaAccumulator;
using quader::QtRelativePointerPacketMode;
using quader::QtRelativePointerStreamAccumulator;
using quader::PlatformPointerCapturePostcondition;
using quader::platform_pointer_capture_transition_achieved;

TEST(QtRelativePointerInputTests, SumsEveryDeltaUntilOneFrameDrainsIt) {
  QtRelativePointerDeltaAccumulator accumulator;
  accumulator.add({2.5f, -1.0f});
  accumulator.add({-0.5f, 4.0f});
  accumulator.add({8.0f, -3.0f});

  const QVec2 frame_delta = accumulator.take();
  EXPECT_FLOAT_EQ(frame_delta.x, 10.0f);
  EXPECT_FLOAT_EQ(frame_delta.y, 0.0f);

  const QVec2 next_frame_delta = accumulator.take();
  EXPECT_FLOAT_EQ(next_frame_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(next_frame_delta.y, 0.0f);
}

TEST(QtRelativePointerInputTests, ClearDiscardsPendingMotion) {
  QtRelativePointerDeltaAccumulator accumulator;
  accumulator.add({13.0f, -7.0f});
  accumulator.clear();

  const QVec2 delta = accumulator.take();
  EXPECT_FLOAT_EQ(delta.x, 0.0f);
  EXPECT_FLOAT_EQ(delta.y, 0.0f);
}

TEST(QtRelativePointerInputTests,
     FallbackAndAbsolutePacketsUseTranslatedQtMotion) {
  QtRelativePointerStreamAccumulator stream;
  stream.add_qt_delta({4.0f, -2.0f});
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::Undetermined);
  QVec2 delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 4.0f);
  EXPECT_FLOAT_EQ(delta.y, -2.0f);

  stream.note_absolute_raw_motion();
  stream.add_qt_delta({3.0f, 5.0f});
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
  delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 3.0f);
  EXPECT_FLOAT_EQ(delta.y, 5.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::Undetermined);
}

TEST(QtRelativePointerInputTests,
     RelativeRawPacketsSuppressTheirFollowingLegacyQtMotion) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({2.0f, -1.0f});
  stream.add_qt_delta({100.0f, 50.0f});
  stream.note_relative_raw_delta({3.0f, 4.0f});
  stream.add_qt_delta({100.0f, 50.0f});

  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::RelativeRaw);
  const QVec2 delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 5.0f);
  EXPECT_FLOAT_EQ(delta.y, 3.0f);
  EXPECT_FLOAT_EQ(stream.take().x, 0.0f);
  EXPECT_FLOAT_EQ(stream.take().y, 0.0f);
}

TEST(QtRelativePointerInputTests,
     MixedRelativeAndAbsolutePacketsRetainBothGenuineMotionStreams) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({2.0f, -1.0f});
  stream.add_qt_delta({100.0f, 50.0f});
  stream.note_absolute_raw_motion();
  stream.add_qt_delta({12.0f, 8.0f});
  stream.note_relative_raw_delta({1.0f, 2.0f});
  stream.add_qt_delta({100.0f, 50.0f});
  stream.note_absolute_raw_motion();
  stream.add_qt_delta({-3.0f, 4.0f});

  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
  const QVec2 delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 12.0f);
  EXPECT_FLOAT_EQ(delta.y, 13.0f);

  // The frame drain resets packet routing, so fallback-only motion in a later
  // interval is never suppressed by a previous relative packet.
  stream.add_qt_delta({6.0f, -7.0f});
  const QVec2 next_delta = stream.take();
  EXPECT_FLOAT_EQ(next_delta.x, 6.0f);
  EXPECT_FLOAT_EQ(next_delta.y, -7.0f);
}

TEST(QtRelativePointerInputTests,
     ZeroRawPacketDoesNotSuppressFallbackAndResetDiscardsAllMotion) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({0.0f, 0.0f});
  stream.add_qt_delta({9.0f, -2.0f});
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::Undetermined);
  QVec2 delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 9.0f);
  EXPECT_FLOAT_EQ(delta.y, -2.0f);

  stream.note_relative_raw_delta({5.0f, 7.0f});
  stream.note_absolute_raw_motion();
  stream.add_qt_delta({11.0f, 13.0f});
  stream.reset();
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::Undetermined);
  delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 0.0f);
  EXPECT_FLOAT_EQ(delta.y, 0.0f);
}

TEST(QtRelativePointerInputTests,
     EnableFailureNeverAdvertisesFakeCaptureAndCanRetry) {
  QtPointerCapturePolicy policy;

  EXPECT_EQ(policy.apply(true, false),
            QtPointerCaptureTransition::EnableFailed);
  EXPECT_FALSE(policy.captured());

  EXPECT_EQ(policy.apply(true, true), QtPointerCaptureTransition::Enabled);
  EXPECT_TRUE(policy.captured());
  EXPECT_EQ(policy.apply(true, false), QtPointerCaptureTransition::NoChange);
  EXPECT_TRUE(policy.captured());
}

TEST(QtRelativePointerInputTests,
     NativeCapturePostconditionOverridesToolkitRequestedStateResult) {
  EXPECT_FALSE(platform_pointer_capture_transition_achieved(
      true, PlatformPointerCapturePostcondition{.available = true,
                                                .achieved = false}));
  EXPECT_TRUE(platform_pointer_capture_transition_achieved(
      false, PlatformPointerCapturePostcondition{.available = true,
                                                 .achieved = true}));
}

TEST(QtRelativePointerInputTests,
     ToolkitFallbackUsesTransitionSuccessWithoutNativePostcondition) {
  EXPECT_TRUE(platform_pointer_capture_transition_achieved(
      true, PlatformPointerCapturePostcondition{}));
  EXPECT_FALSE(platform_pointer_capture_transition_achieved(
      false, PlatformPointerCapturePostcondition{}));
}

TEST(QtRelativePointerInputTests, DisablePostconditionClearsLogicalCapture) {
  QtPointerCapturePolicy policy;
  ASSERT_EQ(policy.apply(true, true), QtPointerCaptureTransition::Enabled);

  EXPECT_EQ(policy.apply(false, true), QtPointerCaptureTransition::Disabled);
  EXPECT_FALSE(policy.captured());
  EXPECT_EQ(policy.apply(false, true), QtPointerCaptureTransition::NoChange);
}

TEST(QtRelativePointerInputTests,
     FailedDisablePostconditionStillClearsLogicalCapture) {
  QtPointerCapturePolicy policy;
  ASSERT_EQ(policy.apply(true, true), QtPointerCaptureTransition::Enabled);

  EXPECT_EQ(policy.apply(false, false),
            QtPointerCaptureTransition::DisableFailed);
  EXPECT_FALSE(policy.captured());
}

TEST(QtRelativePointerInputTests, WarningsAreDeduplicatedPerFailureClass) {
  QtPointerCaptureWarningGate warnings;

  EXPECT_TRUE(warnings.should_warn(QtPointerCaptureFailure::Enable));
  EXPECT_FALSE(warnings.should_warn(QtPointerCaptureFailure::Enable));
  EXPECT_TRUE(warnings.should_warn(QtPointerCaptureFailure::Disable));
  EXPECT_FALSE(warnings.should_warn(QtPointerCaptureFailure::Disable));

  warnings.note_success(QtPointerCaptureFailure::Enable);
  EXPECT_TRUE(warnings.should_warn(QtPointerCaptureFailure::Enable));
  EXPECT_FALSE(warnings.should_warn(QtPointerCaptureFailure::Disable));
}

} // namespace
