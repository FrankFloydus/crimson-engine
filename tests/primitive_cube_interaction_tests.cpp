////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/interactive/create_primitive_tool.hpp"
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

namespace quader {
namespace {

ModelingCommandResult cube_status(const QuaderApiVoidResult &result) {
  return {.ok = result.ok(), .changed = result.changed(),
          .message = std::string(result.error_message())};
}

struct PrimitiveCubeInteractionHarness {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};
  ViewportCameraController camera;
  ViewportToolState tool_state;
  ViewportModalToolState modal;
  ViewportBoxToolContext box;
  int overlay_invalidations = 0;
  ViewportModelingToolPortAdapter ports{
      session.modeling(), {.mark_overlay_dirty = [&] { ++overlay_invalidations; }}};
  CreatePrimitiveTool tool;
  bool snap = false;

  explicit PrimitiveCubeInteractionHarness(bool cylinder = false)
      : PrimitiveCubeInteractionHarness(cylinder ? ViewportPrimitiveCreationShape::Cylinder
                                                : ViewportPrimitiveCreationShape::Box) {}

  explicit PrimitiveCubeInteractionHarness(ViewportPrimitiveCreationShape shape) {
    camera.set_viewport_size(800, 600);
    camera.focus_origin(10);
    tool_state.active_tool = ViewportToolId::CreatePrimitive;
    EXPECT_TRUE(editor.tools().activate(ViewportToolId::CreatePrimitive).ok());
    if (shape != ViewportPrimitiveCreationShape::Box) {
      const double type = shape == ViewportPrimitiveCreationShape::Cone ? 3.0
                         : shape == ViewportPrimitiveCreationShape::Sphere ? 2.0
                         : shape == ViewportPrimitiveCreationShape::Torus ? 4.0 : 1.0;
      EXPECT_TRUE(editor.tools().set_setting(kCreatePrimitiveToolId, kPrimitiveTypeSetting,
                                            type).ok());
    }
    ModelingBoxPreview preview;
    preview.ok = true;
    preview.valid = true;
    preview.drag_state = ModelingBoxDragState::Height;
    preview.corners = {QVec3{-1, -1, -1}, QVec3{-1, -1, 1},
                       QVec3{1, -1, 1}, QVec3{1, -1, -1},
                       QVec3{-1, 1, -1}, QVec3{-1, 1, 1},
                       QVec3{1, 1, 1}, QVec3{1, 1, -1}};
    preview.corner_count = 8;
    if (shape != ViewportPrimitiveCreationShape::Box) {
      for (auto &corner : preview.corners) corner.x *= 2.0f;
    }
    EXPECT_TRUE(session.primitive_creation().accept_preview(preview, "Cube").ok);
    box.live_primitive = [&] {
      return ViewportPrimitiveCreationApiAdapter::from_api(
          editor.tools().live_primitive_creation());
    };
    EXPECT_EQ(box.live_primitive()->shape, shape);
    box.begin_resize = [&](ViewportPrimitiveResizeControl control, QVec3 axis) {
      return cube_status(editor.tools().begin_primitive_resize(
          ViewportPrimitiveCreationApiAdapter::to_api(control), axis));
    };
    box.update_resize = [&](double delta) {
      return cube_status(editor.tools().update_primitive_resize(delta));
    };
    box.end_resize = [&] { return cube_status(editor.tools().end_primitive_resize()); };
    box.cancel_resize = [&] { return cube_status(editor.tools().cancel_primitive_resize()); };
    box.set_segments = [&](int axis, int value) {
      constexpr std::array<std::string_view, 3> kNames{
          kBoxSubdivisionsXSetting, kBoxSubdivisionsYSetting, kBoxSubdivisionsZSetting};
      std::string_view setting = kNames[axis];
      switch (box.live_primitive()->shape) {
      case ViewportPrimitiveCreationShape::Cylinder: setting = kCylinderSideCountSetting; break;
      case ViewportPrimitiveCreationShape::Cone: setting = kConeSideCountSetting; break;
      case ViewportPrimitiveCreationShape::Sphere:
        setting = box.live_primitive()->sphere_type == ViewportSphereType::Quad
            ? kSphereSubdivisionSetting
            : axis == 0 ? kSphereRadialSegmentsSetting : kSphereRingsSetting;
        break;
      case ViewportPrimitiveCreationShape::Torus:
        setting = axis == 0 ? kTorusMajorSegmentsSetting : kTorusMinorSegmentsSetting; break;
      case ViewportPrimitiveCreationShape::Box: break;
      }
      return cube_status(editor.tools().set_setting(kCreatePrimitiveToolId, setting,
                                                    static_cast<double>(value)));
    };
    box.make_event_at = [&](QVec2 point) {
      const auto ray = camera.ray_from_viewport_point(point);
      return ModelingBoxToolEvent{.screen_position = point,
                                   .ray = {ray.origin, ray.direction},
                                   .grid_size = 0.5, .snap_enabled = snap};
    };
    box.accept_preview = [&](std::string name, AssetId material, bool snapping) {
      return cube_status(editor.tools().accept_active_primitive(name, material, snapping));
    };
    box.session_active = [&] { return editor.tools().has_active_primitive_creation(); };
    box.apply_session = [&] { return cube_status(editor.tools().commit_active_tool_preview()); };
    box.cancel_session = [&] { return cube_status(editor.tools().cancel_active_tool_preview()); };
    modal.primitive_creation.project(box.live_primitive(), camera, 1);
  }

