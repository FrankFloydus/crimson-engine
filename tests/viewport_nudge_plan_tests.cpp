////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/viewport_nudge_plan.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace {

using quader::QVec3;
using quader::ViewportNudgeAction;
using quader::ViewportNudgeAxis;
using quader::ViewportNudgePlanRequest;

constexpr float kEpsilon = 0.00001F;
constexpr std::array kActions = {
    ViewportNudgeAction::MoveUp,     ViewportNudgeAction::MoveDown,
    ViewportNudgeAction::MoveLeft,   ViewportNudgeAction::MoveRight,
    ViewportNudgeAction::RotateUp,   ViewportNudgeAction::RotateDown,
    ViewportNudgeAction::RotateLeft, ViewportNudgeAction::RotateRight};

void expect_vector_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kEpsilon);
}

void expect_move(ViewportNudgePlanRequest request, ViewportNudgeAction action,
                 QVec3 expected) {
  request.action = action;
  const auto plan = quader::make_viewport_nudge_plan(request);
  ASSERT_TRUE(plan);
  expect_vector_near(plan->move_delta, expected);
  EXPECT_EQ(plan->rotate_axis, ViewportNudgeAxis::None);
  EXPECT_DOUBLE_EQ(plan->rotate_angle_degrees, 0.0);
}

void expect_rotation(ViewportNudgePlanRequest request,
                     ViewportNudgeAction action, ViewportNudgeAxis axis,
                     double degrees) {
  request.action = action;
  const auto plan = quader::make_viewport_nudge_plan(request);
  ASSERT_TRUE(plan);
  expect_vector_near(plan->move_delta, {});
  EXPECT_EQ(plan->rotate_axis, axis);
  EXPECT_DOUBLE_EQ(plan->rotate_angle_degrees, degrees);
}

TEST(ViewportNudgePlanTests, OrthographicViewsAndRollResolveAllEightActions) {
  struct ViewFixture {
    const char *name;
    QVec3 forward;
    QVec3 up;
    QVec3 right;
    QVec3 left_step;
    QVec3 up_step;
    ViewportNudgeAxis up_axis;
    double up_degrees;
    ViewportNudgeAxis right_axis;
    double right_degrees;
  };
  constexpr std::array<ViewFixture, 9> kViews = {{
      {"Front",
       {0, 0, 1},
       {0, 1, 0},
       {-1, 0, 0},
       {1, 0, 0},
       {0, 1, 0},
       ViewportNudgeAxis::X,
       45,
       ViewportNudgeAxis::Z,
       45},
      {"Back",
       {0, 0, -1},
       {0, 1, 0},
       {1, 0, 0},
       {-1, 0, 0},
       {0, 1, 0},
       ViewportNudgeAxis::X,
       -45,
       ViewportNudgeAxis::Z,
       -45},
      {"Side",
       {-1, 0, 0},
       {0, 1, 0},
       {0, 0, -1},
       {0, 0, 1},
       {0, 1, 0},
       ViewportNudgeAxis::Z,
       45,
       ViewportNudgeAxis::X,
       -45},
      {"Left",
       {1, 0, 0},
       {0, 1, 0},
       {0, 0, 1},
       {0, 0, -1},
       {0, 1, 0},
       ViewportNudgeAxis::Z,
       -45,
       ViewportNudgeAxis::X,
       45},
      {"Top",
       {0, -1, 0},
       {0, 0, -1},
       {1, 0, 0},
       {-1, 0, 0},
       {0, 0, -1},
       ViewportNudgeAxis::X,
       -45,
       ViewportNudgeAxis::Y,
       -45},
      {"Bottom",
       {0, 1, 0},
       {0, 0, 1},
       {1, 0, 0},
       {-1, 0, 0},
       {0, 0, 1},
       ViewportNudgeAxis::X,
       -45,
       ViewportNudgeAxis::Y,
       45},
      {"RolledTop",
       {0, -1, 0},
       {1, 0, 0},
       {0, 0, 1},
       {0, 0, -1},
       {1, 0, 0},
       ViewportNudgeAxis::Z,
       -45,
       ViewportNudgeAxis::Y,
       -45},
      {"RolledBottom",
       {0, 1, 0},
       {1, 0, 0},
       {0, 0, -1},
       {0, 0, 1},
       {1, 0, 0},
       ViewportNudgeAxis::Z,
       45,
       ViewportNudgeAxis::Y,
       45},
      {"InvertedFront",
       {0, 0, 1},
       {0, -1, 0},
       {1, 0, 0},
       {-1, 0, 0},
       {0, -1, 0},
       ViewportNudgeAxis::X,
       -45,
       ViewportNudgeAxis::Z,
       45},
  }};
  for (const auto &view : kViews) {
    SCOPED_TRACE(view.name);
    ViewportNudgePlanRequest request;
    request.orthographic = true;
    request.camera_forward = view.forward;
    request.camera_up = view.up;
    request.camera_right = view.right;
    request.grid_step = 4.0;
    for (const auto up_axis :
         {ViewportNudgeAxis::X, ViewportNudgeAxis::Y, ViewportNudgeAxis::Z}) {
      SCOPED_TRACE(static_cast<int>(up_axis));
      request.up_axis = up_axis;
      expect_move(request, ViewportNudgeAction::MoveUp, view.up_step * 4.0F);
      expect_move(request, ViewportNudgeAction::MoveDown, view.up_step * -4.0F);
      expect_move(request, ViewportNudgeAction::MoveLeft,
                  view.left_step * 4.0F);
      expect_move(request, ViewportNudgeAction::MoveRight,
                  view.left_step * -4.0F);
      expect_rotation(request, ViewportNudgeAction::RotateUp, view.up_axis,
                      view.up_degrees);
      expect_rotation(request, ViewportNudgeAction::RotateDown, view.up_axis,
                      -view.up_degrees);
      expect_rotation(request, ViewportNudgeAction::RotateLeft, view.right_axis,
                      -view.right_degrees);
      expect_rotation(request, ViewportNudgeAction::RotateRight,
                      view.right_axis, view.right_degrees);
    }
  }
}

