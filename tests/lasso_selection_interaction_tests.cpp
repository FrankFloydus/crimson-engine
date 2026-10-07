////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <gtest/gtest.h>

#include <array>
#include <vector>

namespace {

/// Runs real selection publication with canonical ordered pointer input.
class LassoSelectionHarness final {
public:
  quader::SceneSession session;
  quader::QuaderEditorApi editor{
      quader::QuaderApiSessionInternalAccess::from_scene_session(session)};
  quader::ViewportInputRouter router;
  quader::ViewportCameraController camera;
  quader::ViewportSettings settings;
  quader::ViewportInteractionController controller;
  quader::ViewportInputFrame input;
  quader::ViewportInputResult result;
  quader::QVec2 press_point{};
  std::uint64_t sequence = 0;
  std::uint64_t press_sequence = 0;
  float pixel_scale = 1.0f;
  int click_count = 1;
  double time_seconds = 10.0;
  std::vector<quader::QuaderInputChord> suppressed_chords;
  double linear_array_wheel_remainder = 0.0;
  double radial_array_wheel_remainder = 0.0;

  LassoSelectionHarness() {
    camera.set_viewport_size(800.0f, 600.0f);
    camera.focus_origin(8.0f);
    camera.set_view_direction({0.0f, 0.0f, -1.0f});
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    router.set_active_tool(quader::ViewportToolId::LassoSelect);
    EXPECT_TRUE(editor.tools().activate("lasso_select").ok());
    settings.set_bool_value(quader::ViewportBoolSettingId::BackfaceSelection,
                            true);
  }

  quader::QuaderModelingObjectRef cube(quader::QVec3 center = {}) {
    const quader::QVec3 half{1.0f, 1.0f, 1.0f};
    const auto created = editor.modeling().create_box(
        {.min = center - half, .max = center + half}, "Lasso cube");
    EXPECT_TRUE(created.ok()) << created.error_message();
    EXPECT_TRUE(editor.selection().clear().ok());
    return created.value();
  }

  void dispatch(bool pressed, bool down, bool released) {
    input.active_transform_drag = controller.dragging();
    const auto surfaces = session.scene_surface_query().snapshot();
    const quader::ViewportInteractionFrame frame{
        .input_router = router,
        .camera = camera,
        .settings = settings,
        .input = input,
        .input_result = result,
        .left_click_local = press_point,
        .left_click_count = click_count,
        .left_pressed = pressed,
        .left_down = down,
        .left_released = released,
        .viewport_pixel_scale = pixel_scale,
        .monotonic_time_seconds = time_seconds,
        .active_tool_allows_selection_fallback =
            controller.active_tool_allows_selection_fallback(
                router.state().active_tool),
        .scene_surfaces = &surfaces,
        .selection_state = {
            .summary = editor.selection().summary(),
            .scene_revision = session.scene().revision(),
            .asset_revision = session.assets().revision(),
        },
    };
    const auto before = controller.handle_before_active_tool(editor, frame);
    controller.handle_after_active_tool(editor, frame, {}, before);
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    input.pointer_motion_samples.clear();
  }

  void queue_press(quader::QVec2 point,
                   quader::ViewportModifiers modifiers = {}) {
    press_point = point;
    input.mouse_local = point;
    input.modifiers = modifiers;
    press_sequence = ++sequence;
    input.pointer_pressed_edges.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = press_sequence,
        .modifiers = modifiers,
    });
    input.pointer_motion_samples.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = sequence,
        .press_sequence = press_sequence,
    });
  }

  /// Follows production keyboard dispatch before the selection lifecycle.
  void resolve_and_dispatch(bool pressed = false, bool down = false,
                            bool released = false) {
    input.active_transform_drag = controller.dragging();
    input.keybinding_input_consumed = false;
    quader::ViewportKeybindingResolver::resolve(
        editor, router.state(), input, suppressed_chords,
        linear_array_wheel_remainder, radial_array_wheel_remainder);
    result = router.handle_frame(input, camera);
    dispatch(pressed, down, released);
    input.pressed_input_chords.clear();
    input.released_input_chords.clear();
  }

  void queue_move(quader::QVec2 point) {
    input.mouse_local = point;
    input.pointer_motion_samples.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = ++sequence,
        .press_sequence = press_sequence,
    });
  }

  void queue_release(quader::QVec2 point) {
    queue_move(point);
    input.pointer_released_edges.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = sequence,
    });
  }

  void loop(quader::QVec2 start = {250.0f, 150.0f},
            quader::QVec2 end = {550.0f, 450.0f},
            quader::ViewportModifiers modifiers = {}) {
    queue_press(start, modifiers);
    queue_move({end.x, start.y});
    queue_move(end);
    queue_move({start.x, end.y});
    queue_release(start);
    dispatch(true, false, true);
  }

  std::vector<quader::EntityId> selected_entities() const {
    std::vector<quader::EntityId> selected;
    for (const auto &entity : editor.query().entities()) {
      if (entity.selected) selected.push_back(entity.id);
    }
    return selected;
  }
};

