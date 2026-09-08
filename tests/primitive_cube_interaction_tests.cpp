////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/primitive_creation_tool_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/interactive/create_primitive_tool.hpp"
#include "editor/tools/primitive_creation_interaction.hpp"
#include "editor/viewport/primitive_creation_api_adapter.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"

#include <gtest/gtest.h>

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
        setting = axis == 0 ? kSphereRadialSegmentsSetting : kSphereRingsSetting; break;
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

TEST(PrimitiveCubeInteractionTests, EveryShapeUsesHandForHandlesAndGrabsOnlyConfirmedResizeDrags) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Box,
                          ViewportPrimitiveCreationShape::Cylinder,
                          ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere,
                          ViewportPrimitiveCreationShape::Torus}) {
    SCOPED_TRACE(static_cast<int>(shape));
    PrimitiveCubeInteractionHarness h(shape);
    auto &interaction = h.modal.primitive_creation;
    const auto cursor = [&] {
      return interaction.pointer_cursor(h.modal.primitive_creation_drag_started);
    };
    const auto control = interaction.frame().controls[0];
    ASSERT_TRUE(control.visible);
    h.pointer(control.screen_position, false, false, false);
    EXPECT_EQ(cursor(), ViewportPointerCursor::OpenHand);
    const auto projections = interaction.projection_count();
    for (int index = 0; index < 5; ++index) {
      h.pointer(control.screen_position, false, false, false);
      EXPECT_EQ(cursor(), ViewportPointerCursor::OpenHand);
    }
    EXPECT_EQ(interaction.projection_count(), projections);
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    EXPECT_EQ(cursor(), ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(h.pointer(control.screen_position + QVec2{20, 20}, false, true, false).consumed);
    EXPECT_EQ(cursor(), ViewportPointerCursor::ClosedHand);
    EXPECT_TRUE(h.frame({.left_down = true, .viewport_focused = false,
                        .pointer_interaction_canceled = true}).consumed);
    EXPECT_EQ(cursor(), ViewportPointerCursor::Default);
    h.pointer(control.screen_position, false, false, true);

    const auto button = interaction.project(h.box.live_primitive(), h.camera, 1).controls[6];
    ASSERT_TRUE(button.visible);
    h.pointer(button.screen_position, false, false, false);
    EXPECT_EQ(cursor(), ViewportPointerCursor::OpenHand);
    ASSERT_TRUE(h.pointer(button.screen_position, true, true, false).consumed);
    ASSERT_TRUE(h.pointer(button.screen_position + QVec2{20, 20}, false, true, false).consumed);
    EXPECT_EQ(cursor(), ViewportPointerCursor::OpenHand);
    h.pointer(button.screen_position + QVec2{20, 20}, false, false, true);
    EXPECT_EQ(cursor(), ViewportPointerCursor::Default);
    ASSERT_TRUE(h.box.set_segments(0, 128).ok);
    const auto disabled = interaction.project(h.box.live_primitive(), h.camera, 1).controls[6];
    h.pointer(disabled.screen_position, false, false, false);
    EXPECT_EQ(cursor(), ViewportPointerCursor::Default);
    h.box.primitive_controls_visible = false;
    h.pointer(control.screen_position, false, false, false);
    EXPECT_EQ(cursor(), ViewportPointerCursor::Default);
  }
}

TEST(PrimitiveCubeInteractionTests, OrderedWheelConsumesLimitsAndFractionalReversals) {
  PrimitiveCubeInteractionHarness h;
  const auto send = [&](float steps, int axis = 0) {
    const auto &frame = h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1);
    std::array<ViewportNavigationWheelEvent, 1> events{{
        {.action = ViewportInputActionKind::Zoom, .steps = steps,
         .local = frame.controls[6 + axis].screen_position}}};
    EXPECT_TRUE(h.wheel(events).consumed);
    EXPECT_TRUE(events[0].consumed);
  };
  const auto count = [&] { return h.box.live_primitive()->subdivisions[0]; };
  const auto revision = h.box.live_primitive()->revision;
  const auto projections = h.modal.primitive_creation.projection_count();
  send(-0.5f); // A blocked fraction cannot postpone the reverse direction.
  send(0.5f);
  EXPECT_EQ(count(), 1);
  EXPECT_EQ(h.box.live_primitive()->revision, revision);
  EXPECT_EQ(h.modal.primitive_creation.projection_count(), projections);
  send(0.5f);
  EXPECT_EQ(count(), 2);
  send(0.75f);
  send(-0.5f); // Reversal discards the earlier positive remainder.
  send(-0.5f);
  EXPECT_EQ(count(), 1);
  ASSERT_TRUE(h.box.set_segments(0, 128).ok);
  const auto &frame = h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1);
  ASSERT_FALSE(frame.controls[6].enabled);
  std::array<ViewportNavigationWheelEvent, 2> ordered{{
      {.action = ViewportInputActionKind::Zoom, .steps = 1,
       .local = frame.controls[6].screen_position},
      {.action = ViewportInputActionKind::Zoom, .steps = -1,
       .local = frame.controls[6].screen_position}}};
  EXPECT_TRUE(h.wheel(ordered, {.mouse_local = {0, 0}}).consumed);
  EXPECT_TRUE(ordered[0].consumed);
  EXPECT_TRUE(ordered[1].consumed);
  EXPECT_EQ(count(), 127);
  send(0.75f);
  send(-1.0f);
  EXPECT_EQ(count(), 126);
  send(0.5f);
  send(0.5f, 1); // A new button cannot inherit another button's fraction.
  EXPECT_EQ(h.box.live_primitive()->subdivisions[1], 1);
  send(0.5f, 1);
  EXPECT_EQ(h.box.live_primitive()->subdivisions[1], 2);
}