  ViewportToolResult frame(ViewportPointerToolEvent pointer) {
    ViewportToolContext context{.state = tool_state, .camera = camera,
        .ports = ports.ports(), .modal_state = &modal, .box = &box};
    return tool.handle_event(context, {.kind = ViewportToolEventKind::Frame,
        .tool = ViewportToolId::CreatePrimitive, .pointer = pointer});
  }

  ViewportToolResult pointer(QVec2 point, bool press, bool down, bool release,
                             bool control = false) {
    return frame({.hovered = true, .left_clicked = press, .left_down = down,
                  .left_released = release, .viewport_focused = true,
                  .mouse_local = point, .left_click_local = point,
                  .viewport_size = {800, 600}, .modifiers = {.primary = control}});
  }

  ViewportToolResult wheel(std::span<ViewportNavigationWheelEvent> events,
                           ViewportPointerToolEvent pointer = {}) {
    pointer.wheel_events = events;
    ViewportToolContext context{.state = tool_state, .camera = camera,
        .ports = ports.ports(), .modal_state = &modal, .box = &box};
    return tool.handle_event(context, {.kind = ViewportToolEventKind::Wheel,
        .tool = ViewportToolId::CreatePrimitive, .pointer = pointer});
  }
};

TEST(PrimitiveCubeInteractionTests, EveryShapeSharesSixBoundsHandlesAndRetainsProjection) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Box,
                          ViewportPrimitiveCreationShape::Cylinder,
                          ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere,
                          ViewportPrimitiveCreationShape::Torus}) {
    SCOPED_TRACE(static_cast<int>(shape));
    PrimitiveCubeInteractionHarness h(shape);
    auto &interaction = h.modal.primitive_creation;
    for (const float scale : {1.0f, 2.0f, 1.0f}) {
      const auto primitive = h.box.live_primitive();
      ASSERT_TRUE(primitive);
      const auto &frame = interaction.project(primitive, h.camera, scale);
      ASSERT_TRUE(frame.visible);
      for (int index = 0; index < 6; ++index) {
        const auto &control = frame.controls[index];
        EXPECT_TRUE(control.visible);
        EXPECT_TRUE(control.enabled);
        EXPECT_FLOAT_EQ(control.radius_pixels, 6.0f * scale);
        EXPECT_EQ(control.resize_control, static_cast<ViewportPrimitiveResizeControl>(index));
        EXPECT_EQ(interaction.hit_test(control.screen_position), index);
        EXPECT_FLOAT_EQ(length(control.world_position -
            frame.bounds.handles[index].world_position), 0.0f);
      }
      for (int index = 6; index < 9; ++index) {
        EXPECT_FALSE(frame.controls[index].visible);
        EXPECT_FALSE(interaction.wheel_segment_value(index, 1.0).has_value());
      }
      const auto projections = interaction.projection_count();
      const auto revision = frame.revision;
      for (int iteration = 0; iteration < 10; ++iteration) {
        interaction.project(primitive, h.camera, scale);
        interaction.set_subdivision_decrement(iteration % 2 == 0);
      }
      EXPECT_EQ(interaction.projection_count(), projections);
      EXPECT_EQ(interaction.frame().revision, revision);
    }
  }
}

TEST(PrimitiveCubeInteractionTests, PanePresentationSharesFeedbackWithoutRecapturingDrag) {
  PrimitiveCubeInteractionHarness h;
  auto &interaction = h.modal.primitive_creation;
  ViewportCameraController other_camera;
  other_camera.set_viewport_size(640, 480);
  other_camera.view_axis(ViewportViewAxis::Z, 1);
  PrimitiveCreationInteraction presentation;
  const auto primitive = h.box.live_primitive();
  ASSERT_TRUE(primitive);
  interaction.hover(0);
  const auto &frame = presentation.project_presentation(
      primitive, other_camera, 2.0f, interaction.frame());
  EXPECT_EQ(frame.hovered, 0);
  EXPECT_EQ(frame.label_key, "primitive.controls.width");
  EXPECT_DOUBLE_EQ(frame.label_value,
      length(frame.bounds.handles[0].world_position -
             frame.bounds.handles[0].opposite_world_position));
  const auto count = presentation.projection_count();
  const auto owner_count = interaction.projection_count();
  interaction.begin(1, *primitive, h.camera, interaction.frame().controls[1].screen_position);
  presentation.project_presentation(primitive, other_camera, 2.0f, interaction.frame());
  EXPECT_EQ(presentation.active(), 1);
  EXPECT_EQ(interaction.active(), 1);
  EXPECT_EQ(presentation.projection_count(), count);
  EXPECT_EQ(interaction.projection_count(), owner_count);
  EXPECT_EQ(presentation.frame().label_key, "primitive.controls.width");
  for (int iteration = 0; iteration < 10; ++iteration) {
    presentation.project_presentation(primitive, other_camera, 2.0f, interaction.frame());
  }
  EXPECT_EQ(presentation.projection_count(), count);
  interaction.end();
  presentation.project_presentation(primitive, other_camera, 2.0f, interaction.frame());
  EXPECT_EQ(presentation.active(), -1);
}