TEST(LassoSelectionInteractionTests,
     CompleteLoopWithinOneFrameSelectsAndPreservesFinalPath) {
  LassoSelectionHarness harness;
  const auto cube = harness.cube();
  harness.loop();
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{cube.entity});
  const auto &marquee = harness.controller.selection_marquee();
  ASSERT_TRUE(marquee.active);
  ASSERT_EQ(marquee.lasso_points.size(), 5u);
  EXPECT_EQ(marquee.lasso_points.front().x, marquee.lasso_points.back().x);
  EXPECT_EQ(marquee.lasso_points.front().y, marquee.lasso_points.back().y);
  EXPECT_FALSE(harness.controller.dragging());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(harness.selected_entities().empty());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{cube.entity});
  harness.dispatch(false, false, false);
  EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(LassoSelectionInteractionTests,
     MotionOnlyChangesMarqueeAndStationaryFramesRetainItsStorage) {
  LassoSelectionHarness harness;
  (void)harness.cube();
  const auto revision = harness.editor.selection().summary().selection_revision;
  harness.queue_press({250.0f, 150.0f});
  harness.dispatch(true, true, false);
  harness.queue_move({550.0f, 150.0f});
  harness.queue_move({550.0f, 450.0f});
  harness.dispatch(false, true, false);
  ASSERT_TRUE(harness.controller.selection_marquee().active);
  const auto &marquee = harness.controller.selection_marquee();
  const auto *storage = marquee.lasso_points.data();
  const auto path_revision = marquee.path_revision;
  for (int frame = 0; frame < 8; ++frame) {
    harness.dispatch(false, true, false);
    EXPECT_EQ(harness.controller.selection_marquee().lasso_points.data(), storage);
    EXPECT_EQ(harness.controller.selection_marquee().path_revision, path_revision);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision, revision);
  }
  harness.queue_move({250.0f, 450.0f});
  harness.queue_release({250.0f, 150.0f});
  harness.dispatch(false, false, true);
  EXPECT_EQ(harness.selected_entities().size(), 1u);
}

TEST(LassoSelectionInteractionTests,
     FrozenPressModifiersAddAndRemoveDespiteLaterModifierChanges) {
  LassoSelectionHarness harness;
  const auto center = harness.cube();
  const auto side = harness.cube({-5.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(side).ok());
  harness.queue_press({250.0f, 150.0f}, {.shift = true});
  harness.queue_move({550.0f, 150.0f});
  harness.queue_move({550.0f, 450.0f});
  harness.queue_move({250.0f, 450.0f});
  harness.queue_release({250.0f, 150.0f});
  harness.input.modifiers = {.primary = true};
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities().size(), 2u);
  harness.loop({250.0f, 150.0f}, {550.0f, 450.0f}, {.primary = true});
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{side.entity});
  harness.loop();
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{center.entity});
}

TEST(LassoSelectionInteractionTests, PartialIntersectionIsFrozenAtPress) {
  LassoSelectionHarness harness;
  const auto cube = harness.cube();
  harness.queue_press({380.0f, 280.0f});
  harness.dispatch(true, true, false);
  ASSERT_TRUE(harness.editor.tools().set_setting(
      "lasso_select", "select_partial_intersection", true).ok());
  harness.queue_move({420.0f, 280.0f});
  harness.queue_move({420.0f, 320.0f});
  harness.queue_move({380.0f, 320.0f});
  harness.queue_release({380.0f, 280.0f});
  harness.dispatch(false, false, true);
  EXPECT_TRUE(harness.selected_entities().empty());
  EXPECT_EQ(harness.controller.selection_marquee().rule,
            quader::ModelingBoxSelectionRule::Contain);
  harness.loop({380.0f, 280.0f}, {420.0f, 320.0f});
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{cube.entity});
  EXPECT_EQ(harness.controller.selection_marquee().rule,
            quader::ModelingBoxSelectionRule::Crossing);
}

