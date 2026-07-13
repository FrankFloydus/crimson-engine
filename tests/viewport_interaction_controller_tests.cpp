////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_editor_api.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/pivot/pivot_tool_service.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/viewport_box_overlay_builder.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/overlay/viewport_transform_gizmo_overlay_builder.hpp"
#include "editor/session/project_session.hpp"
#include "scene/scene_bounds.hpp"
#include "ui/viewport_hover_controller.hpp"
#include "ui/viewport_host_interaction_policy.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_set>
#include <vector>

namespace {

/// Owns a complete viewport interaction harness for one synthetic controller scenario.
struct ViewportInteractionHarness {
    quader::ProjectSession session;
    quader::QuaderEditorApi editor;
    quader::ViewportInputRouter router;
    quader::ViewportCameraController camera;
    quader::ViewportSettings settings;
    quader::ViewportInteractionController controller;
    quader::ViewportInputFrame input;
    quader::ViewportInputResult input_result;
    std::vector<quader::ViewportSceneBillboardPickCandidate>
        billboard_pick_candidates;

    ViewportInteractionHarness()
        : editor(quader::QuaderApiSessionInternalAccess::from_project_session(
              session)) {
      camera.set_viewport_size(800.0f, 600.0f);
      camera.focus_origin(8.0f);
      camera.set_view_direction({0.0f, 0.0f, -1.0f});

      input.viewport_hovered = true;
      input.viewport_focused = true;
      input.viewport_size = {800.0f, 600.0f};
      input.mouse_screen = {400.0f, 300.0f};
      input.mouse_local = {400.0f, 300.0f};
    }

    [[nodiscard]] quader::ViewportInteractionFrame
    frame(bool left_pressed = false, bool left_down = false,
          bool left_released = false, quader::QVec2 left_click_local = {},
          bool right_pressed = false, bool right_down = false,
          bool right_released = false, quader::QVec2 right_click_local = {}) {
      return {
          .input_router = router,
          .camera = camera,
          .settings = settings,
          .input = input,
          .input_result = input_result,
          .left_click_local = left_click_local,
          .left_click_count = 1,
          .left_pressed = left_pressed,
          .left_down = left_down,
          .left_released = left_released,
          .right_click_local = right_click_local,
          .right_click_count = 1,
          .right_pressed = right_pressed,
          .right_down = right_down,
          .right_released = right_released,
          .scene_billboard_pick_candidates = billboard_pick_candidates,
          .selection_state =
              {
                  .summary = editor.selection().summary(),
                  .scene_revision = session.scene().revision(),
                  .asset_revision = session.assets().revision(),
                  .pivot_revision =
                      session.pivot().selection_pivot_revision(),
              },
      };
    }
};

quader::QuaderModelingObjectRef
create_unit_box(ViewportInteractionHarness &harness) {
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{
              .min = {-1.0f, -1.0f, -1.0f},
              .max = {1.0f, 1.0f, 1.0f},
          },
          "Viewport Interaction Box");
  EXPECT_TRUE(box.ok) << box.message;
  return box.value();
}

quader::QuaderModelingObjectRef
create_box_at(ViewportInteractionHarness &harness, quader::QVec3 center) {
  const quader::QVec3 half{1.0f, 1.0f, 1.0f};
  const quader::QuaderApiResult<quader::QuaderModelingObjectRef> box =
      harness.editor.modeling().create_box(
          quader::QuaderBounds{
              .min = center - half,
              .max = center + half,
          },
          "Viewport Interaction Box");
  EXPECT_TRUE(box.ok) << box.message;
  return box.value();
}

const quader_editor_core::TransformGizmoAxisPrimitive *
find_axis(const quader_editor_core::TransformGizmoFrame &frame,
          quader_editor_core::TransformAxis axis) {
  const auto found = std::find_if(
      frame.axes.begin(), frame.axes.end(),
      [axis](const auto &primitive) { return primitive.axis == axis; });
  return found == frame.axes.end() ? nullptr : &*found;
}

const quader_editor_core::TransformGizmoRingSegmentPrimitive *
find_ring(const quader_editor_core::TransformGizmoFrame &frame,
          quader_editor_core::TransformAxis axis) {
  const auto found =
      std::find_if(frame.ring_segments.begin(), frame.ring_segments.end(),
                   [axis](const auto &primitive) {
                     return primitive.axis == axis && primitive.front_facing;
                   });
  return found == frame.ring_segments.end() ? nullptr : &*found;
}

quader::QVec2 rotated_point(quader::QVec2 pivot, quader::QVec2 point,
                            float radians) {
  const float c = std::cos(radians);
  const float s = std::sin(radians);
  const quader::QVec2 delta = point - pivot;
  return {
      pivot.x + delta.x * c - delta.y * s,
      pivot.y + delta.x * s + delta.y * c,
  };
}

void expect_transform_near(const quader::TransformComponent &actual,
                           const quader::TransformComponent &expected) {
  EXPECT_NEAR(actual.position.x, expected.position.x, 0.001f);
  EXPECT_NEAR(actual.position.y, expected.position.y, 0.001f);
  EXPECT_NEAR(actual.position.z, expected.position.z, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.x, expected.rotation_euler.x, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.y, expected.rotation_euler.y, 0.001f);
  EXPECT_NEAR(actual.rotation_euler.z, expected.rotation_euler.z, 0.001f);
  EXPECT_NEAR(actual.scale.x, expected.scale.x, 0.001f);
  EXPECT_NEAR(actual.scale.y, expected.scale.y, 0.001f);
  EXPECT_NEAR(actual.scale.z, expected.scale.z, 0.001f);
}

quader_editor_core::TransformGizmoFrameInput
test_gizmo_input(quader_editor_core::ViewportInteractionTool tool,
                 const ViewportInteractionHarness &harness) {
  quader_editor_core::TransformGizmoFrameInput input;
  input.tool = tool;
  input.has_selection = true;
  input.has_pivot = true;
  input.pivot = {};
  input.camera = quader::ViewportCameraAdapter::runtime_camera(
      harness.camera, harness.input.viewport_size);
  input.gizmo_size_pixels = quader::kOverlayTransformGizmoSizePixels;
  input.pick_radius_pixels = quader::kOverlayTransformGizmoPickRadiusPixels;
  input.ring_segments = 32;
  return input;
}

quader::ViewportOverlayFrame
build_gizmo_overlay(const quader_editor_core::TransformGizmoFrame &frame,
                    const ViewportInteractionHarness &harness) {
  quader::ViewportOverlayFrame overlay;
  quader::ViewportTransformGizmoOverlayBuilder::append(
      overlay, frame,
      quader::ViewportCameraAdapter::overlay_camera(
          harness.camera, harness.input.viewport_size));
  return overlay;
}

quader::QVec2
axis_screen_point(const quader_editor_core::TransformGizmoAxisPrimitive &axis,
                  float factor) {
  return axis.screen_start + (axis.screen_tip - axis.screen_start) * factor;
}

void select_single_component(ViewportInteractionHarness &harness,
                             quader::QuaderModelingObjectRef object,
                             quader::QuaderSelectionMode mode) {
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok);
  ASSERT_TRUE(harness.editor.selection().set_mode(mode).ok());
  switch (mode) {
  case quader::QuaderSelectionMode::Vertex:
    ASSERT_TRUE(harness.editor.selection().select_vertex(object, 1).ok());
    break;
  case quader::QuaderSelectionMode::Edge:
    ASSERT_TRUE(harness.editor.selection().select_edge(object, 1, 2).ok());
    break;
  case quader::QuaderSelectionMode::Face:
    ASSERT_TRUE(harness.editor.selection().select_face(object, 1).ok());
    break;
  default:
    FAIL() << "Unsupported component mode";
    break;
  }
}

void press_update_release_gizmo(ViewportInteractionHarness &harness,
                                quader::QVec2 start, quader::QVec2 update) {
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  const quader::ViewportBeforeToolInteractionResult press =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(true, true, false, start));
  ASSERT_TRUE(press.gizmo_consumed);

  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  const quader::ViewportBeforeToolInteractionResult drag =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, true, false, start));
  EXPECT_TRUE(drag.gizmo_consumed);

  const quader::ViewportBeforeToolInteractionResult release =
      harness.controller.handle_before_active_tool(
          harness.editor, harness.frame(false, false, true, start));
  EXPECT_TRUE(release.gizmo_consumed);
}

std::vector<quader::EntityId>
selected_entities(const ViewportInteractionHarness &harness) {
  return harness.session.scene().selected_entities();
}

bool has_snap_target_vertex_point(const quader::QuaderOverlayFrame &frame) {
  return std::any_of(frame.points.begin(), frame.points.end(),
                     [](const quader::QuaderOverlayPoint &point) {
                       return point.layer ==
                              quader::QuaderOverlayLayer::SnapTargetVertex;
                     });
}

bool has_triangle_style(const quader::QuaderOverlayFrame &frame,
                        quader::QuaderOverlayStyleRole role) {
  return std::any_of(frame.triangles.begin(), frame.triangles.end(),
                     [role](const quader::QuaderOverlayTriangle &triangle) {
                       return triangle.style_role == role;
                     });
}

bool has_line_style(const quader::QuaderOverlayFrame &frame,
                    quader::QuaderOverlayStyleRole role) {
  return std::any_of(frame.lines.begin(), frame.lines.end(),
                     [role](const quader::QuaderOverlayLine &line) {
                       return line.style_role == role;
                     });
}

} // namespace