TEST(PrimitiveCubeInteractionTests, CommonBoundsEndOnPairsRemainDistinctAndDragOutwards) {
  for (const auto view : {ViewportViewAxis::X, ViewportViewAxis::Y, ViewportViewAxis::Z}) {
    ViewportCameraController camera;
    camera.set_viewport_size(800, 600);
    camera.focus_origin(10);
    camera.view_axis(view, 1);
    const ViewportBoundsResizeState state{
        .local_min = {-1.0f, -1.0f, -1.0f},
        .local_max = {1.0f, 1.0f, 1.0f}};
    BoundsResizeInteraction interaction;
    const auto &frame = interaction.project(state, camera, 1.0f);
    for (int index = 0; index < 6; ++index) {
      const auto handle = frame.handles[index];
      ASSERT_TRUE(handle.visible);
      EXPECT_EQ(interaction.hit_test(handle.screen_position), index);
      interaction.begin(index, state, camera, handle.screen_position);
      const auto anchor = camera.project_world_point(handle.world_position).position;
      const auto direction = handle.screen_position - anchor;
      const auto moved = interaction.displacement(camera, handle.screen_position + direction);
      ASSERT_TRUE(moved.has_value());
      EXPECT_GT(*moved, 0.0);
      const auto baseline = interaction.displacement(camera, handle.screen_position);
      ASSERT_TRUE(baseline.has_value());
      EXPECT_NEAR(*baseline, 0.0, 0.000001);
      interaction.end();
    }
  }
}

TEST(PrimitiveCubeInteractionTests, CommonBoundsRejectInvalidFramesAndDisableZeroExtentAxes) {
  ViewportCameraController camera;
  camera.set_viewport_size(800, 600);
  camera.focus_origin(10);
  ViewportBoundsResizeState state{
      .local_min = {0.0f, -1.0f, -1.0f},
      .local_max = {0.0f, 1.0f, 1.0f}};
  BoundsResizeInteraction interaction;
  const auto &flat = interaction.project(state, camera, 1.0f);
  ASSERT_TRUE(flat.visible);
  EXPECT_FALSE(flat.handles[0].enabled);
  EXPECT_FALSE(flat.handles[1].enabled);
  EXPECT_EQ(interaction.hit_test(flat.handles[0].screen_position), -1);
  interaction.begin(0, state, camera, flat.handles[0].screen_position);
  EXPECT_EQ(interaction.active(), -1);
  state.world_x = {};
  EXPECT_FALSE(interaction.project(state, camera, 1.0f).visible);
  state.world_x = {1.0f, 0.0f, 0.0f};
  state.local_max.x = std::numeric_limits<float>::infinity();
  EXPECT_FALSE(interaction.project(state, camera, 1.0f).visible);
  EXPECT_EQ(interaction.hit_test({400.0f, 300.0f}), -1);
}

TEST(PrimitiveCubeInteractionTests, BoundsClickDoesNotChangeGeometryOrAddHistory) {
  PrimitiveCubeInteractionHarness h;
  const auto before = *h.box.live_primitive();
  const auto content = h.session.modeling().content_revision();
  const auto point = h.modal.primitive_creation.frame().controls[0].screen_position;
  ASSERT_TRUE(h.pointer(point, true, false, true).consumed);
  EXPECT_EQ(h.modal.primitive_creation.active(), -1);
  EXPECT_EQ(h.session.modeling().content_revision(), content);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, before.local_max.x);
  EXPECT_FALSE(h.box.live_primitive()->resizing);
  EXPECT_EQ(h.session.primitive_creation().shape_count(), 1U);
  EXPECT_EQ(h.session.history().undo_count(), 0U);
}