TEST(PrimitiveCubeInteractionTests, WheelHonorsInputOwnersAndActiveClickDirection) {
  PrimitiveCubeInteractionHarness h;
  ASSERT_TRUE(h.box.set_segments(0, 4).ok);
  const auto point = h.modal.primitive_creation.project(
      h.box.live_primitive(), h.camera, 1).controls[6].screen_position;
  for (int guard = 0; guard < 6; ++guard) {
    ViewportPointerToolEvent pointer;
    pointer.navigation_active = guard == 0;
    pointer.text_input_active = guard == 1;
    pointer.viewport_focused = guard != 2;
    pointer.gizmo_consumed = guard == 3;
    pointer.pointer_interaction_canceled = guard == 4;
    pointer.left_down = guard == 5;
    std::array<ViewportNavigationWheelEvent, 1> events{{
        {.action = ViewportInputActionKind::Zoom, .steps = 1, .local = point}}};
    EXPECT_FALSE(h.wheel(events, pointer).consumed);
    EXPECT_FALSE(events[0].consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 4);
  }
  ASSERT_TRUE(h.pointer(point, true, true, false, true).consumed);
  std::array<ViewportNavigationWheelEvent, 1> events{{
      {.action = ViewportInputActionKind::Zoom, .steps = 1, .local = point}}};
  EXPECT_FALSE(h.wheel(events).consumed);
  EXPECT_TRUE(h.modal.primitive_creation.frame().controls[6].subdivision_decrement);
  EXPECT_FALSE(h.modal.primitive_creation.frame().controls[7].subdivision_decrement);
  ASSERT_TRUE(h.pointer(point, false, false, true).consumed);
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 3);
}

TEST(PrimitiveCubeInteractionTests, WheelUsesEachRoundPrimitiveSegmentSettingAndMinimum) {
  for (auto shape : {ViewportPrimitiveCreationShape::Cylinder,
                     ViewportPrimitiveCreationShape::Cone,
                     ViewportPrimitiveCreationShape::Sphere,
                     ViewportPrimitiveCreationShape::Torus}) {
    PrimitiveCubeInteractionHarness h(shape);
    for (int index : {6, 7}) {
      auto control = h.modal.primitive_creation.project(
          h.box.live_primitive(), h.camera, 1).controls[index];
      if (!control.visible) continue;
      ASSERT_TRUE(h.box.set_segments(index - 6, control.minimum_segments).ok);
      control = h.modal.primitive_creation.project(
          h.box.live_primitive(), h.camera, 1).controls[index];
      std::array<ViewportNavigationWheelEvent, 2> events{{
          {.action = ViewportInputActionKind::Zoom, .steps = -1,
           .local = control.screen_position},
          {.action = ViewportInputActionKind::Zoom, .steps = 1,
           .local = control.screen_position}}};
      EXPECT_TRUE(h.wheel(events).consumed);
      EXPECT_TRUE(events[0].consumed);
      EXPECT_TRUE(events[1].consumed);
      EXPECT_EQ(h.modal.primitive_creation.frame().controls[index].segment_count,
                control.minimum_segments + 1);
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    }
  }
}

TEST(PrimitiveCubeInteractionTests, FaceCentersAndOuterEdgesShareCachedHitTargets) {
  PrimitiveCubeInteractionHarness h;
  auto cube = h.box.live_primitive();
  ASSERT_TRUE(cube);
  const auto &frame = h.modal.primitive_creation.project(cube, h.camera, 1);
  ASSERT_TRUE(frame.visible);
  for (int i = 0; i < 9; ++i) {
    ASSERT_TRUE(frame.controls[i].visible);
    EXPECT_EQ(h.modal.primitive_creation.hit_test(frame.controls[i].screen_position), i);
    EXPECT_FLOAT_EQ(frame.controls[i].radius_pixels, i < 6 ? 6.0f : 9.0f);
  }
  const auto builds = h.modal.primitive_creation.projection_count();
  for (int i = 0; i < 20; ++i) h.modal.primitive_creation.project(cube, h.camera, 1);
  EXPECT_EQ(h.modal.primitive_creation.projection_count(), builds);
  h.modal.primitive_creation.project(cube, h.camera, 2);
  EXPECT_EQ(h.modal.primitive_creation.projection_count(), builds + 1);
  EXPECT_FLOAT_EQ(h.modal.primitive_creation.frame().controls[6].radius_pixels, 18);
}