TEST(LassoSelectionInteractionTests, StationaryClickKeepsOrdinaryPicking) {
  LassoSelectionHarness harness;
  const auto cube = harness.cube();
  harness.queue_press({400.0f, 300.0f});
  harness.queue_release({400.0f, 300.0f});
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{cube.entity});
  EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(LassoSelectionInteractionTests,
     OrthographicMultiFrameClickAndDepthDoubleClickKeepOrdinaryPicking) {
  for (const bool depth_double : {false, true}) {
    LassoSelectionHarness harness;
    const auto front = harness.cube({0.0f, 0.0f, 2.0f});
    const auto back = harness.cube({0.0f, 0.0f, -2.0f});
    harness.camera.view_axis(quader::ViewportViewAxis::Z, 1);
    harness.input.selection_click_has_depth_double_click = depth_double;
    harness.queue_press({400.0f, 300.0f});
    harness.dispatch(true, true, false);
    harness.queue_release({400.0f, 300.0f});
    harness.time_seconds = 10.1;
    harness.dispatch(false, false, true);
    if (depth_double) {
      EXPECT_EQ(harness.selected_entities(),
                std::vector<quader::EntityId>{front.entity});
      harness.click_count = 2;
      harness.input.cycle_selection_depth_pressed = true;
      harness.queue_press({400.0f, 300.0f});
      harness.dispatch(true, true, false);
      harness.input.cycle_selection_depth_pressed = false;
      harness.queue_release({400.0f, 300.0f});
      harness.dispatch(false, false, true);
    }
    EXPECT_EQ(harness.selected_entities(), std::vector<quader::EntityId>{
        depth_double ? back.entity : front.entity});
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
  }
}

TEST(LassoSelectionInteractionTests,
     CancelBeforeAndAfterDragThresholdReservesReleaseAndPreservesSelection) {
  for (const bool dragged : {false, true}) {
    for (const bool native_overload : {false, true}) {
      LassoSelectionHarness harness;
      const auto cube = harness.cube();
      ASSERT_TRUE(harness.editor.selection().select_object(cube).ok());
      harness.queue_press({100.0f, 100.0f});
      harness.dispatch(true, true, false);
      if (dragged) {
        harness.queue_move({200.0f, 100.0f});
        harness.dispatch(false, true, false);
      }
      ASSERT_TRUE(harness.controller.dragging());
      EXPECT_TRUE(native_overload
          ? harness.controller.cancel_active_drag(harness.editor,
                                                   harness.session.native_tools())
          : harness.controller.cancel_active_drag(harness.editor));
      harness.queue_release({400.0f, 300.0f});
      harness.dispatch(false, false, true);
      EXPECT_FALSE(harness.controller.dragging());
      EXPECT_FALSE(harness.controller.selection_marquee().active);
      EXPECT_EQ(harness.selected_entities(),
                std::vector<quader::EntityId>{cube.entity});
      EXPECT_TRUE(harness.editor.commands().execute(
          quader::quader_command::kEditClearSelection).ok());
      EXPECT_TRUE(harness.selected_entities().empty());
    }
  }
}

TEST(LassoSelectionInteractionTests,
     EscapeDuringCoalescedPressAndReleaseDoesNotStartAnotherGesture) {
  LassoSelectionHarness harness;
  (void)harness.cube();
  harness.result.cancel_requested = true;
  harness.loop();
  EXPECT_TRUE(harness.selected_entities().empty());
  EXPECT_FALSE(harness.controller.dragging());
}