TEST(ViewportNudgePlanTests, PerspectiveVerticalCorrectionHasStrictThreshold) {
  struct PitchFixture {
    float vertical_alignment;
    ViewportNudgeAxis forward_axis;
    double right_degrees;
  };
  constexpr std::array<PitchFixture, 8> kPitches = {{
      {0.199F, ViewportNudgeAxis::X, 45},
      {0.2F, ViewportNudgeAxis::X, 45},
      {0.201F, ViewportNudgeAxis::Z, 45},
      {-0.199F, ViewportNudgeAxis::X, 45},
      {-0.2F, ViewportNudgeAxis::X, 45},
      {-0.201F, ViewportNudgeAxis::Z, -45},
      {0.8F, ViewportNudgeAxis::Z, 45},
      {-0.8F, ViewportNudgeAxis::Z, -45},
  }};
  for (const auto &pitch : kPitches) {
    SCOPED_TRACE(pitch.vertical_alignment);
    const float horizontal =
        std::sqrt(1.0F - pitch.vertical_alignment * pitch.vertical_alignment);
    ViewportNudgePlanRequest request;
    request.up_axis = ViewportNudgeAxis::Z;
    request.camera_forward = {horizontal, 0.0F, pitch.vertical_alignment};
    request.camera_up = {-pitch.vertical_alignment, 0.0F, horizontal};
    request.camera_right = {0.0F, -1.0F, 0.0F};
    expect_rotation(request, ViewportNudgeAction::RotateRight,
                    pitch.forward_axis, pitch.right_degrees);
    expect_rotation(request, ViewportNudgeAction::RotateLeft,
                    pitch.forward_axis, -pitch.right_degrees);
    expect_rotation(request, ViewportNudgeAction::RotateUp,
                    ViewportNudgeAxis::Y, 45.0);
    request.orthographic = true;
    const bool near_vertical = std::abs(pitch.vertical_alignment) > 0.5F;
    expect_rotation(request, ViewportNudgeAction::RotateRight,
                    near_vertical ? ViewportNudgeAxis::Z : ViewportNudgeAxis::X,
                    near_vertical ? pitch.right_degrees : 45.0);
  }
}