TEST(PrimitiveCubeInteractionTests, EachPlusChangesOnlyItsAxisOnceWithoutDrawing) {
  PrimitiveCubeInteractionHarness h;
  for (int axis = 0; axis < 3; ++axis) {
    const auto &frame = h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1);
    const QVec2 point = frame.controls[6 + axis].screen_position;
    ASSERT_TRUE(h.pointer(point, true, true, false).consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[axis], 1);
    ASSERT_TRUE(h.pointer(point, false, false, true).consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[axis], 2);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
  }
}

TEST(PrimitiveCubeInteractionTests, ControlClickDecrementsOnlyItsAxisWithoutDrawing) {
  PrimitiveCubeInteractionHarness h;
  for (int axis = 0; axis < 3; ++axis) {
    ASSERT_TRUE(h.box.set_segments(
        axis, 3).ok);
  }
  for (int axis = 0; axis < 3; ++axis) {
    const auto before = h.box.live_primitive()->subdivisions;
    const auto &frame = h.modal.primitive_creation.project(h.box.live_primitive(), h.camera, 1);
    const QVec2 point = frame.controls[6 + axis].screen_position;
    ASSERT_TRUE(h.pointer(point, true, false, true, true).consumed);
    auto expected = before;
    --expected[axis];
    EXPECT_EQ(h.box.live_primitive()->subdivisions, expected);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
  }
}

TEST(PrimitiveCubeInteractionTests, ModifierOnlyChangesGlyphWithoutProjectionOrMeshWork) {
  PrimitiveCubeInteractionHarness h;
  auto &interaction = h.modal.primitive_creation;
  const auto projections = interaction.projection_count();
  const auto content = h.session.modeling().content_revision();
  const auto cube_revision = h.box.live_primitive()->revision;
  const auto original = interaction.frame();
  h.pointer({0, 0}, false, false, false, true);
  const auto minus_revision = interaction.frame().revision;
  EXPECT_GT(minus_revision, original.revision);
  for (int axis = 0; axis < 3; ++axis) {
    EXPECT_TRUE(interaction.frame().controls[6 + axis].subdivision_decrement);
    EXPECT_FALSE(interaction.frame().controls[6 + axis].enabled);
    EXPECT_EQ(interaction.frame().controls[6 + axis].screen_position.x,
              original.controls[6 + axis].screen_position.x);
  }
  for (int i = 0; i < 20; ++i) h.pointer({0, 0}, false, false, false, true);
  EXPECT_EQ(interaction.frame().revision, minus_revision);
  h.pointer({0, 0}, false, false, false);
  for (int axis = 0; axis < 3; ++axis) {
    EXPECT_FALSE(interaction.frame().controls[6 + axis].subdivision_decrement);
    EXPECT_TRUE(interaction.frame().controls[6 + axis].enabled);
  }
  EXPECT_EQ(interaction.projection_count(), projections);
  EXPECT_EQ(h.session.modeling().content_revision(), content);
  EXPECT_EQ(h.box.live_primitive()->revision, cube_revision);
}

TEST(PrimitiveCubeInteractionTests, SubdivisionDirectionAndGlyphFreezeAtPress) {
  PrimitiveCubeInteractionHarness h;
  ASSERT_TRUE(h.box.set_segments(0, 3).ok);
  auto &interaction = h.modal.primitive_creation;
  const auto point = interaction.project(h.box.live_primitive(), h.camera, 1)
                         .controls[6].screen_position;
  for (const bool decrement : {true, false}) {
    const auto before = h.box.live_primitive()->subdivisions[0];
    ASSERT_TRUE(h.pointer(point, true, true, false, decrement).consumed);
    ASSERT_TRUE(h.pointer(point, false, true, false, !decrement).consumed);
    EXPECT_EQ(interaction.frame().controls[6].subdivision_decrement, decrement);
    EXPECT_EQ(interaction.frame().controls[7].subdivision_decrement, !decrement);
    ASSERT_TRUE(h.pointer(point, false, false, true, !decrement).consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[0], before + (decrement ? -1 : 1));
    EXPECT_EQ(interaction.frame().controls[6].subdivision_decrement, !decrement);
  }
}