TEST(PrimitiveCubeInteractionTests, EveryShapeUsesHandForBoundsAndGrabsOnlyConfirmedDrags) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Box,
                          ViewportPrimitiveCreationShape::Cylinder,
                          ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere,
                          ViewportPrimitiveCreationShape::Torus}) {
    PrimitiveCubeInteractionHarness h(shape);
    const auto point = h.modal.primitive_creation.frame().controls[0].screen_position;
    h.pointer(point, false, false, false);
    EXPECT_EQ(h.modal.primitive_creation.pointer_cursor(false), ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(h.pointer(point, true, true, false).consumed);
    EXPECT_EQ(h.modal.primitive_creation.pointer_cursor(false), ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(h.pointer(point + QVec2{40.0f, 20.0f}, false, true, false).consumed);
    EXPECT_EQ(h.modal.primitive_creation.pointer_cursor(true), ViewportPointerCursor::ClosedHand);
    ASSERT_TRUE(h.pointer(point + QVec2{40.0f, 20.0f}, false, false, true).consumed);
    EXPECT_EQ(h.modal.primitive_creation.active(), -1);
  }
}

TEST(PrimitiveCubeInteractionTests, DotResizePreservesOppositeFaceAndSnaps) {
  PrimitiveCubeInteractionHarness h;
  h.snap = true;
  const auto cube = h.box.live_primitive();
  ASSERT_TRUE(cube);
  const auto &frame = h.modal.primitive_creation.frame();
  const QVec2 press = frame.controls[0].screen_position;
  const auto end = press + h.camera.project_world_point(
      frame.controls[0].world_position + cube->world_x * 0.8f).position -
      h.camera.project_world_point(frame.controls[0].world_position).position;
  ASSERT_TRUE(h.pointer(press, true, true, false).consumed);
  ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
  ASSERT_TRUE(h.pointer(end, false, false, true).consumed);
  const auto after = h.box.live_primitive();
  ASSERT_TRUE(after);
  EXPECT_NEAR(after->local_max.x, cube->local_max.x + 1.0f, 0.0001f);
  EXPECT_FLOAT_EQ(after->local_min.x, cube->local_min.x);
  EXPECT_FALSE(after->resizing);
  EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
}

TEST(PrimitiveCubeInteractionTests, InterruptedResizeKeepsLastBoundsAndRetiresItsGesture) {
  for (int interruption = 0; interruption < 6; ++interruption) {
    SCOPED_TRACE(interruption);
    PrimitiveCubeInteractionHarness h;
    const auto before = *h.box.live_primitive();
    const auto control = h.modal.primitive_creation.frame().controls[0];
    const auto end = control.screen_position + h.camera.project_world_point(control.world_position + before.world_x).position - h.camera.project_world_point(control.world_position).position;
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    const auto authored = *h.box.live_primitive();
    ASSERT_NE(authored.local_max.x, before.local_max.x);
    h.box.primitive_controls_visible = interruption != 5;
    ASSERT_TRUE(h.frame({.left_down = interruption != 1,
                         .gizmo_consumed = interruption == 4,
                         .navigation_active = interruption == 2,
                         .text_input_active = interruption == 3,
                         .viewport_focused = interruption != 0,
                         .pointer_interaction_canceled = interruption == 0,
                         .mouse_local = {-9000.0f, -9000.0f}}).consumed);
    ASSERT_TRUE(h.box.live_primitive());
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored.local_max.x);
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before.local_min.x);
    EXPECT_FALSE(h.box.live_primitive()->resizing);
    EXPECT_FALSE(h.modal.primary_pointer_gesture);
    EXPECT_EQ(h.modal.primitive_creation.active(), -1);
    EXPECT_EQ(h.modal.primary_release_suppression.active(), interruption != 1);
    if (interruption == 5) EXPECT_FALSE(h.modal.primitive_creation.frame().visible);
    EXPECT_FALSE(h.session.history().can_undo());
    (void)h.pointer(end, false, false, true);
    EXPECT_FALSE(h.modal.primary_release_suppression.active());
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored.local_max.x);
    EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1U);
    const EntityId entity = h.session.scene().primary_selection();
    ASSERT_TRUE(h.editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(h.session.history().undo_count(), 1U);
    ASSERT_TRUE(h.editor.history().undo().ok());
    EXPECT_FALSE(h.session.scene().exists(entity));
    ASSERT_TRUE(h.editor.history().redo().ok());
    EXPECT_TRUE(h.session.scene().exists(entity));
  }
}