TEST(ViewportTransformGizmoOverlayBuilderTests, EmitsGodotStyleTransformGizmoVisuals) {
    ViewportInteractionHarness harness;

    const quader_editor_core::TransformGizmoFrame move_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Move, harness));
    ASSERT_TRUE(move_frame.info.ok);
    const quader::ViewportOverlayFrame move_overlay =
        build_gizmo_overlay(move_frame, harness);

    EXPECT_EQ(
        std::count_if(
            move_overlay.lines.begin(), move_overlay.lines.end(),
            [](const quader::ViewportOverlayLine &line) {
              return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                     line.color == quader::kOverlayToolAxisXColor &&
                     line.width_pixels ==
                         quader::kOverlayTransformGizmoAxisLineWidthPixels;
            }),
        1);
    EXPECT_EQ(std::count_if(
                  move_overlay.triangles.begin(), move_overlay.triangles.end(),
                  [](const quader::ViewportOverlayTriangle &triangle) {
                    return triangle.layer ==
                               quader::ViewportOverlayLayer::ToolPlane &&
                           triangle.color == quader::kOverlayToolPlaneXYColor;
                  }),
              2);
    EXPECT_TRUE(std::none_of(
        move_overlay.triangles.begin(), move_overlay.triangles.end(),
        [](const quader::ViewportOverlayTriangle &triangle) {
          return triangle.color == quader::kOverlayToolCenterColor;
        }));
    EXPECT_TRUE(
        std::none_of(move_overlay.lines.begin(), move_overlay.lines.end(),
                     [](const quader::ViewportOverlayLine &line) {
                       return line.color == quader::kOverlayToolLineShadowColor;
                     }));

    const quader_editor_core::TransformGizmoFrame scale_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Scale, harness));
    ASSERT_TRUE(scale_frame.info.ok);
    const quader::ViewportOverlayFrame scale_overlay =
        build_gizmo_overlay(scale_frame, harness);
    EXPECT_EQ(
        std::count_if(scale_overlay.triangles.begin(),
                      scale_overlay.triangles.end(),
                      [](const quader::ViewportOverlayTriangle &triangle) {
                        return triangle.layer ==
                                   quader::ViewportOverlayLayer::ToolHandle &&
                               triangle.color == quader::kOverlayToolAxisXColor;
                      }),
        12);

    const quader_editor_core::TransformGizmoFrame rotate_frame =
        quader_editor_core::build_transform_gizmo_frame(test_gizmo_input(
            quader_editor_core::ViewportInteractionTool::Rotate, harness));
    ASSERT_TRUE(rotate_frame.info.ok);
    const quader::ViewportOverlayFrame rotate_overlay =
        build_gizmo_overlay(rotate_frame, harness);
    const auto axis_ring_lines = std::count_if(
        rotate_overlay.lines.begin(), rotate_overlay.lines.end(),
        [](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                 line.width_pixels == 3.0f;
        });
    const auto front_facing_ring_segments = std::count_if(
        rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(),
        [](const auto &segment) { return segment.front_facing; });
    EXPECT_EQ(axis_ring_lines, front_facing_ring_segments);
    EXPECT_TRUE(std::any_of(
        rotate_frame.ring_segments.begin(), rotate_frame.ring_segments.end(),
        [](const auto &segment) { return !segment.front_facing; }));
    EXPECT_TRUE(std::none_of(
        rotate_overlay.lines.begin(), rotate_overlay.lines.end(),
        [](const quader::ViewportOverlayLine &line) {
          return line.layer == quader::ViewportOverlayLayer::ToolHandle &&
                 ((line.color >> 24u) & 0xffu) < 100u;
        }));

    quader_editor_core::TransformGizmoFrame rotate_indicator_frame =
        rotate_frame;
    rotate_indicator_frame.info.rotate_indicator = {
        .active = true,
        .axis = quader_editor_core::TransformAxis::Z,
        .world_axis = {0.0f, 0.0f, 1.0f},
        .start_direction = {1.0f, 0.0f, 0.0f},
        .has_mouse_position = true,
        .mouse_position = {0.0f, 2.0f, 0.0f},
        .angle_degrees = 67.5,
    };
    const quader::ViewportOverlayFrame rotate_indicator_overlay =
        build_gizmo_overlay(rotate_indicator_frame, harness);
    EXPECT_GT(std::count_if(
                  rotate_indicator_overlay.triangles.begin(),
                  rotate_indicator_overlay.triangles.end(),
                  [](const quader::ViewportOverlayTriangle &triangle) {
                    return triangle.layer ==
                               quader::ViewportOverlayLayer::ToolPreviewFill &&
                           triangle.color ==
                               quader::kOverlayTransformGizmoRotatePieFillColor;
                  }),
              0);
    EXPECT_EQ(std::count_if(
                  rotate_indicator_overlay.lines.begin(),
                  rotate_indicator_overlay.lines.end(),
                  [](const quader::ViewportOverlayLine &line) {
                    return line.layer ==
                               quader::ViewportOverlayLayer::ToolPreviewLine &&
                           line.color ==
                               quader::kOverlayTransformGizmoRotatePieLineColor;
                  }),
              2);
    EXPECT_EQ(
        std::count_if(
            rotate_indicator_overlay.lines.begin(),
            rotate_indicator_overlay.lines.end(),
            [](const quader::ViewportOverlayLine &line) {
              return line.layer ==
                         quader::ViewportOverlayLayer::ToolPreviewLine &&
                     line.color ==
                         quader::kOverlayTransformGizmoRotatePieLineColor &&
                     std::abs(line.width_pixels - 1.55f) <= 0.001f &&
                     length(line.end - quader::QVec3{0.0f, 2.0f, 0.0f}) <=
                         0.001f;
            }),
        1);
}

TEST(ViewportInteractionControllerTests, RotateGizmoDragEmitsPieAngleIndicator) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Rotate);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
        find_ring(*frame, quader_editor_core::TransformAxis::Z);
    ASSERT_NE(ring, nullptr);

    const quader::QVec2 start = ring->screen_start;
    const quader::QVec2 update =
        rotated_point(frame->info.screen_pivot, start, 0.6f);
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    ASSERT_TRUE(press.gizmo_consumed);

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const quader::ViewportBeforeToolInteractionResult drag =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(drag.gizmo_consumed);

    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.rotate_indicator.active);
    EXPECT_NE(frame->info.rotate_indicator.axis,
              quader_editor_core::TransformAxis::None);
    EXPECT_NE(frame->info.rotate_indicator.axis,
              quader_editor_core::TransformAxis::All);
    EXPECT_NEAR(length(frame->info.rotate_indicator.world_axis), 1.0f, 0.001f);
    ASSERT_TRUE(frame->info.rotate_indicator.has_mouse_position);
    const quader::ViewportScreenPoint projected_mouse =
        harness.camera.project_world_point(
            frame->info.rotate_indicator.mouse_position);
    ASSERT_TRUE(projected_mouse.visible);
    EXPECT_NEAR(projected_mouse.position.x, update.x, 1.0f);
    EXPECT_NEAR(projected_mouse.position.y, update.y, 1.0f);
    EXPECT_GT(std::abs(frame->info.rotate_indicator.angle_degrees), 1.0);
    const double drag_angle = frame->info.rotate_indicator.angle_degrees;

    const quader::ViewportBeforeToolInteractionResult stationary =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, true, false, start));
    ASSERT_TRUE(stationary.gizmo_consumed);
    frame = harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.rotate_indicator.active);
    EXPECT_NEAR(frame->info.rotate_indicator.angle_degrees, drag_angle, 0.001);

    const quader::ViewportOverlayFrame overlay =
        build_gizmo_overlay(*frame, harness);
    EXPECT_GT(std::count_if(overlay.triangles.begin(), overlay.triangles.end(), [](const quader::ViewportOverlayTriangle& triangle) {
        return triangle.layer == quader::ViewportOverlayLayer::ToolPreviewFill &&
                triangle.color == quader::kOverlayTransformGizmoRotatePieFillColor;
    }), 0);

    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(false, false, true, start));
    EXPECT_TRUE(release.gizmo_consumed);
}

TEST(ViewportInteractionControllerTests, ClickFrameSelectsComponentThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, {400.0f, 300.0f}),
        quader::ViewportToolResult{}, before_tool);
    EXPECT_EQ(harness.editor.selection().summary().selected_face_count, 0u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, {400.0f, 300.0f}),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Face);
    EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Face);
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests,
     ClickFrameSelectsPointLightBillboardThroughViewportFallback) {
    ViewportInteractionHarness harness;
    quader::QuaderCreatePointLightArgs args;
    args.name = "Viewport Billboard Point Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> light =
        harness.editor.scene().create_point_light(args);
    ASSERT_TRUE(light.ok()) << light.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected =
        harness.camera.project_world_point({});
    ASSERT_TRUE(projected.visible);
    const quader::QVec2 hit =
        projected.position +
        quader::QVec2{quader::kOverlayPointLightGizmoSizePixels * 0.45f, 0.0f};
    harness.input.mouse_local = hit;
    harness.input.mouse_screen = hit;
    harness.billboard_pick_candidates.push_back({
        .entity = light.value().id,
        .position = {},
        .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
    });

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, hit));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, hit),
        quader::ViewportToolResult{}, before_tool);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, hit),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const std::vector<quader::EntityId> selected = selected_entities(harness);
    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected.front(), light.value().id);
    EXPECT_EQ(harness.editor.selection().summary().mode,
              quader::QuaderSelectionMode::Object);
    EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Object);
}

