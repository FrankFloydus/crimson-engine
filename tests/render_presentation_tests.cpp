////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/render_system.hpp"

#include <gtest/gtest.h>

#include <utility>

namespace quader {
namespace {

TEST(RenderPresentationTests, DefaultsToSynchronizedPresentation) {
  RenderSystem renderer;

  EXPECT_TRUE(renderer.vsync_enabled());
  EXPECT_TRUE(renderer.presentation_is_synchronized());
  EXPECT_TRUE(renderer.ready_for_next_frame());
  EXPECT_FALSE(renderer.supports_hidden_native_pixels());
}

TEST(RenderPresentationTests, RequestedOffIsRetainedBeforeBackendInitialization) {
  RenderSystem renderer;
  renderer.set_display_refresh_rate_hz(144.0f);
  renderer.set_vsync_enabled(false);

  EXPECT_FALSE(renderer.vsync_enabled());
  // A requested flag alone is not evidence that the Vulkan surface supports
  // Immediate. Keep display cadence until the backend confirms the mode.
  EXPECT_TRUE(renderer.presentation_is_synchronized());
  EXPECT_TRUE(renderer.ready_for_next_frame());
  EXPECT_FLOAT_EQ(renderer.display_refresh_rate_hz(), 144.0f);

  renderer.set_vsync_enabled(false);
  renderer.shutdown();
  EXPECT_FALSE(renderer.vsync_enabled());
  renderer.set_vsync_enabled(true);
  EXPECT_TRUE(renderer.vsync_enabled());
  EXPECT_TRUE(renderer.presentation_is_synchronized());
}

TEST(RenderPresentationTests, MissingSurfacesDoNotSubmitOrBlockGpuReadiness) {
  RenderSystem renderer;
  renderer.set_vsync_enabled(false);
  auto surface = renderer.create_additional_swap_chain(nullptr);

  renderer.begin_presentation_frame();
  EXPECT_FALSE(renderer.begin_frame(surface));
  EXPECT_FALSE(renderer.begin_frame());
  renderer.complete_presentation_frame(0u);

  EXPECT_FALSE(surface);
  EXPECT_EQ(surface.get(), nullptr);
  EXPECT_FALSE(surface.needs_initial_frame());
  EXPECT_TRUE(renderer.ready_for_next_frame());
  EXPECT_TRUE(renderer.presentation_is_synchronized());
  EXPECT_DOUBLE_EQ(renderer.frame_performance_stats().frames_per_second, 0.0);
}

TEST(RenderPresentationTests, EmptySurfaceHandlesRemainSafeAcrossMovesAndReset) {
  RenderAdditionalSwapChain original;
  RenderAdditionalSwapChain moved(std::move(original));
  RenderAdditionalSwapChain assigned;
  assigned = std::move(moved);

  original.reset();
  moved.reset();
  assigned.reset();
  assigned.reset();

  EXPECT_FALSE(original);
  EXPECT_FALSE(moved);
  EXPECT_FALSE(assigned);
  EXPECT_EQ(assigned.get(), nullptr);
  EXPECT_FALSE(original.needs_initial_frame());
  EXPECT_FALSE(moved.needs_initial_frame());
  EXPECT_FALSE(assigned.needs_initial_frame());
}

TEST(RenderPresentationTests, UnchangedCompositionDoesNotRepeatQueueHandoff) {
  RenderSharedQueueSubmissionState submission;
  const auto compose = [&submission]() {
    if (submission.needs_handoff()) {
      submission.complete_handoff();
    }
  };

  submission.mark_pending();
  compose();
  for (int frame = 0; frame < 120; ++frame) {
    compose();
  }
  EXPECT_EQ(submission.handoff_count(), 1u);
  EXPECT_FALSE(submission.needs_handoff());

  submission.mark_pending();
  compose();
  EXPECT_EQ(submission.handoff_count(), 2u);
}

TEST(RenderPresentationTests, NestedPaintingDoesNotConsumeActiveProducerScope) {
  RenderSharedQueueSubmissionState submission;
  submission.begin_batch();
  submission.complete_handoff();
  // A modal/nested paint may return into the producer, which can enqueue more
  // commands before the next Qt composition boundary.
  EXPECT_TRUE(submission.needs_handoff());
  submission.begin_batch();
  submission.complete_handoff();
  submission.end_batch();
  submission.complete_handoff();
  EXPECT_TRUE(submission.needs_handoff());

  submission.end_batch();
  EXPECT_TRUE(submission.needs_handoff());
  submission.complete_handoff();
  EXPECT_FALSE(submission.needs_handoff());
}

TEST(RenderPresentationTests, MissingDeviceDoesNotIssueExternalQueueFences) {
  RenderSystem renderer;
  renderer.begin_shared_command_batch();
  EXPECT_TRUE(renderer.finish_external_queue_submission());
  renderer.end_shared_command_batch();
  EXPECT_TRUE(renderer.finish_external_queue_submission());
  EXPECT_EQ(renderer.external_queue_submission_count(), 0u);
}

} // namespace
} // namespace quader