TEST(LassoSelectionInteractionTests,
     EscapeBindingCancelsHeldLassoThenClearsSelectionAfterRelease) {
  const auto escape = quader::quader_input_parse_shortcut("Escape");
  ASSERT_TRUE(escape.has_value());
  ASSERT_EQ(escape->size(), 1u);
  for (const bool dragged : {false, true}) {
    SCOPED_TRACE(dragged);
    LassoSelectionHarness harness;
    const auto cube = harness.cube();
    harness.loop();
    ASSERT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{cube.entity});
    harness.queue_press({100.0f, 100.0f});
    harness.resolve_and_dispatch(true, true, false);
    if (dragged) {
      harness.queue_move({200.0f, 100.0f});
      harness.resolve_and_dispatch(false, true, false);
    }
    ASSERT_TRUE(harness.controller.dragging());
    const auto before = harness.editor.selection().summary().selection_revision;

    harness.input.pressed_input_chords = *escape;
    harness.input.active_input_chords = *escape;
    harness.resolve_and_dispatch(false, true, false);
    EXPECT_TRUE(harness.result.cancel_requested);
    EXPECT_TRUE(harness.result.shortcut_handled);
    EXPECT_EQ(harness.result.shortcut.kind,
              quader::ViewportShortcutKind::CancelActiveTool);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision, before);
    EXPECT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{cube.entity});

    harness.input.active_input_chords.clear();
    harness.input.released_input_chords = *escape;
    harness.resolve_and_dispatch(false, true, false);
    harness.queue_release({400.0f, 300.0f});
    harness.resolve_and_dispatch(false, false, true);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision, before);
    EXPECT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{cube.entity});

    harness.input.pressed_input_chords = *escape;
    harness.input.active_input_chords = *escape;
    harness.resolve_and_dispatch();
    EXPECT_TRUE(harness.input.keybinding_input_consumed);
    EXPECT_FALSE(harness.result.cancel_requested);
    EXPECT_TRUE(harness.input.resolved_shortcuts.empty());
    EXPECT_TRUE(harness.selected_entities().empty());
    EXPECT_EQ(harness.router.state().active_tool,
              quader::ViewportToolId::LassoSelect);
    EXPECT_EQ(harness.editor.query().tool_status().active_tool,
              quader::QuaderViewportTool::LassoSelect);
  }
}

TEST(LassoSelectionInteractionTests,
     IdleEscapeBindingClearsCommittedLassoSelectionAndKeepsToolActive) {
  LassoSelectionHarness harness;
  const auto cube = harness.cube();
  harness.loop();
  ASSERT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{cube.entity});
  ASSERT_FALSE(harness.controller.dragging());
  const auto escape = quader::quader_input_parse_shortcut("Escape");
  ASSERT_TRUE(escape.has_value());
  harness.input.pressed_input_chords = *escape;
  harness.input.active_input_chords = *escape;
  harness.resolve_and_dispatch();
  EXPECT_TRUE(harness.input.keybinding_input_consumed);
  EXPECT_FALSE(harness.result.cancel_requested);
  EXPECT_TRUE(harness.input.resolved_shortcuts.empty());
  EXPECT_TRUE(harness.selected_entities().empty());
  EXPECT_EQ(harness.router.state().active_tool,
            quader::ViewportToolId::LassoSelect);
  EXPECT_EQ(harness.editor.query().tool_status().active_tool,
            quader::QuaderViewportTool::LassoSelect);
}

TEST(LassoSelectionInteractionTests,
     NavigationFocusLossModeSwitchAndResetCancelWithoutPublishing) {
  for (int canceled_by = 0; canceled_by < 4; ++canceled_by) {
    LassoSelectionHarness harness;
    (void)harness.cube();
    harness.queue_press({250.0f, 150.0f});
    harness.queue_move({550.0f, 150.0f});
    harness.dispatch(true, true, false);
    switch (canceled_by) {
    case 0: harness.result.navigation_active = true; break;
    case 1: harness.input.viewport_focused = false; break;
    case 2:
      harness.router.set_active_tool(quader::ViewportToolId::Select);
      break;
    case 3: harness.controller.reset(&harness.editor); break;
    }
    harness.queue_move({550.0f, 450.0f});
    harness.queue_move({250.0f, 450.0f});
    harness.queue_release({250.0f, 150.0f});
    harness.dispatch(false, false, true);
    EXPECT_TRUE(harness.selected_entities().empty());
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
  }
}

TEST(LassoSelectionInteractionTests,
     SamplingUsesCombinedPixelScaleAndAlwaysRetainsReleasePoint) {
  LassoSelectionHarness harness;
  harness.pixel_scale = 2.0f;
  harness.queue_press({100.0f, 100.0f});
  harness.queue_move({103.0f, 100.0f});
  harness.queue_move({104.0f, 100.0f});
  harness.queue_move({104.0f, 104.0f});
  harness.queue_release({101.0f, 102.0f});
  harness.dispatch(true, false, true);
  const auto &points = harness.controller.selection_marquee().lasso_points;
  ASSERT_EQ(points.size(), 4u);
  EXPECT_FLOAT_EQ(points[1].x, 104.0f);
  EXPECT_FLOAT_EQ(points.back().x, 101.0f);
  EXPECT_FLOAT_EQ(points.back().y, 102.0f);
}

