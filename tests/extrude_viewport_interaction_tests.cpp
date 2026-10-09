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
#include "editor/tools/extrude_tool_service.hpp"
#include "editor/tools/extrude_handle_layout.hpp"
#include "editor/tools/extrude_viewport_interaction.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/viewport_tool_overlay_projection.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace quader {
namespace {

/// Exercises native pointer ownership against the real scene and history owners.
struct ExtrudePointerHarness final {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  ViewportCameraController camera;
  ExtrudeViewportInteraction interaction;
  ViewportInputFrame input;
  ViewportInputResult result;
  QuaderModelingObjectRef object;
  QVec2 press;

  ExtrudePointerHarness() {
    camera.set_viewport_size(800.0F, 600.0F);
    camera.focus_origin(6.0F);
    camera.set_view_direction({-0.45F, -0.35F, -0.82F});
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0F, 600.0F};
  }

  void prepare_faces(const QuaderTransform &transform = {}) {
    const auto created = editor.modeling().create_box(
        {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
        "Extrusion pointer box");
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(editor.scene().set_transform(object.entity, transform).ok());
    ASSERT_TRUE(editor.selection().select_object(object).ok());
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_face(object, 1).ok());
    ASSERT_TRUE(session.extrude().set_amount(0.0).ok);
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
    ASSERT_TRUE(editor.tools().commit_active_tool_preview().ok());
    ASSERT_TRUE(session.extrude().handle_state().active);
    const auto endpoint = ExtrudeViewportInteraction::handle_endpoint(
        camera, session.extrude().handle_state());
    ASSERT_TRUE(quader_editor_core::project_viewport_point(
        ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size()),
        endpoint, press));
    input.mouse_local = press;
    input.mouse_screen = press;
  }

  void prepare_corner_faces() {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {-1.0F, 0.0F, 0.0F}),
        quader_poly::add_vertex(document, {1.0F, 0.0F, 0.0F}),
        quader_poly::add_vertex(document, {1.0F, 1.0F, 0.0F}),
        quader_poly::add_vertex(document, {-1.0F, 1.0F, 0.0F}),
        quader_poly::add_vertex(document, {-1.0F, 0.0F, 1.0F}),
        quader_poly::add_vertex(document, {1.0F, 0.0F, 1.0F})};
    (void)quader_poly::add_face(
        document, std::array{vertices[0], vertices[1], vertices[2], vertices[3]});
    (void)quader_poly::add_face(
        document, std::array{vertices[1], vertices[0], vertices[4], vertices[5]});
    const auto created = editor.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Perpendicular extrusion faces"});
    ASSERT_TRUE(created.ok()) << created.error_message();
    object = created.value();
    ASSERT_TRUE(editor.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(editor.selection().select_faces(object, {1, 2}).ok());
    ASSERT_TRUE(editor.tools().activate(ViewportToolId::Extrude).ok());
    ASSERT_TRUE(session.extrude().preview_active());
    ASSERT_EQ(session.modeling().selection_summary().selected_face_count, 2u);
  }

  ViewportNativeToolInteractionResult frame(bool pressed, bool down,
                                            bool released, bool snap = false,
                                            bool scene_snap = false,
                                            double vertex_radius = 0.0) {
    return interaction.handle_frame(
        session.extrude(), camera,
        {.input = input,
         .input_result = result,
         .left_pressed = pressed,
         .left_down = down,
         .left_released = released,
         .left_click_local = press,
         .scene_snap_enabled = scene_snap,
         .grid_snap_enabled = snap,
         .scene_vertex_snap_radius = vertex_radius},
        &session.modeling());
  }

  void move(float pixels = 60.0F) {
    move_by({pixels, -pixels});
  }

  void move_by(QVec2 offset) {
    input.mouse_local = press + offset;
    input.mouse_screen = input.mouse_local;
  }

  void choose_handle() {
    const auto layout = build_extrude_handle_layout(
        camera, session.extrude().handle_state());
    ASSERT_TRUE(layout.valid);
    press = layout.button_center_screen;
    ASSERT_EQ(hit_extrude_handle(layout, press), ExtrudeHandlePart::NewExtrusion);
    move_by({});
  }

  void choose_selected_face() {
    const auto handle = session.extrude().handle_state();
    camera.set_view_direction(handle.world_direction * -1.0F);
    ASSERT_TRUE(quader_editor_core::project_viewport_point(
        ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size()),
        handle.world_position, press));
    press.x += build_extrude_handle_layout(camera, handle).button_radius_pixels + 6.0F;
    ASSERT_FALSE(ExtrudeViewportInteraction::handle_hit(camera, handle, press));
    move_by({});
  }
};

void expect_extrude_mesh_geometry(const QuaderMeshAsset &actual,
                                  const QuaderMeshAsset &expected) {
  EXPECT_EQ(actual.indices, expected.indices);
  ASSERT_EQ(actual.vertices.size(), expected.vertices.size());
  for (std::size_t index = 0; index < actual.vertices.size(); ++index) {
    EXPECT_FLOAT_EQ(actual.vertices[index].position.x,
                    expected.vertices[index].position.x);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.y,
                    expected.vertices[index].position.y);
    EXPECT_FLOAT_EQ(actual.vertices[index].position.z,
                    expected.vertices[index].position.z);
  }
}

QVec3 selected_face_center(const ViewportOverlayFrame &frame) {
  QVec3 sum{};
  std::size_t triangle_count = 0u;
  for (const auto &triangle : frame.triangles) {
    if (triangle.layer != ViewportOverlayLayer::SelectedVisibleFace) {
      continue;
    }
    sum = sum + triangle.a + triangle.b + triangle.c;
    ++triangle_count;
  }
  EXPECT_GT(triangle_count, 0u);
  return triangle_count == 0u ? QVec3{}
      : sum / static_cast<float>(triangle_count * 3u);
}

TEST(ExtrudeViewportInteractionTests, EmptyActivationAndModifierClicksKeepSelectionInput) {
  ExtrudePointerHarness harness;
  EXPECT_FALSE(harness.editor.tools().activate(ViewportToolId::Extrude).ok());
  EXPECT_FALSE(harness.frame(true, true, false).consumed);
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_EQ(harness.session.extrude().work_stats_for_tests().geometry_update_count,
            0u);
  ASSERT_TRUE(harness.session.extrude().cancel().ok);
  harness.prepare_faces();
  harness.choose_selected_face();
  for (const ViewportModifiers modifier : {
           ViewportModifiers{.primary = true},
           ViewportModifiers{.shift = true},
           ViewportModifiers{.alt = true}}) {
    harness.input.modifiers = modifier;
    EXPECT_FALSE(harness.frame(true, true, false).consumed);
    EXPECT_FALSE(harness.interaction.dragging());
  }
}

TEST(ExtrudeViewportInteractionTests, DiskAcceptsInitialCtrlAndShiftModifiers) {
  for (const auto modifiers : {ViewportModifiers{.primary = true},
                                ViewportModifiers{.shift = true},
                                ViewportModifiers{.primary = true, .shift = true}}) {
    SCOPED_TRACE(modifiers.primary);
    SCOPED_TRACE(modifiers.shift);
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(
        modeling::FaceExtrudeMode::AlongNormals).ok);
    harness.camera.set_view_direction(
        harness.session.extrude().handle_state().world_direction * -1.0F);
    harness.choose_handle();
    const auto handle = harness.session.extrude().handle_state();
    const double world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
        ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
        handle.world_position);
    harness.input.modifiers = modifiers;
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move_by({0.0F, -60.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    double expected_world = 120.0 * world_per_pixel * (modifiers.shift ? 0.1 : 1.0);
    if (modifiers.primary) {
      const double grid = viewport_grid_world_size_for_preset(harness.input.grid_preset);
      expected_world = std::round(expected_world / grid) * grid;
    }
    EXPECT_NEAR(harness.session.extrude().amount(),
                expected_world * viewport_host_defaults().map_units_per_world_unit,
                1.0e-5);
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  }
}

