////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/bridge_preview_handle_layout.hpp"
#include "editor/tools/bridge_preview_viewport_interaction.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

/// Drives a real pending Bridge through the native pointer owner and history.
struct BridgePointerHarness final {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  ViewportCameraController camera;
  BridgePreviewViewportInteraction interaction;
  ViewportInputFrame input;
  ViewportInputResult result;
  QuaderModelingObjectRef object;
  std::optional<QuaderMeshAsset> source_mesh;
  QVec2 press;
  QVec2 projected_axis;

  BridgePointerHarness() {
    camera.set_viewport_size(800.0F, 600.0F);
    camera.focus_origin(8.0F);
    camera.set_view_direction({-0.6F, -0.35F, -0.72F});
    input.viewport_size = camera.viewport_size();
    input.viewport_hovered = true;
    input.viewport_focused = true;
  }

  void prepare() {
    quader_poly::Document document;
    const std::array first{
        quader_poly::add_vertex(document, {-1.0F, -1.0F, -1.0F}),
        quader_poly::add_vertex(document, {1.0F, -1.0F, -1.0F}),
        quader_poly::add_vertex(document, {1.0F, 1.0F, -1.0F}),
        quader_poly::add_vertex(document, {-1.0F, 1.0F, -1.0F})};
    const std::array second{
        quader_poly::add_vertex(document, {0.0F, -0.5F, 1.0F}),
        quader_poly::add_vertex(document, {2.0F, -0.5F, 1.0F}),
        quader_poly::add_vertex(document, {2.0F, 1.5F, 1.0F}),
        quader_poly::add_vertex(document, {0.0F, 1.5F, 1.0F})};
    const auto first_face = quader_poly::add_face(document, first);
    const auto second_face = quader_poly::add_face(
        document, std::array{second[3], second[2], second[1], second[0]});
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Bridge handle fixture"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(editor.selection().select_object(object).ok());
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_faces(
        object, {static_cast<int>(first_face), static_cast<int>(second_face)}).ok());
    auto settings = session.modeling().interaction_settings();
    settings.bridge_steps = 6;
    ASSERT_TRUE(session.modeling().set_interaction_settings(settings).ok);
    source_mesh = editor.query().compiled_object_mesh(object);
    ASSERT_TRUE(source_mesh);
    ASSERT_TRUE(editor.modeling().begin_preview("preview_bridge_faces").ok());
    ASSERT_TRUE(session.modeling().bridge_preview_handle_state());
  }

  void choose(BridgePreviewHandlePart part) {
    const auto state = session.modeling().bridge_preview_handle_state();
    ASSERT_TRUE(state);
    const auto layout = build_bridge_preview_handle_layout(camera, *state);
    const std::size_t index = part == BridgePreviewHandlePart::Start ? 0u : 1u;
    ASSERT_TRUE(layout.visible[index]);
    press = layout.screen_centers[index];
    ASSERT_EQ(hit_bridge_preview_handle(layout, press), part);
    const QVec3 axis = index == 0u ? state->start_handle_axis : state->end_handle_axis;
    const auto tip = camera.project_world_point(layout.centers[index] + axis);
    ASSERT_TRUE(tip.visible);
    projected_axis = tip.position - press;
    ASSERT_GT(std::hypot(projected_axis.x, projected_axis.y), 1.0F);
    input.mouse_local = press;
    input.mouse_screen = press;
  }

  void move(float amount = 0.4F) {
    input.mouse_local = press + projected_axis * amount;
    input.mouse_screen = input.mouse_local;
  }

  ViewportNativeToolInteractionResult frame(bool pressed, bool down, bool released) {
    return interaction.handle_frame(
        session.modeling(), camera,
        {.input = input, .input_result = result,
         .left_pressed = pressed, .left_down = down,
         .left_released = released, .left_click_local = press},
        [&] {
          const auto applied = editor.modeling().commit_preview();
          return CommandResult{.ok = applied.ok(), .changed = applied.changed(),
                               .message = applied.error_message()};
        });
  }

  ViewportNativeToolInteractionResult native_frame(
      bool pressed, bool down, bool released) {
    return session.native_tools().handle_pointer_frame(
        session.scene(), session.assets(), &session.point_placement(), camera,
        {.input = input, .input_result = result,
         .left_pressed = pressed, .left_down = down,
         .left_released = released, .left_click_local = press});
  }
};