TEST(ViewportInteractionControllerTests,
     ContextMenuPointLightBillboardPreservesSelectedHitSelection) {
    ViewportInteractionHarness harness;
    quader::QuaderCreatePointLightArgs first_args;
    first_args.name = "First Billboard Point Light";
    const quader::QuaderApiResult<quader::QuaderEntityRef> first =
        harness.editor.scene().create_point_light(first_args);
    ASSERT_TRUE(first.ok()) << first.error_message();

    quader::QuaderCreatePointLightArgs second_args;
    second_args.name = "Second Billboard Point Light";
    second_args.transform.position = {3.0f, 0.0f, 0.0f};
    const quader::QuaderApiResult<quader::QuaderEntityRef> second =
        harness.editor.scene().create_point_light(second_args);
    ASSERT_TRUE(second.ok()) << second.error_message();

    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({first.value().id, second.value().id},
                                    second.value().id)
                    .ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected =
        harness.camera.project_world_point({});
    ASSERT_TRUE(projected.visible);
    harness.input.mouse_local = projected.position;
    harness.input.mouse_screen = projected.position;
    harness.billboard_pick_candidates.push_back({
        .entity = first.value().id,
        .position = {},
        .size_pixels = quader::kOverlayPointLightGizmoSizePixels,
    });

    const quader::QuaderSelectionSummary summary =
        harness.controller.pick_context_menu_target(harness.editor,
                                                    harness.frame());

    EXPECT_TRUE(summary.has_selection);
    EXPECT_EQ(summary.selected_object_count, 2u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_NE(std::find(selected.begin(), selected.end(), first.value().id),
              selected.end());
    EXPECT_NE(std::find(selected.begin(), selected.end(), second.value().id),
              selected.end());
    EXPECT_EQ(harness.session.scene().primary_selection(), second.value().id);
}

TEST(ViewportInteractionControllerTests, VertexSnapDoesNotStealPendingMoveGizmoVertexClick) {
    ViewportInteractionHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
    (void)harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, true);
    ASSERT_TRUE(
        harness.settings.bool_value(quader::ViewportBoolSettingId::VertexSnap));

    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Vertex);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader::QVec2 click = frame->info.screen_pivot;

    harness.input.mouse_local = click;
    harness.input.mouse_screen = click;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, click));
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(press.selection_click_requested);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(has_snap_target_vertex_point(
        harness.editor.query().selection_overlay()));

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, click);
    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     release_frame);
    EXPECT_FALSE(release.gizmo_consumed);
    ASSERT_TRUE(release.selection_click_requested);
    EXPECT_EQ(release.selection_click_local.x, click.x);
    EXPECT_EQ(release.selection_click_local.y, click.y);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{}, release);

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Vertex);
    EXPECT_EQ(selection.selected_vertex_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Vertex);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(has_snap_target_vertex_point(
        harness.editor.query().selection_overlay()));
}

TEST(ViewportInteractionControllerTests, ShortMoveGizmoClickKeepsEdgeSelection) {
    ViewportInteractionHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});

    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Edge);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader::QVec2 click = frame->info.screen_pivot;

    harness.input.mouse_local = click;
    harness.input.mouse_screen = click;
    const quader::ViewportBeforeToolInteractionResult press =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, click));
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(press.selection_click_requested);

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, click);
    const quader::ViewportBeforeToolInteractionResult release =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     release_frame);
    EXPECT_FALSE(release.gizmo_consumed);
    ASSERT_TRUE(release.selection_click_requested);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{}, release);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 1u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
    EXPECT_FALSE(harness.controller.dragging());
}

TEST(ViewportInteractionControllerTests, AltClickEdgeSelectsLoopThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Edge)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);

    const quader::ViewportScreenPoint edge_midpoint =
        harness.camera.project_world_point({1.0f, 0.0f, 1.0f});
    ASSERT_TRUE(edge_midpoint.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = edge_midpoint.position;
    harness.input.mouse_screen = edge_midpoint.position;

    const quader::ViewportInteractionFrame press_frame =
        harness.frame(true, true, false, edge_midpoint.position);
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     press_frame);
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, press_frame, quader::ViewportToolResult{},
        before_tool);

    const quader::ViewportInteractionFrame release_frame =
        harness.frame(false, false, true, edge_midpoint.position);
    harness.controller.handle_after_active_tool(
        harness.editor, release_frame, quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Edge);
    EXPECT_EQ(selection.selected_edge_count, 4u);
    EXPECT_EQ(selection.active_kind, quader::QuaderActiveSelectionKind::Edge);
}

TEST(ViewportInteractionControllerTests,
     AltHoverObjectModeUsesFacePlaneHoverOverlay) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint face_point =
        harness.camera.project_world_point({0.0f, 0.0f, 1.0f});
    ASSERT_TRUE(face_point.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = face_point.position;
    harness.input.mouse_screen = face_point.position;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderOverlayFrame overlay =
        harness.editor.query().selection_overlay();
    EXPECT_TRUE(has_triangle_style(
        overlay, quader::QuaderOverlayStyleRole::FacePlaneHoverFace));
    EXPECT_TRUE(has_line_style(
        overlay, quader::QuaderOverlayStyleRole::FacePlaneHoverWire));
    EXPECT_FALSE(has_triangle_style(
        overlay, quader::QuaderOverlayStyleRole::RemoveSelectionFace));
}

TEST(ViewportInteractionControllerTests,
     AltRightClickCutsSelectedMeshByHoveredFacePlane) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef target = create_unit_box(harness);
    quader::QuaderTransform target_transform;
    target_transform.position = {4.0f, 0.0f, 0.0f};
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(target.entity, target_transform)
                    .ok());
    const quader::QuaderModelingObjectRef second_target =
        create_unit_box(harness);
    quader::QuaderTransform second_target_transform;
    second_target_transform.position = {4.0f, -3.0f, 0.0f};
    ASSERT_TRUE(harness.editor.scene()
                    .set_transform(second_target.entity,
                                   second_target_transform)
                    .ok());
    const quader::QuaderApiResult<quader::QuaderModelingObjectRef> reference =
        harness.editor.modeling().create_box(
            quader::QuaderBounds{
                .min = {4.0f, 2.0f, -1.0f},
                .max = {6.0f, 4.0f, 1.0f},
            },
            "Face Plane Reference");
    ASSERT_TRUE(reference.ok) << reference.message;
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({target, second_target})
                    .ok());
    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);
    const std::size_t object_count_before =
        harness.editor.modeling().objects().size();
    const std::unordered_set<int> object_ids_before{
        target.modeling_object_id, second_target.modeling_object_id,
        reference.value().modeling_object_id};

    harness.camera.set_view_direction({1.0f, 0.0f, 0.0f});
    const quader::ViewportScreenPoint face_point =
        harness.camera.project_world_point({4.0f, 3.0f, 0.0f});
    ASSERT_TRUE(face_point.visible);
    harness.input.modifiers.alt = true;
    harness.input.mouse_local = face_point.position;
    harness.input.mouse_screen = face_point.position;

    harness.controller.handle_after_active_tool(
        harness.editor,
        harness.frame(false, false, false, {}, true, true, false,
                      face_point.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const std::vector<quader::QuaderModelingObjectRecord> objects =
        harness.editor.modeling().objects();
    EXPECT_EQ(objects.size(), object_count_before + 2U);
    std::vector<quader::QuaderModelingObjectRecord> created_cut_halves;
    for (const quader::QuaderModelingObjectRecord &object : objects) {
      if (!object_ids_before.contains(object.modeling_object_id)) {
        created_cut_halves.push_back(object);
      }
    }
    ASSERT_EQ(created_cut_halves.size(), 2U);
    const auto expect_created_transform =
        [&](std::size_t index, quader::QVec3 expected_position) {
          const quader::TransformComponent *cut_half_transform =
              harness.session.scene().transform(created_cut_halves[index].entity);
          ASSERT_NE(cut_half_transform, nullptr);
          EXPECT_NEAR(cut_half_transform->position.x, expected_position.x,
                      0.001f);
          EXPECT_NEAR(cut_half_transform->position.y, expected_position.y,
                      0.001f);
          EXPECT_NEAR(cut_half_transform->position.z, expected_position.z,
                      0.001f);
        };
    expect_created_transform(0, target_transform.position);
    expect_created_transform(1, second_target_transform.position);
    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_FALSE(selection.has_selection);
    EXPECT_EQ(selection.selected_object_count, 0u);
    EXPECT_EQ(selection.selected_vertex_count, 0u);
    EXPECT_EQ(selection.selected_edge_count, 0u);
    EXPECT_EQ(selection.selected_face_count, 0u);
    EXPECT_TRUE(harness.editor.history().can_undo());

    ASSERT_TRUE(harness.editor.selection()
                    .select_object(created_cut_halves.front().entity)
                    .ok());
    const quader::QuaderSelectionSummary selected_cut_half =
        harness.editor.selection().summary();
    EXPECT_EQ(selected_cut_half.selected_object_count, 1u);
    EXPECT_EQ(harness.session.scene().selected_entities().front(),
              created_cut_halves.front().entity);
}

TEST(ViewportInteractionControllerTests, ShiftClickRebuildsSliceQuadPreviewThroughViewportFallback) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok());
    ASSERT_TRUE(harness.editor.selection().select_face(box, 1).ok());
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);

    const quader::QuaderApiVoidResult preview =
        harness.editor.commands().execute(
            harness.editor.commands().command_id_for_modeling_action(
                "slice_quad"));
    ASSERT_TRUE(preview.ok()) << preview.error_message();
    const std::optional<quader::QuaderMeshSnapshot> one_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(one_face_preview.has_value());

    harness.camera.set_view_direction({-1.0f, 0.0f, 0.0f});
    const quader::ViewportScreenPoint side_face =
        harness.camera.project_world_point({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(side_face.visible);
    harness.input.modifiers.shift = true;
    harness.input.operation_preview_active = true;
    harness.input.mouse_local = side_face.position;
    harness.input.mouse_screen = side_face.position;

    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor,
            harness.frame(true, true, false, side_face.position));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, side_face.position),
        quader::ViewportToolResult{}, before_tool);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, side_face.position),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary summary = harness.editor.selection().summary();
    EXPECT_EQ(summary.mode, quader::QuaderSelectionMode::Face);
    EXPECT_GT(summary.selected_face_count, 1u);
    const std::optional<quader::QuaderMeshSnapshot> two_face_preview =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(two_face_preview.has_value());
    EXPECT_GT(two_face_preview->vertex_count, one_face_preview->vertex_count);
    EXPECT_GT(two_face_preview->primitive_count,
              one_face_preview->primitive_count);
}