TEST(ExtrudeViewportInteractionTests, StationaryDiskClickDoesNotSnapOrChangeHistory) {
  for (const bool primary : {false, true}) {
    for (const bool vertex_snap : {false, true}) {
      SCOPED_TRACE(primary);
      SCOPED_TRACE(vertex_snap);
      ExtrudePointerHarness harness;
      harness.prepare_faces();
      ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
      ASSERT_TRUE(harness.editor.commands().execute(quader_command::kToolsActivateExtrude).ok());
      harness.choose_handle();
      const auto before = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(before.has_value());
      const auto history_revision = harness.session.history().revision();
      const auto work = harness.session.extrude().work_stats_for_tests();
      const auto snap_queries = harness.session.modeling().mesh_snap_query_count();
      harness.input.modifiers.primary = primary;
      harness.input.transform_vertex_snap_down = vertex_snap;
      ASSERT_TRUE(harness.frame(true, true, false).claim_interaction_pane);
      for (int index = 0; index < 3; ++index) {
        const auto stationary = harness.frame(false, true, false);
        EXPECT_TRUE(stationary.consumed);
        EXPECT_FALSE(stationary.command_attempted);
        EXPECT_FALSE(harness.session.extrude().dragging());
      }
      const auto released = harness.frame(false, false, true);
      EXPECT_TRUE(released.consumed);
      EXPECT_FALSE(released.command_attempted);
      EXPECT_FALSE(released.selection_click_requested);
      EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 2.15);
      EXPECT_EQ(harness.session.history().revision(), history_revision);
      EXPECT_EQ(harness.session.extrude().work_stats_for_tests().geometry_update_count,
                work.geometry_update_count);
      EXPECT_EQ(harness.session.modeling().mesh_snap_query_count(), snap_queries);
      const auto after = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(after.has_value());
      expect_extrude_mesh_geometry(*after, *before);
    }
  }
}

TEST(ExtrudeViewportInteractionTests, OnePixelDiskMotionPreviewsAndReturningToOriginCancels) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
    harness.camera.set_view_direction(
        harness.session.extrude().handle_state().world_direction * -1.0F);
    harness.choose_handle();
    const auto before = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(before.has_value());
    const auto history_revision = harness.session.history().revision();
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move_by({0.0F, -1.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_TRUE(harness.session.extrude().dragging());
    EXPECT_GT(harness.session.extrude().amount(), 0.0);
    harness.move_by({});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 0.0);
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
    EXPECT_EQ(harness.session.history().revision(), history_revision);
    const auto after = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(after.has_value());
    expect_extrude_mesh_geometry(*after, *before);
  }
}

TEST(ExtrudeViewportInteractionTests, CoalescedOnePixelDiskReleaseAdjustsTheExistingStep) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_mode(modeling::FaceExtrudeMode::AlongNormals).ok);
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kToolsActivateExtrude).ok());
  harness.camera.set_view_direction(
      harness.session.extrude().handle_state().world_direction * -1.0F);
  harness.choose_handle();
  const auto handle = harness.session.extrude().handle_state();
  const double world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
      ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
      handle.adjustment_origin);
  harness.input.pointer_pressed_edges = {{
      .button = QuaderMouseButton::Left, .local = harness.press, .sequence = 200u}};
  harness.input.pointer_released_edges = {{
      .button = QuaderMouseButton::Left,
      .local = harness.press + QVec2{0.0F, -1.0F}, .sequence = 201u}};
  harness.move_by({0.0F, -100.0F});
  const auto released = harness.frame(true, false, true);
  ASSERT_TRUE(released.command_result.ok) << released.command_result.message;
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_TRUE(harness.session.extrude().adjustable());
  EXPECT_NEAR(harness.session.extrude().amount(),
              2.15 + 2.0 * world_per_pixel *
                  viewport_host_defaults().map_units_per_world_unit, 1.0e-5);
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto undone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(undone.has_value());
  expect_extrude_mesh_geometry(*undone, *source);
}

TEST(ExtrudeViewportInteractionTests, DragReleaseKeepsPreviewUntilApplyRecordsOneUndo) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto before = harness.editor.query().mesh(harness.object.mesh);
  ASSERT_TRUE(before);
  const auto undo_count = harness.session.history().undo_count();
  EXPECT_TRUE(harness.frame(true, true, false).consumed);
  harness.move();
  const auto drag = harness.frame(false, true, false);
  ASSERT_TRUE(drag.command_result.ok) << drag.command_result.message;
  ASSERT_TRUE(harness.session.extrude().dragging());
  harness.input.modal_tool_finalize_pressed = true;
  EXPECT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_TRUE(harness.session.extrude().dragging());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  harness.input.modal_tool_finalize_pressed = false;
  const auto work = harness.session.extrude().work_stats_for_tests();
  for (int frame = 0; frame < 12; ++frame) {
    EXPECT_TRUE(harness.frame(false, true, false).consumed);
  }
  EXPECT_EQ(harness.session.extrude().work_stats_for_tests().geometry_update_count,
            work.geometry_update_count);
  EXPECT_EQ(harness.session.extrude().work_stats_for_tests().source_capture_count,
            work.source_capture_count);
  const auto released = harness.frame(false, false, true);
  ASSERT_TRUE(released.command_result.ok) << released.command_result.message;
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_NE(harness.session.extrude().amount(), 0.0);
  EXPECT_TRUE(harness.session.extrude().preview_active());
  EXPECT_TRUE(harness.editor.tools().active_settings().can_apply);
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  EXPECT_EQ(harness.editor.query().mesh(harness.object.mesh)->index_count,
            before->index_count);
  harness.input.modal_tool_finalize_pressed = true;
  const auto applied = harness.frame(false, false, false);
  EXPECT_TRUE(applied.consumed);
  ASSERT_TRUE(applied.command_attempted);
  ASSERT_TRUE(applied.command_result.ok) << applied.command_result.message;
  EXPECT_FALSE(harness.session.extrude().preview_active());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1);
  EXPECT_GT(harness.editor.query().mesh(harness.object.mesh)->index_count,
            before->index_count);
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kEditUndo).ok());
  EXPECT_EQ(harness.editor.query().mesh(harness.object.mesh)->index_count,
            before->index_count);
}

TEST(ExtrudeViewportInteractionTests,
     PendingSourceConsumesMissesModifiedClicksAndStationaryFaceReleases) {
  for (const bool selected_face : {false, true}) {
    for (const ViewportModifiers modifier : {
             ViewportModifiers{}, ViewportModifiers{.primary = true},
             ViewportModifiers{.shift = true}, ViewportModifiers{.alt = true}}) {
      SCOPED_TRACE(selected_face);
      SCOPED_TRACE(modifier.primary);
      SCOPED_TRACE(modifier.shift);
      SCOPED_TRACE(modifier.alt);
      ExtrudePointerHarness harness;
      harness.prepare_faces();
      ASSERT_TRUE(harness.editor.commands().execute(
          quader_command::kToolsActivateExtrude).ok());
      ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
      ASSERT_TRUE(harness.session.extrude().preview_active());
      const auto before = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(before.has_value());
      const auto selection = harness.editor.selection().summary();
      const auto history = harness.session.history().undo_count();
      if (selected_face) {
        harness.choose_selected_face();
      } else {
        harness.press = {790.0F, 590.0F};
        harness.move_by({});
      }
      harness.input.modifiers = modifier;
      const auto pressed = harness.frame(true, true, false);
      EXPECT_TRUE(pressed.consumed);
      EXPECT_TRUE(pressed.blocks_transform_gizmo);
      const auto held = harness.frame(false, true, false);
      EXPECT_TRUE(held.consumed);
      const auto released = harness.frame(false, false, true);
      EXPECT_TRUE(released.consumed);
      EXPECT_FALSE(released.selection_click_requested);
      EXPECT_TRUE(harness.session.extrude().preview_active());
      EXPECT_FALSE(harness.session.extrude().dragging());
      EXPECT_EQ(harness.editor.selection().summary().selected_face_count,
                selection.selected_face_count);
      EXPECT_EQ(harness.editor.selection().summary().active_face_id,
                selection.active_face_id);
      EXPECT_EQ(harness.session.history().undo_count(), history);
      const auto after = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(after.has_value());
      expect_extrude_mesh_geometry(*after, *before);
    }
  }
}