TEST(PrimitiveCubeInteractionTests, ResizeUsesFirstMatchingReleaseBeforeAnotherCoalescedPress) {
  for (const bool second_released : {false, true}) {
    SCOPED_TRACE(second_released);
    PrimitiveCubeInteractionHarness h;
    h.camera.view_axis(ViewportViewAxis::Z, 1);
    h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1.0f);
    const auto before = *h.box.live_primitive();
    const auto control = h.modal.primitive_creation.frame().controls[0];
    const auto end_at = [&](float delta) {
      return control.screen_position + h.camera.project_world_point(
          control.world_position + before.world_x * delta).position -
          h.camera.project_world_point(control.world_position).position;
    };
    const std::array first_press{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left, .local = control.screen_position, .sequence = 10u}};
    ASSERT_TRUE(h.frame({.hovered = true, .left_clicked = true, .left_down = true,
        .mouse_local = control.screen_position, .left_click_local = control.screen_position,
        .pointer_pressed_edges = first_press}).consumed);
    const std::array second_press{ViewportPointerButtonEdge{
        .button = QuaderMouseButton::Left, .local = end_at(1.0f), .sequence = 12u}};
    const std::array releases{
        ViewportPointerButtonEdge{.button = QuaderMouseButton::Left, .local = end_at(0.6f), .sequence = 11u},
        ViewportPointerButtonEdge{.button = QuaderMouseButton::Left, .local = end_at(1.8f), .sequence = 13u}};
    ASSERT_TRUE(h.frame({.hovered = true, .left_clicked = true, .left_down = !second_released,
        .left_released = true, .mouse_local = end_at(1.8f), .left_click_local = second_press[0].local,
        .pointer_pressed_edges = second_press,
        .pointer_released_edges = std::span(releases).first(second_released ? 2u : 1u)}).consumed);
    const auto resized = h.box.live_primitive();
    ASSERT_TRUE(resized);
    EXPECT_NEAR(resized->local_max.x, before.local_max.x + 0.6f, 0.0001f);
    EXPECT_FLOAT_EQ(resized->local_min.x, before.local_min.x);
    EXPECT_FALSE(resized->resizing);
    EXPECT_FALSE(h.modal.primary_pointer_gesture);
    EXPECT_EQ(h.modal.primitive_creation.active(), -1);
    EXPECT_EQ(h.modal.primary_release_suppression.active(), !second_released);
    (void)h.pointer(end_at(1.8f), false, false, true);
    EXPECT_FALSE(h.modal.primary_release_suppression.active());
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, resized->local_max.x);
    EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1u);
    EXPECT_FALSE(h.session.history().can_undo());
  }
}

TEST(PrimitiveCubeInteractionTests, RejectedCoalescedFinalSampleEndsOnlyItsCapturedResize) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    for (const bool second_released : {false, true}) {
      SCOPED_TRACE(static_cast<int>(failure));
      SCOPED_TRACE(second_released);
      PrimitiveCubeInteractionHarness h;
      h.camera.view_axis(ViewportViewAxis::Z, 1);
      h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1.0f);
      const auto before = *h.box.live_primitive();
      const auto control = h.modal.primitive_creation.frame().controls[0];
      const auto end_at = [&](float delta) {
        return control.screen_position + h.camera.project_world_point(
            control.world_position + before.world_x * delta).position -
            h.camera.project_world_point(control.world_position).position;
      };
      const std::array first_press{ViewportPointerButtonEdge{
          .button = QuaderMouseButton::Left, .local = control.screen_position, .sequence = 10u}};
      ASSERT_TRUE(h.frame({.hovered = true, .left_clicked = true, .left_down = true,
          .mouse_local = control.screen_position, .left_click_local = control.screen_position,
          .pointer_pressed_edges = first_press}).consumed);
      ASSERT_TRUE(h.pointer(end_at(0.4f), false, true, false).consumed);
      const auto accepted = *h.box.live_primitive();
      const auto content_revision = h.session.modeling().content_revision();
      const auto update_resize = h.box.update_resize;
      int rejected_samples = 0;
      h.box.update_resize = [&](double delta) {
        EXPECT_TRUE(std::isfinite(delta));
        EXPECT_NEAR(delta, 0.9, 0.0001);
        h.session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
        const auto result = update_resize(delta);
        h.session.modeling().set_modeling_sdk_projection_failure_for_tests(
            ModelingSdkProjectionFailurePoint::None);
        ++rejected_samples;
        EXPECT_FALSE(result.ok);
        return result;
      };
      const std::array second_press{ViewportPointerButtonEdge{
          .button = QuaderMouseButton::Left, .local = end_at(1.2f), .sequence = 12u}};
      const std::array releases{
          ViewportPointerButtonEdge{.button = QuaderMouseButton::Left, .local = end_at(0.9f), .sequence = 11u},
          ViewportPointerButtonEdge{.button = QuaderMouseButton::Left, .local = end_at(1.8f), .sequence = 13u}};
      ASSERT_TRUE(h.frame({.hovered = true, .left_clicked = true, .left_down = !second_released,
          .left_released = true, .mouse_local = end_at(1.8f), .left_click_local = second_press[0].local,
          .pointer_pressed_edges = second_press,
          .pointer_released_edges = std::span(releases).first(second_released ? 2u : 1u)}).consumed);
      h.box.update_resize = update_resize;
      EXPECT_EQ(rejected_samples, 1);
      EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, accepted.local_max.x);
      EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before.local_min.x);
      EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
      EXPECT_FALSE(h.box.live_primitive()->resizing);
      EXPECT_FALSE(h.modal.primary_pointer_gesture);
      EXPECT_EQ(h.modal.primitive_creation.active(), -1);
      EXPECT_EQ(h.modal.primary_release_suppression.active(), !second_released);
      if (!second_released) ASSERT_TRUE(h.pointer(end_at(1.8f), false, true, false).consumed);
      (void)h.pointer(end_at(1.8f), false, false, true);
      EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, accepted.local_max.x);
      EXPECT_FALSE(h.modal.primary_pointer_gesture);
      EXPECT_FALSE(h.modal.primary_release_suppression.active());
      EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1u);
      EXPECT_FALSE(h.session.history().can_undo());
    }
  }
}