TEST(PrimitiveCubeInteractionTests, SubdivisionLimitsConsumeClickWithoutPublication) {
  PrimitiveCubeInteractionHarness h;
  auto &interaction = h.modal.primitive_creation;
  for (const bool decrement : {true, false}) {
    const int limit = decrement ? 1 : 128;
    ASSERT_TRUE(h.box.set_segments(0, limit).ok);
    const auto point = interaction.project(h.box.live_primitive(), h.camera, 1)
                           .controls[6].screen_position;
    h.pointer(point, false, false, false, decrement);
    EXPECT_FALSE(interaction.frame().controls[6].enabled);
    const auto content = h.session.modeling().content_revision();
    const auto revision = h.box.live_primitive()->revision;
    EXPECT_TRUE(h.pointer(point, true, false, true, decrement).consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[0], limit);
    EXPECT_EQ(h.box.live_primitive()->revision, revision);
    EXPECT_EQ(h.session.modeling().content_revision(), content);
    EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
    EXPECT_TRUE(h.pointer(point, true, false, true, !decrement).consumed);
    EXPECT_EQ(h.box.live_primitive()->subdivisions[0], decrement ? 2 : 127);
  }
}

TEST(PrimitiveCubeInteractionTests, DotResizePreservesOppositeFaceAndSnaps) {
  PrimitiveCubeInteractionHarness h;
  h.snap = true;
  const auto cube = h.box.live_primitive();
  ASSERT_TRUE(cube);
  const auto &frame = h.modal.primitive_creation.frame();
  const QVec2 press = frame.controls[0].screen_position;
  const auto end = h.camera.project_world_point(
      frame.controls[0].world_position + cube->world_x * 0.8f).position;
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

TEST(PrimitiveCubeInteractionTests, CaptureLossRestoresBoundsAndReservesRelease) {
  PrimitiveCubeInteractionHarness h;
  const auto before = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = h.camera.project_world_point(control.world_position + before->world_x).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  ASSERT_TRUE(h.frame({.left_down = true, .viewport_focused = false,
                       .pointer_interaction_canceled = true}).consumed);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, before->local_max.x);
  EXPECT_TRUE(h.pointer(end, false, false, true).consumed);
  EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
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

TEST(PrimitiveCubeInteractionTests, DraggingOffPlusDoesNotIncrementOrStartFootprint) {
  PrimitiveCubeInteractionHarness h;
  const auto press = h.modal.primitive_creation.frame().controls[6].screen_position;
  h.pointer(press, true, true, false);
  EXPECT_TRUE(h.pointer({press.x + 40, press.y}, false, false, true).consumed);
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 1);
  EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
}

TEST(PrimitiveCubeInteractionTests, OlderReleaseCannotCompleteNewPlusPress) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[6].screen_position;
  std::array<ViewportPointerButtonEdge, 1> press{{
      {.button = QuaderMouseButton::Left, .local = point, .sequence = 20}}};
  h.frame({.hovered = true, .left_clicked = true, .left_down = true,
           .mouse_local = point, .left_click_local = point,
           .pointer_pressed_edges = press});
  std::array<ViewportPointerButtonEdge, 1> release{{
      {.button = QuaderMouseButton::Left, .local = point, .sequence = 19}}};
  h.frame({.hovered = true, .left_down = true, .left_released = true,
           .mouse_local = point, .pointer_released_edges = release});
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 1);
  EXPECT_EQ(h.modal.primitive_creation.active(), 6);
  release[0].sequence = 21;
  h.frame({.hovered = true, .left_released = true, .mouse_local = point,
           .pointer_released_edges = release});
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 2);
}

TEST(PrimitiveCubeInteractionTests, HiddenControlsAllowOrdinaryFootprintPress) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[6].screen_position;
  h.box.primitive_controls_visible = false;
  EXPECT_TRUE(h.pointer(point, true, true, false).consumed);
  EXPECT_EQ(h.modal.primitive_creation.active(), -1);
  EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Footprint);
}

TEST(PrimitiveCubeInteractionTests, FastClickWithBothEdgesInOneFrameIncrementsOnce) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[6].screen_position;
  EXPECT_TRUE(h.pointer(point, true, false, true).consumed);
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 2);
  EXPECT_EQ(h.modal.primitive_creation.active(), -1);
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
  EXPECT_TRUE(h.editor.tools().flip_primitive_faces().ok());
}

TEST(PrimitiveCubeInteractionTests, ResizeReturnsToBaselineAfterCrossingThreshold) {
  PrimitiveCubeInteractionHarness h;
  const auto before = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = h.camera.project_world_point(control.world_position + before->world_x).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  h.pointer(control.screen_position, false, false, true);
  EXPECT_NEAR(h.box.live_primitive()->local_max.x, before->local_max.x, 0.0001f);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before->local_min.x);
  EXPECT_FALSE(h.box.live_primitive()->resizing);
}