TEST(ExtrudeViewportInteractionTests,
     GizmoRemainsProjectedThroughoutEveryModeAndAcceptedDragSample) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
    harness.choose_handle();
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    for (const float distance : {10.0F, 25.0F, 40.0F}) {
      SCOPED_TRACE(distance);
      harness.move(distance);
      ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
      ASSERT_TRUE(harness.session.extrude().dragging());
      const auto handle = harness.session.extrude().handle_state();
      ASSERT_TRUE(handle.active);
      ASSERT_TRUE(handle.dragging);
      const auto layout = build_extrude_handle_layout(harness.camera, handle);
      ASSERT_TRUE(layout.valid);
      const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
          harness.camera, harness.camera.viewport_size());
      const ViewportToolOverlaySource source({
          .overlay_camera = &overlay_camera,
          .viewport_camera = &harness.camera,
          .extrusion_handle = &handle});
      const auto frame = source.build_dynamic_tool_overlay_frame();
      ASSERT_FALSE(frame.triangles.empty());
      ASSERT_EQ(frame.lines.size(), 1u);
      EXPECT_FLOAT_EQ(frame.lines.front().start.x, handle.world_position.x);
      EXPECT_FLOAT_EQ(frame.lines.front().start.y, handle.world_position.y);
      EXPECT_FLOAT_EQ(frame.lines.front().start.z, handle.world_position.z);
    }
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
    EXPECT_TRUE(build_extrude_handle_layout(
        harness.camera, harness.session.extrude().handle_state()).valid);
  }
}

TEST(ExtrudeViewportInteractionTests,
     FinalizingPendingExtrusionSuppressesAnUnstartedFacePressRelease) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  const auto undo_count = harness.session.history().undo_count();
  harness.choose_selected_face();
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  ASSERT_FALSE(harness.session.extrude().dragging());
  ASSERT_TRUE(harness.editor.tools().active_settings().can_apply);
  harness.input.modal_tool_finalize_pressed = true;
  const auto applied = harness.frame(false, true, false);
  ASSERT_TRUE(applied.command_result.ok);
  EXPECT_TRUE(applied.command_attempted);
  EXPECT_TRUE(applied.sync_active_tool);
  EXPECT_FALSE(harness.session.extrude().preview_active());
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count + 1);
  harness.input.modal_tool_finalize_pressed = false;
  const auto released = harness.frame(false, false, true);
  EXPECT_TRUE(released.consumed);
  EXPECT_FALSE(released.selection_click_requested);
  EXPECT_FALSE(released.command_attempted);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto undone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(undone.has_value());
  expect_extrude_mesh_geometry(*undone, *source);
}

TEST(ExtrudeViewportInteractionTests, EscapeCancelsOnlyDragAndReservesItsRelease) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto before = harness.editor.query().mesh(harness.object.mesh);
  ASSERT_TRUE(before);
  (void)harness.frame(true, true, false);
  harness.move();
  (void)harness.frame(false, true, false);
  ASSERT_TRUE(harness.session.extrude().dragging());
  harness.result.cancel_requested = true;
  EXPECT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_TRUE(harness.session.extrude().active());
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_EQ(harness.editor.query().mesh(harness.object.mesh)->index_count,
            before->index_count);
  harness.result.cancel_requested = false;
  EXPECT_TRUE(harness.frame(false, false, true).consumed);
  harness.result.cancel_requested = true;
  EXPECT_TRUE(harness.frame(false, false, false).sync_active_tool);
  EXPECT_FALSE(harness.session.extrude().active());
}

TEST(ExtrudeViewportInteractionTests, ToolSwitchAcceptsPendingDragAfterExplicitCancelRestoresPriorDrag) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  ASSERT_TRUE(harness.session.extrude().begin_drag().ok);
  ASSERT_TRUE(harness.session.extrude().update_drag(2.15).ok);
  ASSERT_TRUE(harness.session.native_tools().cancel_active().ok);
  EXPECT_TRUE(harness.session.extrude().active());
  EXPECT_FALSE(harness.session.extrude().dragging());
  const auto before = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(before.has_value());
  const auto steps = harness.session.history().undo_count();
  ASSERT_TRUE(harness.session.extrude().begin_drag().ok);
  ASSERT_TRUE(harness.session.extrude().update_drag(3.0).ok);
  const auto authored = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(authored.has_value());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  EXPECT_FALSE(harness.session.extrude().active());
  EXPECT_FALSE(harness.session.extrude().dragging());
  expect_extrude_mesh_geometry(
      *harness.editor.query().compiled_object_mesh(harness.object), *authored);
  EXPECT_EQ(harness.session.history().undo_count(), steps + 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_extrude_mesh_geometry(
      *harness.editor.query().compiled_object_mesh(harness.object), *before);
}

TEST(ExtrudeViewportInteractionTests,
     EscapeDuringStationaryPendingPressCancelsPreviewAndReservesRelease) {
  for (const bool face_press : {false, true}) {
    SCOPED_TRACE(face_press);
    ExtrudePointerHarness harness;
    ASSERT_NO_FATAL_FAILURE(harness.prepare_faces());
    const auto source = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(source.has_value());
    const auto steps = harness.session.history().undo_count();
    ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
    ASSERT_TRUE(harness.editor.commands().execute(
        quader_command::kToolsActivateExtrude).ok());
    ASSERT_TRUE(harness.session.extrude().preview_active());
    if (face_press) {
      ASSERT_NO_FATAL_FAILURE(harness.choose_selected_face());
    } else {
      ASSERT_NO_FATAL_FAILURE(harness.choose_handle());
    }
    ASSERT_TRUE(harness.frame(true, true, false).claim_interaction_pane);
    ASSERT_FALSE(harness.session.extrude().dragging());
    harness.result.cancel_requested = true;
    const auto canceled = harness.frame(false, true, false);
    ASSERT_TRUE(canceled.command_result.ok);
    EXPECT_TRUE(canceled.sync_active_tool);
    EXPECT_FALSE(harness.session.extrude().active());
    EXPECT_FALSE(harness.session.extrude().preview_active());
    EXPECT_TRUE(harness.session.modeling().tool_preview_meshes().empty());
    EXPECT_EQ(harness.session.history().undo_count(), steps);
    const auto restored = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(restored.has_value());
    expect_extrude_mesh_geometry(*restored, *source);
    harness.result.cancel_requested = false;
    const auto released = harness.frame(false, false, true);
    EXPECT_TRUE(released.consumed);
    EXPECT_FALSE(released.selection_click_requested);
    EXPECT_FALSE(released.command_attempted);
    EXPECT_EQ(harness.session.modeling().selection_summary().selected_face_count, 1U);
  }
}

TEST(ExtrudeViewportInteractionTests, NavigationAndStaleReleasesCannotCommitGesture) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  harness.input.navigation_active = true;
  EXPECT_FALSE(harness.frame(true, true, false).consumed);
  EXPECT_FALSE(harness.interaction.dragging());
  harness.input.navigation_active = false;
  harness.input.pointer_pressed_edges = {
      {.button = QuaderMouseButton::Left, .local = harness.press, .sequence = 20}};
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.input.pointer_pressed_edges.clear();
  harness.move();
  (void)harness.frame(false, true, false);
  ASSERT_TRUE(harness.session.extrude().dragging());
  harness.input.pointer_released_edges = {
      {.button = QuaderMouseButton::Left, .local = harness.input.mouse_local,
       .sequence = 19}};
  (void)harness.frame(false, true, true);
  EXPECT_TRUE(harness.session.extrude().dragging());
  harness.input.pointer_released_edges.front().sequence = 21;
  (void)harness.frame(false, false, true);
  EXPECT_FALSE(harness.session.extrude().dragging());
}