TEST(ViewportInteractionControllerTests, DragFrameBoxSelectsThroughViewportFallback) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{300.0f, 200.0f};
    const quader::QVec2 end{500.0f, 400.0f};
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Contain);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::QuaderSelectionSummary selection = harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Object);
    EXPECT_EQ(selection.selected_object_count, 1u);
    EXPECT_EQ(harness.router.state().selection_mode,
              quader::ViewportSelectionMode::Object);
    EXPECT_TRUE(harness.controller.selection_marquee().active);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(ViewportInteractionControllerTests, ShiftBoxDragAddsSelectionBeforeRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().select_object(left).ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected_right =
        harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projected_right.visible);
    const quader::QVec2 start =
        projected_right.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end =
        projected_right.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.shift = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 2u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_NE(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, PrimaryBoxDragRemovesSelectionBeforeRelease) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef left =
        create_box_at(harness, {-3.0f, 0.0f, 0.0f});
    const quader::QuaderModelingObjectRef right =
        create_box_at(harness, {3.0f, 0.0f, 0.0f});

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().select_objects({left, right}).ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::ViewportScreenPoint projected_right =
        harness.camera.project_world_point({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(projected_right.visible);
    const quader::QVec2 start =
        projected_right.position - quader::QVec2{120.0f, 120.0f};
    const quader::QVec2 end =
        projected_right.position + quader::QVec2{120.0f, 120.0f};

    harness.input.modifiers.primary = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const quader::ViewportBeforeToolInteractionResult before_tool =
        harness.controller.handle_before_active_tool(
            harness.editor, harness.frame(true, true, false, start));
    EXPECT_FALSE(before_tool.gizmo_consumed);
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, true, false, start),
        quader::ViewportToolResult{}, before_tool);

    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(false, true, false, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
    const std::vector<quader::EntityId> selected = selected_entities(harness);
    EXPECT_NE(std::find(selected.begin(), selected.end(), left.entity), selected.end());
    EXPECT_EQ(std::find(selected.begin(), selected.end(), right.entity), selected.end());
}

TEST(ViewportInteractionControllerTests, FastDragFrameRetainsCommittedBoxMarqueeForRender) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{300.0f, 200.0f};
    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(ViewportInteractionControllerTests, FastDragStartsFromAcceptedPressWhenCurrentMouseIsOutsideViewport) {
    ViewportInteractionHarness harness;
    (void)create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Object)
                    .ok);
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Object);

    const quader::QVec2 start{500.0f, 400.0f};
    const quader::QVec2 end{-20.0f, -20.0f};
    harness.input.viewport_hovered = false;
    harness.input.mouse_local = end;
    harness.input.mouse_screen = end;

    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(true, false, true, start),
        quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});

    const quader::ViewportSelectionMarquee marquee =
        harness.controller.selection_marquee();
    ASSERT_TRUE(marquee.active);
    EXPECT_EQ(marquee.start.x, start.x);
    EXPECT_EQ(marquee.start.y, start.y);
    EXPECT_EQ(marquee.end.x, end.x);
    EXPECT_EQ(marquee.end.y, end.y);
    EXPECT_EQ(marquee.rule, quader::ModelingBoxSelectionRule::Crossing);
    EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);
}

TEST(ViewportInteractionControllerTests, TransformGizmoFrameSurvivesNavigationFrames) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);

    harness.input_result.navigation_active = true;
    harness.input_result.camera_changed = true;
    const quader::ViewportBeforeToolInteractionResult navigation =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());

    EXPECT_FALSE(navigation.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    EXPECT_TRUE(frame->info.ok);
}

TEST(ViewportInteractionControllerTests,
     CameraOnlyFramesReuseSelectionPivotWithoutBoundsRecomputation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              1u);

    harness.input_result.navigation_active = true;
    harness.input_result.camera_changed = true;
    for (const quader::QVec3 direction : {
             quader::QVec3{-0.8f, -0.2f, -0.5f},
             quader::QVec3{-0.5f, -0.6f, -0.4f},
             quader::QVec3{-0.2f, -0.4f, -0.9f},
             quader::QVec3{-0.7f, -0.5f, -0.1f},
         }) {
      harness.camera.set_view_direction(direction);
      (void)harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
    }

    const quader::TransformGizmoSelectionCacheStats &camera_stats =
        harness.controller.transform_gizmo_selection_cache_stats();
    EXPECT_EQ(camera_stats.selection_pivot_recompute_count, 1u);
    EXPECT_EQ(camera_stats.selection_cache_hit_count, 4u);
    ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);

    harness.input_result = {};
    ASSERT_TRUE(harness.editor.selection().clear().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              1u);
    EXPECT_EQ(harness.controller.transform_gizmo_frame(), nullptr);

    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              2u);

    harness.session.pivot().begin_quick({3.0f, 4.0f, 5.0f});
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              3u);
    const quader_editor_core::TransformGizmoFrame *quick_pivot_frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(quick_pivot_frame, nullptr);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.x, 3.0f);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.y, 4.0f);
    EXPECT_FLOAT_EQ(quick_pivot_frame->info.pivot.z, 5.0f);

    harness.session.pivot().end_quick_edit();
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    EXPECT_EQ(harness.controller.transform_gizmo_selection_cache_stats()
                  .selection_pivot_recompute_count,
              3u);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotRejectsUnstableMeshBeforeActivation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    const quader::AssetId mesh =
        harness.session.scene().mesh(box.entity)->mesh;
    ASSERT_TRUE(harness.session.assets().update_mesh(
        mesh, [](quader::MeshAsset &asset) {
          asset.has_stable_cpu_topology = false;
        }));

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
}

TEST(ViewportInteractionControllerTests,
     FailedPivotActivationPreservesMultiSelectionAndActiveObject) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef first = create_unit_box(harness);
    const quader::QuaderModelingObjectRef invalid = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection()
                    .select_objects({first.entity, invalid.entity},
                                    invalid.entity)
                    .ok);
    const quader::AssetId mesh =
        harness.session.scene().mesh(invalid.entity)->mesh;
    ASSERT_TRUE(harness.session.assets().update_mesh(
        mesh, [](quader::MeshAsset &asset) {
          for (quader::MeshVertex &vertex : asset.vertices) {
            vertex.position.x = std::numeric_limits<float>::max();
          }
        }));
    const std::vector<quader::EntityId> selected_before =
        harness.session.scene().selected_entities();
    const quader::EntityId active_before =
        harness.session.scene().primary_selection();

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
    EXPECT_EQ(harness.session.scene().selected_entities(), selected_before);
    EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotRejectsSingularWorldTransformBeforeActivation) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    quader::TransformComponent singular =
        *harness.session.scene().transform(box.entity);
    singular.scale = {1.0f, 0.0f, 1.0f};
    harness.session.scene().set_transform(box.entity, singular);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);

    EXPECT_FALSE(
        harness.editor.tools().can_activate(quader::ViewportToolId::Pivot));
    EXPECT_FALSE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    EXPECT_FALSE(harness.session.pivot().persistent_active());
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotNeverPublishesANonFinitePreview) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    harness.session.history().clear();
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const std::optional<quader::QVec3> before =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(before.has_value());

    harness.session.pivot().set_persistent_preview_position(
        {std::numeric_limits<float>::quiet_NaN(), 2.0f, 3.0f});
    const std::optional<quader::QVec3> after =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(after.has_value());
    EXPECT_NEAR(after->x, before->x, 0.001f);
    EXPECT_NEAR(after->y, before->y, 0.001f);
    EXPECT_NEAR(after->z, before->z, 0.001f);
    const quader::PivotHandleState handle =
        harness.session.pivot().handle_state();
    EXPECT_TRUE(std::isfinite(handle.world_position.x));
    EXPECT_TRUE(std::isfinite(handle.world_position.y));
    EXPECT_TRUE(std::isfinite(handle.world_position.z));

    ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok);
    EXPECT_FALSE(harness.editor.history().can_undo());
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
}