class BridgeOverlayTarget final : public ViewportOverlaySubmissionTarget {
public:
  ViewportOverlayCameraUpdate update_overlay_cameras(
      std::size_t, std::uint8_t, ViewportOverlayCamera) override {
    return {true, true};
  }
  void submit_persistent_overlay(std::size_t, std::uint8_t,
                                 ViewportOverlayFrame,
                                 ViewportOverlayCamera) override {}
  void submit_dynamic_overlay(std::size_t, std::uint8_t,
                              ViewportOverlayFrame frame,
                              ViewportOverlayCamera) override {
    ++submit_count;
    submitted = std::move(frame);
  }
  int submit_count = 0;
  ViewportOverlayFrame submitted;
};

void expect_position_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

TEST(BridgePreviewViewportInteractionTests,
     IndependentDragsSynchronizeSettingsReleaseKeepsPreviewAndApplyRecordsOneUndo) {
  BridgePointerHarness harness;
  harness.prepare();
  const auto history_count = harness.session.history().undo_count();
  const auto source = harness.source_mesh;
  ASSERT_TRUE(source);
  const auto initial = *harness.session.modeling().bridge_preview_handle_state();
  harness.choose(BridgePreviewHandlePart::Start);
  const auto pressed = harness.frame(true, true, false);
  EXPECT_TRUE(pressed.consumed);
  EXPECT_TRUE(pressed.blocks_transform_gizmo);
  EXPECT_TRUE(pressed.claim_interaction_pane);
  EXPECT_FALSE(pressed.command_attempted);
  harness.move();
  const auto updated = harness.frame(false, true, false);
  ASSERT_TRUE(updated.command_attempted);
  ASSERT_TRUE(updated.command_result.ok) << updated.command_result.message;
  const auto first = harness.session.modeling().interaction_settings();
  EXPECT_GT(first.bridge_start_tension, 1.0);
  EXPECT_DOUBLE_EQ(first.bridge_end_tension, 1.0);
  const auto first_state = *harness.session.modeling().bridge_preview_handle_state();
  EXPECT_EQ(first_state.generation, initial.generation);
  expect_position_near(first_state.control_points[2], initial.control_points[2]);
  expect_position_near(first_state.control_points[1],
      first_state.control_points[0] +
          initial.start_handle_axis * static_cast<float>(first.bridge_start_tension));
  const auto settings_revision = harness.session.modeling().tool_settings_revision();
  for (int index = 0; index < 8; ++index) {
    EXPECT_FALSE(harness.frame(false, true, false).command_attempted);
  }
  EXPECT_EQ(harness.session.modeling().tool_settings_revision(), settings_revision);
  EXPECT_FALSE(harness.frame(false, false, true).command_attempted);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  EXPECT_EQ(harness.session.history().undo_count(), history_count);

  harness.choose(BridgePreviewHandlePart::End);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  const auto end_update = harness.frame(false, false, true);
  ASSERT_TRUE(end_update.command_result.ok) << end_update.command_result.message;
  const auto both = harness.session.modeling().interaction_settings();
  EXPECT_DOUBLE_EQ(both.bridge_start_tension, first.bridge_start_tension);
  EXPECT_GT(both.bridge_end_tension, 1.0);
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
  harness.input.modal_tool_finalize_pressed = true;
  const auto preview_mesh = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(preview_mesh);
  const auto applied = harness.frame(false, false, false);
  EXPECT_TRUE(applied.consumed);
  ASSERT_TRUE(applied.command_result.ok) << applied.command_result.message;
  EXPECT_FALSE(harness.session.modeling().operation_preview_active());
  EXPECT_EQ(harness.session.history().undo_count(), history_count + 1u);
  EXPECT_FALSE(harness.frame(false, false, false).command_attempted);
  EXPECT_EQ(harness.session.history().undo_count(), history_count + 1u);
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kEditUndo).ok());
  const auto undone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(undone);
  EXPECT_EQ(undone->indices, source->indices);
  ASSERT_EQ(undone->vertices.size(), source->vertices.size());
  for (std::size_t index = 0u; index < undone->vertices.size(); ++index) {
    expect_position_near(undone->vertices[index].position, source->vertices[index].position);
  }
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kEditRedo).ok());
  const auto redone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(redone);
  EXPECT_EQ(redone->indices, preview_mesh->indices);
  ASSERT_EQ(redone->vertices.size(), preview_mesh->vertices.size());
  for (std::size_t index = 0u; index < redone->vertices.size(); ++index) {
    expect_position_near(redone->vertices[index].position,
                         preview_mesh->vertices[index].position);
  }
}