TEST(PrimitiveCubeInteractionTests, ReturningToPlusAfterDraggingDoesNotClick) {
  PrimitiveCubeInteractionHarness h;
  const auto point = h.modal.primitive_creation.frame().controls[6].screen_position;
  h.pointer(point, true, true, false);
  h.pointer({point.x + 40, point.y}, false, true, false);
  h.pointer(point, false, false, true);
  EXPECT_EQ(h.box.live_primitive()->subdivisions[0], 1);
}

TEST(PrimitiveCubeInteractionTests, RepeatedSnappedDragSkipsMeshAndOverlayWork) {
  PrimitiveCubeInteractionHarness h;
  h.snap = true;
  const auto cube = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = h.camera.project_world_point(control.world_position + cube->world_x).position;
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

TEST(PrimitiveCubeInteractionTests, CameraChangeCancelsResizeAndReservesRelease) {
  PrimitiveCubeInteractionHarness h;
  const auto before = h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = h.camera.project_world_point(control.world_position + before->world_x).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
  ASSERT_NE(h.box.live_primitive()->local_max.x, before->local_max.x);
  EXPECT_TRUE(h.frame({.left_down = true, .mouse_local = end,
                       .camera_changed = true}).consumed);
  EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.x, before->local_max.x);
  EXPECT_TRUE(h.pointer(end, false, false, true).consumed);
}

TEST(PrimitiveCubeInteractionTests, ControllerCancellationAfterSelectionLossRestoresResize) {
  PrimitiveCubeInteractionHarness h;
  ViewportInteractionController controller;
  const auto before = h.box.live_primitive();
  const auto control = controller.primitive_creation_controls(
      h.editor.tools().live_primitive_creation(), h.camera, 1).controls[0];
  const auto end = h.camera.project_world_point(control.world_position + before->world_x).position;
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

TEST(PrimitiveCubeInteractionTests, CylinderProjectsThreeDotsAndOneRimButtonWithCachedPicking) {
  PrimitiveCubeInteractionHarness h(true);
  auto &interaction = h.modal.primitive_creation;
  for (const bool orthographic : {false, true}) {
    if (orthographic) h.camera.view_axis(ViewportViewAxis::Z, 1);
    const auto primitive = h.box.live_primitive();
    ASSERT_TRUE(primitive);
    const auto &frame = interaction.project(primitive, h.camera, 2);
    int visible = 0;
    for (int index = 0; index < 9; ++index) {
      EXPECT_EQ(frame.controls[index].visible, index == 0 || index == 2 || index == 3 || index == 6);
      if (!frame.controls[index].visible) continue;
      ++visible;
      EXPECT_EQ(interaction.hit_test(frame.controls[index].screen_position), index);
      EXPECT_FLOAT_EQ(frame.controls[index].radius_pixels, index < 6 ? 12 : 18);
    }
    EXPECT_EQ(visible, 4);
    EXPECT_EQ(frame.controls[0].resize_control, ViewportPrimitiveResizeControl::Radius);
    EXPECT_FLOAT_EQ(frame.controls[0].resize_axis.y, 0);
    const auto count = interaction.projection_count();
    const auto revision = frame.revision;
    for (int i = 0; i < 20; ++i) interaction.project(primitive, h.camera, 2);
    EXPECT_EQ(interaction.projection_count(), count);
    EXPECT_EQ(frame.revision, revision);
  }
}

TEST(PrimitiveCubeInteractionTests, CylinderHeightDotsMoveOnlyChosenEndAndUseSnapBypass) {
  for (const int index : {2, 3}) {
    for (const bool snap : {false, true}) {
      PrimitiveCubeInteractionHarness h(true);
      h.snap = snap;
      const auto before = *h.box.live_primitive();
      const auto control = h.modal.primitive_creation.frame().controls[index];
      const float displacement = index == 2 ? 0.8f : -0.8f;
      const QVec2 end = h.camera.project_world_point(
          control.world_position + before.world_y * displacement).position;
      ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
      ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
      ASSERT_TRUE(h.pointer(end, false, false, true).consumed);
      const auto after = *h.box.live_primitive();
      const float expected = snap ? (index == 2 ? 1.0f : -1.0f) : displacement;
      EXPECT_NEAR(after.local_max.y, before.local_max.y + (index == 2 ? expected : 0), 0.0001);
      EXPECT_NEAR(after.local_min.y, before.local_min.y + (index == 3 ? expected : 0), 0.0001);
      EXPECT_FLOAT_EQ(after.local_min.x, before.local_min.x);
      EXPECT_FLOAT_EQ(after.local_max.z, before.local_max.z);
      EXPECT_FALSE(after.resizing);
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    }
  }
}

TEST(PrimitiveCubeInteractionTests, CylinderRadiusKeepsCenterAndAspectThroughSettingsAndRepeatResize) {
  PrimitiveCubeInteractionHarness h(true);
  auto &interaction = h.modal.primitive_creation;
  for (const float displacement : {0.8f, -0.3f}) {
    const auto before = *h.box.live_primitive();
    const auto control = interaction.project(before, h.camera, 1).controls[0];
    const QVec3 world_axis = before.world_x * control.resize_axis.x +
                            before.world_z * control.resize_axis.z;
    const auto end = h.camera.project_world_point(control.world_position + world_axis * displacement).position;
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, false, true).consumed);
    const auto after = *h.box.live_primitive();
    const auto size = after.local_max - after.local_min;
    const auto original_size = before.local_max - before.local_min;
    EXPECT_NEAR(size.x / size.z, original_size.x / original_size.z, 0.0001);
    EXPECT_NEAR(after.local_min.x + after.local_max.x,
                before.local_min.x + before.local_max.x, 0.0001);
    EXPECT_NEAR(after.local_min.z + after.local_max.z,
                before.local_min.z + before.local_max.z, 0.0001);
    EXPECT_FLOAT_EQ(after.local_min.y, before.local_min.y);
    EXPECT_FLOAT_EQ(after.local_max.y, before.local_max.y);
    EXPECT_EQ(size.x > original_size.x, displacement > 0);
    ASSERT_TRUE(h.editor.tools().toggle_primitive_caps().ok());
    ASSERT_TRUE(h.editor.tools().flip_primitive_faces().ok());
    ASSERT_TRUE(h.box.set_segments(0, 12).ok);
    const auto edited = *h.box.live_primitive();
    EXPECT_FLOAT_EQ(edited.local_min.x, after.local_min.x);
    EXPECT_FLOAT_EQ(edited.local_max.z, after.local_max.z);
    EXPECT_NE(edited.caps, after.caps);
    EXPECT_NE(edited.flip_faces, after.flip_faces);
    EXPECT_EQ(edited.side_count, 12);
  }
}