TEST(ViewportInteractionControllerTests,
     PivotActivationSurvivesTheQtPostActivationInteractionReset) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 drag_position =
        press_position + quader::QVec2{72.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = press_position;
    harness.input.mouse_screen = press_position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(true, true, false, press_position))
                    .gizmo_consumed);
    harness.input.mouse_local = drag_position;
    harness.input.mouse_screen = drag_position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, true, false, press_position))
                    .gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    EXPECT_GT(quader::length(
                  harness.session.scene().transform(box.entity)->position -
                  before.position),
              0.001f);

    const quader::QuaderApiVoidResult activated =
        harness.editor.tools().activate(quader::ViewportToolId::Pivot);
    ASSERT_TRUE(activated.ok) << activated.message;
    ASSERT_TRUE(harness.session.pivot().persistent_active());
    EXPECT_TRUE(harness.controller.dragging());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
    const std::uint64_t scene_revision_after_activation =
        harness.session.scene().revision();
    const std::uint64_t asset_revision_after_activation =
        harness.session.assets().revision();

    // Qt clears its local interaction state immediately after dispatching the
    // native activation. The resulting second modeling cancel is a no-op and
    // must not invalidate the newly captured Pivot session.
    harness.controller.reset(&harness.editor);

    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_TRUE(harness.session.pivot().persistent_active());
    EXPECT_EQ(harness.editor.tools().active_tool(), "pivot");
    EXPECT_TRUE(harness.session.pivot().handle_state().visible);
    EXPECT_EQ(harness.session.scene().revision(),
              scene_revision_after_activation);
    EXPECT_EQ(harness.session.assets().revision(),
              asset_revision_after_activation);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotStartsAtMovePivotForFreshOffOriginMesh) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    harness.session.history().clear();

    const quader::TransformComponent transform_before =
        *harness.session.scene().transform(box.entity);
    const quader::AssetId mesh_before =
        harness.session.scene().mesh(box.entity)->mesh;
    const std::uint64_t scene_revision_before =
        harness.session.scene().revision();
    const std::uint64_t asset_revision_before =
        harness.session.assets().revision();
    const std::optional<quader::SceneBounds> bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_before.has_value());
    EXPECT_FALSE(transform_before.custom_pivot);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *move =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(move, nullptr);
    const quader::QVec3 move_pivot = move->info.pivot;
    EXPECT_NEAR(move_pivot.x, 4.0f, 0.001f);
    EXPECT_NEAR(move_pivot.y, -2.0f, 0.001f);
    EXPECT_NEAR(move_pivot.z, 3.0f, 0.001f);
    EXPECT_GT(quader::length(move_pivot), 0.1f);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const quader::PivotHandleState pivot =
        harness.session.pivot().handle_state();
    ASSERT_TRUE(pivot.visible);
    ASSERT_TRUE(pivot.editing);
    ASSERT_TRUE(pivot.persistent);
    EXPECT_EQ(pivot.target, box.entity);
    EXPECT_NEAR(pivot.world_position.x, move_pivot.x, 0.001f);
    EXPECT_NEAR(pivot.world_position.y, move_pivot.y, 0.001f);
    EXPECT_NEAR(pivot.world_position.z, move_pivot.z, 0.001f);
    const std::optional<quader::QVec3> preview =
        harness.session.pivot().persistent_preview_position();
    ASSERT_TRUE(preview.has_value());
    EXPECT_NEAR(preview->x, move_pivot.x, 0.001f);
    EXPECT_NEAR(preview->y, move_pivot.y, 0.001f);
    EXPECT_NEAR(preview->z, move_pivot.z, 0.001f);

    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_FALSE(harness.session.pivot().persistent_active());
    EXPECT_FALSE(harness.session.pivot().handle_state().visible);
    EXPECT_FALSE(harness.editor.tools().active_settings().active);
    EXPECT_FALSE(harness.editor.history().can_undo());
    ASSERT_NE(harness.session.scene().transform(box.entity), nullptr);
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          transform_before);
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
    ASSERT_NE(harness.session.scene().mesh(box.entity), nullptr);
    EXPECT_EQ(harness.session.scene().mesh(box.entity)->mesh, mesh_before);
    EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
    EXPECT_EQ(harness.session.assets().revision(), asset_revision_before);
    const std::optional<quader::SceneBounds> bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_after.has_value());
    EXPECT_NEAR(bounds_after->min.x, bounds_before->min.x, 0.001f);
    EXPECT_NEAR(bounds_after->min.y, bounds_before->min.y, 0.001f);
    EXPECT_NEAR(bounds_after->min.z, bounds_before->min.z, 0.001f);
    EXPECT_NEAR(bounds_after->max.x, bounds_before->max.x, 0.001f);
    EXPECT_NEAR(bounds_after->max.y, bounds_before->max.y, 0.001f);
    EXPECT_NEAR(bounds_after->max.z, bounds_before->max.z, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotBakesAndClearsTheVisibleQuickPivotOverride) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box =
        create_box_at(harness, {4.0f, -2.0f, 3.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    const std::optional<quader::SceneBounds> bounds_before =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_before.has_value());
    harness.session.history().clear();

    const quader::QVec3 quick_position{5.0f, -1.0f, 4.0f};
    harness.session.pivot().begin_quick(quick_position);
    harness.session.pivot().end_quick_edit();
    const std::optional<quader::QVec3> quick_query =
        harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(quick_query.has_value());
    EXPECT_NEAR(quick_query->x, quick_position.x, 0.001f);
    EXPECT_NEAR(quick_query->y, quick_position.y, 0.001f);
    EXPECT_NEAR(quick_query->z, quick_position.z, 0.001f);

    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    const quader::PivotHandleState handle =
        harness.session.pivot().handle_state();
    ASSERT_TRUE(handle.visible);
    EXPECT_NEAR(handle.world_position.x, quick_position.x, 0.001f);
    EXPECT_NEAR(handle.world_position.y, quick_position.y, 0.001f);
    EXPECT_NEAR(handle.world_position.z, quick_position.z, 0.001f);

    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(applied.changed);
    EXPECT_FALSE(harness.session.pivot().selection_pivot_override().valid);
    ASSERT_TRUE(harness.session.scene().transform(box.entity)->custom_pivot);
    std::optional<quader::QVec3> committed =
        harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(committed.has_value());
    EXPECT_NEAR(committed->x, quick_position.x, 0.001f);
    EXPECT_NEAR(committed->y, quick_position.y, 0.001f);
    EXPECT_NEAR(committed->z, quick_position.z, 0.001f);
    const std::optional<quader::SceneBounds> bounds_after =
        quader::entity_world_bounds(harness.session.scene(),
                                    harness.session.assets(), box.entity);
    ASSERT_TRUE(bounds_after.has_value());
    EXPECT_NEAR(bounds_after->min.x, bounds_before->min.x, 0.001f);
    EXPECT_NEAR(bounds_after->min.y, bounds_before->min.y, 0.001f);
    EXPECT_NEAR(bounds_after->min.z, bounds_before->min.z, 0.001f);
    EXPECT_NEAR(bounds_after->max.x, bounds_before->max.x, 0.001f);
    EXPECT_NEAR(bounds_after->max.y, bounds_before->max.y, 0.001f);
    EXPECT_NEAR(bounds_after->max.z, bounds_before->max.z, 0.001f);

    ASSERT_TRUE(harness.editor.history().undo().ok);
    EXPECT_FALSE(harness.session.scene().transform(box.entity)->custom_pivot);
    committed = harness.editor.query().selection_transform_pivot();
    ASSERT_TRUE(committed.has_value());
    EXPECT_NEAR(committed->x, 4.0f, 0.001f);
    EXPECT_NEAR(committed->y, -2.0f, 0.001f);
    EXPECT_NEAR(committed->z, 3.0f, 0.001f);
}

TEST(ViewportInteractionControllerTests,
     PersistentPivotApplyRefreshesTransformGizmoAndUndoRedo) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Pivot).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Pivot);
    harness.session.pivot().set_persistent_preview_position(
        {0.0f, 1.0f, 0.0f});
    const quader::QuaderApiVoidResult applied =
        harness.editor.tools().commit_active_tool_preview();
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(applied.changed);

    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 1.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.history().undo().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);

    ASSERT_TRUE(harness.editor.history().redo().ok);
    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.x, 0.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.y, 1.0f);
    EXPECT_FLOAT_EQ(gizmo->info.pivot.z, 0.0f);
}