TEST(PrimitiveCubeInteractionTests, RejectedResizePublicationRetainsCaptureForRetryReleaseAndCancel) {
  for (const auto failure : {ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                             ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    for (int completion = 0; completion < 3; ++completion) {
      SCOPED_TRACE(static_cast<int>(failure));
      SCOPED_TRACE(completion);
      PrimitiveCubeInteractionHarness h;
      h.camera.view_axis(ViewportViewAxis::Z, 1);
      h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1.0f);
      const auto before = *h.box.live_primitive();
      const auto control = h.modal.primitive_creation.frame().controls[0];
      const auto end_at = [&](float delta) {
        return control.screen_position + h.camera.project_world_point(
            control.world_position + before.world_x * delta).position -
            h.camera.project_world_point(control.world_position).position;
      };
      ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
      ASSERT_TRUE(h.pointer(end_at(0.4f), false, true, false).consumed);
      const auto accepted = *h.box.live_primitive();
      EXPECT_NEAR(accepted.local_max.x, before.local_max.x + 0.4f, 0.0001f);
      const auto content_revision = h.session.modeling().content_revision();
      const auto overlay_invalidations = h.overlay_invalidations;
      const auto update_resize = h.box.update_resize;
      int rejected_samples = 0;
      h.box.update_resize = [&](double delta) {
        EXPECT_TRUE(std::isfinite(delta));
        EXPECT_NEAR(delta, 0.9, 0.0001);
        h.session.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
        const auto result = update_resize(delta);
        h.session.modeling().set_modeling_sdk_projection_failure_for_tests(
            ModelingSdkProjectionFailurePoint::None);
        ++rejected_samples;
        EXPECT_FALSE(result.ok);
        EXPECT_FALSE(result.changed);
        return result;
      };
      const bool released = completion == 1;
      ASSERT_TRUE(h.pointer(end_at(0.9f), false, !released, released).consumed);
      ASSERT_EQ(rejected_samples, 1);
      h.box.update_resize = update_resize;
      ASSERT_TRUE(h.box.live_primitive());
      EXPECT_EQ(h.box.live_primitive()->resizing, !released);
      EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, accepted.local_max.x);
      EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before.local_min.x);
      if (released) EXPECT_GT(h.box.live_primitive()->revision, accepted.revision);
      else EXPECT_EQ(h.box.live_primitive()->revision, accepted.revision);
      EXPECT_EQ(h.session.modeling().content_revision(), content_revision);
      EXPECT_EQ(h.overlay_invalidations, overlay_invalidations + (released ? 1 : 0));
      EXPECT_EQ(h.modal.primary_pointer_gesture.has_value(), !released);
      EXPECT_EQ(h.modal.primitive_creation.active(), released ? -1 : 0);
      EXPECT_FALSE(h.modal.primary_release_suppression.active());
      if (completion == 0) {
        ASSERT_TRUE(h.pointer(end_at(0.9f), false, true, false).consumed);
        EXPECT_NEAR(h.box.live_primitive()->local_max.x, before.local_max.x + 0.9f, 0.0001f);
        ASSERT_TRUE(h.pointer(end_at(0.9f), false, false, true).consumed);
      } else if (released) {
        // The matching release immediately completes the last accepted sample.
        (void)h.pointer(end_at(0.9f), false, false, false);
        EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, accepted.local_max.x);
      } else {
        ASSERT_TRUE(h.editor.tools().cancel_primitive_resize().ok());
        ASSERT_TRUE(h.pointer(end_at(0.9f), false, true, false).consumed);
        EXPECT_TRUE(h.modal.primary_release_suppression.active());
        ASSERT_TRUE(h.pointer(end_at(0.9f), false, false, true).consumed);
        EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, before.local_max.x);
      }
      EXPECT_FALSE(h.box.live_primitive()->resizing);
      EXPECT_FALSE(h.modal.primary_pointer_gesture);
      EXPECT_EQ(h.modal.primitive_creation.active(), -1);
      EXPECT_FALSE(h.modal.primary_release_suppression.active());
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1u);
      EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
      EXPECT_FALSE(h.session.history().can_undo());
    }
  }
}