TEST(PrimitiveCubeInteractionTests, CylinderSegmentButtonUsesCtrlAndBothLimitsWithoutNewFootprint) {
  PrimitiveCubeInteractionHarness h(true);
  auto &interaction = h.modal.primitive_creation;
  for (const int initial : {3, 4, 127, 128}) {
    for (const bool decrement : {false, true}) {
      ASSERT_TRUE(h.box.set_segments(0, initial).ok);
      const auto button = interaction.project(h.box.live_primitive(), h.camera, 1).controls[6];
      h.pointer(button.screen_position, false, false, false, decrement);
      const bool enabled = decrement ? initial > 3 : initial < 128;
      EXPECT_EQ(interaction.frame().controls[6].enabled, enabled);
      EXPECT_EQ(interaction.frame().controls[6].subdivision_decrement, decrement);
      const auto revision = h.box.live_primitive()->revision;
      const auto content = h.session.modeling().content_revision();
      ASSERT_TRUE(h.pointer(button.screen_position, true, true, false, decrement).consumed);
      ASSERT_TRUE(h.pointer(button.screen_position, false, false, true, !decrement).consumed);
      EXPECT_EQ(h.box.live_primitive()->side_count, initial + (enabled ? (decrement ? -1 : 1) : 0));
      if (!enabled) {
        EXPECT_EQ(h.box.live_primitive()->revision, revision);
        EXPECT_EQ(h.session.modeling().content_revision(), content);
      }
      EXPECT_EQ(h.session.modeling().box_drag_state(), ModelingBoxDragState::Idle);
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    }
  }
}

TEST(PrimitiveCubeInteractionTests, CylinderCanceledRadiusRestoresBoundsAndReservesRelease) {
  for (const bool undo : {false, true}) {
    PrimitiveCubeInteractionHarness h(true);
    const auto before = *h.box.live_primitive();
    const auto control = h.modal.primitive_creation.frame().controls[0];
    const auto end = h.camera.project_world_point(control.world_position +
        before.world_x * control.resize_axis.x + before.world_z * control.resize_axis.z).position;
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    EXPECT_NE(h.box.live_primitive()->local_max.x, before.local_max.x);
    if (undo) {
      ASSERT_TRUE(h.editor.history().undo().ok());
      EXPECT_TRUE(h.pointer(end, false, true, false).consumed);
    } else {
      EXPECT_TRUE(h.frame({.left_down = true, .viewport_focused = false,
                           .pointer_interaction_canceled = true}).consumed);
    }
    EXPECT_TRUE(h.pointer(end, false, false, true).consumed);
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_min.x, before.local_min.x);
    EXPECT_FLOAT_EQ(h.box.live_primitive()->local_max.z, before.local_max.z);
    EXPECT_FALSE(h.box.live_primitive()->resizing);
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
  }
}

