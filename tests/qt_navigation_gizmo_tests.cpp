////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_navigation_gizmo.hpp"
#include "ui/qt_viewport_chrome_layout.hpp"

namespace {

using quader::QVec2;
using quader::QVec3;
using quader::QtNavigationGizmoInteraction;
using quader::ViewportCameraController;
using quader::ViewportCameraPose;
using quader::ViewportPaneId;
using quader::ViewportProjection;

constexpr QVec2 kGizmoSize{96.0f, 96.0f};

QVec3 expected_forward(quader_editor_core::ViewAxis axis, int sign) {
  const float component = sign < 0 ? 1.0f : -1.0f;
  switch (axis) {
  case quader_editor_core::ViewAxis::X:
    return {component, 0.0f, 0.0f};
  case quader_editor_core::ViewAxis::Y:
    return {0.0f, component, 0.0f};
  case quader_editor_core::ViewAxis::Z:
    return {0.0f, 0.0f, component};
  }
  return {};
}

} // namespace

TEST(QtViewportChromeLayoutTests,
     KeepsUnifiedDisplayMenuAndToolbarInsideEveryPaneWidth) {
  constexpr int kMenuWidth = 110;
  constexpr int kInset = 6;

  for (int pane_width = 0; pane_width <= 400; ++pane_width) {
    const quader::QtViewportChromeLayout layout =
        quader::qt_viewport_chrome_layout(pane_width, kMenuWidth, kInset);
    EXPECT_GE(layout.toolbar_x, 0) << pane_width;
    EXPECT_GE(layout.toolbar_width, 0) << pane_width;
    EXPECT_LE(layout.toolbar_x + layout.toolbar_width, pane_width)
        << pane_width;
    if (pane_width > 0) {
      EXPECT_GT(layout.menu_width, 0) << pane_width;
    }
  }
}

TEST(QtViewportChromeLayoutTests,
     UsesReferenceMinimumDisplayMenuWidthWhenThePaneCanAccommodateIt) {
  constexpr int kMenuWidth = 110;
  constexpr int kInset = 6;

  const quader::QtViewportChromeLayout wide =
      quader::qt_viewport_chrome_layout(400, kMenuWidth, kInset);
  EXPECT_EQ(wide.menu_width, quader::kQtViewportDisplayMenuMinimumWidth);
  EXPECT_EQ(wide.toolbar_width, quader::kQtViewportDisplayMenuMinimumWidth);
  EXPECT_EQ(wide.toolbar_x,
            400 - kInset - quader::kQtViewportDisplayMenuMinimumWidth);

  const quader::QtViewportChromeLayout long_label =
      quader::qt_viewport_chrome_layout(400, 180, kInset);
  EXPECT_EQ(long_label.menu_width, 180);

  const quader::QtViewportChromeLayout narrow =
      quader::qt_viewport_chrome_layout(40, kMenuWidth, kInset);
  EXPECT_EQ(narrow.menu_width, 28);
  EXPECT_EQ(narrow.toolbar_width, 28);
  EXPECT_EQ(narrow.toolbar_x, kInset);
}

TEST(QtNavigationGizmoUpdatePolicyTests,
     CameraSnapshotSkipsStablePreparesButTracksCameraMotion) {
  ViewportCameraController first_camera;
  ViewportCameraController second_camera;
  std::optional<quader::QtNavigationGizmoCameraSnapshot> current =
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera);

  EXPECT_FALSE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera)));

  first_camera.set_view_direction({1.0f, 0.0f, 0.0f});
  const quader::QtNavigationGizmoCameraSnapshot moved =
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera);
  EXPECT_TRUE(
      quader::qt_navigation_gizmo_camera_refresh_required(current, moved));

  current = moved;
  EXPECT_FALSE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera)));
  EXPECT_TRUE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&second_camera)));
}

TEST(QtNavigationGizmoUpdatePolicyTests,
     VisibilityRefreshRunsOnlyForRequestedStateTransitions) {
  EXPECT_FALSE(
      quader::qt_navigation_gizmo_visibility_refresh_required(false, false));
  EXPECT_FALSE(
      quader::qt_navigation_gizmo_visibility_refresh_required(true, true));
  EXPECT_TRUE(
      quader::qt_navigation_gizmo_visibility_refresh_required(false, true));
  EXPECT_TRUE(
      quader::qt_navigation_gizmo_visibility_refresh_required(true, false));
}

TEST(QtNavigationGizmoInteractionTests,
     FaceClickSnapsTheOwningPaneWithoutLockingOrbit) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);
  ASSERT_TRUE(primitives.ok);
  ASSERT_FALSE(primitives.faces.empty());
  const quader_editor_core::ViewCubeFacePrimitive face =
      primitives.faces.back();

  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Front, kGizmoSize,
                                face.screen_position));
  EXPECT_TRUE(interaction.input_locked());
  EXPECT_EQ(interaction.locked_pane_id(), ViewportPaneId::Front);
  EXPECT_TRUE(camera.is_dragging());

  ASSERT_TRUE(interaction.release(camera, ViewportPaneId::Front));
  EXPECT_FALSE(interaction.input_locked());
  EXPECT_FALSE(camera.is_dragging());
  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.orthographic_view_locked());

  const ViewportCameraPose pose = camera.pose();
  const QVec3 forward = normalize(pose.target - pose.eye);
  const QVec3 expected = expected_forward(face.axis, face.sign);
  EXPECT_NEAR(dot(forward, expected), 1.0f, 0.001f);
}

TEST(QtNavigationGizmoInteractionTests,
     DragOrbitsAfterThresholdAndDoesNotApplyThePressedFaceSnap) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  const ViewportCameraPose before = camera.pose();
  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);
  ASSERT_FALSE(primitives.faces.empty());
  const QVec2 press_position = primitives.faces.front().screen_position;

  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Perspective,
                                kGizmoSize, press_position));
  ASSERT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                               {12.0f, 6.0f}));
  EXPECT_TRUE(interaction.drag_started());
  ASSERT_TRUE(interaction.release(camera, ViewportPaneId::Perspective));

  const ViewportCameraPose after = camera.pose();
  EXPECT_GT(length(after.eye - before.eye), 0.001f);
  EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
  EXPECT_FALSE(camera.orthographic_view_locked());
  EXPECT_FALSE(interaction.input_locked());
}

TEST(QtNavigationGizmoInteractionTests,
     PointerLockRejectsOtherPanesUntilTheOwningPaneReleases) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);

  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Side, kGizmoSize,
                                primitives.screen_position));
  EXPECT_FALSE(
      interaction.drag(camera, ViewportPaneId::Top, {10.0f, 0.0f}));
  EXPECT_FALSE(interaction.release(camera, ViewportPaneId::Top));
  EXPECT_TRUE(interaction.input_locked());
  EXPECT_EQ(interaction.locked_pane_id(), ViewportPaneId::Side);

  interaction.cancel(camera);
  EXPECT_FALSE(interaction.input_locked());
  EXPECT_FALSE(camera.is_dragging());
}

TEST(QtNavigationGizmoInteractionTests,
     PressOutsideTheCoreViewCubeDoesNotCaptureViewportInput) {
  ViewportCameraController camera;
  QtNavigationGizmoInteraction interaction;

  EXPECT_FALSE(interaction.press(camera, ViewportPaneId::Perspective,
                                 kGizmoSize, {0.0f, 0.0f}));
  EXPECT_FALSE(interaction.input_locked());
  EXPECT_FALSE(camera.is_dragging());
}