TEST(PrimitiveCubeInteractionTests, FocusAlonePreservesResizeAndFailedCompletionCanRetry) {
  PrimitiveCubeInteractionHarness h;
  const auto before = *h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = control.screen_position + h.camera.project_world_point(control.world_position + before.world_x).position - h.camera.project_world_point(control.world_position).position;
  ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
  ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
  const auto authored = *h.box.live_primitive();
  ASSERT_TRUE(h.frame({.left_down = true, .viewport_focused = false,
                       .mouse_local = {-9000.0f, -9000.0f}}).consumed);
  EXPECT_TRUE(h.box.live_primitive()->resizing);
  EXPECT_TRUE(h.modal.primary_pointer_gesture);
  EXPECT_EQ(h.box.live_primitive()->revision, authored.revision);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored.local_max.x);

  const auto end_resize = h.box.end_resize;
  h.box.end_resize = [] {
    return ModelingCommandResult{.ok = false, .message = "Completion rejected for test."};
  };
  ASSERT_TRUE(h.frame({.left_down = true,
                       .pointer_interaction_canceled = true}).consumed);
  EXPECT_TRUE(h.box.live_primitive()->resizing);
  EXPECT_FALSE(h.modal.primary_pointer_gesture);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored.local_max.x);
  h.box.end_resize = end_resize;
  ASSERT_TRUE(h.frame({.left_down = true,
                       .mouse_local = {-9000.0f, -9000.0f}}).consumed);
  EXPECT_FALSE(h.box.live_primitive()->resizing);
  EXPECT_FALSE(h.modal.primary_pointer_gesture);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored.local_max.x);
}

TEST(PrimitiveCubeInteractionTests, PublicUndoCancelsResizeWithoutReusingHeldPress) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[0].screen_position;
  h.pointer(point, true, true, false);
  ASSERT_TRUE(h.editor.history().undo().ok());
  ASSERT_FALSE(h.box.live_primitive()->resizing);
  EXPECT_TRUE(h.pointer(point, false, true, false).consumed);
  EXPECT_TRUE(h.pointer(point, false, false, true).consumed);
  EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
}

TEST(PrimitiveCubeInteractionTests, OrthographicEndOnAxisHasFiniteScreenFallback) {
  PrimitiveCubeInteractionHarness h;
  h.camera.view_axis(ViewportViewAxis::X, 1);
  auto &interaction = h.modal.primitive_creation;
  const auto cube = h.box.live_primitive();
  const auto &frame = interaction.project(cube, h.camera, 1);
  const auto press = frame.controls[0].screen_position;
  interaction.begin(0, *cube, h.camera, press);
  const auto delta = interaction.displacement(h.camera, {press.x, press.y - 30});
  ASSERT_TRUE(delta);
  EXPECT_GT(*delta, 0);
  EXPECT_TRUE(std::isfinite(*delta));
}

TEST(PrimitiveCubeInteractionTests, HiddenControlsAllowOrdinaryFootprintPress) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[0].screen_position;
  h.box.primitive_controls_visible = false;
  EXPECT_TRUE(h.pointer(point, true, true, false).consumed);
  EXPECT_EQ(h.modal.primitive_creation.active(), -1);
  EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Footprint);
}

TEST(PrimitiveCubeInteractionTests, SelectionLossRetiresOwnerResizeAndTrailingRelease) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[0].screen_position;
  h.pointer(point, true, true, false);
  h.pointer({point.x + 40, point.y}, false, true, false);
  ASSERT_TRUE(h.editor.selection().clear().ok());
  EXPECT_TRUE(h.pointer(point, false, true, false).consumed);
  EXPECT_TRUE(h.pointer(point, false, false, true).consumed);
  EXPECT_FALSE(h.box.live_primitive());
  EXPECT_TRUE(h.session.scene().selected_entities().empty());
  EXPECT_FALSE(h.editor.tools().has_active_primitive_creation());
  EXPECT_FALSE(h.editor.tools().flip_primitive_faces().ok());
  EXPECT_EQ(h.session.history().undo_count(), 2U);
  EXPECT_EQ(h.session.modeling().objects().size(), 1U);
  const EntityId entity = h.session.modeling().objects().front().entity;
  ASSERT_TRUE(h.editor.history().undo().ok());
  EXPECT_TRUE(h.session.scene().exists(entity));
  EXPECT_EQ(h.session.scene().primary_selection(), entity);
  ASSERT_TRUE(h.editor.history().undo().ok());
  EXPECT_FALSE(h.session.scene().exists(entity));
  ASSERT_TRUE(h.editor.history().redo().ok());
  EXPECT_TRUE(h.session.scene().exists(entity));
  ASSERT_TRUE(h.editor.history().redo().ok());
  EXPECT_TRUE(h.session.scene().selected_entities().empty());
}

TEST(PrimitiveCubeInteractionTests, ResizeReturnsToBaselineAfterCrossingThreshold) {
  PrimitiveCubeInteractionHarness h;
  const auto before = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = control.screen_position + h.camera.project_world_point(control.world_position + before->world_x).position - h.camera.project_world_point(control.world_position).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  h.pointer(control.screen_position, false, false, true);
  EXPECT_NEAR(h.box.live_primitive()->local_max.x, before->local_max.x, 0.0001f);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before->local_min.x);
  EXPECT_FALSE(h.box.live_primitive()->resizing);
}