TEST(LassoSelectionInteractionTests,
     NewerPressReleaseCannotCompleteAnOlderLasso) {
  LassoSelectionHarness harness;
  (void)harness.cube();
  harness.queue_press({250.0f, 150.0f});
  harness.queue_move({550.0f, 150.0f});
  harness.queue_move({550.0f, 450.0f});
  harness.queue_move({250.0f, 450.0f});
  harness.dispatch(true, true, false);
  harness.queue_press({400.0f, 300.0f});
  harness.queue_release({400.0f, 300.0f});
  harness.dispatch(true, false, true);
  EXPECT_TRUE(harness.selected_entities().empty());
  EXPECT_FALSE(harness.controller.selection_marquee().active);
}

TEST(LassoSelectionInteractionTests,
     ReleaseUsesEventPositionAndRejectsSamplesFromOtherPresses) {
  LassoSelectionHarness harness;
  (void)harness.cube();
  harness.queue_press({250.0f, 150.0f});
  harness.queue_move({550.0f, 150.0f});
  harness.queue_move({550.0f, 450.0f});
  harness.queue_move({250.0f, 450.0f});
  harness.queue_release({250.0f, 150.0f});
  harness.input.pointer_motion_samples.push_back({
      .button = quader::QuaderMouseButton::Left,
      .local = {700.0f, 500.0f},
      .sequence = ++harness.sequence,
      .press_sequence = harness.press_sequence + 100u,
  });
  harness.input.mouse_local = {700.0f, 500.0f};
  harness.dispatch(true, false, true);
  const auto &marquee = harness.controller.selection_marquee();
  ASSERT_EQ(marquee.lasso_points.size(), 5u);
  EXPECT_FLOAT_EQ(marquee.end.x, 250.0f);
  EXPECT_FLOAT_EQ(marquee.end.y, 150.0f);
  EXPECT_EQ(harness.selected_entities().size(), 1u);
}

TEST(LassoSelectionInteractionTests,
     VertexLassoIncludesOccludedAndCoincidentVerticesWithBackfacePickingOff) {
  LassoSelectionHarness harness;
  const auto first = harness.cube();
  const auto second = harness.cube();
  harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection, false);
  ASSERT_TRUE(harness.editor.selection().select_objects({first, second}).ok());
  const auto entered = harness.editor.mode().set(quader::QuaderEditorMode::Edit);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_EQ(harness.editor.mode().state().participants.size(), 2u);
  const auto kind = harness.editor.selection().set_mode(quader::QuaderSelectionMode::Vertex);
  ASSERT_TRUE(kind.ok()) << kind.error_message();
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Vertex);
  harness.loop();
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 16u);
  EXPECT_FALSE(harness.controller.dragging());
}

TEST(LassoSelectionInteractionTests,
     EdgeLassoIncludesOccludedAndCoincidentEdgesWithBackfacePickingOff) {
  LassoSelectionHarness harness;
  const auto first = harness.cube();
  const auto second = harness.cube();
  harness.settings.set_bool_value(
      quader::ViewportBoolSettingId::BackfaceSelection, false);
  ASSERT_TRUE(harness.editor.selection().select_objects({first, second}).ok());
  const auto entered = harness.editor.mode().set(quader::QuaderEditorMode::Edit);
  ASSERT_TRUE(entered.ok()) << entered.error_message();
  ASSERT_EQ(harness.editor.mode().state().participants.size(), 2u);
  const auto kind = harness.editor.selection().set_mode(quader::QuaderSelectionMode::Edge);
  ASSERT_TRUE(kind.ok()) << kind.error_message();
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Edge);
  harness.loop();
  EXPECT_EQ(harness.editor.selection().summary().selected_edge_count, 24u);
  EXPECT_FALSE(harness.controller.dragging());
}

TEST(LassoSelectionInteractionTests,
     CollinearAndFullyRetracedDragPreserveExistingSelection) {
  for (const bool retraced : {false, true}) {
    LassoSelectionHarness harness;
    const auto cube = harness.cube();
    ASSERT_TRUE(harness.editor.selection().select_object(cube).ok());
    const auto before = harness.editor.selection().summary().selection_revision;
    harness.queue_press({100.0f, 100.0f});
    harness.queue_move({200.0f, 100.0f});
    if (retraced) {
      harness.queue_move({200.0f, 200.0f});
      harness.queue_move({100.0f, 200.0f});
      harness.queue_move({100.0f, 100.0f});
      harness.queue_move({100.0f, 200.0f});
      harness.queue_move({200.0f, 200.0f});
      harness.queue_move({200.0f, 100.0f});
    } else {
      harness.queue_move({300.0f, 100.0f});
    }
    harness.queue_release({100.0f, 100.0f});
    harness.dispatch(true, false, true);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision, before);
    EXPECT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{cube.entity});
  }
}

} // namespace
