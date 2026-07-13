////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_relative_pointer_input.hpp"
#include "viewport/viewport_input_router.hpp"

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
using quader::ViewportCameraController;
using quader::ViewportInputFrame;
using quader::ViewportInputRouter;
using quader::platform_pointer_capture_transition_achieved;
using quader::qt_relative_pointer_capture_requires_recenter;

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
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
  delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 0.0f);
  EXPECT_FLOAT_EQ(delta.y, 0.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);

  stream.add_qt_delta({3.0f, 5.0f});
  delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 3.0f);
  EXPECT_FLOAT_EQ(delta.y, 5.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
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
     QtMotionQueuedBeforeRawClassificationCannotCauseCaptureJump) {
  QtRelativePointerStreamAccumulator stream;

  // Under event pressure (for example held WASD plus mouse look), Qt can
  // translate a cursor-sized legacy/warp move before the native filter sees
  // the matching relative raw packet. Raw must replace, not add to, that move.
  stream.add_qt_delta({350.0f, -180.0f});
  stream.note_relative_raw_delta({3.0f, -2.0f});

  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::RelativeRaw);
  const QVec2 delta = stream.take();
  EXPECT_FLOAT_EQ(delta.x, 3.0f);
  EXPECT_FLOAT_EQ(delta.y, -2.0f);
}

TEST(QtRelativePointerInputTests,
     RelativeRawSourceSurvivesFrameDrainsAndCoalescedLegacyMotion) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({2.0f, -1.0f});

  const QVec2 first_frame_delta = stream.take();
  EXPECT_FLOAT_EQ(first_frame_delta.x, 2.0f);
  EXPECT_FLOAT_EQ(first_frame_delta.y, -1.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::RelativeRaw);

  // Windows does not preserve a one-to-one relationship between WM_INPUT and
  // translated Qt moves. Neither a delayed echo nor an extra coalesced/warp
  // move may enter a capture whose authoritative source is relative raw input.
  stream.add_qt_delta({100.0f, 50.0f});
  stream.add_qt_delta({-80.0f, 40.0f});
  const QVec2 delayed_legacy_delta = stream.take();
  EXPECT_FLOAT_EQ(delayed_legacy_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(delayed_legacy_delta.y, 0.0f);

  stream.note_relative_raw_delta({6.0f, -7.0f});
  const QVec2 next_raw_delta = stream.take();
  EXPECT_FLOAT_EQ(next_raw_delta.x, 6.0f);
  EXPECT_FLOAT_EQ(next_raw_delta.y, -7.0f);
}