TEST(ViewportNudgePlanTests,
     UprightPerspectiveYawAndPitchRotateHorizontallyAroundNativeY) {
  constexpr float kShallowPitch = 0.5235987756F;
  constexpr float kSteepPitch = 1.0471975512F;
  for (const float yaw : {0.0F, 0.4F, 1.3F, 2.7F, -0.9F, -2.2F}) {
    SCOPED_TRACE(yaw);
    for (const float pitch :
         {kShallowPitch, -kShallowPitch, kSteepPitch, -kSteepPitch}) {
      SCOPED_TRACE(pitch);
      ViewportNudgePlanRequest request;
      ASSERT_EQ(request.up_axis, ViewportNudgeAxis::Y);
      request.camera_forward = {std::cos(pitch) * std::sin(yaw),
                                std::sin(pitch),
                                -std::cos(pitch) * std::cos(yaw)};
      request.camera_right = quader::normalize(
          quader::cross(request.camera_forward, {0.0F, 1.0F, 0.0F}));
      request.camera_up =
          quader::cross(request.camera_right, request.camera_forward);
      expect_rotation(request, ViewportNudgeAction::RotateRight,
                      ViewportNudgeAxis::Y, pitch > 0.0F ? 45.0 : -45.0);
      expect_rotation(request, ViewportNudgeAction::RotateLeft,
                      ViewportNudgeAxis::Y, pitch > 0.0F ? -45.0 : 45.0);
    }
  }
}

TEST(ViewportNudgePlanTests, ExactTiesUsePositiveAxesBeforeNegativeAxes) {
  struct TieFixture {
    QVec3 direction;
    QVec3 expected;
  };
  constexpr std::array<TieFixture, 12> kTies = {{
      {{1, 1, 0}, {1, 0, 0}},
      {{1, -1, 0}, {1, 0, 0}},
      {{-1, 1, 0}, {0, 1, 0}},
      {{-1, -1, 0}, {-1, 0, 0}},
      {{1, 0, 1}, {1, 0, 0}},
      {{1, 0, -1}, {1, 0, 0}},
      {{-1, 0, 1}, {0, 0, 1}},
      {{-1, 0, -1}, {-1, 0, 0}},
      {{0, 1, 1}, {0, 1, 0}},
      {{0, 1, -1}, {0, 1, 0}},
      {{0, -1, 1}, {0, 0, 1}},
      {{0, -1, -1}, {0, -1, 0}},
  }};
  for (std::size_t index = 0; index < kTies.size(); ++index) {
    SCOPED_TRACE(index);
    const auto &tie = kTies[index];
    ViewportNudgePlanRequest request;
    request.orthographic = true;
    request.camera_up = tie.direction;
    request.camera_right = tie.direction.x == 0.0F   ? QVec3{1, 0, 0}
                           : tie.direction.y == 0.0F ? QVec3{0, 1, 0}
                                                     : QVec3{0, 0, 1};
    request.camera_forward =
        quader::cross(request.camera_up, request.camera_right);
    expect_move(request, ViewportNudgeAction::MoveUp, tie.expected);
    expect_move(request, ViewportNudgeAction::MoveDown, tie.expected * -1.0F);
  }
}