TEST(PrimitiveCubeInteractionTests, RepeatedSnappedDragSkipsMeshAndOverlayWork) {
  PrimitiveCubeInteractionHarness h;
  h.snap = true;
  const auto cube = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = control.screen_position + h.camera.project_world_point(control.world_position + cube->world_x).position - h.camera.project_world_point(control.world_position).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  // The first post-publication frame refreshes the retained projection once.
  h.pointer(end, false, true, false);
  const auto revision = h.box.live_primitive()->revision;
  const auto content = h.session.modeling().content_revision();
  const auto projections = h.modal.primitive_creation.projection_count();
  const auto invalidations = h.overlay_invalidations;
  for (int i = 0; i < 20; ++i) h.pointer(end, false, true, false);
  EXPECT_EQ(h.box.live_primitive()->revision, revision);
  EXPECT_EQ(h.session.modeling().content_revision(), content);
  EXPECT_EQ(h.modal.primitive_creation.projection_count(), projections);
  EXPECT_EQ(h.overlay_invalidations, invalidations);
}

TEST(PrimitiveCubeInteractionTests, CameraChangeFinishesLastResizeAndReservesRelease) {
  PrimitiveCubeInteractionHarness h;
  const auto before = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = control.screen_position + h.camera.project_world_point(control.world_position + before->world_x).position - h.camera.project_world_point(control.world_position).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  const float authored_max = h.box.live_primitive()->local_max.x;
  EXPECT_TRUE(h.frame({.left_down = true, .mouse_local = end,
                       .camera_changed = true}).consumed);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, authored_max);
  EXPECT_FALSE(h.box.live_primitive()->resizing);
  EXPECT_TRUE(h.pointer(end, false, false, true).consumed);
}

TEST(PrimitiveCubeInteractionTests, ControllerCancellationAfterSelectionLossRestoresResize) {
  PrimitiveCubeInteractionHarness h;
  ViewportInteractionController controller;
  const auto before = h.box.live_primitive();
  const auto control = controller.primitive_creation_controls(
      h.editor.tools().live_primitive_creation(), h.camera, 1).controls[0];
  const auto end = control.screen_position + h.camera.project_world_point(control.world_position + before->world_x).position - h.camera.project_world_point(control.world_position).position;
  ViewportToolContext context{.state = h.tool_state, .camera = h.camera,
      .ports = h.ports.ports(), .box = &h.box};
  const auto send = [&](QVec2 point, bool press, bool down, bool release) {
    return controller.handle_active_tool_event(h.tool_state, context,
        {.kind = ViewportToolEventKind::Frame,
         .tool = ViewportToolId::CreatePrimitive,
         .pointer = {.hovered = true, .left_clicked = press, .left_down = down,
                     .left_released = release, .mouse_local = point,
                     .left_click_local = point}});
  };
  ASSERT_TRUE(send(control.screen_position, true, true, false).consumed);
  ASSERT_TRUE(send(end, false, true, false).consumed);
  ASSERT_TRUE(controller.dragging());
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  ASSERT_TRUE(h.editor.selection().clear().ok());
  ASSERT_TRUE(controller.cancel_active_drag(h.editor));
  EXPECT_FALSE(controller.primitive_creation_pointer_active());
  EXPECT_FALSE(h.editor.tools().cancel_primitive_cube_resize().changed());
  EXPECT_TRUE(h.session.scene().selected_entities().empty());
  EXPECT_TRUE(send(end, false, false, true).consumed);
}

TEST(PrimitiveCubeInteractionTests, QuadSphereTypeChangeRetiresResizeWithoutRestoringOldBounds) {
  for (const bool to_quad : {true, false}) {
    PrimitiveCubeInteractionHarness h(ViewportPrimitiveCreationShape::Sphere);
    if (!to_quad) {
      ASSERT_TRUE(h.editor.tools().set_setting(kCreatePrimitiveToolId, kSphereTypeSetting, 1.0).ok());
    }
    const auto control = h.modal.primitive_creation.project(
        h.box.live_primitive(), h.camera, 1).controls[2];
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    const auto end = control.screen_position + h.camera.project_world_point(
        control.world_position + QVec3{0, 0.7f, 0}).position -
        h.camera.project_world_point(control.world_position).position;
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    const auto resized = *h.box.live_primitive();
    ASSERT_TRUE(resized.resizing);
    ASSERT_TRUE(h.editor.tools().set_setting(kCreatePrimitiveToolId, kSphereTypeSetting,
                                             to_quad ? 1.0 : 0.0).ok());
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, false, true).consumed);
    const auto after = *h.box.live_primitive();
    EXPECT_FALSE(after.resizing);
    EXPECT_FLOAT_EQ(after.local_min.y, resized.local_min.y);
    EXPECT_FLOAT_EQ(after.local_max.y, resized.local_max.y);
    EXPECT_EQ(after.sphere_type, to_quad ? ViewportSphereType::Quad : ViewportSphereType::Uv);
    EXPECT_EQ(h.modal.primitive_creation.active(), -1);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
  }
}

} // namespace
} // namespace quader