TEST(ExtrudeViewportInteractionTests,
     DiskAdjustsPendingAmountForEveryModeAndReturningToPressKeepsResult) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    for (const double initial_amount : {2.15, -2.15}) {
      SCOPED_TRACE(static_cast<int>(mode));
      SCOPED_TRACE(initial_amount);
      ExtrudePointerHarness harness;
      harness.prepare_faces();
      const auto source = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(source.has_value());
      ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
      ASSERT_TRUE(harness.session.extrude().set_amount(initial_amount).ok);
      const auto committed = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(committed.has_value());
      const auto handle = harness.session.extrude().handle_state();
      harness.camera.set_view_direction(handle.world_direction * -1.0F);
      harness.choose_handle();
      const auto runtime_camera = ViewportCameraAdapter::runtime_camera(
          harness.camera, harness.camera.viewport_size());
      const double world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
          runtime_camera, handle.world_position);
      ASSERT_TRUE(harness.frame(true, true, false).consumed);
      harness.move_by({0.0F, -30.0F});
      ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
      const double displacement = 60.0 * world_per_pixel *
          viewport_host_defaults().map_units_per_world_unit;
      EXPECT_NEAR(harness.session.extrude().amount(),
                  initial_amount + displacement, 1.0e-5);
      const auto preview = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(preview.has_value());
      EXPECT_EQ(preview->indices.size(), committed->indices.size());
      harness.move_by({0.0F, 30.0F});
      ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
      EXPECT_NEAR(harness.session.extrude().amount(),
                  initial_amount - displacement, 1.0e-5);
      harness.move_by({});
      ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
      EXPECT_NEAR(harness.session.extrude().amount(), initial_amount, 1.0e-5);
      ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
      auto current = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(current.has_value());
      expect_extrude_mesh_geometry(*current, *committed);
      ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
      ASSERT_TRUE(harness.editor.history().undo().ok());
      current = harness.editor.query().compiled_object_mesh(harness.object);
      ASSERT_TRUE(current.has_value());
      expect_extrude_mesh_geometry(*current, *source);
    }
  }
}

TEST(ExtrudeViewportInteractionTests, SelectedFaceKeepsOnePixelClickSelectionAndLargerDrag) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto handle = harness.session.extrude().handle_state();
  harness.camera.set_view_direction(handle.world_direction * -1.0F);
  ASSERT_TRUE(quader_editor_core::project_viewport_point(
      ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
      handle.world_position, harness.press));
  harness.press.x += build_extrude_handle_layout(harness.camera, handle)
                         .button_radius_pixels + 6.0F;
  harness.input.mouse_local = harness.press;
  harness.input.mouse_screen = harness.press;
  ASSERT_FALSE(ExtrudeViewportInteraction::handle_hit(
      harness.camera, handle, harness.press));
  const auto ray = harness.camera.ray_from_viewport_point(harness.press);
  ASSERT_TRUE(harness.session.extrude().selected_face_hit(
      {.ray = {.origin = ray.origin, .direction = ray.direction}}));
  EXPECT_TRUE(harness.frame(true, true, false).consumed);
  harness.move_by({0.0F, -1.0F});
  EXPECT_FALSE(harness.frame(false, true, false).command_attempted);
  EXPECT_TRUE(harness.frame(false, false, true).selection_click_requested);
  EXPECT_FALSE(harness.session.extrude().dragging());
  harness.move_by({});
  (void)harness.frame(true, true, false);
  harness.move();
  const auto dragged = harness.frame(false, true, false);
  ASSERT_TRUE(dragged.command_result.ok) << dragged.command_result.message;
  EXPECT_TRUE(harness.session.extrude().dragging());
}

TEST(ExtrudeViewportInteractionTests,
     DraggingLockedCapAdjustsPendingExtrusionWithoutAddingAnotherLayer) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kToolsActivateExtrude).ok());
  const auto committed = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(committed.has_value());
  harness.choose_selected_face();
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  EXPECT_FALSE(harness.frame(false, false, true).selection_click_requested);
  EXPECT_TRUE(harness.session.extrude().adjustable());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 2.15);
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move_by({0.0F, -20.0F});
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  EXPECT_GT(harness.session.extrude().amount(), 2.15);
  const auto adjusted = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(adjusted.has_value());
  EXPECT_EQ(adjusted->indices.size(), committed->indices.size());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto undone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(undone.has_value());
  expect_extrude_mesh_geometry(*undone, *source);
}

TEST(ExtrudeViewportInteractionTests,
     ShiftPrecisionScalesOnlyNewPointerDeltas) {
  ExtrudePointerHarness reference;
  reference.prepare_faces();
  ASSERT_TRUE(reference.frame(true, true, false).consumed);
  reference.move(60.0F);
  ASSERT_TRUE(reference.frame(false, true, false).command_result.ok);
  const double ordinary_60 = reference.session.extrude().amount();
  reference.move(90.0F);
  ASSERT_TRUE(reference.frame(false, true, false).command_result.ok);
  const double ordinary_90 = reference.session.extrude().amount();
  reference.move(120.0F);
  ASSERT_TRUE(reference.frame(false, true, false).command_result.ok);
  const double ordinary_120 = reference.session.extrude().amount();
  ASSERT_NE(ordinary_60, ordinary_90);
  ASSERT_NE(ordinary_90, ordinary_120);
  for (const auto modifier : {ViewportModifiers{.shift = true}}) {
    SCOPED_TRACE(modifier.primary);
    SCOPED_TRACE(modifier.shift);
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move(60.0F);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), ordinary_60, 1.0e-6);
    harness.input.modifiers = modifier;
    const auto precision_enabled = harness.frame(false, true, false);
    ASSERT_TRUE(precision_enabled.consumed);
    EXPECT_FALSE(precision_enabled.command_attempted);
    EXPECT_NEAR(harness.session.extrude().amount(), ordinary_60, 1.0e-6);
    harness.move(90.0F);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    const double precision_scale = modifier.shift ? 0.1 : 1.0;
    const double precise = ordinary_60 +
        (ordinary_90 - ordinary_60) * precision_scale;
    EXPECT_NEAR(harness.session.extrude().amount(), precise, 1.0e-5);
    harness.input.modifiers = {};
    const auto precision_disabled = harness.frame(false, true, false);
    ASSERT_TRUE(precision_disabled.consumed);
    EXPECT_FALSE(precision_disabled.command_attempted);
    EXPECT_NEAR(harness.session.extrude().amount(), precise, 1.0e-5);
    harness.move(120.0F);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(),
                precise + ordinary_120 - ordinary_90, 1.0e-5);
  }
}

TEST(ExtrudeViewportInteractionTests,
     CoalescedReleaseUsesEventTimePointerAndPrecisionModifiers) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  ASSERT_TRUE(harness.session.extrude().set_mode(
      modeling::FaceExtrudeMode::AlongNormals).ok);
  harness.camera.set_view_direction(
      harness.session.extrude().handle_state().world_direction * -1.0F);
  harness.choose_handle();
  harness.input.pointer_pressed_edges = {{
      .button = QuaderMouseButton::Left,
      .local = harness.press,
      .sequence = 40u,
  }};
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.input.pointer_pressed_edges.clear();
  harness.move_by({0.0F, -30.0F});
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  const double ordinary = harness.session.extrude().amount();
  ASSERT_GT(ordinary, 0.0);
  harness.input.modifiers.shift = true;
  const auto precision_enabled = harness.frame(false, true, false);
  ASSERT_TRUE(precision_enabled.consumed);
  EXPECT_FALSE(precision_enabled.command_attempted);
  EXPECT_NEAR(harness.session.extrude().amount(), ordinary, 1.0e-6);

  // Mouse release precedes both a later pointer move and Shift key-up in the
  // same frame; the captured release owns the final gesture sample.
  harness.move_by({0.0F, -90.0F});
  harness.input.modifiers = {};
  harness.input.pointer_released_edges = {{
      .button = QuaderMouseButton::Left,
      .local = harness.press + QVec2{0.0F, -60.0F},
      .sequence = 41u,
      .modifiers = ViewportModifiers{.shift = true},
  }};
  const auto released = harness.frame(false, false, true);
  ASSERT_TRUE(released.command_result.ok) << released.command_result.message;
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_TRUE(harness.session.extrude().adjustable());
  EXPECT_NEAR(harness.session.extrude().amount(), ordinary * 1.1, 1.0e-5);
}