TEST(ViewportNudgePlanTests,
     PerspectiveCorrectionRetainsInvertedViewportUpSign) {
  for (const float pitch : {0.201F, -0.201F}) {
    SCOPED_TRACE(pitch);
    const float horizontal = std::sqrt(1.0F - pitch * pitch);
    ViewportNudgePlanRequest request;
    request.up_axis = ViewportNudgeAxis::Z;
    request.camera_forward = {horizontal, 0.0F, pitch};
    request.camera_up = {pitch, 0.0F, -horizontal};
    request.camera_right = {0.0F, 1.0F, 0.0F};
    expect_move(request, ViewportNudgeAction::MoveUp, {0.0F, 0.0F, -1.0F});
    expect_rotation(request, ViewportNudgeAction::RotateRight,
                    ViewportNudgeAxis::Z, pitch > 0.0F ? -45.0 : 45.0);
    expect_rotation(request, ViewportNudgeAction::RotateLeft,
                    ViewportNudgeAxis::Z, pitch > 0.0F ? 45.0 : -45.0);
  }
}

QVec3 reorient_frame_vector(QVec3 vector, bool reflected) {
  if (reflected) {
    vector.x *= -1.0F;
  }
  const auto axis = quader::normalize(QVec3{1.0F, 2.0F, 3.0F});
  constexpr float kAngle = 0.73F;
  return vector * std::cos(kAngle) +
         quader::cross(axis, vector) * std::sin(kAngle) +
         axis * (quader::dot(axis, vector) * (1.0F - std::cos(kAngle)));
}

TEST(ViewportNudgePlanTests,
     ReorientedAndReflectedComponentFramesPreserveNormalAxisNudgeIntent) {
  ViewportNudgePlanRequest request;
  request.up_axis = ViewportNudgeAxis::Z;
  request.camera_forward = quader::normalize(QVec3{0.8F, 0.45F, 0.3F});
  request.camera_right = quader::normalize(
      quader::cross(request.camera_forward, {0.0F, 0.0F, 1.0F}));
  request.camera_up =
      quader::cross(request.camera_right, request.camera_forward);
  request.grid_step = 3.75;
  expect_rotation(request, ViewportNudgeAction::RotateRight,
                  ViewportNudgeAxis::Z, 45.0);
  expect_rotation(request, ViewportNudgeAction::RotateLeft,
                  ViewportNudgeAxis::Z, -45.0);
  for (const bool reflected : {false, true}) {
    SCOPED_TRACE(reflected);
    for (const bool orthographic : {false, true}) {
      SCOPED_TRACE(orthographic);
      request.orthographic = orthographic;
      for (const auto action : kActions) {
        SCOPED_TRACE(static_cast<int>(action));
        request.action = action;
        auto rotated = request;
        rotated.camera_right =
            reorient_frame_vector(request.camera_right, reflected);
        rotated.camera_up = reorient_frame_vector(request.camera_up, reflected);
        rotated.camera_forward =
            reorient_frame_vector(request.camera_forward, reflected);
        rotated.gizmo_x_axis =
            reorient_frame_vector(request.gizmo_x_axis, reflected);
        rotated.gizmo_y_axis =
            reorient_frame_vector(request.gizmo_y_axis, reflected);
        rotated.gizmo_z_axis =
            reorient_frame_vector(request.gizmo_z_axis, reflected);
        const auto original_plan = quader::make_viewport_nudge_plan(request);
        const auto rotated_plan = quader::make_viewport_nudge_plan(rotated);
        ASSERT_TRUE(original_plan);
        ASSERT_TRUE(rotated_plan);
        expect_vector_near(
            rotated_plan->move_delta,
            reorient_frame_vector(original_plan->move_delta, reflected));
        EXPECT_EQ(rotated_plan->rotate_axis, original_plan->rotate_axis);
        EXPECT_DOUBLE_EQ(rotated_plan->rotate_angle_degrees,
                         original_plan->rotate_angle_degrees);
      }
    }
  }
}