TEST(PrimitiveCubeInteractionTests, CylinderRadiusRepeatedSnappedPositionSkipsMeshAndOverlayWork) {
  PrimitiveCubeInteractionHarness h(true);
  h.snap = true;
  const auto before = *h.box.live_primitive();
  const auto control = h.modal.primitive_creation.frame().controls[0];
  const auto end = h.camera.project_world_point(control.world_position +
      before.world_x * control.resize_axis.x + before.world_z * control.resize_axis.z).position;
  h.pointer(control.screen_position, true, true, false);
  h.pointer(end, false, true, false);
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

TEST(PrimitiveCubeInteractionTests, CylinderRadiusDragUsesRotatedScaledPlacementAxes) {
  PrimitiveCubeInteractionHarness h(true);
  auto primitive = *h.box.live_primitive();
  primitive.world_origin = {1, 0, 0};
  primitive.world_x = {0, 0, 2};
  primitive.world_y = {0, 1.5f, 0};
  primitive.world_z = {-0.5f, 0, 0};
  PrimitiveCreationInteraction interaction;
  const auto control = interaction.project(primitive, h.camera, 1.5f).controls[0];
  ASSERT_TRUE(control.visible);
  ASSERT_EQ(interaction.hit_test(control.screen_position), 0);
  const QVec3 axis = primitive.world_x * control.resize_axis.x +
                     primitive.world_z * control.resize_axis.z;
  interaction.begin(0, primitive, h.camera, control.screen_position);
  const auto end = h.camera.project_world_point(control.world_position + axis * 0.75f);
  ASSERT_TRUE(end.visible);
  const auto displacement = interaction.displacement(h.camera, end.position);
  ASSERT_TRUE(displacement);
  EXPECT_NEAR(*displacement, 0.75, 0.0001);
  EXPECT_NEAR(interaction.axis_world_scale(), length(axis), 0.0001);
}

TEST(PrimitiveCubeInteractionTests, RoundShapesExposeOnlyRelevantControlsAndGatedFeedback) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere,
                          ViewportPrimitiveCreationShape::Torus}) {
    PrimitiveCubeInteractionHarness h(shape);
    auto &interaction = h.modal.primitive_creation;
    const auto primitive = h.box.live_primitive();
    ASSERT_TRUE(primitive);
    const auto &frame = interaction.project(primitive, h.camera, 1.5f);
    int dots = 0, buttons = 0;
    for (int index = 0; index < 9; ++index) {
      if (!frame.controls[index].visible) continue;
      index < 6 ? ++dots : ++buttons;
      EXPECT_EQ(interaction.hit_test(frame.controls[index].screen_position), index);
    }
    EXPECT_EQ(dots, 3);
    EXPECT_EQ(buttons, shape == ViewportPrimitiveCreationShape::Cone ? 1 : 2);
    interaction.hover(6);
    EXPECT_TRUE(frame.label_is_count);
    EXPECT_EQ(frame.label_value, frame.controls[6].segment_count);
    EXPECT_EQ(frame.highlight_point_count, 65u);
    const auto key = frame.label_key;
    if (buttons == 2) {
      const auto loop = frame.highlight_points;
      interaction.hover(7);
      EXPECT_NE(frame.label_key, key);
      bool different = false;
      for (std::size_t index = 0; index < loop.size(); ++index) {
        different |= length(loop[index] - frame.highlight_points[index]) > 0.001f;
      }
      EXPECT_TRUE(different);
    }
    const auto builds = interaction.projection_count();
    const auto revision = frame.revision;
    for (int repeat = 0; repeat < 20; ++repeat) {
      interaction.project(primitive, h.camera, 1.5f);
      interaction.hover(buttons == 2 ? 7 : 6);
    }
    EXPECT_EQ(interaction.projection_count(), builds);
    EXPECT_EQ(frame.revision, revision);
    interaction.clear();
    EXPECT_TRUE(frame.label_key.empty());
    EXPECT_EQ(frame.highlight_point_count, 0u);
  }
}

TEST(PrimitiveCubeInteractionTests, ConeSphereDotsRetainOppositePoleAndFootprintProportions) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere}) {
    for (const int index : {0, 2, 3}) {
      PrimitiveCubeInteractionHarness h(shape);
      const auto before = *h.box.live_primitive();
      const auto control = h.modal.primitive_creation.frame().controls[index];
      const QVec3 axis = before.world_x * control.resize_axis.x +
          before.world_y * control.resize_axis.y + before.world_z * control.resize_axis.z;
      const float delta = index == 3 ? -0.7f : 0.7f;
      const auto end = h.camera.project_world_point(control.world_position + axis * delta).position;
      ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
      ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
      ASSERT_TRUE(h.pointer(end, false, false, true).consumed);
      const auto after = *h.box.live_primitive();
      EXPECT_FALSE(after.resizing);
      if (index == 0) {
        EXPECT_GT(after.local_max.x, before.local_max.x);
        EXPECT_NEAR((after.local_max.x - after.local_min.x) /
                    (after.local_max.z - after.local_min.z), 2, 0.0001);
        EXPECT_FLOAT_EQ(after.local_min.y, before.local_min.y);
        EXPECT_FLOAT_EQ(after.local_max.y, before.local_max.y);
      } else {
        EXPECT_NEAR(after.local_max.y, before.local_max.y + (index == 2 ? delta : 0), 0.0001);
        EXPECT_NEAR(after.local_min.y, before.local_min.y + (index == 3 ? delta : 0), 0.0001);
      }
      EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
    }
  }
}