TEST(BridgePreviewViewportInteractionTests,
     CancelRestoresOnlyCurrentDragAndReservesReleaseBeforeCancelingThePreview) {
  BridgePointerHarness harness;
  harness.prepare();
  harness.choose(BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  (void)harness.frame(false, false, true);
  const auto accepted = harness.session.modeling().interaction_settings();
  const auto history_count = harness.session.history().undo_count();
  harness.choose(BridgePreviewHandlePart::End);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  harness.result.cancel_requested = true;
  const auto canceled = harness.frame(false, true, false);
  ASSERT_TRUE(canceled.command_result.ok) << canceled.command_result.message;
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
  harness.result.cancel_requested = false;
  EXPECT_TRUE(harness.frame(false, false, true).consumed);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  harness.result.cancel_requested = true;
  const auto canceled_preview = harness.frame(false, false, false);
  ASSERT_TRUE(canceled_preview.command_result.ok);
  EXPECT_FALSE(harness.session.modeling().operation_preview_active());
  EXPECT_EQ(harness.session.history().undo_count(), history_count);
}

TEST(BridgePreviewViewportInteractionTests,
     CaptureLossAndNavigationRetainTheLastValidSampleAndFocusAloneKeepsCapture) {
  for (const bool navigation : {false, true}) {
    SCOPED_TRACE(navigation);
    BridgePointerHarness harness;
    harness.prepare();
    harness.choose(BridgePreviewHandlePart::Start);
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move();
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    const auto accepted = harness.session.modeling().interaction_settings();
    harness.input.viewport_focused = false;
    harness.move(2.0F);
    EXPECT_TRUE(harness.frame(false, true, false).consumed);
    EXPECT_TRUE(harness.interaction.dragging());
    EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
    harness.input.viewport_focused = true;
    harness.input.pointer_interaction_canceled = !navigation;
    harness.input.navigation_active = navigation;
    EXPECT_TRUE(harness.frame(false, true, false).consumed);
    EXPECT_FALSE(harness.interaction.dragging());
    EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
    EXPECT_TRUE(harness.session.modeling().operation_preview_active());
    EXPECT_TRUE(harness.frame(false, false, true).consumed);
    EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  }
}

TEST(BridgePreviewViewportInteractionTests,
     StaleReleaseAndExternalNumericEditsCannotOverwriteTheAcceptedCurve) {
  BridgePointerHarness harness;
  harness.prepare();
  harness.choose(BridgePreviewHandlePart::Start);
  harness.input.pointer_pressed_edges = {
      {.button = QuaderMouseButton::Left, .local = harness.press, .sequence = 12u}};
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.input.pointer_pressed_edges.clear();
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  const auto accepted = harness.session.modeling().interaction_settings();
  harness.input.pointer_released_edges = {
      {.button = QuaderMouseButton::Left, .local = {0.0F, 0.0F}, .sequence = 8u}};
  EXPECT_TRUE(harness.frame(false, false, true).consumed);
  EXPECT_TRUE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  harness.input.pointer_released_edges.clear();
  auto numerical = accepted;
  numerical.bridge_start_tension = 0.4;
  ASSERT_TRUE(harness.session.modeling().set_interaction_settings(numerical).ok);
  harness.move(2.0F);
  EXPECT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), numerical);
  EXPECT_TRUE(harness.frame(false, false, true).consumed);
  EXPECT_EQ(harness.session.modeling().interaction_settings(), numerical);
}

