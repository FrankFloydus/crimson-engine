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
using quader::ViewportAxisLock;
using quader::ViewportDragMode;
using quader::ViewportPaneId;
using quader::ViewportProjection;
using quader::ViewportViewAxis;

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
     CameraSnapshotSkipsPanAndDollyButTracksOrientation) {
  ViewportCameraController first_camera;
  ViewportCameraController second_camera;
  first_camera.set_viewport_size(800.0f, 600.0f);
  std::optional<quader::QtNavigationGizmoCameraSnapshot> current =
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera);

  EXPECT_FALSE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera)));

  first_camera.begin_drag({100.0f, 100.0f}, ViewportDragMode::Pan);
  first_camera.update_drag({140.0f, 125.0f});
  first_camera.end_drag();
  EXPECT_FALSE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera)));

  first_camera.scroll(2.0f);
  EXPECT_FALSE(quader::qt_navigation_gizmo_camera_refresh_required(
      current,
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera)));

  first_camera.set_view_direction({1.0f, 0.0f, 0.0f});
  const quader::QtNavigationGizmoCameraSnapshot rotated =
      quader::qt_navigation_gizmo_camera_snapshot(&first_camera);
  EXPECT_TRUE(
      quader::qt_navigation_gizmo_camera_refresh_required(current, rotated));

  current = rotated;
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

TEST(QtNavigationGizmoUpdatePolicyTests,
     EmptyHoverHitsRemainStableUntilARealHitChanges) {
  const quader_editor_core::ViewCubeHit empty;
  EXPECT_TRUE(quader::qt_navigation_gizmo_hits_match(empty, empty));

  quader_editor_core::ViewCubeHit hit;
  hit.hit = true;
  EXPECT_FALSE(quader::qt_navigation_gizmo_hits_match(empty, hit));
  EXPECT_FALSE(quader::qt_navigation_gizmo_hits_match(hit, empty));
  EXPECT_TRUE(quader::qt_navigation_gizmo_hits_match(hit, hit));
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
  EXPECT_FALSE(camera.is_dragging());

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
     DragThresholdPreservesMotionAcrossEquivalentEventChunking) {
  const auto run_drag = [](bool split_motion) {
    ViewportCameraController camera;
    camera.set_viewport_size(800.0f, 600.0f);
    QtNavigationGizmoInteraction interaction;
    const quader_editor_core::ViewCubePrimitives primitives =
        interaction.primitives(camera, kGizmoSize);
    EXPECT_FALSE(primitives.faces.empty());
    if (primitives.faces.empty()) {
      return camera.pose();
    }
    EXPECT_TRUE(interaction.press(camera, ViewportPaneId::Perspective,
                                  kGizmoSize,
                                  primitives.faces.front().screen_position));
    if (split_motion) {
      EXPECT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                                   {3.0f, 0.0f}));
      EXPECT_FALSE(interaction.drag_started());
      EXPECT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                                   {3.0f, 0.0f}));
    } else {
      EXPECT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                                   {6.0f, 0.0f}));
    }
    EXPECT_TRUE(interaction.drag_started());
    EXPECT_TRUE(interaction.release(camera, ViewportPaneId::Perspective));
    return camera.pose();
  };

  const ViewportCameraPose single_event = run_drag(false);
  const ViewportCameraPose split_events = run_drag(true);
  const auto expect_vector_near = [](QVec3 actual, QVec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.000001f);
    EXPECT_NEAR(actual.y, expected.y, 0.000001f);
    EXPECT_NEAR(actual.z, expected.z, 0.000001f);
  };
  expect_vector_near(split_events.eye, single_event.eye);
  expect_vector_near(split_events.target, single_event.target);
  expect_vector_near(split_events.up, single_event.up);
}

TEST(QtNavigationGizmoInteractionTests,
     OrthographicZoomConvertsToEquivalentPerspectiveScaleWhenDragStarts) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(30.0f);
  camera.view_axis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);
  camera.scroll(-64.0f);
  const float orthographic_size = camera.state().orthographic_size;
  ASSERT_GT(orthographic_size, 1200.0f);
  ASSERT_LE(orthographic_size, 1200.0f * 1.18f + 0.001f);

  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);
  ASSERT_FALSE(primitives.faces.empty());
  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Perspective,
                                kGizmoSize,
                                primitives.faces.front().screen_position));

  ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FALSE(camera.is_dragging());
  ASSERT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                               {12.0f, 6.0f}));
  ASSERT_EQ(camera.state().projection, ViewportProjection::Perspective);
  const float perspective_visible_height =
      2.0f * camera.state().distance *
      std::tan(camera.state().fov_degrees *
               static_cast<float>(std::acos(-1.0)) / 360.0f);
  EXPECT_NEAR(perspective_visible_height, orthographic_size, 0.001f);
  EXPECT_GT(camera.state().distance, 500.0f);

  interaction.cancel(camera);
}

TEST(QtNavigationGizmoInteractionTests,
     CanceledOrthographicPressIsCameraNeutral) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(30.0f);
  camera.view_axis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);
  camera.scroll(-4.0f);
  const ViewportCameraPose before = camera.pose();
  const float size_before = camera.state().orthographic_size;

  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);
  ASSERT_FALSE(primitives.faces.empty());
  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Perspective,
                                kGizmoSize,
                                primitives.faces.front().screen_position));
  interaction.cancel(camera);

  EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
  EXPECT_FLOAT_EQ(camera.state().orthographic_size, size_before);
  EXPECT_EQ(quader::viewport_camera_pose_persistence_signature(camera.pose()),
            quader::viewport_camera_pose_persistence_signature(before));
  EXPECT_FALSE(camera.is_dragging());
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

TEST(QtNavigationGizmoInteractionTests,
     AbandonDropsPointerStateWithoutCallingThePreviousCamera) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  QtNavigationGizmoInteraction interaction;
  const quader_editor_core::ViewCubePrimitives primitives =
      interaction.primitives(camera, kGizmoSize);
  ASSERT_TRUE(primitives.ok);
  ASSERT_FALSE(primitives.faces.empty());

  ASSERT_TRUE(interaction.press(camera, ViewportPaneId::Perspective,
                                kGizmoSize,
                                primitives.faces.front().screen_position));
  ASSERT_TRUE(interaction.input_locked());
  ASSERT_TRUE(interaction.drag(camera, ViewportPaneId::Perspective,
                               {20.0f, 0.0f}));
  ASSERT_TRUE(camera.is_dragging());

  interaction.abandon();

  EXPECT_FALSE(interaction.input_locked());
  EXPECT_FALSE(interaction.drag_started());
  // Owner-free abandon intentionally cannot end the previous camera drag.
  // A normal live-owner cancel remains responsible for that path.
  EXPECT_TRUE(camera.is_dragging());
  camera.end_drag();
}