TEST(ViewportNudgePlanTests,
     NormalizesScaledDirectionsAndPreservesReflectedAxis) {
  ViewportNudgePlanRequest request;
  request.camera_right = {9.0F, 0.0F, 0.0F};
  request.camera_up = {0.0F, 4.0F, 0.0F};
  request.camera_forward = {0.0F, 0.0F, -7.0F};
  request.gizmo_x_axis = {-3.0F, 0.0F, 0.0F};
  request.gizmo_y_axis = {0.0F, 8.0F, 0.0F};
  request.gizmo_z_axis = {0.0F, 0.0F, 2.0F};
  request.grid_step = 2.5;
  expect_move(request, ViewportNudgeAction::MoveLeft, {-2.5F, 0.0F, 0.0F});
  expect_move(request, ViewportNudgeAction::MoveUp, {0.0F, 2.5F, 0.0F});
  expect_rotation(request, ViewportNudgeAction::RotateUp, ViewportNudgeAxis::X,
                  45.0);
  expect_rotation(request, ViewportNudgeAction::RotateRight,
                  ViewportNudgeAxis::Z, -45.0);
}

TEST(ViewportNudgePlanTests,
     DisabledGridUsesOneUnitAndRotationAlwaysUses45Degrees) {
  for (const double grid_step :
       {0.125, 16.0, 0.0, -2.0, std::numeric_limits<double>::quiet_NaN()}) {
    SCOPED_TRACE(grid_step);
    ViewportNudgePlanRequest request;
    request.grid_step = grid_step;
    request.grid_snap_enabled = false;
    expect_move(request, ViewportNudgeAction::MoveRight, {1.0F, 0.0F, 0.0F});
    expect_move(request, ViewportNudgeAction::MoveDown, {0.0F, -1.0F, 0.0F});
    for (const bool snapping : {false, true}) {
      request.grid_snap_enabled = snapping;
      expect_rotation(request, ViewportNudgeAction::RotateUp,
                      ViewportNudgeAxis::X, -45.0);
      expect_rotation(request, ViewportNudgeAction::RotateDown,
                      ViewportNudgeAxis::X, 45.0);
      expect_rotation(request, ViewportNudgeAction::RotateLeft,
                      ViewportNudgeAxis::Z, 45.0);
      expect_rotation(request, ViewportNudgeAction::RotateRight,
                      ViewportNudgeAxis::Z, -45.0);
    }
  }
}

TEST(ViewportNudgePlanTests, RejectsInvalidFramesActionsAndSnappedMoveSteps) {
  constexpr std::array kVectorMembers = {
      &ViewportNudgePlanRequest::camera_right,
      &ViewportNudgePlanRequest::camera_up,
      &ViewportNudgePlanRequest::camera_forward,
      &ViewportNudgePlanRequest::gizmo_x_axis,
      &ViewportNudgePlanRequest::gizmo_y_axis,
      &ViewportNudgePlanRequest::gizmo_z_axis};
  const std::array invalid_vectors = {
      QVec3{}, QVec3{std::numeric_limits<float>::infinity(), 0.0F, 0.0F},
      QVec3{0.0F, std::numeric_limits<float>::quiet_NaN(), 0.0F}};
  for (const auto member : kVectorMembers) {
    for (const auto invalid_vector : invalid_vectors) {
      for (const auto action :
           {ViewportNudgeAction::MoveUp, ViewportNudgeAction::RotateUp}) {
        ViewportNudgePlanRequest request;
        request.*member = invalid_vector;
        request.action = action;
        EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
      }
    }
  }
  ViewportNudgePlanRequest request;
  request.camera_forward = request.camera_up;
  EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
  request = {};
  request.gizmo_y_axis = request.gizmo_x_axis;
  EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
  request = {};
  request.action = static_cast<ViewportNudgeAction>(-1);
  EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
  for (const auto up_axis :
       {ViewportNudgeAxis::None, static_cast<ViewportNudgeAxis>(-1)}) {
    request = {};
    request.up_axis = up_axis;
    EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
  }
  for (const double step : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN(),
                            std::numeric_limits<double>::max(),
                            std::numeric_limits<double>::denorm_min()}) {
    SCOPED_TRACE(step);
    request = {};
    request.grid_step = step;
    EXPECT_FALSE(quader::make_viewport_nudge_plan(request));
  }
}

} // namespace