TEST(BridgePreviewViewportInteractionTests,
     NonFiniteSamplesRetainPreviewAndReversalRecoversBeforeRelease) {
  BridgePointerHarness harness;
  harness.prepare();
  harness.choose(BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  const auto accepted = harness.session.modeling().interaction_settings();
  harness.input.mouse_local.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(harness.frame(false, true, false).command_attempted);
  EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  harness.move(0.2F);
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  EXPECT_LT(harness.session.modeling().interaction_settings().bridge_start_tension,
            accepted.bridge_start_tension);
}

TEST(BridgePreviewViewportInteractionTests,
     ReturningToTheAcceptedSampleClearsAnInvalidPointerWithoutRegenerating) {
  BridgePointerHarness harness;
  harness.prepare();
  harness.choose(BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  const auto accepted = harness.session.modeling().interaction_settings();
  const auto revision = harness.session.modeling().tool_settings_revision();
  harness.input.mouse_local.x = std::numeric_limits<float>::quiet_NaN();
  EXPECT_FALSE(harness.frame(false, true, false).command_attempted);
  harness.move();
  const auto released = harness.frame(false, false, true);
  EXPECT_FALSE(released.command_attempted);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  EXPECT_EQ(harness.session.modeling().tool_settings_revision(), revision);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
}

TEST(BridgePreviewViewportInteractionTests,
     ZeroTensionHandlesRemainPickableAndClampIndependently) {
  BridgePointerHarness harness;
  harness.prepare();
  auto settings = harness.session.modeling().interaction_settings();
  settings.bridge_start_tension = 0.0;
  settings.bridge_end_tension = 0.0;
  ASSERT_TRUE(harness.session.modeling().set_interaction_settings(settings).ok);
  const auto state = *harness.session.modeling().bridge_preview_handle_state();
  expect_position_near(state.control_points[0], state.control_points[1]);
  expect_position_near(state.control_points[2], state.control_points[3]);
  harness.choose(BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move(100.0F);
  const auto upper = harness.frame(false, true, false);
  ASSERT_TRUE(upper.command_result.ok) << upper.command_result.message;
  EXPECT_DOUBLE_EQ(harness.session.modeling().interaction_settings().bridge_start_tension, 3.0);
  EXPECT_DOUBLE_EQ(harness.session.modeling().interaction_settings().bridge_end_tension, 0.0);
  harness.move(-100.0F);
  const auto lower = harness.frame(false, false, true);
  ASSERT_TRUE(lower.command_result.ok) << lower.command_result.message;
  EXPECT_DOUBLE_EQ(harness.session.modeling().interaction_settings().bridge_start_tension, 0.0);
  EXPECT_DOUBLE_EQ(harness.session.modeling().interaction_settings().bridge_end_tension, 0.0);
}

TEST(BridgePreviewViewportInteractionTests,
     NativeCoordinatorRoutesCancelPrepassAndSharesOnePreviewBetweenPaneCameras) {
  BridgePointerHarness harness;
  harness.prepare();
  const auto initial = harness.session.modeling().interaction_settings();
  harness.choose(BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.native_frame(true, true, false).claim_interaction_pane);
  harness.move();
  ASSERT_TRUE(harness.native_frame(false, true, false).command_result.ok);
  EXPECT_TRUE(harness.session.native_tools().bridge_pointer_dragging());
  EXPECT_TRUE(harness.session.native_tools().pointer_dragging());
  const auto captured = harness.session.native_tools().presentation_snapshot();
  ASSERT_TRUE(captured.bridge_handle);
  EXPECT_TRUE(captured.bridge_dragging);
  EXPECT_EQ(captured.bridge_hovered_part, BridgePreviewHandlePart::Start);
  ViewportInteractionController controller;
  ASSERT_TRUE(controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
  EXPECT_FALSE(harness.session.native_tools().pointer_dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), initial);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  harness.result.cancel_requested = true;
  EXPECT_TRUE(harness.native_frame(false, true, false).consumed);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
  harness.result.cancel_requested = false;
  EXPECT_TRUE(harness.native_frame(false, false, true).consumed);

  harness.camera.set_view_direction({0.7F, -0.25F, -0.67F});
  harness.choose(BridgePreviewHandlePart::End);
  ASSERT_TRUE(harness.native_frame(true, true, false).claim_interaction_pane);
  harness.move();
  const auto updated = harness.native_frame(false, false, true);
  ASSERT_TRUE(updated.command_result.ok) << updated.command_result.message;
  const auto shared = harness.session.native_tools().presentation_snapshot();
  ASSERT_TRUE(shared.bridge_handle);
  EXPECT_FALSE(shared.bridge_dragging);
  EXPECT_GT(shared.bridge_handle->end_tension, 1.0);
  EXPECT_DOUBLE_EQ(harness.session.modeling().interaction_settings().bridge_start_tension, 1.0);
  EXPECT_EQ(shared.bridge_handle->generation, captured.bridge_handle->generation);
  const auto history_count = harness.session.history().undo_count();
  harness.input.modal_tool_finalize_pressed = true;
  const auto applied = harness.native_frame(false, false, false);
  ASSERT_TRUE(applied.command_result.ok) << applied.command_result.message;
  EXPECT_EQ(harness.session.history().undo_count(), history_count + 1u);
  EXPECT_FALSE(harness.session.modeling().operation_preview_active());
}

TEST(BridgePreviewViewportInteractionTests,
     EndOnHandleUsesBoundedMotionAndCameraChangesRetireCaptureAtAcceptedValue) {
  BridgePointerHarness harness;
  harness.prepare();
  const auto state = *harness.session.modeling().bridge_preview_handle_state();
  harness.camera.set_view_direction(normalize(state.start_handle_axis) * -1.0F);
  const auto layout = build_bridge_preview_handle_layout(harness.camera, state);
  ASSERT_TRUE(layout.visible[0]);
  harness.press = layout.screen_centers[0];
  harness.input.mouse_local = harness.press;
  ASSERT_EQ(hit_bridge_preview_handle(layout, harness.press), BridgePreviewHandlePart::Start);
  ASSERT_TRUE(harness.frame(true, true, false).claim_interaction_pane);
  harness.input.mouse_local.y -= 12.0F;
  const auto moved = harness.frame(false, true, false);
  ASSERT_TRUE(moved.command_result.ok) << moved.command_result.message;
  const auto accepted = harness.session.modeling().interaction_settings();
  EXPECT_GT(accepted.bridge_start_tension, 1.0);
  EXPECT_LT(accepted.bridge_start_tension, 2.0);
  harness.result.camera_changed = true;
  harness.input.mouse_local.y -= 200.0F;
  EXPECT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.modeling().interaction_settings(), accepted);
  EXPECT_TRUE(harness.session.modeling().operation_preview_active());
}

TEST(BridgePreviewViewportInteractionTests,
     SharedLayoutAndRetainedSubmissionFollowTheActualCurveAndReuseUnchangedFrames) {
  BridgePointerHarness harness;
  harness.prepare();
  auto state = *harness.session.modeling().bridge_preview_handle_state();
  const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
      harness.camera, harness.camera.viewport_size());
  BridgeOverlayTarget target;
  ViewportOverlaySubmissionCoordinator coordinator;
  const auto settings = ViewportSettings::defaults();
  const auto submit = [&]() {
    const ViewportToolOverlaySource source({
        .source_identity = reinterpret_cast<std::uintptr_t>(&state),
        .overlay_camera = &overlay_camera,
        .viewport_camera = &harness.camera,
        .bridge_handle = &state,
        .gizmos_visible = true});
    return coordinator.submit(
        {.viewport_index = 0u, .layer_mask = 1u, .camera = overlay_camera,
         .dynamic_tool_source = source.source_stamp(),
         .dynamic_tool_frame_provider = &source}, settings, target);
  };
  ASSERT_TRUE(submit().dynamic_submitted);
  ASSERT_EQ(target.submitted.lines.size(), 34u);
  ASSERT_EQ(target.submitted.triangles.size(), 48u);
  expect_position_near(target.submitted.lines.front().start, state.control_points[0]);
  expect_position_near(target.submitted.lines[31u].end, state.control_points[3]);
  const auto plan = OverlayRenderableBuilder::build_layer(
      target.submitted, overlay_camera, ViewportOverlayLayer::ToolHandle);
  ASSERT_FALSE(plan.face_fill_batches.empty());
  EXPECT_FALSE(plan.face_fill_batches.front().state.depth_write);
  for (const float scale : {1.0F, 2.0F}) {
    const auto layout = build_bridge_preview_handle_layout(harness.camera, state, scale);
    ASSERT_TRUE(layout.visible[0]);
    ASSERT_TRUE(layout.visible[1]);
    EXPECT_FLOAT_EQ(layout.radius_pixels, 7.0F * scale);
    EXPECT_EQ(hit_bridge_preview_handle(layout, layout.screen_centers[0]),
              BridgePreviewHandlePart::Start);
    const auto projected_rim = harness.camera.project_world_point(
        layout.centers[0] + layout.camera_right * layout.world_radii[0]);
    ASSERT_TRUE(projected_rim.visible);
    EXPECT_NEAR(std::hypot(projected_rim.position.x - layout.screen_centers[0].x,
                           projected_rim.position.y - layout.screen_centers[0].y),
                layout.radius_pixels, 0.001F);
  }
  const auto compose_count = coordinator.work_counters().dynamic_source_compose_count;
  const auto submit_count = target.submit_count;
  EXPECT_TRUE(submit().camera_only);
  EXPECT_EQ(target.submit_count, submit_count);
  EXPECT_EQ(coordinator.work_counters().dynamic_source_compose_count, compose_count);
  state.control_points[1] = state.control_points[1] + state.start_handle_axis * 0.3F;
  EXPECT_TRUE(submit().dynamic_submitted);
  EXPECT_EQ(target.submit_count, submit_count + 1);
  expect_position_near(target.submitted.lines[32u].end, state.control_points[1]);
  EXPECT_TRUE(submit().camera_only);
  EXPECT_EQ(target.submit_count, submit_count + 1);
}

} // namespace
} // namespace quader