TEST(ExtrudeViewportInteractionTests, GridAndAxisUseSemanticInput) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  (void)harness.frame(true, true, false);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  const double ordinary = harness.session.extrude().amount();
  ASSERT_NE(ordinary, 0.0);
  const QVec3 normal = harness.session.extrude().handle_state().world_direction;
  harness.input.tool_constraint_x_pressed = std::abs(normal.x) > 0.5F;
  harness.input.tool_constraint_y_pressed = std::abs(normal.y) > 0.5F;
  harness.input.tool_constraint_z_pressed = std::abs(normal.z) > 0.5F;
  harness.input.modifiers.primary = true;
  const auto constrained = harness.frame(false, true, false, true);
  ASSERT_TRUE(constrained.command_result.ok) << constrained.command_result.message;
  const double step = viewport_grid_world_size_for_preset(harness.input.grid_preset) *
                      viewport_host_defaults().map_units_per_world_unit;
  EXPECT_NEAR(harness.session.extrude().amount() / step,
              std::round(harness.session.extrude().amount() / step), 1.0e-6);
  harness.input.tool_constraint_x_pressed = false;
  harness.input.tool_constraint_y_pressed = false;
  harness.input.tool_constraint_z_pressed = false;
  EXPECT_TRUE(harness.session.extrude().dragging());
}

TEST(ExtrudeViewportInteractionTests,
     CtrlSnapsAbsoluteAmountAndCoalescedReleaseKeepsEventTimeCtrl) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_mode(
      modeling::FaceExtrudeMode::AlongNormals).ok);
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kToolsActivateExtrude).ok());
  harness.camera.set_view_direction(
      harness.session.extrude().handle_state().world_direction * -1.0F);
  harness.choose_handle();
  harness.input.pointer_pressed_edges = {{
      .button = QuaderMouseButton::Left, .local = harness.press, .sequence = 100u}};
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.input.pointer_pressed_edges.clear();
  harness.move_by({0.0F, -30.375F});
  ASSERT_TRUE(harness.frame(false, true, false, true).command_result.ok);
  const double free_amount = harness.session.extrude().amount();
  const double step = viewport_grid_world_size_for_preset(harness.input.grid_preset) *
      viewport_host_defaults().map_units_per_world_unit;
  const double snapped = std::round(free_amount / step) * step;
  ASSERT_GT(std::abs(free_amount - snapped), 1.0e-4);
  harness.input.modifiers.primary = true;
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  EXPECT_NEAR(harness.session.extrude().amount(), snapped, 1.0e-6);
  harness.input.modifiers.primary = false;
  ASSERT_TRUE(harness.frame(false, true, false, true).command_result.ok);
  EXPECT_NEAR(harness.session.extrude().amount(), free_amount, 1.0e-5);

  // Ctrl was held on mouse release, then released before the same frame drained.
  harness.move_by({0.0F, -200.0F});
  harness.input.pointer_released_edges = {{
      .button = QuaderMouseButton::Left,
      .local = harness.press + QVec2{0.0F, -60.75F},
      .sequence = 101u,
      .modifiers = ViewportModifiers{.primary = true}}};
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  const double release_free_amount = 2.15 + (free_amount - 2.15) * 2.0;
  const double release_snapped = std::round(release_free_amount / step) * step;
  ASSERT_GT(std::abs(release_free_amount - release_snapped), 1.0e-4);
  ASSERT_GT(release_snapped, 0.0);
  EXPECT_NEAR(harness.session.extrude().amount(), release_snapped, 1.0e-6);
  EXPECT_FALSE(harness.session.extrude().dragging());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  const auto undone = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(undone.has_value());
  expect_extrude_mesh_geometry(*undone, *source);
}

TEST(ExtrudeViewportInteractionTests,
     SmallFreeAdjustmentsIgnoreAutomaticSnappingAndVSnapsOnlyWhileHeld) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
    auto prospective_handle = harness.session.extrude().handle_state();
    harness.camera.set_view_direction(prospective_handle.world_direction * -1.0F);
    prospective_handle.world_position = prospective_handle.world_position +
        prospective_handle.world_direction *
            static_cast<float>(2.15 / viewport_host_defaults().map_units_per_world_unit);
    const auto prospective_layout = build_extrude_handle_layout(
        harness.camera, prospective_handle);
    ASSERT_TRUE(prospective_layout.valid);
    const auto target_ray = harness.camera.ray_from_viewport_point(
        prospective_layout.button_center_screen + QVec2{0.0F, -31.125F});
    const QVec3 target_corner = target_ray.origin + target_ray.direction * 4.0F;
    ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Object).ok());
    const auto target = harness.editor.modeling().create_box(
        {.min = target_corner, .max = target_corner + QVec3{0.05F, 0.05F, 0.05F}},
        "Explicit vertex snap target");
    ASSERT_TRUE(target.ok());
    ASSERT_TRUE(harness.editor.selection().select_object(harness.object).ok());
    ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    ASSERT_TRUE(harness.editor.selection().select_face(harness.object, 1).ok());
    ASSERT_TRUE(harness.editor.commands().execute(quader_command::kToolsActivateExtrude).ok());
    ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
    ASSERT_TRUE(harness.session.extrude().preview_active());
    ASSERT_DOUBLE_EQ(harness.session.extrude().amount(), 2.15);
    const auto handle = harness.session.extrude().handle_state();
    harness.camera.set_view_direction(handle.world_direction * -1.0F);
    harness.choose_handle();
    const double world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
        ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
        handle.adjustment_origin);
    const double units = viewport_host_defaults().map_units_per_world_unit;
    const auto queries_before = harness.session.modeling().mesh_snap_query_count();
    ASSERT_TRUE(harness.frame(true, true, false, true, true, 0.15).consumed);
    double previous = 2.15;
    for (int index = 0; index < 10; ++index) {
      const float pixels = 30.0F + static_cast<float>(index) * 0.125F;
      harness.move_by({0.0F, -pixels});
      ASSERT_TRUE(harness.frame(false, true, false, true, true, 0.15).command_result.ok);
      const double amount = harness.session.extrude().amount();
      EXPECT_GT(amount, previous);
      EXPECT_NEAR(amount, 2.15 + 2.0 * world_per_pixel * pixels * units, 1.0e-4);
      previous = amount;
    }
    EXPECT_EQ(harness.session.modeling().mesh_snap_query_count(), queries_before);
    const auto ray = harness.camera.ray_from_viewport_point(harness.input.mouse_local);
    const auto candidate = harness.session.modeling().pick_mesh_vertex_snap_target(
        {.origin = ray.origin, .direction = ray.direction}, 0.15, true);
    ASSERT_TRUE(candidate.has_value());
    const double snapped = 2.15 + dot(candidate->world_position - handle.adjustment_origin,
                                      handle.adjustment_direction) * units;
    ASSERT_GT(std::abs(snapped - previous), 1.0e-4);
    const auto explicit_queries = harness.session.modeling().mesh_snap_query_count();
    harness.input.transform_vertex_snap_down = true;
    ASSERT_TRUE(harness.frame(false, true, false, true, true, 0.15).command_result.ok);
    EXPECT_GT(harness.session.modeling().mesh_snap_query_count(), explicit_queries);
    EXPECT_NEAR(harness.session.extrude().amount(), snapped, 1.0e-4);
    harness.input.transform_vertex_snap_down = false;
    ASSERT_TRUE(harness.frame(false, true, false, true, true, 0.15).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), previous, 1.0e-4);
    // Mouse-up owns its held state even if the key transition drains later.
    // Individual also verifies legacy edges without an event-time snapshot.
    const bool snap_on_release = mode != modeling::FaceExtrudeMode::AlongNormals;
    harness.input.transform_vertex_snap_down = mode != modeling::FaceExtrudeMode::Region;
    harness.input.pointer_released_edges = {{
        .button = QuaderMouseButton::Left, .local = harness.input.mouse_local}};
    if (mode != modeling::FaceExtrudeMode::Individual) {
      harness.input.pointer_released_edges.front().held_input =
          ViewportPointerHeldInputState{.transform_vertex_snap_down = snap_on_release};
    }
    ASSERT_TRUE(harness.frame(false, false, true, true, true, 0.15).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(),
                snap_on_release ? snapped : previous, 1.0e-4);
  }
}