TEST(ViewportInteractionControllerTests, HoverSurvivesNavigationSuppressionAndClearsWhenPointerLeaves) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);

    ASSERT_TRUE(harness.editor.selection()
                    .set_mode(quader::QuaderSelectionMode::Face)
                    .ok);
    harness.router.set_selection_mode(quader::ViewportSelectionMode::Face);
    const quader::QuaderApiResult<quader::QuaderHoverSummary> hover =
        harness.editor.selection().hover_face(box, 0);
    ASSERT_TRUE(hover.ok) << hover.message;
    ASSERT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.navigation_active = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.navigation_active = false;
    harness.input_result.camera_changed = true;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::Face);

    harness.input_result.camera_changed = false;
    harness.input.viewport_hovered = false;
    harness.controller.handle_after_active_tool(
        harness.editor, harness.frame(), quader::ViewportToolResult{},
        quader::ViewportBeforeToolInteractionResult{});
    EXPECT_EQ(harness.editor.selection().hover_summary().target.kind,
              quader::QuaderActiveSelectionKind::None);
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedObjectThroughViewportFrames) {
    /// Names one transform tool case covered by the shared gizmo drag assertions.
    struct ToolCase {
        quader::ViewportToolId tool = quader::ViewportToolId::Move;
        const char* label = "";
    };

    for (const ToolCase tool_case : {
             ToolCase{quader::ViewportToolId::Move, "move"},
             ToolCase{quader::ViewportToolId::Rotate, "rotate"},
             ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
      SCOPED_TRACE(tool_case.label);
      ViewportInteractionHarness harness;
      const quader::QuaderModelingObjectRef box = create_unit_box(harness);
      ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
      ASSERT_TRUE(harness.editor.tools().activate(tool_case.tool).ok);
      harness.router.set_active_tool(tool_case.tool);

      const quader::ViewportBeforeToolInteractionResult warmup =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
      EXPECT_FALSE(warmup.gizmo_consumed);
      const quader_editor_core::TransformGizmoFrame *frame =
          harness.controller.transform_gizmo_frame();
      ASSERT_NE(frame, nullptr);

      quader::ViewportOverlayFrame overlay;
      quader::ViewportTransformGizmoOverlayBuilder::append(
          overlay, *frame,
          quader::ViewportCameraAdapter::overlay_camera(
              harness.camera, harness.input.viewport_size));
      EXPECT_FALSE(overlay.lines.empty());

      quader::QVec2 start{};
      quader::QVec2 update{};
      if (tool_case.tool == quader::ViewportToolId::Rotate) {
        const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
            find_ring(*frame, quader_editor_core::TransformAxis::Z);
        ASSERT_NE(ring, nullptr);
        start = ring->screen_start;
        update = rotated_point(frame->info.screen_pivot, start, 0.6f);
      } else if (tool_case.tool == quader::ViewportToolId::Scale) {
        start = frame->info.screen_pivot;
        update = start + quader::QVec2{120.0f, 0.0f};
      } else {
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, quader_editor_core::TransformAxis::X);
        ASSERT_NE(axis, nullptr);
        start = axis->screen_tip;
        update = start + quader::QVec2{120.0f, 0.0f};
      }

      const quader::TransformComponent before_transform =
          *harness.session.scene().transform(box.entity);
      press_update_release_gizmo(harness, start, update);
      const quader::TransformComponent after_transform =
          *harness.session.scene().transform(box.entity);

      if (tool_case.tool == quader::ViewportToolId::Move) {
        EXPECT_GT(length(after_transform.position - before_transform.position),
                  0.001f);
      } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
        EXPECT_GT(length(after_transform.rotation_euler -
                         before_transform.rotation_euler),
                  0.001f);
      } else {
        EXPECT_GT(length(after_transform.scale - before_transform.scale),
                  0.001f);
      }
      EXPECT_EQ(harness.editor.selection().summary().selected_object_count, 1u);

      ASSERT_TRUE(harness.editor.history().can_undo());
      const quader::QuaderApiVoidResult undo =
          harness.editor.commands().execute(
              quader::quader_command::kEditUndo);
      ASSERT_TRUE(undo.ok()) << undo.error_message();
      EXPECT_TRUE(undo.changed());
      ASSERT_NE(harness.session.scene().transform(box.entity), nullptr);
      expect_transform_near(*harness.session.scene().transform(box.entity),
                            before_transform);
    }
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoCommitsPressMoveReleaseCollapsedIntoOneFrame) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 release_position =
        press_position + quader::QVec2{120.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = release_position;
    harness.input.mouse_screen = release_position;
    const quader::ViewportBeforeToolInteractionResult collapsed =
        harness.controller.handle_before_active_tool(
            harness.editor,
            harness.frame(true, false, true, press_position));

    EXPECT_TRUE(collapsed.gizmo_consumed);
    EXPECT_FALSE(collapsed.selection_click_requested);
    EXPECT_FALSE(harness.controller.dragging());
    const quader::TransformComponent after =
        *harness.session.scene().transform(box.entity);
    EXPECT_GT(length(after.position - before.position), 0.001f);
    ASSERT_TRUE(harness.editor.history().can_undo());
    const quader::QuaderApiVoidResult undo = harness.editor.commands().execute(
        quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     MoveGizmoAppliesFinalReleasePositionBeforeCommit) {
    ViewportInteractionHarness harness;
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        quader::ViewportBoolSettingId::VertexSnap, false));
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    ASSERT_TRUE(harness.editor.selection().select_object(box).ok);
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Move);

    (void)harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const quader_editor_core::TransformGizmoAxisPrimitive *axis =
        find_axis(*gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const quader::QVec2 press_position = axis->screen_tip;
    const quader::QVec2 drag_position =
        press_position + quader::QVec2{48.0f, 0.0f};
    const quader::QVec2 release_position =
        press_position + quader::QVec2{140.0f, 0.0f};
    const quader::TransformComponent before =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = press_position;
    harness.input.mouse_screen = press_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(true, true, false, press_position))
                    .gizmo_consumed);
    harness.input.mouse_local = drag_position;
    harness.input.mouse_screen = drag_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, true, false, press_position))
                    .gizmo_consumed);
    const quader::TransformComponent during_drag =
        *harness.session.scene().transform(box.entity);

    harness.input.mouse_local = release_position;
    harness.input.mouse_screen = release_position;
    EXPECT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.frame(false, false, true, press_position))
                    .gizmo_consumed);

    EXPECT_FALSE(harness.controller.dragging());
    const quader::TransformComponent after =
        *harness.session.scene().transform(box.entity);
    EXPECT_GT(length(during_drag.position - before.position), 0.001f);
    EXPECT_GT(length(after.position - during_drag.position), 0.001f);
    ASSERT_TRUE(harness.editor.history().can_undo());
    const quader::QuaderApiVoidResult undo = harness.editor.commands().execute(
        quader::quader_command::kEditUndo);
    ASSERT_TRUE(undo.ok()) << undo.error_message();
    EXPECT_TRUE(undo.changed());
    expect_transform_near(*harness.session.scene().transform(box.entity),
                          before);
}

TEST(ViewportInteractionControllerTests,
     ShiftRotateGizmoDragExtrudesSelectedFace) {
    ViewportInteractionHarness harness;
    const quader::QuaderModelingObjectRef box = create_unit_box(harness);
    select_single_component(harness, box, quader::QuaderSelectionMode::Face);
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Rotate).ok);
    harness.router.set_active_tool(quader::ViewportToolId::Rotate);
    harness.input.modifiers.shift = true;

    const std::optional<quader::QuaderMeshSnapshot> before_mesh =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(before_mesh.has_value());

    const quader::ViewportBeforeToolInteractionResult warmup =
        harness.controller.handle_before_active_tool(harness.editor,
                                                     harness.frame());
    ASSERT_FALSE(warmup.gizmo_consumed);
    const quader_editor_core::TransformGizmoFrame *frame =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(frame, nullptr);
    const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
        find_ring(*frame, quader_editor_core::TransformAxis::Z);
    ASSERT_NE(ring, nullptr);

    const quader::QVec2 start = ring->screen_start;
    const quader::QVec2 update =
        rotated_point(frame->info.screen_pivot, start, 0.6f);
    press_update_release_gizmo(harness, start, update);

    const std::optional<quader::QuaderMeshSnapshot> after_mesh =
        harness.editor.query().mesh(box.mesh);
    ASSERT_TRUE(after_mesh.has_value());
    EXPECT_GT(after_mesh->vertex_count, before_mesh->vertex_count);
    EXPECT_GT(after_mesh->index_count, before_mesh->index_count);

    const quader::QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_EQ(selection.mode, quader::QuaderSelectionMode::Face);
    EXPECT_EQ(selection.selected_face_count, 1u);
    EXPECT_EQ(selection.selected_object_count, 1u);
}

TEST(ViewportInteractionControllerTests, MoveGizmoComponentAxisDragKeepsPickedAxisUnderMouse) {
    /**
     * Stores the component selection mode exercised by a gizmo axis drag case.
     */
    struct ComponentModeCase {
        quader::QuaderSelectionMode mode = quader::QuaderSelectionMode::Vertex;
        const char* label = "";
    };
    /**
     * Stores the transform axis exercised by a gizmo drag case.
     */
    struct AxisCase {
      quader_editor_core::TransformAxis axis =
          quader_editor_core::TransformAxis::X;
      const char *label = "";
    };

    for (const ComponentModeCase mode_case : {
             ComponentModeCase{quader::QuaderSelectionMode::Vertex, "vertex"},
             ComponentModeCase{quader::QuaderSelectionMode::Edge, "edge"},
             ComponentModeCase{quader::QuaderSelectionMode::Face, "face"},
         }) {
      for (const AxisCase axis_case : {
               AxisCase{quader_editor_core::TransformAxis::X, "x"},
               AxisCase{quader_editor_core::TransformAxis::Z, "z"},
           }) {
        SCOPED_TRACE(mode_case.label);
        SCOPED_TRACE(axis_case.label);
        ViewportInteractionHarness harness;
        harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
        ASSERT_TRUE(harness.settings.set_bool_value(
            quader::ViewportBoolSettingId::GridSnap, false));
        ASSERT_TRUE(harness.settings.set_bool_value(
            quader::ViewportBoolSettingId::VertexSnap, false));

        const quader::QuaderModelingObjectRef box = create_unit_box(harness);
        select_single_component(harness, box, mode_case.mode);
        ASSERT_TRUE(
            harness.editor.tools().activate(quader::ViewportToolId::Move).ok);
        harness.router.set_active_tool(quader::ViewportToolId::Move);

        const std::optional<quader::QVec3> before_center =
            harness.editor.query().selection_center();
        ASSERT_TRUE(before_center.has_value());

        const quader::ViewportBeforeToolInteractionResult warmup =
            harness.controller.handle_before_active_tool(harness.editor,
                                                         harness.frame());
        ASSERT_FALSE(warmup.gizmo_consumed);
        const quader_editor_core::TransformGizmoFrame *frame =
            harness.controller.transform_gizmo_frame();
        ASSERT_NE(frame, nullptr);
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, axis_case.axis);
        ASSERT_NE(axis, nullptr);

        const quader::QVec2 axis_delta = axis->screen_tip - axis->screen_start;
        const float axis_length = std::sqrt(axis_delta.x * axis_delta.x +
                                            axis_delta.y * axis_delta.y);
        ASSERT_GT(axis_length, 1.0f);
        const quader::QVec2 axis_direction = axis_delta * (1.0f / axis_length);
        const quader::QVec2 start = axis_screen_point(*axis, 0.25f);
        const quader::QVec2 update = start + axis_direction * 120.0f;

        harness.input.mouse_local = start;
        harness.input.mouse_screen = start;
        const quader::ViewportBeforeToolInteractionResult press =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(true, true, false, start));
        ASSERT_TRUE(press.gizmo_consumed);

        harness.input.mouse_local = update;
        harness.input.mouse_screen = update;
        const quader::ViewportBeforeToolInteractionResult drag =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, true, false, start));
        ASSERT_TRUE(drag.gizmo_consumed);

        const std::uint64_t scene_revision_after_drag =
            harness.session.scene().revision();
        const std::uint64_t asset_revision_after_drag =
            harness.session.assets().revision();

        const quader::ViewportBeforeToolInteractionResult refresh =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, true, false, start));
        ASSERT_TRUE(refresh.gizmo_consumed);
        EXPECT_EQ(harness.session.scene().revision(), scene_revision_after_drag);
        EXPECT_EQ(harness.session.assets().revision(), asset_revision_after_drag);
        frame = harness.controller.transform_gizmo_frame();
        ASSERT_NE(frame, nullptr);
        axis = find_axis(*frame, axis_case.axis);
        ASSERT_NE(axis, nullptr);
        const quader::QVec2 refreshed_grab_point =
            axis_screen_point(*axis, 0.25f);
        EXPECT_NEAR(refreshed_grab_point.x, update.x, 12.0f);
        EXPECT_NEAR(refreshed_grab_point.y, update.y, 12.0f);

        const quader::ViewportBeforeToolInteractionResult release =
            harness.controller.handle_before_active_tool(
                harness.editor, harness.frame(false, false, true, start));
        ASSERT_TRUE(release.gizmo_consumed);

        const std::optional<quader::QVec3> after_center =
            harness.editor.query().selection_center();
        ASSERT_TRUE(after_center.has_value());
        const quader::QVec3 delta = *after_center - *before_center;
        EXPECT_NEAR(delta.y, 0.0f, 0.01f);
        if (axis_case.axis == quader_editor_core::TransformAxis::X) {
          EXPECT_GT(std::abs(delta.x), 0.01f);
          EXPECT_NEAR(delta.z, 0.0f, 0.01f);
        } else {
          EXPECT_NEAR(delta.x, 0.0f, 0.01f);
          EXPECT_GT(std::abs(delta.z), 0.01f);
        }
      }
    }
}