TEST(QtRelativePointerInputTests,
     DelayedCapturedWarpDoesNotSnapHeldFlyMovementAcrossFrameDrains) {
  QtRelativePointerStreamAccumulator stream;
  ViewportInputRouter router;
  ViewportCameraController camera;
  ViewportInputFrame frame;
  frame.viewport_hovered = true;
  frame.viewport_focused = true;
  frame.viewport_size = {800.0f, 600.0f};
  frame.mouse_screen = {400.0f, 300.0f};
  frame.delta_seconds = 1.0f / 60.0f;
  frame.navigation_bindings.fly_pressed = true;
  frame.navigation_bindings.fly_down = true;
  ASSERT_TRUE(router.handle_frame(frame, camera).fly_navigation_active);

  // A physical move can overtake the queued capture-recenter event. Its raw
  // packet is authoritative; the translated legacy move must not be mixed in.
  stream.note_relative_raw_delta({80.0f, -40.0f});
  stream.add_qt_delta({80.0f, -40.0f});
  frame.navigation_bindings.fly_pressed = false;
  frame.mouse_delta = stream.take();
  frame.fly_local_direction = {1.0f, 0.0f, 1.0f};
  ASSERT_TRUE(router.handle_frame(frame, camera).camera_changed);
  const quader::ViewportCameraPose after_raw = camera.pose();
  const QVec2 expected_raw_delta = frame.mouse_delta;
  EXPECT_FLOAT_EQ(expected_raw_delta.x, 80.0f);
  EXPECT_FLOAT_EQ(expected_raw_delta.y, -40.0f);

  // The delayed Qt warp reaches the surface after the raw frame drained. It
  // is an inverse, cursor-sized absolute delta and previously snapped the
  // camera back while RMB and W/D remained held.
  stream.add_qt_delta({-80.0f, 40.0f});
  frame.mouse_delta = stream.take();
  ASSERT_TRUE(router.handle_frame(frame, camera).camera_changed);
  const quader::ViewportCameraPose after_delayed_warp = camera.pose();
  EXPECT_FLOAT_EQ(frame.mouse_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(frame.mouse_delta.y, 0.0f);

  const quader::QVec3 forward_after_raw =
      quader::normalize(after_raw.target - after_raw.eye);
  const quader::QVec3 forward_after_delayed_warp = quader::normalize(
      after_delayed_warp.target - after_delayed_warp.eye);
  EXPECT_NEAR(forward_after_delayed_warp.x, forward_after_raw.x, 0.0001f);
  EXPECT_NEAR(forward_after_delayed_warp.y, forward_after_raw.y, 0.0001f);
  EXPECT_NEAR(forward_after_delayed_warp.z, forward_after_raw.z, 0.0001f);
  EXPECT_GT(quader::length(after_delayed_warp.eye - after_raw.eye), 0.0f);
}

TEST(QtRelativePointerInputTests,
     ButtonOnlyEventsDoNotChangeAuthoritativeRawSource) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({2.0f, -1.0f});
  (void)stream.take();

  stream.add_qt_delta({0.0f, 0.0f});
  stream.add_qt_delta({100.0f, 50.0f});
  const QVec2 delayed_legacy_delta = stream.take();
  EXPECT_FLOAT_EQ(delayed_legacy_delta.x, 0.0f);
  EXPECT_FLOAT_EQ(delayed_legacy_delta.y, 0.0f);

  stream.note_absolute_raw_motion();
  stream.add_qt_delta({6.0f, -7.0f});
  const QVec2 standalone_fallback_delta = stream.take();
  EXPECT_FLOAT_EQ(standalone_fallback_delta.x, 6.0f);
  EXPECT_FLOAT_EQ(standalone_fallback_delta.y, -7.0f);
}

TEST(QtRelativePointerInputTests,
     AbsoluteRawPacketExpiresMissingRelativeLegacyEcho) {
  QtRelativePointerStreamAccumulator stream;
  stream.note_relative_raw_delta({2.0f, -1.0f});

  const QVec2 first_frame_delta = stream.take();
  EXPECT_FLOAT_EQ(first_frame_delta.x, 2.0f);
  EXPECT_FLOAT_EQ(first_frame_delta.y, -1.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::RelativeRaw);

  // Windows may coalesce or omit the translated move for the relative packet.
  // Once an absolute device packet arrives, its following Qt move is genuine
  // fallback input and must not be consumed by the stale suppression token.
  stream.note_absolute_raw_motion();
  stream.add_qt_delta({6.0f, -7.0f});
  const QVec2 absolute_fallback_delta = stream.take();
  EXPECT_FLOAT_EQ(absolute_fallback_delta.x, 6.0f);
  EXPECT_FLOAT_EQ(absolute_fallback_delta.y, -7.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
}

TEST(QtRelativePointerInputTests,
     SourceTransitionsNeverMixRelativeAndAbsoluteMotionStreams) {
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
  EXPECT_FLOAT_EQ(delta.x, -3.0f);
  EXPECT_FLOAT_EQ(delta.y, 4.0f);

  // The final absolute packet keeps Qt authoritative in the following frame.
  stream.add_qt_delta({6.0f, -7.0f});
  const QVec2 next_delta = stream.take();
  EXPECT_FLOAT_EQ(next_delta.x, 6.0f);
  EXPECT_FLOAT_EQ(next_delta.y, -7.0f);
  EXPECT_EQ(stream.packet_mode(), QtRelativePointerPacketMode::QtFallback);
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
     OnlyConfirmedRelativeRawCaptureAvoidsRecentering) {
  EXPECT_FALSE(qt_relative_pointer_capture_requires_recenter(
      true, QtRelativePointerPacketMode::RelativeRaw));
  EXPECT_TRUE(qt_relative_pointer_capture_requires_recenter(
      true, QtRelativePointerPacketMode::QtFallback));
  EXPECT_TRUE(qt_relative_pointer_capture_requires_recenter(
      true, QtRelativePointerPacketMode::Undetermined));
  EXPECT_TRUE(qt_relative_pointer_capture_requires_recenter(
      false, QtRelativePointerPacketMode::RelativeRaw));
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