TEST(ExtrudeViewportInteractionTests,
     RegionDragCrossesVanishingPointWithoutSignFlipAndReturnsToSource) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source_handle = harness.session.extrude().handle_state();
  const auto normal = source_handle.world_direction;
  constexpr float kHalfPi = 1.5707963268F;
  QVec3 rotation{};
  if (std::abs(normal.y) > 0.5F) {
    rotation.x = normal.y > 0.0F ? kHalfPi : -kHalfPi;
  } else if (std::abs(normal.x) > 0.5F) {
    rotation.y = normal.x > 0.0F ? -kHalfPi : kHalfPi;
  } else if (normal.z < 0.0F) {
    rotation.y = 2.0F * kHalfPi;
  }
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(harness.editor.scene().set_transform(
      harness.object.entity, {.rotation_euler = rotation}).ok());
  const QVec3 rotated_center = transform_point(
      harness.session.scene().world_transform_matrix(harness.object.entity),
      source_handle.world_position);
  ASSERT_TRUE(harness.editor.scene().set_transform(
      harness.object.entity,
      {.location = QVec3{1.0F, 0.0F, 0.0F} - rotated_center,
       .rotation_euler = rotation}).ok());
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().select_face(harness.object, 1).ok());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Extrude).ok());
  const auto handle = harness.session.extrude().handle_state();
  ASSERT_NEAR(handle.world_direction.z, 1.0F, 1.0e-6F);
  ASSERT_NEAR(handle.world_position.x, 1.0F, 1.0e-6F);
  ASSERT_NEAR(handle.world_position.z, 0.0F, 1.0e-6F);
  ASSERT_TRUE(harness.camera.restore_perspective_pose({
      .eye = {0.0F, 0.0F, 10.0F},
      .target = {0.0F, 0.0F, 0.0F},
      .up = {0.0F, 1.0F, 0.0F}}));
  harness.choose_handle();
  const auto before = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(before.has_value());
  const auto history_before = harness.editor.history().snapshot();
  const double world_per_pixel = quader_editor_core::viewport_world_units_per_pixel(
      ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
      handle.world_position);
  const double displayed_per_pixel = 10.0 * world_per_pixel *
      viewport_host_defaults().map_units_per_world_unit;
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  double previous_amount = 0.0;
  for (const float pixels : {-20.0F, -40.0F, -60.0F, -80.0F, -100.0F}) {
    SCOPED_TRACE(pixels);
    harness.move_by({pixels, 0.0F});
    const auto sample = harness.frame(false, true, false);
    ASSERT_TRUE(sample.command_result.ok) << sample.command_result.message;
    const double amount = harness.session.extrude().amount();
    EXPECT_TRUE(std::isfinite(amount));
    EXPECT_LT(amount, previous_amount);
    EXPECT_NEAR(amount, pixels * displayed_per_pixel, 0.001);
    previous_amount = amount;
  }
  EXPECT_LT(harness.input.mouse_local.x, 400.0F);
  harness.move_by({100.0F, 0.0F});
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  EXPECT_NEAR(harness.session.extrude().amount(), -previous_amount, 0.001);
  harness.move_by({});
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 0.0);
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  const auto restored = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(restored.has_value());
  expect_extrude_mesh_geometry(*restored, *before);
  EXPECT_EQ(harness.editor.history().snapshot().undo_label, history_before.undo_label);
}

TEST(ExtrudeViewportInteractionTests,
     PerpendicularFacesFollowTheVisibleGizmoInEveryModeAndCameraDirection) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    for (const float view_angle : {90.0F, 2.0F, 178.0F}) {
      for (const QVec2 screen_direction : {QVec2{0.0F, 1.0F},
                                            QVec2{0.0F, -1.0F},
                                            QVec2{1.0F, 0.0F},
                                            QVec2{-1.0F, 0.0F}}) {
        SCOPED_TRACE(static_cast<int>(mode));
        SCOPED_TRACE(view_angle);
        SCOPED_TRACE(screen_direction.x);
        SCOPED_TRACE(screen_direction.y);
        ExtrudePointerHarness harness;
        harness.prepare_corner_faces();
        ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
        const auto initial_handle = harness.session.extrude().handle_state();
        const QVec3 normal = normalize(QVec3{0.0F, 1.0F, 1.0F});
        ASSERT_NEAR(dot(initial_handle.world_direction, normal), 1.0F, 1.0e-6F);
        const QVec3 sideways{1.0F, 0.0F, 0.0F};
        const QVec3 perpendicular = normalize(cross(normal, sideways));
        const float radians = view_angle * 0.01745329252F;
        const QVec3 toward_camera = normal * std::cos(radians) +
                                    sideways * std::sin(radians);
        const QVec3 projected_normal = normalize(
            normal - toward_camera * dot(normal, toward_camera));
        const QVec3 camera_up = screen_direction.y != 0.0F
            ? projected_normal * -screen_direction.y
            : perpendicular * -screen_direction.x;
        ASSERT_TRUE(harness.camera.restore_perspective_pose({
            .eye = initial_handle.world_position + toward_camera * 6.0F,
            .target = initial_handle.world_position,
            .up = camera_up}));
        harness.choose_handle();
        const auto layout = build_extrude_handle_layout(
            harness.camera, initial_handle);
        ASSERT_TRUE(layout.valid);
        const QVec2 projected_stem =
            layout.button_center_screen - layout.pivot_screen;
        const float stem_length = std::hypot(projected_stem.x, projected_stem.y);
        ASSERT_GT(stem_length, 1.0F);
        const QVec2 outward = projected_stem / stem_length;
        EXPECT_NEAR(outward.x, screen_direction.x, 0.001F);
        EXPECT_NEAR(outward.y, screen_direction.y, 0.001F);
        const auto source = harness.editor.query().compiled_object_mesh(harness.object);
        ASSERT_TRUE(source.has_value());
        const QVec3 source_face_center = selected_face_center(
            harness.session.modeling().selection_overlay_frame());
        const auto history_revision = harness.session.history().revision();
        ASSERT_TRUE(harness.frame(true, true, false).consumed);
        double first_amount = 0.0;
        double previous = 0.0;
        for (const float pixels : {10.0F, 20.0F, 30.0F}) {
          harness.move_by(outward * pixels);
          const auto sample = harness.frame(false, true, false);
          ASSERT_TRUE(sample.command_result.ok) << sample.command_result.message;
          const double amount = harness.session.extrude().amount();
          ASSERT_GT(amount, previous);
          if (pixels == 10.0F) {
            first_amount = amount;
          }
          EXPECT_NEAR(amount, first_amount * pixels / 10.0, 0.0001);
          const QVec3 cap_center = selected_face_center(
              harness.session.modeling().selection_overlay_frame());
          EXPECT_GT(dot(cap_center - source_face_center, normal), 0.0F);
          const auto preview = harness.editor.query().compiled_object_mesh(harness.object);
          ASSERT_TRUE(preview.has_value());
          EXPECT_GT(preview->indices.size(), source->indices.size());
          previous = amount;
        }
        if (view_angle != 90.0F) {
          EXPECT_NEAR(first_amount,
                      20.0 * layout.world_units_per_pixel *
                          viewport_host_defaults().map_units_per_world_unit,
                      0.0001);
        }
        harness.move_by(outward * 10.0F);
        ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
        EXPECT_NEAR(harness.session.extrude().amount(), first_amount, 0.0001);
        for (const float pixels : {-10.0F, -20.0F}) {
          harness.move_by(outward * pixels);
          ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
          EXPECT_NEAR(harness.session.extrude().amount(),
                      first_amount * pixels / 10.0, 0.0001);
          const QVec3 cap_center = selected_face_center(
              harness.session.modeling().selection_overlay_frame());
          EXPECT_LT(dot(cap_center - source_face_center, normal), 0.0F);
        }
        harness.move_by({});
        ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
        EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 0.0);
        ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
        EXPECT_TRUE(harness.session.extrude().preview_active());
        EXPECT_EQ(harness.session.history().revision(), history_revision);
        const auto restored = harness.editor.query().compiled_object_mesh(harness.object);
        ASSERT_TRUE(restored.has_value());
        expect_extrude_mesh_geometry(*restored, *source);
      }
    }
  }
}