TEST(ViewportInteractionControllerTests, MoveRotateAndScaleGizmosDragSelectedPointLightThroughViewportFrames) {
    /**
     * Captures the viewport tool under test and the trace label for per-tool gizmo assertions.
     */
    struct ToolCase {
        quader::ViewportToolId tool = quader::ViewportToolId::Move;
        const char* label = "";
    };

    for (const ToolCase tool_case : {
             ToolCase{quader::ViewportToolId::Move, "move"},
             ToolCase{quader::ViewportToolId::Rotate, "rotate"},
             ToolCase{quader::ViewportToolId::Scale, "scale"},
         }) {
      SCOPED_TRACE(tool_case.label);
      ViewportInteractionHarness harness;
      quader::QuaderCreatePointLightArgs args;
      args.name = "Viewport Point Light";
      args.select_created = true;
      const quader::QuaderApiResult<quader::QuaderEntityRef> light =
          harness.editor.scene().create_point_light(args);
      ASSERT_TRUE(light.ok()) << light.error_message();
      ASSERT_NE(harness.session.scene().light(light.value().id), nullptr);

      ASSERT_TRUE(harness.editor.tools().activate(tool_case.tool).ok);
      harness.router.set_active_tool(tool_case.tool);

      const quader::ViewportBeforeToolInteractionResult warmup =
          harness.controller.handle_before_active_tool(harness.editor,
                                                       harness.frame());
      EXPECT_FALSE(warmup.gizmo_consumed);
      const quader_editor_core::TransformGizmoFrame *frame =
          harness.controller.transform_gizmo_frame();
      ASSERT_NE(frame, nullptr);

      quader::QVec2 start{};
      quader::QVec2 update{};
      if (tool_case.tool == quader::ViewportToolId::Rotate) {
        const quader_editor_core::TransformGizmoRingSegmentPrimitive *ring =
            find_ring(*frame, quader_editor_core::TransformAxis::Z);
        ASSERT_NE(ring, nullptr);
        start = ring->screen_start;
        update = rotated_point(frame->info.screen_pivot, start, 0.6f);
      } else if (tool_case.tool == quader::ViewportToolId::Scale) {
        start = frame->info.screen_pivot;
        update = start + quader::QVec2{120.0f, 0.0f};
      } else {
        const quader_editor_core::TransformGizmoAxisPrimitive *axis =
            find_axis(*frame, quader_editor_core::TransformAxis::X);
        ASSERT_NE(axis, nullptr);
        start = axis->screen_tip;
        update = start + quader::QVec2{120.0f, 0.0f};
      }

      const quader::TransformComponent before_transform =
          *harness.session.scene().transform(light.value().id);
      press_update_release_gizmo(harness, start, update);
      const quader::TransformComponent after_transform =
          *harness.session.scene().transform(light.value().id);

      if (tool_case.tool == quader::ViewportToolId::Move) {
        EXPECT_GT(length(after_transform.position - before_transform.position),
                  0.001f);
      } else if (tool_case.tool == quader::ViewportToolId::Rotate) {
        EXPECT_GT(length(after_transform.rotation_euler -
                         before_transform.rotation_euler),
                  0.001f);
      } else {
        EXPECT_GT(length(after_transform.scale - before_transform.scale),
                  0.001f);
      }
      EXPECT_EQ(harness.editor.selection().summary().active_kind,
                quader::QuaderActiveSelectionKind::Light);

      ASSERT_TRUE(harness.editor.history().can_undo());
      const quader::QuaderApiVoidResult undo =
          harness.editor.commands().execute(
              quader::quader_command::kEditUndo);
      ASSERT_TRUE(undo.ok()) << undo.error_message();
      EXPECT_TRUE(undo.changed());
      ASSERT_NE(harness.session.scene().transform(light.value().id), nullptr);
      expect_transform_near(
          *harness.session.scene().transform(light.value().id),
          before_transform);
    }
}

TEST(ViewportBoxOverlayBuilderTests, EmitsDashedFootprintOutlineWithoutFill) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 10.0f, 0.0f};
    camera.forward = {0.0f, -1.0f, 0.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 0.0f, 1.0f};
    camera.viewport_size = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographic_size = 8.0f;

    quader::ModelingBoxPreview footprint;
    footprint.ok = true;
    footprint.valid = true;
    footprint.drag_state = quader::ModelingBoxDragState::Idle;
    footprint.footprint_count = 4u;
    footprint.footprint = {{
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 1.0f},
    }};

    quader::ViewportOverlayFrame footprint_overlay;
    quader::ViewportBoxOverlayBuilder::append(footprint_overlay, camera,
                                              footprint);
    ASSERT_GT(footprint_overlay.lines.size(), 4u);
    EXPECT_TRUE(footprint_overlay.triangles.empty());
    for (const quader::ViewportOverlayLine &line : footprint_overlay.lines) {
      EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
      EXPECT_EQ(line.color, quader::kOverlayToolBoxPreviewLineColor);
      EXPECT_FLOAT_EQ(line.width_pixels, 2.0f);
      EXPECT_EQ(line.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
    }

    quader::ModelingBoxPreview box = footprint;
    box.drag_state = quader::ModelingBoxDragState::Height;
    box.corner_count = 8u;
    box.corners = {{
            {-1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, -1.0f},
            {1.0f, 0.0f, 1.0f},
            {-1.0f, 0.0f, 1.0f},
            {-1.0f, 2.0f, -1.0f},
            {1.0f, 2.0f, -1.0f},
            {1.0f, 2.0f, 1.0f},
            {-1.0f, 2.0f, 1.0f},
    }};

    quader::ViewportOverlayFrame box_overlay;
    quader::ViewportBoxOverlayBuilder::append(box_overlay, camera, box);
    EXPECT_EQ(box_overlay.lines.size(), footprint_overlay.lines.size());
    EXPECT_TRUE(box_overlay.triangles.empty());
}