TEST(PrimitiveCubeInteractionTests, RoundShapeButtonsChangeOnlyTheirSettingAndFreezeCtrlAtLimits) {
  for (const auto shape : {ViewportPrimitiveCreationShape::Cone,
                          ViewportPrimitiveCreationShape::Sphere,
                          ViewportPrimitiveCreationShape::Torus}) {
    PrimitiveCubeInteractionHarness h(shape);
    auto &interaction = h.modal.primitive_creation;
    const int axes = shape == ViewportPrimitiveCreationShape::Cone ? 1 : 2;
    for (int axis = 0; axis < axes; ++axis) {
      ASSERT_TRUE(h.box.set_segments(axis, 6).ok);
      auto before = interaction.project(h.box.live_primitive(), h.camera, 1);
      const auto button = before.controls[axis + 6];
      ASSERT_TRUE(h.pointer(button.screen_position, true, true, false, true).consumed);
      ASSERT_TRUE(h.pointer(button.screen_position, false, false, true, false).consumed);
      const auto after = interaction.project(h.box.live_primitive(), h.camera, 1);
      EXPECT_EQ(after.controls[axis + 6].segment_count, 5);
      if (axes == 2) EXPECT_EQ(after.controls[7 - axis].segment_count, before.controls[7 - axis].segment_count);
      ASSERT_TRUE(h.box.set_segments(axis, button.minimum_segments).ok);
      const auto minimum = interaction.project(h.box.live_primitive(), h.camera, 1).controls[axis + 6];
      h.pointer(minimum.screen_position, false, false, false, true);
      EXPECT_FALSE(interaction.frame().controls[axis + 6].enabled);
      const auto revision = h.box.live_primitive()->revision;
      h.pointer(minimum.screen_position, true, true, false, true);
      h.pointer(minimum.screen_position, false, false, true, true);
      EXPECT_EQ(h.box.live_primitive()->revision, revision);
    }
    EXPECT_EQ(h.session.primitive_creation().shape_count(), 1);
  }
}

TEST(PrimitiveCubeInteractionTests, TorusBoundaryDragsPreserveTheOtherBoundaryAndCancelRatioWithBounds) {
  for (const int index : {0, 1, 2}) {
    PrimitiveCubeInteractionHarness h(ViewportPrimitiveCreationShape::Torus);
    const auto before = *h.box.live_primitive();
    const auto control = h.modal.primitive_creation.frame().controls[index];
    const QVec3 axis = before.world_x * control.resize_axis.x +
        before.world_y * control.resize_axis.y + before.world_z * control.resize_axis.z;
    const auto end = h.camera.project_world_point(control.world_position + axis * 0.2f).position;
    ASSERT_TRUE(h.pointer(control.screen_position, true, true, false).consumed);
    ASSERT_TRUE(h.pointer(end, false, true, false).consumed);
    const auto after = *h.box.live_primitive();
    if (index == 0) {
      EXPECT_GT(after.local_max.x, before.local_max.x);
      EXPECT_NEAR((after.local_max.x - after.local_min.x) * (1 - 2 * after.tube_radius_ratio),
                  (before.local_max.x - before.local_min.x) * (1 - 2 * before.tube_radius_ratio), 0.0001);
    } else if (index == 1) {
      EXPECT_FLOAT_EQ(after.local_min.x, before.local_min.x);
      EXPECT_FLOAT_EQ(after.local_max.z, before.local_max.z);
      EXPECT_LT(after.tube_radius_ratio, before.tube_radius_ratio);
    } else {
      EXPECT_NEAR(after.local_max.y, before.local_max.y + 0.2f, 0.0001);
      EXPECT_FLOAT_EQ(after.local_min.y, before.local_min.y);
    }
    EXPECT_TRUE(h.frame({.left_down = true, .viewport_focused = false,
                        .pointer_interaction_canceled = true}).consumed);
    EXPECT_TRUE(h.pointer(end, false, false, true).consumed);
    const auto canceled = *h.box.live_primitive();
    EXPECT_FLOAT_EQ(canceled.local_max.x, before.local_max.x);
    EXPECT_FLOAT_EQ(canceled.local_max.y, before.local_max.y);
    EXPECT_DOUBLE_EQ(canceled.tube_radius_ratio, before.tube_radius_ratio);
    EXPECT_FALSE(canceled.resizing);
  }
}

} // namespace
} // namespace quader