TEST(ExtrudeViewportInteractionTests,
     PerpendicularFacesFollowTheProjectedStemInAnOrthographicSideView) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ExtrudePointerHarness harness;
    harness.prepare_corner_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
    harness.camera.view_axis(ViewportViewAxis::X, 1);
    harness.camera.focus_origin(6.0F);
    harness.choose_handle();
    const auto layout = build_extrude_handle_layout(
        harness.camera, harness.session.extrude().handle_state());
    const QVec2 stem = layout.button_center_screen - layout.pivot_screen;
    const QVec2 outward = stem / std::hypot(stem.x, stem.y);
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move_by(outward * 20.0F);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    const double amount = harness.session.extrude().amount();
    ASSERT_GT(amount, 0.0);
    harness.move_by(outward * 10.0F);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), amount * 0.5, 0.0001);
    harness.move_by({});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 0.0);
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  }
}

TEST(ExtrudeViewportInteractionTests,
     EveryModeUsesVerticalFallbackWhenTheHandleAxisHasNoScreenDirection) {
  for (const auto mode : {modeling::FaceExtrudeMode::Region,
                          modeling::FaceExtrudeMode::AlongNormals,
                          modeling::FaceExtrudeMode::Individual}) {
    SCOPED_TRACE(static_cast<int>(mode));
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    ASSERT_TRUE(harness.session.extrude().set_mode(mode).ok);
    harness.camera.set_view_direction(
        harness.session.extrude().handle_state().world_direction * -1.0F);
    harness.choose_handle();
    const auto before = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(before.has_value());
    const auto history_before = harness.editor.history().snapshot();
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move_by({60.0F, 0.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    ASSERT_TRUE(harness.session.extrude().dragging());
    EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 0.0);
    harness.move_by({60.0F, -30.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    const double upward = harness.session.extrude().amount();
    ASSERT_GT(upward, 0.0);
    harness.move_by({120.0F, -30.0F});
    const auto horizontal = harness.frame(false, true, false);
    ASSERT_TRUE(horizontal.consumed);
    EXPECT_FALSE(horizontal.command_attempted);
    EXPECT_NEAR(harness.session.extrude().amount(), upward, 1.0e-6);
    harness.move_by({60.0F, -60.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), upward * 2.0, 1.0e-5);
    harness.move_by({60.0F, 0.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), 0.0, 1.0e-6);
    harness.move_by({60.0F, 30.0F});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), -upward, 1.0e-5);
    harness.move_by({});
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    EXPECT_NEAR(harness.session.extrude().amount(), 0.0, 1.0e-6);
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
    const auto restored = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(restored.has_value());
    expect_extrude_mesh_geometry(*restored, *before);
    EXPECT_EQ(harness.editor.history().snapshot().undo_label,
              history_before.undo_label);
  }
}

TEST(ExtrudeViewportInteractionTests,
     RepeatedDiskAndSettingsEditsStayPendingUntilApplyStartsANewStep) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  ASSERT_TRUE(harness.session.extrude().adjustable());
  const auto undo_count = harness.session.history().undo_count();
  const auto first = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "extrude", "extrude.amount", 3.25).ok());
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "extrude", "extrude.amount", 4.5).ok());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 4.5);
  for (const float pixels : {25.0F, 15.0F}) {
    harness.choose_handle();
    ASSERT_TRUE(harness.frame(true, true, false).consumed);
    harness.move(pixels);
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
    const auto adjusted = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(adjusted.has_value());
    EXPECT_EQ(adjusted->indices.size(), first->indices.size());
  }
  const auto first_final = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(first_final.has_value());
  ASSERT_TRUE(harness.editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  EXPECT_EQ(harness.session.history().undo_count(), undo_count);
  const auto repeated = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(repeated.has_value());
  expect_extrude_mesh_geometry(*repeated, *first_final);
  const double first_amount = harness.session.extrude().amount();
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), first_amount);
  EXPECT_FALSE(harness.session.extrude().adjustable());
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "extrude", "extrude.amount", 2.15).ok());
  const auto second_initial = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(second_initial.has_value());
  EXPECT_GT(second_initial->indices.size(), first_final->indices.size());
  harness.choose_handle();
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move(25.0F);
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  const auto second = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->indices.size(), second_initial->indices.size());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  auto current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *first_final);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *source);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *first_final);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *second);
}

TEST(ExtrudeViewportInteractionTests,
     CancelingANewDiskDragPreservesAppliedGeometryAndNextAmountStartsANewStep) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.session.extrude().set_amount(2.15).ok);
  ASSERT_TRUE(harness.editor.commands().execute(
      quader_command::kToolsActivateExtrude).ok());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  const auto committed = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(committed.has_value());
  harness.choose_handle();
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move(35.0F);
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.session.extrude().dragging());
  harness.result.cancel_requested = true;
  ASSERT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_FALSE(harness.session.extrude().adjustable());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 2.15);
  auto current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *committed);
  harness.result.cancel_requested = false;
  EXPECT_TRUE(harness.frame(false, false, true).consumed);

  ASSERT_TRUE(harness.editor.tools().set_setting(
      "extrude", "extrude.amount", 4.5).ok());
  const auto adjusted = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(adjusted.has_value());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), 4.5);
  EXPECT_GT(adjusted->indices.size(), committed->indices.size());
  harness.choose_handle();
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move(20.0F);
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.frame(false, false, true).command_result.ok);
  EXPECT_TRUE(harness.session.extrude().adjustable());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *committed);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *source);
}

TEST(ExtrudeViewportInteractionTests, ControllerRetainsDeferredDoubleClickProvenance) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto handle = harness.session.extrude().handle_state();
  harness.camera.set_view_direction(handle.world_direction * -1.0F);
  ASSERT_TRUE(quader_editor_core::project_viewport_point(
      ViewportCameraAdapter::runtime_camera(harness.camera, harness.camera.viewport_size()),
      handle.world_position, harness.press));
  harness.press.x += build_extrude_handle_layout(harness.camera, handle)
                         .button_radius_pixels + 6.0F;
  harness.input.mouse_local = harness.press;
  harness.input.mouse_screen = harness.press;
  ViewportInteractionController controller;
  ViewportInputRouter router;
  router.set_active_tool(ViewportToolId::Extrude);
  ViewportSettings settings;
  const auto dispatch = [&](bool pressed, bool down, bool released) {
    const auto surfaces = harness.session.scene_surface_query().snapshot();
    const ViewportInteractionFrame interaction{
        .input_router = router,
        .camera = harness.camera,
        .settings = settings,
        .input = harness.input,
        .input_result = harness.result,
        .left_click_local = harness.press,
        .left_click_count = 2,
        .left_pressed = pressed,
        .left_down = down,
        .left_released = released,
        .active_tool_allows_selection_fallback = true,
        .scene_surfaces = &surfaces,
        .selection_state = {.summary = harness.editor.selection().summary(),
                            .scene_revision = harness.session.scene().revision(),
                            .asset_revision = harness.session.assets().revision()}};
    const ViewportNativeToolInteractionFrame native{
        .input = harness.input,
        .input_result = harness.result,
        .left_pressed = pressed,
        .left_down = down,
        .left_released = released,
        .left_click_local = harness.press};
    return controller.handle_tool_pointer_lifecycle(
        harness.editor, harness.session.scene(), harness.session.assets(),
        harness.session.native_tools(), &harness.session.point_placement(),
        harness.camera, {.interaction = interaction, .native_tool = native});
  };
  ASSERT_TRUE(dispatch(true, true, false).native_tool.consumed);
  const auto released = dispatch(false, false, true);
  ASSERT_TRUE(released.before_tool.selection_click_requested);
  EXPECT_EQ(released.before_tool.selection_click_count, 2);
  EXPECT_FLOAT_EQ(released.before_tool.selection_click_local.x, harness.press.x);
  EXPECT_FLOAT_EQ(released.before_tool.selection_click_local.y, harness.press.y);
}