TEST(ViewportSelectionMarqueeOverlayBuilderTests, EmitsFillAndRuleSpecificEdges) {
    quader::ViewportOverlayCamera camera;
    camera.eye = {0.0f, 0.0f, 10.0f};
    camera.forward = {0.0f, 0.0f, -1.0f};
    camera.right = {1.0f, 0.0f, 0.0f};
    camera.up = {0.0f, 1.0f, 0.0f};
    camera.viewport_size = {800.0f, 600.0f};
    camera.projection = quader::ViewportOverlayProjection::Orthographic;
    camera.orthographic_size = 12.0f;

    quader::ViewportOverlayFrame inactive_overlay;
    quader::ViewportSelectionMarquee inactive;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(inactive_overlay,
                                                           camera, inactive);
    EXPECT_TRUE(inactive_overlay.lines.empty());
    EXPECT_TRUE(inactive_overlay.triangles.empty());

    quader::ViewportSelectionMarquee contain;
    contain.active = true;
    contain.start = {100.0f, 120.0f};
    contain.end = {180.0f, 160.0f};
    contain.rule = quader::ModelingBoxSelectionRule::Contain;

    quader::ViewportOverlayFrame contain_overlay;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(contain_overlay,
                                                           camera, contain);
    ASSERT_EQ(contain_overlay.triangles.size(), 2u);
    ASSERT_EQ(contain_overlay.lines.size(), 4u);
    for (const quader::ViewportOverlayTriangle &triangle :
         contain_overlay.triangles) {
      EXPECT_EQ(triangle.layer, quader::ViewportOverlayLayer::ToolPreviewFill);
      EXPECT_EQ(triangle.color, quader::kOverlaySelectionMarqueeFillColor);
      EXPECT_EQ(triangle.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      EXPECT_EQ(triangle.source, quader::ViewportOverlaySource::Tool);
    }
    for (const quader::ViewportOverlayLine &line : contain_overlay.lines) {
      EXPECT_EQ(line.layer, quader::ViewportOverlayLayer::ToolPreviewLine);
      EXPECT_EQ(line.color, quader::kOverlaySelectionMarqueeLineColor);
      EXPECT_FLOAT_EQ(line.width_pixels, 1.5f);
      EXPECT_EQ(line.style_role,
                quader::ViewportOverlayStyleRole::ExplicitColor);
      EXPECT_EQ(line.source, quader::ViewportOverlaySource::Tool);
    }

    quader::ViewportSelectionMarquee crossing = contain;
    crossing.rule = quader::ModelingBoxSelectionRule::Crossing;
    quader::ViewportOverlayFrame crossing_overlay;
    quader::ViewportSelectionMarqueeOverlayBuilder::append(crossing_overlay,
                                                           camera, crossing);
    EXPECT_EQ(crossing_overlay.triangles.size(), 2u);
    EXPECT_GT(crossing_overlay.lines.size(), contain_overlay.lines.size());
}

TEST(ViewportHostInteractionPolicyTests,
     FocusFollowsHoverWithoutStealingNonViewportKeyboardFocus) {
  using quader::ViewportPaneFocusRequest;
  using quader::ViewportPaneFocusTrigger;
  using quader::viewport_pane_focus_decision;

  const auto passive_hover = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::Hover,
  });
  EXPECT_TRUE(passive_hover.focus_workspace_pane);
  EXPECT_FALSE(passive_hover.focus_native_surface);

  const auto viewport_hover = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::Hover,
      .viewport_keyboard_focus = true,
  });
  EXPECT_TRUE(viewport_hover.focus_workspace_pane);
  EXPECT_TRUE(viewport_hover.focus_native_surface);

  const auto native_focus_in = viewport_pane_focus_decision({
      .trigger = ViewportPaneFocusTrigger::FocusIn,
  });
  EXPECT_TRUE(native_focus_in.focus_workspace_pane);
  EXPECT_FALSE(native_focus_in.focus_native_surface);

  for (const ViewportPaneFocusTrigger trigger : {
           ViewportPaneFocusTrigger::PointerPress,
           ViewportPaneFocusTrigger::Wheel,
           ViewportPaneFocusTrigger::Programmatic,
       }) {
    const auto decision = viewport_pane_focus_decision({.trigger = trigger});
    EXPECT_TRUE(decision.focus_workspace_pane);
    EXPECT_TRUE(decision.focus_native_surface);
  }

  const auto rejected_other_pane =
      viewport_pane_focus_decision(ViewportPaneFocusRequest{
          .trigger = ViewportPaneFocusTrigger::PointerPress,
          .interaction_locked = true,
          .requested_pane_is_interaction_owner = false,
      });
  EXPECT_FALSE(rejected_other_pane.focus_workspace_pane);
  EXPECT_FALSE(rejected_other_pane.focus_native_surface);

  const auto retained_owner =
      viewport_pane_focus_decision(ViewportPaneFocusRequest{
          .trigger = ViewportPaneFocusTrigger::Wheel,
          .interaction_locked = true,
          .requested_pane_is_interaction_owner = true,
      });
  EXPECT_TRUE(retained_owner.focus_workspace_pane);
  EXPECT_TRUE(retained_owner.focus_native_surface);
}

TEST(ViewportHostInteractionPolicyTests,
     OutsidePointerCancelsModalToolsWithLegacyExceptions) {
  using quader::ViewportExternalPointerCancelRequest;
  using quader::should_cancel_modal_tool_for_external_pointer;

  ViewportExternalPointerCancelRequest request{
      .external_pointer_event = true,
      .modal_tool_owns_viewport = true,
  };
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));

  request.external_pointer_event = false;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.external_pointer_event = true;
  request.modal_tool_owns_viewport = false;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.modal_tool_owns_viewport = true;
  request.linear_array_active = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.linear_array_active = false;
  request.navigation_active = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));

  request.navigation_active = false;
  request.left_button_release_only = true;
  request.insert_edge_loop_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.insert_edge_loop_dragging = false;
  request.cut_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.cut_dragging = false;
  request.mirror_dragging = true;
  EXPECT_FALSE(should_cancel_modal_tool_for_external_pointer(request));
  request.mirror_dragging = false;
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));

  request.left_button_release_only = false;
  request.insert_edge_loop_dragging = true;
  EXPECT_TRUE(should_cancel_modal_tool_for_external_pointer(request));
}

TEST(ViewportHostInteractionPolicyTests,
     RepeatedShortcutDoesNotRestartAnAlreadyActiveEditorTool) {
  using quader::should_dispatch_viewport_tool_activation;

  EXPECT_FALSE(should_dispatch_viewport_tool_activation({}));
  EXPECT_TRUE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = true,
      .editor_tool_matches_requested = true,
  }));
  EXPECT_TRUE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = false,
      .editor_tool_matches_requested = false,
  }));
  EXPECT_FALSE(should_dispatch_viewport_tool_activation({
      .activation_requested = true,
      .tool_changed = false,
      .editor_tool_matches_requested = true,
  }));
}

TEST(ViewportHostInteractionPolicyTests,
     OrderedEdgesDispatchEverySameFrameKeyboardPress) {
  const std::vector<quader::QuaderInputChord> pressed{
      {.device = quader::QuaderInputDevice::Keyboard,
       .key = quader::QuaderKeyCode::W},
      {.device = quader::QuaderInputDevice::Keyboard,
       .key = quader::QuaderKeyCode::E},
  };
  std::vector<quader::QuaderKeyCode> dispatched;

  quader::dispatch_ordered_viewport_input_edges(
      pressed, {},
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind) {
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Pressed);
        dispatched.push_back(chord.key);
        return true;
      });

  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderKeyCode::W,
                         quader::QuaderKeyCode::E}));
}

TEST(ViewportHostInteractionPolicyTests,
     PendingChordOwnsContinuationBeforeStatelessQuickAction) {
  const quader::QuaderInputChord continuation{
      .device = quader::QuaderInputDevice::Keyboard,
      .key = quader::QuaderKeyCode::D,
  };
  int stateless_action_attempts = 0;
  int stateful_dispatches = 0;

  EXPECT_TRUE(quader::dispatch_viewport_input_edge(
      continuation, quader::ViewportInputEdgeKind::Pressed,
      []() { return true; },
      [&](const quader::QuaderInputChord &,
          quader::ViewportInputEdgeKind) {
        ++stateless_action_attempts;
        return true;
      },
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind) {
        EXPECT_EQ(chord, continuation);
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Pressed);
        ++stateful_dispatches;
        return false;
      }));
  EXPECT_EQ(stateless_action_attempts, 0);
  EXPECT_EQ(stateful_dispatches, 1);
}

TEST(ViewportHostInteractionPolicyTests,
     DirectionalWheelBindingPrecedesGenericWheelFallback) {
  const quader::QuaderInputChord generic{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::Wheel,
  };
  const quader::QuaderInputChord up{
      .device = quader::QuaderInputDevice::Mouse,
      .gesture = quader::QuaderInputGesture::WheelUp,
  };
  const std::vector<quader::QuaderInputChord> wheel{generic, up};
  std::vector<quader::QuaderInputGesture> dispatched;

  quader::dispatch_ordered_viewport_input_edges(
      {}, wheel,
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind kind) {
        EXPECT_EQ(kind, quader::ViewportInputEdgeKind::Wheel);
        dispatched.push_back(chord.gesture);
        return chord.gesture == quader::QuaderInputGesture::WheelUp;
      });
  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderInputGesture::WheelUp}));

  dispatched.clear();
  quader::dispatch_ordered_viewport_input_edges(
      {}, wheel,
      [&](const quader::QuaderInputChord &chord,
          quader::ViewportInputEdgeKind) {
        dispatched.push_back(chord.gesture);
        return chord.gesture == quader::QuaderInputGesture::Wheel;
      });
  EXPECT_EQ(dispatched,
            (std::vector{quader::QuaderInputGesture::WheelUp,
                         quader::QuaderInputGesture::Wheel}));
}

TEST(ViewportHoverControllerTests,
     ResolvedGridSnapToggleInvalidatesStationaryBoxHover) {
  quader::ViewportHoverController controller;
  quader::ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  quader::ViewportHoverWorkRequest request{
      .active_tool = quader::ViewportToolId::Box,
      .grid_preset = 6,
      .grid_snap_enabled = true,
      .mouse_local = {320.0f, 240.0f},
      .viewport_size = {800.0f, 600.0f},
  };

  const quader::ViewportHoverWorkKey snapped =
      controller.make_key(camera, request);
  EXPECT_TRUE(controller.claim(snapped));
  EXPECT_FALSE(controller.claim(snapped));

  request.grid_snap_enabled = false;
  const quader::ViewportHoverWorkKey unsnapped =
      controller.make_key(camera, request);
  EXPECT_FALSE(snapped.matches(unsnapped));
  EXPECT_TRUE(controller.claim(unsnapped));
}

TEST(ViewportHostInteractionPolicyTests,
     BoxFootprintKeepsOverlayPassVisibleWhenDisplayTogglesAreOff) {
  using quader::ViewportOverlayPassVisibilityRequest;
  using quader::should_enable_viewport_overlay_pass;

  EXPECT_FALSE(should_enable_viewport_overlay_pass({}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass({
      .tool_overlay_visible = true,
  }));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.overlays_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.gizmos_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.wireframe_pane = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{.pivot_handle_visible = true}));
  EXPECT_TRUE(should_enable_viewport_overlay_pass(
      ViewportOverlayPassVisibilityRequest{
          .linear_array_handle_visible = true,
      }));
}

TEST(ViewportHostInteractionPolicyTests,
     SceneOverlayExtractionCountStaysOneForOneToFourRequestingPanes) {
  std::array<bool, 4> pane_requests = {};
  EXPECT_EQ(quader::viewport_scene_overlay_snapshot_extraction_count(
                pane_requests),
            0u);

  for (std::size_t pane_count = 1; pane_count <= pane_requests.size();
       ++pane_count) {
    pane_requests.fill(false);
    std::fill_n(pane_requests.begin(), pane_count, true);
    EXPECT_EQ(quader::viewport_scene_overlay_snapshot_extraction_count(
                  std::span<const bool>(pane_requests.data(), pane_count)),
              1u)
        << "pane_count=" << pane_count;
  }
}
