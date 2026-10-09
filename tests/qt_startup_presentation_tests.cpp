////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_startup_presentation.hpp"

namespace {

TEST(QtStartupPresentationTests,
     RequiresThreeReadyEqualSamplesAndRestartsBeforePresentation) {
  quader::QtStartupPresentationState state;
  using Action = quader::QtStartupPresentationAction;

  EXPECT_EQ(quader::qt_startup_presentation_action(state, false, 0u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1326u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1912u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1912u),
            Action::GeometryStabilized);
  EXPECT_TRUE(quader::qt_startup_native_child_should_be_visible(
      Action::GeometryStabilized));

  const Action invalidated =
      quader::qt_startup_presentation_action(state, true, 1920u);
  EXPECT_EQ(invalidated, Action::WaitForStableGeometry);
  EXPECT_FALSE(
      quader::qt_startup_native_child_should_be_visible(invalidated));
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1920u),
            Action::GeometryStabilized);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, false, 1920u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1920u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1920u),
            Action::GeometryStabilized);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1920u),
            Action::PresentationReady);
  EXPECT_TRUE(quader::qt_startup_native_child_should_be_visible(
      Action::PresentationReady));

  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 1400u),
            Action::PresentationReady);
}

TEST(QtStartupPresentationTests,
     GeometryInvalidationRestartsPendingPresentationButPreservesCompletedReveal) {
  quader::QtStartupPresentationState state;
  using Action = quader::QtStartupPresentationAction;
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::GeometryStabilized);
  quader::qt_invalidate_startup_presentation_geometry(state);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::GeometryStabilized);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::PresentationReady);

  quader::qt_invalidate_startup_presentation_geometry(state);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, false, 0u),
            Action::PresentationReady);
  // A subsequent load owns a fresh stabilization sequence even at the same size.
  state = {};
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 320u),
            Action::WaitForStableGeometry);
}

TEST(QtStartupPresentationTests,
     EmptyGeometryCannotCompleteAnOtherwiseReadyTransition) {
  quader::QtStartupPresentationState state;
  using Action = quader::QtStartupPresentationAction;
  for (int sample = 0; sample < 8; ++sample) {
    EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 0u),
              Action::WaitForStableGeometry);
  }
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 640u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 640u),
            Action::GeometryStabilized);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 0u),
            Action::WaitForStableGeometry);
  EXPECT_EQ(quader::qt_startup_presentation_action(state, true, 640u),
            Action::WaitForStableGeometry);
}

TEST(QtStartupPresentationTests,
     RejectsSavedNormalExtentInsideMaximizedNativeClient) {
  EXPECT_FALSE(quader::qt_startup_client_extents_match(1326, 1013, 1896,
                                                       1013));
  EXPECT_TRUE(quader::qt_startup_client_extents_match(1896, 1013, 1896,
                                                      1013));
  EXPECT_TRUE(quader::qt_startup_client_extents_match(1895, 1012, 1896,
                                                      1013));
  EXPECT_FALSE(
      quader::qt_startup_client_extents_match(0, 1013, 1896, 1013));
}

} // namespace