TEST(ExtrudeViewportInteractionTests, FailedReleaseUpdateRetainsTheLastValidPendingPreview) {
  ExtrudePointerHarness harness;
  harness.prepare_faces();
  const auto before = harness.editor.query().mesh(harness.object.mesh);
  ASSERT_TRUE(before);
  (void)harness.frame(true, true, false);
  harness.move();
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.session.extrude().dragging());
  const auto accepted = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(accepted.has_value());
  const double accepted_amount = harness.session.extrude().amount();
  harness.input.mouse_local = {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::quiet_NaN()};
  const auto released = harness.frame(false, false, true);
  EXPECT_FALSE(released.command_result.ok);
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_TRUE(harness.session.extrude().active());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), accepted_amount);
  EXPECT_TRUE(harness.session.extrude().preview_active());
  const auto current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *accepted);
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(harness.editor.query().mesh(harness.object.mesh)->index_count,
            before->index_count);
}

TEST(ExtrudeViewportInteractionTests,
     InvalidFiniteSampleRetainsPreviewAfterReleaseAndIsNotRetriedUnchanged) {
  ExtrudePointerHarness harness;
  harness.prepare_faces({.rotation_euler = {0.3F, 0.4F, 0.5F}});
  ASSERT_TRUE(harness.session.extrude().set_mode(
      modeling::FaceExtrudeMode::AlongNormals).ok);
  harness.choose_handle();
  const auto source = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(source.has_value());
  ASSERT_TRUE(harness.frame(true, true, false).consumed);
  harness.move_by({0.0F, -30.0F});
  ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
  ASSERT_TRUE(harness.session.extrude().dragging());
  const auto accepted = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(accepted.has_value());
  const double accepted_amount = harness.session.extrude().amount();
  harness.move_by({0.0F, -1.0e20F});
  EXPECT_TRUE(harness.frame(false, true, false).consumed);
  EXPECT_TRUE(harness.session.extrude().dragging());
  EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), accepted_amount);
  auto current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *accepted);
  const auto rejected_work = harness.session.extrude().work_stats_for_tests();
  for (int frame = 0; frame < 4; ++frame) {
    EXPECT_TRUE(harness.frame(false, true, false).consumed);
  }
  EXPECT_EQ(harness.session.extrude().work_stats_for_tests().geometry_update_count,
            rejected_work.geometry_update_count);
  const auto released = harness.frame(false, false, true);
  EXPECT_FALSE(released.command_result.ok);
  EXPECT_FALSE(harness.session.extrude().dragging());
  EXPECT_TRUE(harness.session.extrude().active());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *accepted);
  EXPECT_TRUE(harness.session.extrude().preview_active());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  current = harness.editor.query().compiled_object_mesh(harness.object);
  ASSERT_TRUE(current.has_value());
  expect_extrude_mesh_geometry(*current, *source);
}

TEST(ExtrudeViewportInteractionTests, InterruptedDragRetainsGeometryAndFocusAloneKeepsGesture) {
  for (int interruption = 0; interruption < 5; ++interruption) {
    SCOPED_TRACE(interruption);
    ExtrudePointerHarness harness;
    harness.prepare_faces();
    const auto before = harness.editor.query().mesh(harness.object.mesh);
    ASSERT_TRUE(before);
    (void)harness.frame(true, true, false);
    harness.move();
    ASSERT_TRUE(harness.frame(false, true, false).command_result.ok);
    ASSERT_TRUE(harness.session.extrude().dragging());
    const auto accepted = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(accepted.has_value());
    const double accepted_amount = harness.session.extrude().amount();
    const auto history_before = harness.session.history().undo_count();
    harness.input.navigation_active = interruption == 0;
    harness.input.viewport_focused = interruption != 1;
    harness.input.text_input_active = interruption == 2;
    harness.input.pointer_interaction_canceled = interruption == 4;
    const bool held = interruption != 3;
    harness.move_by({300.0f, 300.0f});
    EXPECT_TRUE(harness.frame(false, held, false).consumed);
    if (interruption == 1) {
      EXPECT_TRUE(harness.session.extrude().dragging());
      EXPECT_EQ(harness.session.history().undo_count(), history_before);
      EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), accepted_amount);
      EXPECT_TRUE(harness.frame(false, false, true).consumed);
    }
    EXPECT_FALSE(harness.session.extrude().dragging());
    EXPECT_TRUE(harness.session.extrude().active());
    EXPECT_TRUE(harness.session.extrude().preview_active());
    EXPECT_DOUBLE_EQ(harness.session.extrude().amount(), accepted_amount);
    const auto current = harness.editor.query().compiled_object_mesh(harness.object);
    ASSERT_TRUE(current.has_value());
    expect_extrude_mesh_geometry(*current, *accepted);
    EXPECT_EQ(harness.session.history().undo_count(), history_before);
    ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
    EXPECT_EQ(harness.session.history().undo_count(), history_before + 1u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.editor.query().mesh(harness.object.mesh)->index_count,
              before->index_count);
  }
}

TEST(ExtrudeViewportInteractionTests, HandleProjectionAndRetainedStampTrackCameraAndOwner) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0F, 600.0F);
  camera.focus_origin(6.0F);
  camera.set_view_direction({0.0F, 0.0F, -1.0F});
  ExtrudeHandleState handle{.active = true,
                            .world_direction = {1.0F, 0.0F, 0.0F},
                            .revision = 1};
  QVec2 endpoint;
  ASSERT_TRUE(quader_editor_core::project_viewport_point(
      ViewportCameraAdapter::runtime_camera(camera, camera.viewport_size()),
      ExtrudeViewportInteraction::handle_endpoint(camera, handle), endpoint));
  EXPECT_TRUE(ExtrudeViewportInteraction::handle_hit(camera, handle, endpoint));
  EXPECT_FALSE(ExtrudeViewportInteraction::handle_hit(
      camera, handle, endpoint + QVec2{0.0F, 20.0F}));
  auto overlay_camera = ViewportCameraAdapter::overlay_camera(camera, camera.viewport_size());
  ViewportToolOverlaySourceInputs inputs{
      .overlay_camera = &overlay_camera,
      .viewport_camera = &camera,
      .extrusion_handle = &handle};
  const auto initial = ViewportToolOverlaySource(inputs).source_stamp();
  EXPECT_EQ(initial.publication_revision,
            ViewportToolOverlaySource(inputs).source_stamp().publication_revision);
  ++handle.revision;
  EXPECT_NE(initial.publication_revision,
            ViewportToolOverlaySource(inputs).source_stamp().publication_revision);
  const auto before_highlight = ViewportToolOverlaySource(inputs).source_stamp();
  handle.new_hovered = true;
  EXPECT_NE(before_highlight.publication_revision,
            ViewportToolOverlaySource(inputs).source_stamp().publication_revision);
  const auto overlay = ViewportToolOverlaySource(inputs).build_dynamic_tool_overlay_frame();
  EXPECT_FALSE(overlay.lines.empty());
  EXPECT_FALSE(overlay.triangles.empty());
  handle.active = false;
  EXPECT_TRUE(ViewportToolOverlaySource(inputs).build_dynamic_tool_overlay_frame().lines.empty());
}

} // namespace
} // namespace quader
