////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/curve/curve_tool_service.hpp"
#include "editor/input/quader_input_binding.hpp"
#include "editor/input/viewport_keybinding_resolver.hpp"
#include "editor/interactions/selection_edit.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_native_tool_interaction.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace quader {
namespace {

/// Provides the real public editor and its pointer-driven Curve service.
struct CurveToolHarness {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  ViewportCameraController camera;
  ViewportNativeToolInteraction interaction;
  ViewportInteractionController controller;
  ViewportInputRouter router;
  ViewportSettings settings;
  ViewportInputFrame input;
  ViewportInputResult input_result;
  std::vector<QuaderInputChord> suppressed_chords;
  double linear_wheel_remainder = 0.0;
  double radial_wheel_remainder = 0.0;
  bool scene_snap_enabled = false;
  bool grid_snap_enabled = false;
  std::function<ViewportMeshSnapQueryResult(const ViewportRay &, double)>
      mesh_snap_query;
  std::function<std::optional<double>(const ViewportRay &)>
      mesh_surface_distance;
  std::vector<ViewportSceneCurvePickCandidate> curve_snap_candidates;

  CurveToolHarness() {
    camera.set_viewport_size(800.0f, 600.0f);
    camera.focus_origin(16.0f);
    camera.view_axis(ViewportViewAxis::Y, 1);
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    input.mouse_local = {400.0f, 300.0f};
    input.mouse_screen = input.mouse_local;
  }

  ViewportNativeToolInteractionResult frame(bool pressed, bool down,
                                             bool released,
                                             bool gizmo = false,
                                             bool suppress_hover = false) {
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    if (pressed) {
      input.pointer_pressed_edges.push_back(
          {.button = QuaderMouseButton::Left, .local = input.mouse_local});
    }
    if (released) {
      input.pointer_released_edges.push_back(
          {.button = QuaderMouseButton::Left, .local = input.mouse_local});
    }
    const auto result = interaction.handle_frame(
        editor, session.scene(), session.assets(), nullptr, nullptr, nullptr,
        &session.point_placement(), camera,
        {.input = input,
         .input_result = input_result,
         .navigation_gizmo_consumed = gizmo,
         .suppress_hover_work = suppress_hover,
         .left_pressed = pressed,
         .left_down = down,
         .left_released = released,
         .left_click_local = input.mouse_local,
         .scene_snap_enabled = scene_snap_enabled,
         .grid_snap_enabled = grid_snap_enabled,
         .scene_mesh_surface_distance = mesh_surface_distance},
        &session.curve());
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    return result;
  }

  ViewportNativeToolInteractionResult dispatch(
      bool transform_gizmo_consumed = false) {
    return interaction.handle_frame(
        editor, session.scene(), session.assets(), nullptr, nullptr, nullptr,
        &session.point_placement(), camera,
        {.input = input,
         .input_result = input_result,
         .transform_gizmo_consumed = transform_gizmo_consumed,
         .scene_snap_enabled = scene_snap_enabled,
         .grid_snap_enabled = grid_snap_enabled,
         .scene_mesh_surface_distance = mesh_surface_distance},
        &session.curve());
  }

  [[nodiscard]] ViewportInteractionFrame gizmo_frame(
      bool pressed = false, bool down = false, bool released = false,
      QVec2 click_local = {}) {
    return {
        .input_router = router,
        .camera = camera,
        .settings = settings,
        .input = input,
        .input_result = input_result,
        .left_click_local = click_local,
        .left_pressed = pressed,
        .left_down = down,
         .left_released = released,
         .active_tool_allows_selection_fallback = false,
         .scene_curve_pick_candidates = curve_snap_candidates,
         .scene_mesh_surface_distance = mesh_surface_distance,
         .scene_mesh_snap_query = mesh_snap_query,
         .selection_state =
            {
                .summary = editor.selection().summary(),
                .scene_revision = session.scene().revision(),
                .asset_revision = session.assets().revision(),
                .pivot_revision = 0,
            },
    };
  }

  void place(QVec3 point) {
    ASSERT_TRUE(session.curve().begin_point(point).ok);
    ASSERT_TRUE(session.curve().commit_drag().ok);
  }

  void resolve_chord(std::string_view shortcut = {}) {
    if (shortcut.empty()) {
      resolve_chords({});
    } else {
      resolve_chords({shortcut});
    }
  }

  void resolve_chords(std::initializer_list<std::string_view> shortcuts) {
    input.pressed_input_chords.clear();
    for (const std::string_view shortcut : shortcuts) {
      const auto mapping = quader_input_parse_shortcut(shortcut);
      ASSERT_TRUE(mapping.has_value());
      ASSERT_EQ(mapping->size(), 1u);
      input.pressed_input_chords.push_back(mapping->front());
    }
    ViewportToolState state;
    state.active_tool = ViewportToolId::Curve;
    input.active_tool_owns_viewport = true;
    ViewportKeybindingResolver::resolve(editor, state, input, suppressed_chords,
                                       linear_wheel_remainder,
                                       radial_wheel_remainder);
  }

  QuaderModelingObjectRef make_curve() {
    const auto created = editor.modeling().create_curve({
        .points = {{.position = {-2.0f, 0.0f, 0.0f},
                    .outgoing_handle = {1.0f, 0.0f, 1.0f}},
                   {.position = {2.0f, 0.0f, 0.0f},
                    .incoming_handle = {-1.0f, 0.0f, 1.0f}}},
    });
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.value();
  }

  void activate_curve_edit() {
    ASSERT_TRUE(editor.tools().activate(kCurveToolId).ok());
    router.set_active_tool(ViewportToolId::Curve);
    input.active_tool_owns_viewport = true;
  }

  void curve_selection_frame(bool pressed, bool down, bool released,
                             QVec2 press_local) {
    controller.handle_after_active_tool(
        editor, gizmo_frame(pressed, down, released, press_local), {}, {},
        nullptr, &session.curve());
  }

  ViewportNativeToolInteractionResult curve_native_selection_frame(
      bool pressed, bool down, bool released, QVec2 press_local) {
    const ViewportNativeToolInteractionResult native =
        frame(pressed, down, released);
    curve_selection_frame(pressed, down, released, press_local);
    return native;
  }

  void click_curve_control(QVec2 local,
                           ViewportModifiers modifiers = {}) {
    input.modifiers = modifiers;
    input.mouse_local = local;
    input.mouse_screen = local;
    curve_selection_frame(true, true, false, local);
    curve_selection_frame(false, false, true, local);
    input.modifiers = {};
  }

  void marquee_curve_controls(QVec2 start, QVec2 end,
                              ViewportModifiers modifiers = {}) {
    input.modifiers = modifiers;
    input.mouse_local = start;
    input.mouse_screen = start;
    curve_selection_frame(true, true, false, start);
    input.mouse_local = end;
    input.mouse_screen = end;
    curve_selection_frame(false, true, false, start);
    curve_selection_frame(false, false, true, start);
    input.modifiers = {};
  }
};

void passive_curve_selection_frame(CurveToolHarness &harness, bool pressed,
                                   bool down, bool released,
                                   QVec2 press_local) {
  ViewportInteractionFrame frame =
      harness.gizmo_frame(pressed, down, released, press_local);
  frame.active_tool_allows_selection_fallback = true;
  const ViewportBeforeToolInteractionResult before =
      harness.controller.handle_before_active_tool(harness.editor, frame,
                                                   &harness.session.curve());
  harness.controller.handle_after_active_tool(
      harness.editor, frame, {}, before, nullptr, &harness.session.curve());
}

void click_passive_curve_selection(CurveToolHarness &harness, QVec2 local,
                                   ViewportModifiers modifiers = {}) {
  harness.input.modifiers = modifiers;
  harness.input.mouse_local = local;
  harness.input.mouse_screen = local;
  passive_curve_selection_frame(harness, true, true, false, local);
  passive_curve_selection_frame(harness, false, false, true, local);
  harness.input.modifiers = {};
}

void expect_point(modeling::Vec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001f);
  EXPECT_NEAR(actual.y, expected.y, 0.0001f);
  EXPECT_NEAR(actual.z, expected.z, 0.0001f);
}

QVec2 curve_control_screen(const CurveToolHarness &harness,
                           const CurveToolPreviewState &preview,
                           std::size_t point_index,
                           CurveControlKind kind) {
  const auto projected = harness.camera.project_world_point(transform_point(
      preview.world_transform,
      curve_control_local_position(preview.document->points()[point_index],
                                   kind)));
  EXPECT_TRUE(projected.visible);
  return projected.position;
}

void expect_control_selection(
    const CurveControlSelection &selection,
    std::span<const CurveControlTarget> expected,
    std::optional<CurveControlTarget> active) {
  EXPECT_EQ(selection.controls.size(), expected.size());
  for (const CurveControlTarget target : expected) {
    EXPECT_TRUE(selection.contains(target));
  }
  EXPECT_EQ(selection.active, active);
}

void expect_control_selection(
    const CurveControlSelection &selection,
    std::initializer_list<CurveControlTarget> expected,
    std::optional<CurveControlTarget> active) {
  expect_control_selection(
      selection,
      std::span<const CurveControlTarget>{expected.begin(), expected.size()},
      active);
}

bool replace_curve_control_selection(
    CurveToolService &curve,
    std::optional<CurveControlTarget> target = std::nullopt) {
  const std::span<const CurveControlTarget> hits =
      target ? std::span<const CurveControlTarget>{&*target, 1u}
             : std::span<const CurveControlTarget>{};
  return curve.edit_control_selection(
      hits, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      target);
}

CommandResult begin_curve_control_selection_drag(
    CurveToolService &curve, CurveControlTarget target) {
  (void)replace_curve_control_selection(curve, target);
  return curve.begin_selected_control_drag();
}

const quader_editor_core::TransformGizmoAxisPrimitive *find_curve_gizmo_axis(
    const quader_editor_core::TransformGizmoFrame &frame,
    quader_editor_core::TransformAxis axis) {
  const auto found = std::find_if(
      frame.axes.begin(), frame.axes.end(),
      [axis](const auto &primitive) { return primitive.axis == axis; });
  return found == frame.axes.end() ? nullptr : &*found;
}

const quader_editor_core::TransformGizmoPlanePrimitive *find_curve_gizmo_plane(
    const quader_editor_core::TransformGizmoFrame &frame,
    quader_editor_core::TransformAxis axis) {
  const auto found = std::find_if(
      frame.planes.begin(), frame.planes.end(),
      [axis](const auto &primitive) { return primitive.axis == axis; });
  return found == frame.planes.end() ? nullptr : &*found;
}

QVec2 axis_screen_point(
    const quader_editor_core::TransformGizmoAxisPrimitive &axis,
    float factor) {
  return axis.screen_start + (axis.screen_tip - axis.screen_start) * factor;
}

QVec2 plane_screen_center(
    const quader_editor_core::TransformGizmoPlanePrimitive &plane) {
  return (plane.screen0 + plane.screen1 + plane.screen2 + plane.screen3) *
         0.25f;
}

TEST(CurveToolServiceTests, PointerPreviewDoesNotDirtyUnchangedMeshSelection) {
  CurveToolHarness harness;
  const auto mesh = harness.editor.modeling().create_box(
      {.min = {8.0f, 0.0f, 8.0f}, .max = {10.0f, 2.0f, 10.0f}});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  const auto scene_revision = harness.session.scene().revision();
  const auto asset_revision = harness.session.assets().revision();
  const auto content_revision = harness.session.modeling().content_revision();
  const auto selection_revision = harness.session.scene().selection_revision();

  const auto hovered = harness.frame(false, false, false);
  EXPECT_TRUE(hovered.curve_consumed);
  EXPECT_FALSE(hovered.mark_overlay_dirty);
  ASSERT_TRUE(harness.session.curve().preview_state().hover_world.has_value());
  const auto hover_revision = harness.session.curve().preview_state().revision;
  const auto unchanged = harness.frame(false, false, false);
  EXPECT_FALSE(unchanged.mark_overlay_dirty);
  EXPECT_EQ(harness.session.curve().preview_state().revision, hover_revision);

  const auto pressed = harness.frame(true, true, false);
  EXPECT_TRUE(pressed.curve_consumed);
  EXPECT_FALSE(pressed.mark_overlay_dirty);
  ASSERT_TRUE(harness.session.curve().dragging());
  harness.input.mouse_local.x += 40.0f;
  const auto dragged = harness.frame(false, true, false);
  EXPECT_FALSE(dragged.mark_overlay_dirty);
  EXPECT_GT(harness.session.curve().preview_state().revision, hover_revision);
  const auto released = harness.frame(false, false, true);
  EXPECT_FALSE(released.mark_overlay_dirty);
  EXPECT_FALSE(harness.session.curve().dragging());
  EXPECT_EQ(harness.session.scene().revision(), scene_revision);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision);
  EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
  EXPECT_EQ(harness.session.scene().selection_revision(), selection_revision);
  EXPECT_TRUE(harness.editor.query().entity(mesh.value().entity)->selected);
}

TEST(CurveToolServiceTests,
     DisabledSceneSnappingSkipsMeshPlacementCacheDuringDrawingAndDrag) {
  CurveToolHarness harness;
  for (int index = 0; index < 24; ++index) {
    SCOPED_TRACE(index);
    const auto mesh = harness.editor.modeling().create_box({
        .min = {static_cast<float>(index), 0.0f, 4.0f},
        .max = {static_cast<float>(index + 1), 1.0f, 5.0f},
    });
    ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  }

  std::size_t visibility_filter_calls = 0u;
  harness.session.point_placement().set_viewport_visibility_filter(
      [&](EntityId) {
        ++visibility_filter_calls;
        return true;
      },
      []() { return 1u; });
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());

  // A cold or explicitly invalidated cache must remain untouched whenever
  // scene landmark snapping is disabled.
  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_TRUE(harness.session.curve().preview_state().hover_world.has_value());
  EXPECT_EQ(visibility_filter_calls, 0u);

  harness.scene_snap_enabled = true;
  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_GT(visibility_filter_calls, 0u);

  visibility_filter_calls = 0u;
  harness.scene_snap_enabled = false;
  harness.grid_snap_enabled = false;
  harness.session.point_placement().invalidate();

  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_TRUE(harness.session.curve().preview_state().hover_world.has_value());
  EXPECT_EQ(visibility_filter_calls, 0u);
  EXPECT_TRUE(harness.frame(true, true, false).curve_consumed);
  harness.input.mouse_local.x += 24.0f;
  EXPECT_TRUE(harness.frame(false, true, false).curve_consumed);
  EXPECT_EQ(visibility_filter_calls, 0u);
  EXPECT_TRUE(harness.frame(false, false, true).curve_consumed);
  EXPECT_EQ(visibility_filter_calls, 0u);
}

TEST(CurveToolServiceTests,
     CurveDescriptorSuppressesSelectionFallbackWhileSelectPreservesIt) {
  ViewportInteractionController controller;
  EXPECT_FALSE(controller.active_tool_allows_selection_fallback(
      ViewportToolId::Curve));
  EXPECT_TRUE(controller.active_tool_allows_selection_fallback(
      ViewportToolId::Select));
}

TEST(CurveToolServiceTests,
     InactiveCurveToolAttachesSelectedCurveAndPublishesEditableControls) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_FALSE(curve.active());
  ASSERT_EQ(harness.session.modeling().selection_mode(),
            ModelingSelectionMode::Object);
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.controls_editable());
  const CurveToolPreviewState &preview = curve.preview_state();
  ASSERT_TRUE(preview.active);
  EXPECT_FALSE(preview.drawing);
  EXPECT_EQ(preview.entity, object.entity);
  ASSERT_TRUE(preview.document.has_value());

  const QVec2 first_anchor_screen =
      curve_control_screen(harness, preview, 0u, CurveControlKind::Anchor);
  const std::optional<ViewportCurveControlPickHit> hit =
      pick_viewport_curve_control(harness.camera, preview,
                                  first_anchor_screen);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target,
            (CurveControlTarget{preview.document->points()[0].id,
                                CurveControlKind::Anchor}));
}

TEST(CurveToolServiceTests,
     PassiveCurveControlMissUsesCanonicalPlainClickDeselection) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.controls_editable());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget first_anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, first_anchor));

  click_passive_curve_selection(harness, {64.0f, 64.0f});

  const auto snapshot = harness.editor.query().entity(object.entity);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FALSE(snapshot->selected);
  const QuaderSelectionSummary selection = harness.editor.selection().summary();
  EXPECT_FALSE(selection.has_selection);
  EXPECT_EQ(selection.selected_object_count, 0u);
  EXPECT_TRUE(curve.control_selection().empty());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_FALSE(curve.controls_editable());
  EXPECT_FALSE(curve.preview_state().active);
  EXPECT_FALSE(curve.preview_state().document.has_value());
}

TEST(CurveToolServiceTests,
     ForegroundMeshDepthPreventsHiddenCurveControlFromConsumingPlainClick) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.controls_editable());
  const CurveToolPreviewState &preview = curve.preview_state();
  ASSERT_TRUE(preview.document.has_value());
  const QVec2 anchor_screen =
      curve_control_screen(harness, preview, 0u, CurveControlKind::Anchor);
  const auto control_hit =
      pick_viewport_curve_control(harness.camera, preview, anchor_screen);
  ASSERT_TRUE(control_hit.has_value());
  ASSERT_GT(control_hit->ray_distance, 0.0);

  std::size_t mesh_depth_queries = 0u;
  harness.mesh_surface_distance =
      [&](const ViewportRay &) -> std::optional<double> {
    ++mesh_depth_queries;
    return control_hit->ray_distance * 0.5;
  };
  click_passive_curve_selection(harness, anchor_screen);

  EXPECT_GT(mesh_depth_queries, 0u);
  EXPECT_TRUE(curve.control_selection().empty());
  const auto snapshot = harness.editor.query().entity(object.entity);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FALSE(snapshot->selected)
      << "the hidden control must fall through to canonical scene picking";
  EXPECT_FALSE(harness.editor.selection().summary().has_selection);
}

TEST(CurveToolServiceTests,
     PassiveCurveControlModifierMissPreservesCanonicalSelection) {
  constexpr std::array modifiers{
      ViewportModifiers{.shift = true},
      ViewportModifiers{.primary = true},
  };
  for (const ViewportModifiers modifier : modifiers) {
    SCOPED_TRACE(modifier.shift ? "Shift miss" : "Primary miss");
    CurveToolHarness harness;
    const QuaderModelingObjectRef object = harness.make_curve();
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
    harness.router.set_selection_mode(ViewportSelectionMode::Object);
    harness.input.active_tool_owns_viewport = false;

    CurveToolService &curve = harness.session.curve();
    ASSERT_TRUE(curve.refresh().ok);
    ASSERT_TRUE(curve.controls_editable());
    ASSERT_TRUE(curve.preview_state().document.has_value());
    const CurveControlTarget first_anchor{
        curve.preview_state().document->points()[0].id,
        CurveControlKind::Anchor};
    ASSERT_TRUE(replace_curve_control_selection(curve, first_anchor));

    click_passive_curve_selection(harness, {64.0f, 64.0f}, modifier);

    const auto snapshot = harness.editor.query().entity(object.entity);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_TRUE(snapshot->selected);
    const QuaderSelectionSummary selection =
        harness.editor.selection().summary();
    EXPECT_TRUE(selection.has_selection);
    EXPECT_EQ(selection.selected_object_count, 1u);
    expect_control_selection(curve.control_selection(), {first_anchor},
                             first_anchor);
    EXPECT_FALSE(curve.refresh().changed);
    EXPECT_TRUE(curve.controls_editable());
  }
}

TEST(CurveToolServiceTests, DraftUsesSymmetricHandlesAndCreatesOneUndoItem) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.begin_point({-2.0f, 0.0f, 0.0f}).ok);
  ASSERT_TRUE(curve.update_drag({-1.0f, 0.0f, 2.0f}).ok);
  ASSERT_TRUE(curve.commit_drag().ok);
  harness.place({2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(curve.can_apply());
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
  const auto draft = *curve.preview_state().document;
  expect_point(draft.points()[0].incoming_handle, {-1.0f, 0.0f, -2.0f});
  expect_point(draft.points()[0].outgoing_handle, {1.0f, 0.0f, 2.0f});
  expect_point(draft.points()[1].outgoing_handle, {});

  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  EXPECT_TRUE(curve.active());
  EXPECT_FALSE(curve.drawing());
  const EntityId entity = curve.preview_state().entity;
  ASSERT_NE(harness.session.scene().curve(entity), nullptr);
  EXPECT_TRUE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.native_tools().has_pending_history_preview());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.scene().exists(entity));
  EXPECT_FALSE(harness.session.history().can_undo());
  ASSERT_TRUE(harness.editor.history().redo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(draft));
}

TEST(CurveToolServiceTests, PendingClosureAndNewCurveDoNotDiscardAWorkingDraft) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_closed(true).ok);
  harness.place({-2.0f, 0.0f, 0.0f});
  EXPECT_TRUE(curve.closed());
  EXPECT_FALSE(curve.can_apply());
  const auto first = *curve.preview_state().document;
  EXPECT_FALSE(curve.apply().ok);
  EXPECT_FALSE(curve.new_curve().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(first));
  harness.place({2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(curve.apply().ok);
  EXPECT_TRUE(curve.preview_state().document->closed());
  ASSERT_TRUE(curve.new_curve().ok);
  EXPECT_TRUE(curve.drawing());
  EXPECT_EQ(curve.point_count(), 0u);
  EXPECT_FALSE(curve.refresh().changed);
  EXPECT_TRUE(curve.drawing());
}

TEST(CurveToolServiceTests, CanceledAndNoopDragsKeepRedoAndCleanState) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_closed(true).ok);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  harness.session.mark_clean();
  const auto before = *curve.preview_state().document;
  const auto scene_revision = harness.session.scene().revision();
  const auto asset_revision = harness.session.assets().revision();
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(begin_curve_control_selection_drag(curve, target).ok);
  ASSERT_TRUE(curve.update_drag({-4.0f, 3.0f, 1.0f}).ok);
  ASSERT_TRUE(curve.cancel_drag().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_TRUE(harness.editor.history().can_redo());
  EXPECT_FALSE(harness.session.dirty());
  EXPECT_EQ(harness.session.scene().revision(), scene_revision);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision);

  ASSERT_TRUE(begin_curve_control_selection_drag(curve, target).ok);
  ASSERT_TRUE(curve.commit_drag().ok);
  EXPECT_TRUE(harness.editor.history().can_redo());
  EXPECT_FALSE(harness.session.dirty());
  const auto snapshot = harness.editor.modeling().curve_snapshot(object);
  ASSERT_TRUE(snapshot.ok());
  EXPECT_TRUE(snapshot.value().document.has_same_geometry(before));
}

TEST(CurveToolServiceTests, TransformedAnchorsAndHandlesEditInLocalSpace) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.scene().set_transform(
      object.entity,
      {.position = {7.0f, 3.0f, -4.0f},
       .rotation_euler = {15.0f, 35.0f, 8.0f},
       .scale = {2.0f, 0.75f, -1.5f}}).ok());
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const auto world = curve.preview_state().world_transform;
  const auto point_id = before.points()[0].id;
  ASSERT_TRUE(begin_curve_control_selection_drag(curve, {.point = point_id}).ok);
  ASSERT_TRUE(curve.update_drag(transform_point(world, {-1.0f, 2.0f, 3.0f})).ok);
  ASSERT_TRUE(curve.commit_drag().ok);
  auto edited = *curve.preview_state().document;
  expect_point(edited.points()[0].position, {-1.0f, 2.0f, 3.0f});
  expect_point(edited.points()[0].outgoing_handle,
               {before.points()[0].outgoing_handle.x,
                before.points()[0].outgoing_handle.y,
                before.points()[0].outgoing_handle.z});

  ASSERT_TRUE(begin_curve_control_selection_drag(curve, {.point = point_id,
      .kind = CurveControlKind::OutgoingHandle}).ok);
  ASSERT_TRUE(curve.update_drag(transform_point(world, {2.0f, 1.0f, 4.0f})).ok);
  ASSERT_TRUE(curve.commit_drag().ok);
  edited = *curve.preview_state().document;
  expect_point(edited.points()[0].outgoing_handle, {3.0f, -1.0f, 1.0f});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  expect_point(curve.preview_state().document->points()[0].position,
               {-1.0f, 2.0f, 3.0f});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));

}

TEST(CurveToolServiceTests, InvalidInputAndUnchangedHoverPreservePublishedState) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  harness.place({0.0f, 0.0f, 0.0f});
  ASSERT_TRUE(replace_curve_control_selection(curve));
  const auto before = *curve.preview_state().document;
  const auto revision = curve.preview_state().revision;
  const auto settings_revision = curve.tool_settings_revision();
  EXPECT_FALSE(curve.begin_point({std::numeric_limits<float>::infinity(), 0.0f, 0.0f}).ok);
  const CurveControlTarget invalid{.point = {99u, 1u}};
  const std::array invalid_hit{invalid};
  EXPECT_FALSE(curve.edit_control_selection(
      invalid_hit, quader_editor_core::SelectionEditOperation::Add, nullptr,
      invalid));
  EXPECT_FALSE(curve.begin_selected_control_drag().ok);
  EXPECT_EQ(curve.preview_state().revision, revision);
  EXPECT_EQ(curve.tool_settings_revision(), settings_revision);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.set_hover(QVec3{1.0f, 0.0f, 0.0f}).changed);
  const auto hover_revision = curve.preview_state().revision;
  EXPECT_FALSE(curve.set_hover(QVec3{1.0f, 0.0f, 0.0f}).changed);
  EXPECT_FALSE(curve.refresh().changed);
  EXPECT_EQ(curve.preview_state().revision, hover_revision);
  EXPECT_EQ(curve.preview_state().document->content_identity(), before.content_identity());
}

TEST(CurveToolServiceTests, SourceDeletionAndDocumentReplacementClearStaleControls) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  ASSERT_TRUE(replace_curve_control_selection(
      curve, CurveControlTarget{.point = before.points()[0].id}));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  // Simulates an external owner invalidation while a control is selected.
  ASSERT_TRUE(harness.session.scene().destroy_entity(object.entity));
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_FALSE(harness.session.scene().exists(object.entity));
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(curve.control_selection().active.has_value());
  EXPECT_TRUE(curve.drawing());
  EXPECT_EQ(curve.point_count(), 0u);

  harness.place({1.0f, 0.0f, 0.0f});
  harness.session.replace_document(SceneContentDocument{});
  EXPECT_FALSE(harness.session.curve().active());
  EXPECT_FALSE(harness.session.curve().preview_state().document.has_value());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests, PointerCaptureBlocksSelectionAndCancelsForNavigation) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.input_result.navigation_active = true;
  EXPECT_TRUE(harness.frame(true, true, false).curve_consumed);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
  harness.input_result.navigation_active = false;
  harness.input.text_input_active = true;
  EXPECT_TRUE(harness.frame(true, true, false).curve_consumed);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
  harness.input.text_input_active = false;
  EXPECT_TRUE(harness.frame(true, true, false, true).curve_consumed);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
  EXPECT_TRUE(harness.frame(true, true, false, false, true).curve_consumed);
  EXPECT_EQ(harness.session.curve().point_count(), 0u);

  const auto press = harness.frame(true, true, false);
  EXPECT_TRUE(press.curve_consumed);
  EXPECT_TRUE(press.claim_interaction_pane);
  EXPECT_TRUE(harness.interaction.dragging());
  EXPECT_TRUE(harness.session.curve().dragging());
  harness.input.mouse_local.x += 80.0f;
  (void)harness.frame(false, true, false);
  harness.input_result.navigation_active = true;
  (void)harness.frame(false, true, false);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_FALSE(harness.session.curve().dragging());
  EXPECT_FALSE(harness.session.curve().control_selection().active.has_value());
  EXPECT_EQ(harness.session.curve().point_count(), 0u);
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests, PointerReleaseCommitsAndCaptureLossCancelsOnlyTheGesture) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  (void)harness.frame(true, true, false);
  (void)harness.frame(false, false, true);
  ASSERT_EQ(harness.session.curve().point_count(), 1u);
  ASSERT_TRUE(harness.session.curve().control_selection().active.has_value());
  EXPECT_EQ(harness.session.curve().control_selection().active->kind,
            CurveControlKind::Anchor);
  harness.input.mouse_local.x += 100.0f;
  (void)harness.frame(true, true, false);
  EXPECT_EQ(harness.session.curve().point_count(), 2u);
  (void)harness.frame(false, false, false);
  EXPECT_EQ(harness.session.curve().point_count(), 1u);
  EXPECT_FALSE(harness.session.curve().dragging());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     ExistingAnchorAndHandleClicksSelectWithoutFreePointerMovement) {
  for (const CurveControlKind kind : {CurveControlKind::Anchor,
                                      CurveControlKind::OutgoingHandle}) {
    SCOPED_TRACE(kind == CurveControlKind::Anchor ? "anchor" : "handle");
    CurveToolHarness harness;
    const auto object = harness.make_curve();
    ASSERT_TRUE(harness.editor.scene().set_transform(
        object.entity,
        {.position = {0.0f, 3.0f, 0.0f},
         .rotation_euler = {0.0f, 25.0f, 0.0f},
         .scale = {1.5f, 0.75f, 2.0f}}).ok());
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    harness.session.history().clear();
    harness.session.mark_clean();
    CurveToolService &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const CurveControlTarget target{.point = before.points()[0].id,
                                    .kind = kind};
    const auto world_position = transform_point(
        curve.preview_state().world_transform,
        curve_control_local_position(before.points()[0], kind));
    const auto screen = harness.camera.project_world_point(world_position);
    ASSERT_TRUE(screen.visible);
    const auto content_revision = harness.session.modeling().content_revision();
    const auto scene_revision = harness.session.scene().revision();
    const auto history_before = harness.editor.history().snapshot();

    harness.input.mouse_local = screen.position;
    const auto pressed = harness.curve_native_selection_frame(
        true, true, false, screen.position);
    EXPECT_TRUE(pressed.curve_consumed);
    EXPECT_FALSE(curve.dragging());
    EXPECT_FALSE(harness.interaction.dragging());
    EXPECT_TRUE(curve.control_selection().empty())
        << "shared click selection publishes only on release";

    harness.input.mouse_local += QVec2{3.0f, 0.0f};
    EXPECT_TRUE(harness.curve_native_selection_frame(
        false, true, false, screen.position).curve_consumed);
    EXPECT_TRUE(harness.curve_native_selection_frame(
        false, false, true, screen.position).curve_consumed);
    EXPECT_FALSE(curve.dragging());
    EXPECT_FALSE(harness.interaction.dragging());
    ASSERT_TRUE(curve.control_selection().active.has_value());
    EXPECT_EQ(*curve.control_selection().active, target);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(harness.session.scene().revision(), scene_revision);
    EXPECT_FALSE(harness.session.dirty());
    const auto history_after = harness.editor.history().snapshot();
    EXPECT_EQ(history_after.can_undo, history_before.can_undo);
    EXPECT_EQ(history_after.can_redo, history_before.can_redo);
    EXPECT_EQ(history_after.undo_label, history_before.undo_label);
    EXPECT_EQ(history_after.redo_label, history_before.redo_label);
  }
}

TEST(CurveToolServiceTests,
     PassiveCurveControlGizmoFollowsCanonicalSelectionFallbackPolicy) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_active_tool(ViewportToolId::Select);
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_FALSE(curve.active());
  ASSERT_TRUE(curve.controls_editable());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));

  ViewportInteractionFrame select_frame = harness.gizmo_frame();
  select_frame.active_tool_allows_selection_fallback = true;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     select_frame, &curve);
  EXPECT_NE(harness.controller.transform_gizmo_frame(), nullptr);

  harness.router.set_active_tool(ViewportToolId::Poly);
  harness.input.active_tool_owns_viewport = true;
  harness.input.modal_tool_active = true;
  ViewportInteractionFrame modal_frame = harness.gizmo_frame();
  modal_frame.active_tool_allows_selection_fallback = false;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     modal_frame, &curve);
  EXPECT_EQ(harness.controller.transform_gizmo_frame(), nullptr);
  EXPECT_TRUE(curve.controls_editable())
      << "The modal tool suspends passive interaction without detaching the "
         "canonical Curve selection.";
}

TEST(CurveToolServiceTests,
     PassiveCurveKeepsObjectTransformGizmoUntilAControlIsSelected) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_FALSE(curve.active());
  ASSERT_TRUE(curve.controls_editable());
  ASSERT_TRUE(curve.control_selection().empty());
  const std::optional<QVec3> object_pivot =
      harness.editor.query().selection_transform_pivot();
  ASSERT_TRUE(object_pivot.has_value());

  struct ToolCase final {
    ViewportToolId tool;
    quader_editor_core::ViewportInteractionTool interaction_tool;
  };
  constexpr std::array tool_cases{
      ToolCase{ViewportToolId::Move,
               quader_editor_core::ViewportInteractionTool::Move},
      ToolCase{ViewportToolId::Rotate,
               quader_editor_core::ViewportInteractionTool::Rotate},
      ToolCase{ViewportToolId::Scale,
               quader_editor_core::ViewportInteractionTool::Scale},
  };
  for (const ToolCase &tool_case : tool_cases) {
    harness.router.set_active_tool(tool_case.tool);
    ViewportInteractionFrame frame = harness.gizmo_frame();
    frame.active_tool_allows_selection_fallback = true;
    (void)harness.controller.handle_before_active_tool(harness.editor, frame,
                                                       &curve);
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_EQ(gizmo->info.tool, tool_case.interaction_tool);
    EXPECT_NEAR(gizmo->info.pivot.x, object_pivot->x, 0.0001f);
    EXPECT_NEAR(gizmo->info.pivot.y, object_pivot->y, 0.0001f);
    EXPECT_NEAR(gizmo->info.pivot.z, object_pivot->z, 0.0001f);
  }

  // Curve control picking remains the earlier click route. Once it selects an
  // anchor, the shared gizmo switches from the whole object to that control.
  harness.router.set_active_tool(ViewportToolId::Move);
  const CurveToolPreviewState &preview = curve.preview_state();
  const QVec2 anchor_screen =
      curve_control_screen(harness, preview, 0, CurveControlKind::Anchor);
  click_passive_curve_selection(harness, anchor_screen);
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(curve.control_selection().active->kind, CurveControlKind::Anchor);
  const std::optional<QVec3> control_pivot =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(control_pivot.has_value());

  ViewportInteractionFrame control_frame = harness.gizmo_frame();
  control_frame.active_tool_allows_selection_fallback = true;
  (void)harness.controller.handle_before_active_tool(
      harness.editor, control_frame, &curve);
  const quader_editor_core::TransformGizmoFrame *control_gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(control_gizmo, nullptr);
  EXPECT_EQ(control_gizmo->info.tool,
            quader_editor_core::ViewportInteractionTool::Move);
  EXPECT_NEAR(control_gizmo->info.pivot.x, control_pivot->x, 0.0001f);
  EXPECT_NEAR(control_gizmo->info.pivot.y, control_pivot->y, 0.0001f);
  EXPECT_NEAR(control_gizmo->info.pivot.z, control_pivot->z, 0.0001f);

  ASSERT_TRUE(replace_curve_control_selection(curve));
  (void)harness.controller.handle_before_active_tool(
      harness.editor, control_frame, &curve);
  const quader_editor_core::TransformGizmoFrame *restored_object_gizmo =
      harness.controller.transform_gizmo_frame();
  ASSERT_NE(restored_object_gizmo, nullptr);
  EXPECT_NEAR(restored_object_gizmo->info.pivot.x, object_pivot->x, 0.0001f);
  EXPECT_NEAR(restored_object_gizmo->info.pivot.y, object_pivot->y, 0.0001f);
  EXPECT_NEAR(restored_object_gizmo->info.pivot.z, object_pivot->z, 0.0001f);
}

TEST(CurveToolServiceTests,
     PassiveCurveControlPressWinsAtObjectAndControlGizmoOverlap) {
  CurveToolHarness harness;
  const auto created = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f}},
                 {.position = {0.0f, 0.0f, 0.0f}},
                 {.position = {2.0f, 0.0f, 0.0f}}},
  });
  ASSERT_TRUE(created.ok()) << created.error_message();
  ASSERT_TRUE(harness.editor.selection().select_object(created.value()).ok());
  harness.router.set_active_tool(ViewportToolId::Move);
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.controls_editable());

  const auto passive_frame = [&](bool pressed = false, bool down = false,
                                 bool released = false,
                                 QVec2 click_local = QVec2{}) {
    ViewportInteractionFrame frame =
        harness.gizmo_frame(pressed, down, released, click_local);
    frame.active_tool_allows_selection_fallback = true;
    return frame;
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  const auto *object_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(object_gizmo, nullptr);
  const CurveToolPreviewState preview = curve.preview_state();
  ASSERT_TRUE(preview.document.has_value());
  const QVec2 middle_anchor =
      curve_control_screen(harness, preview, 1, CurveControlKind::Anchor);
  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  object_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(object_gizmo, nullptr);
  EXPECT_NEAR(middle_anchor.x, object_gizmo->info.screen_pivot.x, 0.001f);
  EXPECT_NEAR(middle_anchor.y, object_gizmo->info.screen_pivot.y, 0.001f);

  click_passive_curve_selection(harness, middle_anchor);
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(curve.control_selection().active->point,
            preview.document->points()[1].id);
  EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());

  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  const auto *control_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(control_gizmo, nullptr);
  const auto *control_x = find_curve_gizmo_axis(
      *control_gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(control_x, nullptr);

  // Put an unselected anchor exactly on the selected control's X-axis tip so
  // the regression does not depend on camera scale or gizmo-size preference.
  const modeling::CurveControlPoint third = preview.document->points()[2];
  ASSERT_TRUE(harness.editor.modeling()
                  .set_curve_point(
                      created.value(), third.id,
                      {.position = {control_x->world_tip.x,
                                    control_x->world_tip.y,
                                    control_x->world_tip.z},
                       .incoming_handle = third.incoming_handle,
                       .outgoing_handle = third.outgoing_handle})
                  .ok());
  ASSERT_TRUE(curve.refresh().ok);
  harness.session.history().clear();
  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  control_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(control_gizmo, nullptr);
  control_x = find_curve_gizmo_axis(
      *control_gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(control_x, nullptr);
  const QVec2 third_anchor_screen =
      curve_control_screen(harness, curve.preview_state(), 2,
                           CurveControlKind::Anchor);
  EXPECT_NEAR(third_anchor_screen.x, control_x->screen_tip.x, 0.001f);
  EXPECT_NEAR(third_anchor_screen.y, control_x->screen_tip.y, 0.001f);

  click_passive_curve_selection(harness, third_anchor_screen);
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(curve.control_selection().active->point,
            third.id);
  EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());

  // Away from actual controls, the ordinary object gizmo still owns its
  // press and starts the editor transform backend.
  ASSERT_TRUE(replace_curve_control_selection(curve));
  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  object_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(object_gizmo, nullptr);
  const auto *object_z = find_curve_gizmo_axis(
      *object_gizmo, quader_editor_core::TransformAxis::Z);
  ASSERT_NE(object_z, nullptr);
  const QVec2 object_press = axis_screen_point(*object_z, 0.65f);
  EXPECT_FALSE(pick_unoccluded_viewport_curve_control(
                   harness.camera, curve.preview_state(), object_press,
                   harness.mesh_surface_distance)
                   .has_value());
  harness.input.mouse_local = object_press;
  harness.input.mouse_screen = object_press;
  const auto object_press_result =
      harness.controller.handle_before_active_tool(
          harness.editor, passive_frame(true, true, false, object_press),
          &curve);
  EXPECT_TRUE(object_press_result.gizmo_consumed);
  EXPECT_TRUE(harness.controller.transform_gizmo_pointer_active());
  harness.input.mouse_local = object_press + QVec2{0.0f, -40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  EXPECT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      passive_frame(false, true, false, object_press), &curve)
                  .gizmo_consumed);
  EXPECT_TRUE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
}

TEST(CurveToolServiceTests,
     SelectedControlMoveGizmoUsesAxisAndPlaneConstraintsAndOneHistoryEdit) {
  /// Describes one control kind and gizmo constraint exercised by this test.
  struct MoveCase {
    CurveControlKind kind;
    quader_editor_core::TransformAxis axis;
    bool plane;
    const char *label;
  };
  constexpr std::array cases{
      MoveCase{CurveControlKind::Anchor,
               quader_editor_core::TransformAxis::X, false, "anchor x"},
      MoveCase{CurveControlKind::OutgoingHandle,
               quader_editor_core::TransformAxis::Xz, true, "handle xz"},
  };

  for (const MoveCase &move_case : cases) {
    SCOPED_TRACE(move_case.label);
    CurveToolHarness harness;
    harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    harness.router.set_active_tool(ViewportToolId::Curve);
    harness.input.active_tool_owns_viewport = true;
    ASSERT_TRUE(harness.settings.set_bool_value(
        ViewportBoolSettingId::GridSnap, false));
    ASSERT_TRUE(harness.settings.set_bool_value(
        ViewportBoolSettingId::VertexSnap, false));
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(QuaderTransformSpace::Global)
                    .ok());

    CurveToolService &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const CurveControlTarget target{.point = before.points()[0].id,
                                    .kind = move_case.kind};
    ASSERT_TRUE(replace_curve_control_selection(curve, target));
    const std::optional<QVec3> pivot_before =
        curve.selected_controls_world_pivot();
    ASSERT_TRUE(pivot_before.has_value());
    harness.session.history().clear();
    harness.session.mark_clean();

    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), &curve);
    const quader_editor_core::TransformGizmoFrame *gizmo =
        harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    EXPECT_EQ(gizmo->info.tool,
              quader_editor_core::ViewportInteractionTool::Move);
    EXPECT_NEAR(gizmo->info.pivot.x, pivot_before->x, 0.0001f);
    EXPECT_NEAR(gizmo->info.pivot.y, pivot_before->y, 0.0001f);
    EXPECT_NEAR(gizmo->info.pivot.z, pivot_before->z, 0.0001f);
    const auto first_stats =
        harness.controller.transform_gizmo_selection_cache_stats();
    EXPECT_EQ(first_stats.curve_control_pivot_recompute_count, 1u);
    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), &curve);
    const auto cached_stats =
        harness.controller.transform_gizmo_selection_cache_stats();
    EXPECT_EQ(cached_stats.curve_control_pivot_recompute_count,
              first_stats.curve_control_pivot_recompute_count);
    EXPECT_GT(cached_stats.curve_control_cache_hit_count,
              first_stats.curve_control_cache_hit_count);
    gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);

    QVec2 start;
    QVec2 update;
    if (move_case.plane) {
      const auto *plane = find_curve_gizmo_plane(*gizmo, move_case.axis);
      ASSERT_NE(plane, nullptr);
      start = plane_screen_center(*plane);
      update = start + QVec2{72.0f, -54.0f};
    } else {
      const auto *axis = find_curve_gizmo_axis(*gizmo, move_case.axis);
      ASSERT_NE(axis, nullptr);
      const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
      const float screen_axis_length =
          std::sqrt(screen_axis.x * screen_axis.x +
                    screen_axis.y * screen_axis.y);
      ASSERT_GT(screen_axis_length, 1.0f);
      const QVec2 direction = screen_axis * (1.0f / screen_axis_length);
      start = axis_screen_point(*axis, 0.3f);
      update = start + direction * 96.0f;
    }

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    const auto press = harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(true, true, false, start), &curve);
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(harness.controller.dragging());

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const auto drag = harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(false, true, false, start), &curve);
    ASSERT_TRUE(drag.gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    ASSERT_TRUE(curve.dragging());
    ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

    const auto release = harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(false, false, true, start), &curve);
    ASSERT_TRUE(release.gizmo_consumed);
    EXPECT_FALSE(release.selection_click_requested);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(curve.dragging());
    ASSERT_TRUE(curve.control_selection().active.has_value());
    EXPECT_EQ(*curve.control_selection().active, target);
    ASSERT_TRUE(harness.session.history().can_undo());

    const auto edited = *curve.preview_state().document;
    const std::optional<QVec3> pivot_after =
        curve.selected_controls_world_pivot();
    ASSERT_TRUE(pivot_after.has_value());
    const QVec3 delta = *pivot_after - *pivot_before;
    if (move_case.plane) {
      EXPECT_NEAR(delta.y, 0.0f, 0.001f);
      EXPECT_GT(std::abs(delta.x) + std::abs(delta.z), 0.01f);
      expect_point(edited.points()[0].position,
                   {before.points()[0].position.x,
                    before.points()[0].position.y,
                    before.points()[0].position.z});
      expect_point(edited.points()[0].incoming_handle,
                   {before.points()[0].incoming_handle.x,
                    before.points()[0].incoming_handle.y,
                    before.points()[0].incoming_handle.z});
    } else {
      EXPECT_GT(std::abs(delta.x), 0.01f);
      EXPECT_NEAR(delta.y, 0.0f, 0.001f);
      EXPECT_NEAR(delta.z, 0.0f, 0.001f);
      expect_point(edited.points()[0].incoming_handle,
                   {before.points()[0].incoming_handle.x,
                    before.points()[0].incoming_handle.y,
                    before.points()[0].incoming_handle.z});
      expect_point(edited.points()[0].outgoing_handle,
                   {before.points()[0].outgoing_handle.x,
                    before.points()[0].outgoing_handle.y,
                    before.points()[0].outgoing_handle.z});
    }
    expect_point(edited.points()[1].position,
                 {before.points()[1].position.x,
                  before.points()[1].position.y,
                  before.points()[1].position.z});
    expect_point(edited.points()[1].incoming_handle,
                 {before.points()[1].incoming_handle.x,
                  before.points()[1].incoming_handle.y,
                  before.points()[1].incoming_handle.z});

    ASSERT_TRUE(harness.editor.history().undo().ok());
    ASSERT_TRUE(curve.refresh().ok);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_FALSE(harness.session.history().can_undo())
        << "one gizmo gesture must create exactly one history item";
    ASSERT_TRUE(curve.control_selection().active.has_value());
    EXPECT_EQ(*curve.control_selection().active, target);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    ASSERT_TRUE(curve.refresh().ok);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(edited));
  }
}

TEST(CurveToolServiceTests,
     AnchorAxisGizmoUsesMeshVertexSnapAndPublishesCanonicalOverlay) {
  struct AxisCase {
    quader_editor_core::TransformAxis axis;
    QVec3 target_offset;
    const char *label;
  };
  constexpr std::array cases{
      AxisCase{quader_editor_core::TransformAxis::X, {7.0f, 0.0f, 0.0f},
               "X"},
      AxisCase{quader_editor_core::TransformAxis::Y, {0.0f, 7.0f, 0.0f},
               "Y"},
      AxisCase{quader_editor_core::TransformAxis::Z, {0.0f, 0.0f, 7.0f},
               "Z"},
  };

  for (const AxisCase &axis_case : cases) {
    SCOPED_TRACE(axis_case.label);
    CurveToolHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
    harness.make_curve();
    harness.activate_curve_edit();
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::SnappingEnabled, true);
    ASSERT_TRUE(harness.settings.bool_value(
        ViewportBoolSettingId::SnappingEnabled));
    ASSERT_TRUE(harness.settings.set_bool_value(
        ViewportBoolSettingId::GridSnap, false));
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::VertexSnap, true);
    ASSERT_TRUE(
        harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(QuaderTransformSpace::Global)
                    .ok());

    CurveToolService &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const CurveControlTarget anchor{before.points()[0].id,
                                    CurveControlKind::Anchor};
    ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
    const std::optional<QVec3> pivot_before =
        curve.selected_controls_world_pivot();
    ASSERT_TRUE(pivot_before.has_value());
    const QVec3 snap_world = *pivot_before + axis_case.target_offset;
    constexpr int kSnapObjectId = 73;
    constexpr quader_poly::ElementId kSnapVertexId = 19;
    int snap_query_count = 0;
    double queried_radius = 0.0;
    harness.mesh_snap_query =
        [&](const ViewportRay &, double vertex_radius)
        -> ViewportMeshSnapQueryResult {
      ++snap_query_count;
      queried_radius = vertex_radius;
      return {
          .vertex_target = ViewportMeshVertexSnapTarget{
              .modeling_object_id = kSnapObjectId,
              .vertex_id = kSnapVertexId,
              .world_position = snap_world,
              .ray_distance = 4.0,
              .pick_distance = 0.0,
          },
          .nearest_surface_ray_distance = 4.0,
      };
    };

    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), &curve);
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto *axis = find_curve_gizmo_axis(*gizmo, axis_case.axis);
    ASSERT_NE(axis, nullptr);
    const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
    const float screen_axis_length =
        std::sqrt(screen_axis.x * screen_axis.x +
                  screen_axis.y * screen_axis.y);
    ASSERT_GT(screen_axis_length, 1.0f);
    const QVec2 start = axis_screen_point(*axis, 0.65f);
    const QVec2 update =
        start + screen_axis * (96.0f / screen_axis_length);

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(true, true, false, start), &curve)
                    .gizmo_consumed);
    EXPECT_EQ(snap_query_count, 0);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, true, false, start), &curve)
                    .gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    ASSERT_TRUE(curve.dragging());
    EXPECT_EQ(snap_query_count, 1);
    EXPECT_DOUBLE_EQ(
        queried_radius,
        harness.settings.float_value(ViewportFloatSettingId::PickVertexRadius));

    const std::optional<QVec3> snapped_pivot =
        curve.selected_controls_world_pivot();
    ASSERT_TRUE(snapped_pivot.has_value());
    EXPECT_NEAR(snapped_pivot->x, snap_world.x, 0.0001f);
    EXPECT_NEAR(snapped_pivot->y, snap_world.y, 0.0001f);
    EXPECT_NEAR(snapped_pivot->z, snap_world.z, 0.0001f);

    const auto &snap_points =
        harness.controller.transform_gizmo_snap_overlay_points();
    ASSERT_EQ(snap_points.size(), 2u);
    const quader_editor_core::SelectionOverlayStyle default_style;
    EXPECT_EQ(snap_points[0].layer,
              quader_editor_core::SelectionOverlayLayer::HoverVertex);
    EXPECT_EQ(snap_points[1].layer,
              quader_editor_core::SelectionOverlayLayer::SnapTargetVertex);
    EXPECT_EQ(snap_points[0].color_abgr,
              default_style.hover_vertex_color_abgr);
    EXPECT_EQ(snap_points[1].color_abgr,
              default_style.snap_target_vertex_color_abgr);
    EXPECT_FLOAT_EQ(snap_points[0].size_pixels,
                    default_style.vertex_point_size_pixels +
                        default_style.selected_hover_point_growth_pixels);
    EXPECT_FLOAT_EQ(snap_points[1].size_pixels,
                    default_style.snap_target_point_size_pixels);
    for (const auto &point : snap_points) {
      EXPECT_NEAR(point.position.x, snap_world.x, 0.0001f);
      EXPECT_NEAR(point.position.y, snap_world.y, 0.0001f);
      EXPECT_NEAR(point.position.z, snap_world.z, 0.0001f);
      EXPECT_EQ(point.object_id, kSnapObjectId);
      EXPECT_EQ(point.vertex_id, kSnapVertexId);
    }

    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, false, true, start), &curve)
                    .gizmo_consumed);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(curve.dragging());
    EXPECT_EQ(snap_query_count, 1);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());
  }
}

TEST(CurveToolServiceTests,
     AnchorSnapOverlayReusesStorageAndClearsOnMissToggleAndCancel) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, true);
  ASSERT_TRUE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::VertexSnap, true);
  ASSERT_TRUE(harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  CurveToolService &curve = harness.session.curve();
  const modeling::CurveDocument before = *curve.preview_state().document;
  const CurveControlTarget anchor{before.points()[0].id,
                                  CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
  const QVec3 pivot = *curve.selected_controls_world_pivot();
  const QVec3 snap_world = pivot + QVec3{6.0f, 0.0f, 0.0f};
  bool snap_available = true;
  int snap_query_count = 0;
  harness.mesh_snap_query =
      [&](const ViewportRay &, double) -> ViewportMeshSnapQueryResult {
    ++snap_query_count;
    ViewportMeshSnapQueryResult result;
    if (snap_available) {
      result.vertex_target = ViewportMeshVertexSnapTarget{
          .modeling_object_id = 94,
          .vertex_id = 27,
          .world_position = snap_world,
          .ray_distance = 4.0,
          .pick_distance = 0.0,
      };
    }
    return result;
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  QVec2 update = start + QVec2{64.0f, 0.0f};
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  const auto &first_overlay =
      harness.controller.transform_gizmo_snap_overlay_points();
  ASSERT_EQ(first_overlay.size(), 2u);
  const auto *overlay_storage = first_overlay.data();
  const std::size_t overlay_capacity = first_overlay.capacity();
  ASSERT_GE(overlay_capacity, 2u);

  snap_available = false;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());

  snap_available = true;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  const auto &reused_overlay =
      harness.controller.transform_gizmo_snap_overlay_points();
  ASSERT_EQ(reused_overlay.size(), 2u);
  EXPECT_EQ(reused_overlay.data(), overlay_storage);
  EXPECT_EQ(reused_overlay.capacity(), overlay_capacity);

  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::VertexSnap, false));
  const int query_count_before_toggle = snap_query_count;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  EXPECT_EQ(snap_query_count, query_count_before_toggle);
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());

  (void)harness.settings.set_bool_value(ViewportBoolSettingId::VertexSnap,
                                        true);
  ASSERT_TRUE(
      harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_EQ(harness.controller.transform_gizmo_snap_overlay_points().size(),
            2u);
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
}

TEST(CurveToolServiceTests,
     PassiveAnchorGizmoSnapsToUnselectedAnchorOnSameCurve) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_active_tool(ViewportToolId::Select);
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, true);
  ASSERT_TRUE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  (void)harness.settings.set_bool_value(ViewportBoolSettingId::VertexSnap,
                                        true);
  ASSERT_TRUE(
      harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(QuaderTransformSpace::Global)
                  .ok());

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_FALSE(curve.active());
  ASSERT_TRUE(curve.controls_editable());
  const CurveToolPreviewState preview = curve.preview_state();
  ASSERT_TRUE(preview.document.has_value());
  ASSERT_EQ(preview.document->point_count(), 2u);
  const CurveControlTarget source{preview.document->points()[0].id,
                                  CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, source));
  const QVec3 target_world = transform_point(
      preview.world_transform,
      curve_control_local_position(preview.document->points()[1],
                                   CurveControlKind::Anchor));
  harness.curve_snap_candidates.push_back({
      .entity = object.entity,
      .world_transform = preview.world_transform,
      .document =
          std::make_shared<const modeling::CurveDocument>(*preview.document),
  });
  std::size_t mesh_snap_queries = 0u;
  std::size_t separate_surface_queries = 0u;
  harness.mesh_snap_query =
      [&](const ViewportRay &, double) -> ViewportMeshSnapQueryResult {
    ++mesh_snap_queries;
    return {.nearest_surface_ray_distance = 1000.0};
  };
  harness.mesh_surface_distance =
      [&](const ViewportRay &) -> std::optional<double> {
    ++separate_surface_queries;
    return 1.0;
  };
  harness.session.history().clear();
  harness.session.mark_clean();

  const auto passive_frame = [&](bool pressed = false, bool down = false,
                                 bool released = false,
                                 QVec2 click_local = QVec2{}) {
    ViewportInteractionFrame frame =
        harness.gizmo_frame(pressed, down, released, click_local);
    frame.active_tool_allows_selection_fallback = true;
    return frame;
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  const ViewportScreenPoint projected_target =
      harness.camera.project_world_point(target_world);
  ASSERT_TRUE(projected_target.visible);
  const QVec2 update = projected_target.position;

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor, passive_frame(true, true, false, start),
                      &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor, passive_frame(false, true, false, start),
                      &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  EXPECT_EQ(mesh_snap_queries, 1u);
  EXPECT_EQ(separate_surface_queries, 0u)
      << "Curve snapping must reuse the surface depth prepared by the mesh "
         "snap query.";
  const std::optional<QVec3> snapped = curve.selected_controls_world_pivot();
  ASSERT_TRUE(snapped.has_value());
  EXPECT_NEAR(snapped->x, target_world.x, 0.0001f);
  EXPECT_NEAR(snapped->y, target_world.y, 0.0001f);
  EXPECT_NEAR(snapped->z, target_world.z, 0.0001f);

  const auto &snap_points =
      harness.controller.transform_gizmo_snap_overlay_points();
  ASSERT_EQ(snap_points.size(), 2u);
  for (const auto &point : snap_points) {
    EXPECT_NEAR(point.position.x, target_world.x, 0.0001f);
    EXPECT_NEAR(point.position.y, target_world.y, 0.0001f);
    EXPECT_NEAR(point.position.z, target_world.z, 0.0001f);
    EXPECT_EQ(point.object_id, -1);
    EXPECT_EQ(point.vertex_id, quader_poly::kInvalidElementId);
  }

  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      passive_frame(false, false, true, start), &curve)
                  .gizmo_consumed);
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());
  ASSERT_TRUE(harness.session.history().can_undo());
  const auto edited = curve.preview_state().document;
  ASSERT_TRUE(edited.has_value());
  expect_point(edited->points()[0].position, target_world);
  EXPECT_EQ(edited->points()[1].id, preview.document->points()[1].id);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(
      *preview.document));
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     AuthoredOtherCurveAndMeshTargetsUseDeterministicCrossDomainDepthOrdering) {
  struct CompetitionCase {
    double mesh_depth_delta;
    bool expect_curve_target;
    const char *label;
  };
  constexpr std::array cases{
      CompetitionCase{0.0, false, "exact depth and pick tie prefers mesh"},
      CompetitionCase{2.0e-8, true,
                      "Curve closer beyond depth epsilon prefers Curve"},
  };

  for (const CompetitionCase &competition : cases) {
    SCOPED_TRACE(competition.label);
    CurveToolHarness harness;
    const QuaderModelingObjectRef source_object = harness.make_curve();
    const auto target_created = harness.editor.modeling().create_curve({
        .points = {{.position = {5.0f, 0.0f, 0.0f}},
                   {.position = {7.0f, 0.0f, 0.0f}}},
        .name = "Independent Snap Target",
    });
    ASSERT_TRUE(target_created.ok()) << target_created.error_message();
    const QuaderModelingObjectRef target_object = target_created.value();
    const auto target_snapshot =
        harness.editor.modeling().curve_snapshot(target_object);
    ASSERT_TRUE(target_snapshot.ok()) << target_snapshot.error_message();
    ASSERT_EQ(target_snapshot.value().document.point_count(), 2u);
    const modeling::CurveControlPoint target_anchor =
        target_snapshot.value().document.points()[0];
    const QVec3 target_world{target_anchor.position.x, target_anchor.position.y,
                             target_anchor.position.z};
    const QVec3 mesh_world = target_world + QVec3{1.0f, 0.0f, 0.0f};

    ASSERT_TRUE(harness.editor.selection().clear().ok());
    ASSERT_TRUE(
        harness.editor.selection().select_object(source_object).ok());
    harness.router.set_active_tool(ViewportToolId::Select);
    harness.router.set_selection_mode(ViewportSelectionMode::Object);
    harness.input.active_tool_owns_viewport = false;
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::SnappingEnabled, true);
    ASSERT_TRUE(harness.settings.bool_value(
        ViewportBoolSettingId::SnappingEnabled));
    ASSERT_TRUE(harness.settings.set_bool_value(
        ViewportBoolSettingId::GridSnap, false));
    (void)harness.settings.set_bool_value(ViewportBoolSettingId::VertexSnap,
                                          true);
    ASSERT_TRUE(
        harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(QuaderTransformSpace::Global)
                    .ok());

    CurveToolService &curve = harness.session.curve();
    ASSERT_TRUE(curve.refresh().ok);
    ASSERT_FALSE(curve.active());
    ASSERT_TRUE(curve.controls_editable());
    const CurveToolPreviewState source_preview = curve.preview_state();
    ASSERT_TRUE(source_preview.document.has_value());
    const CurveControlTarget source_anchor{
        source_preview.document->points()[0].id, CurveControlKind::Anchor};
    ASSERT_TRUE(replace_curve_control_selection(curve, source_anchor));

    ViewportCurvePickCache candidate_cache;
    const auto authored_candidates = candidate_cache.candidates(
        harness.session.scene(), harness.session.assets(), 1u,
        [](EntityId) { return true; });
    harness.curve_snap_candidates.assign(authored_candidates.begin(),
                                         authored_candidates.end());
    ASSERT_TRUE(std::any_of(
        harness.curve_snap_candidates.begin(),
        harness.curve_snap_candidates.end(),
        [target_object](const ViewportSceneCurvePickCandidate &candidate) {
          return candidate.entity == target_object.entity;
        }));

    harness.mesh_snap_query =
        [&](const ViewportRay &ray,
            double) -> ViewportMeshSnapQueryResult {
      const QVec3 from_origin = target_world - ray.origin;
      const double curve_ray_distance =
          static_cast<double>(dot(from_origin, ray.direction));
      const QVec3 closest =
          ray.origin +
          ray.direction * static_cast<float>(curve_ray_distance);
      const double curve_pick_distance = static_cast<double>(
          std::sqrt(length_squared(target_world - closest)));
      const double mesh_ray_distance =
          curve_ray_distance + competition.mesh_depth_delta;
      return {
          .vertex_target = ViewportMeshVertexSnapTarget{
              .modeling_object_id = 991,
              .vertex_id = 211,
              .world_position = mesh_world,
              .ray_distance = mesh_ray_distance,
              .pick_distance = curve_pick_distance,
          },
          .nearest_surface_ray_distance = mesh_ray_distance,
      };
    };

    const auto passive_frame = [&](bool pressed = false, bool down = false,
                                   bool released = false,
                                   QVec2 click_local = QVec2{}) {
      ViewportInteractionFrame frame =
          harness.gizmo_frame(pressed, down, released, click_local);
      frame.active_tool_allows_selection_fallback = true;
      return frame;
    };
    (void)harness.controller.handle_before_active_tool(
        harness.editor, passive_frame(), &curve);
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto *axis = find_curve_gizmo_axis(
        *gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const QVec2 start = axis_screen_point(*axis, 0.65f);
    const ViewportScreenPoint target_screen =
        harness.camera.project_world_point(target_world);
    ASSERT_TRUE(target_screen.visible);

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(true, true, false, start), &curve)
                    .gizmo_consumed);
    harness.input.mouse_local = target_screen.position;
    harness.input.mouse_screen = target_screen.position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(false, true, false, start), &curve)
                    .gizmo_consumed);
    ASSERT_TRUE(curve.dragging());

    const QVec3 expected =
        competition.expect_curve_target ? target_world : mesh_world;
    const std::optional<QVec3> snapped =
        curve.selected_controls_world_pivot();
    ASSERT_TRUE(snapped.has_value());
    EXPECT_NEAR(snapped->x, expected.x, 0.0001f);
    EXPECT_NEAR(snapped->y, expected.y, 0.0001f);
    EXPECT_NEAR(snapped->z, expected.z, 0.0001f);
    const auto &snap_points =
        harness.controller.transform_gizmo_snap_overlay_points();
    ASSERT_EQ(snap_points.size(), 2u);
    for (const auto &point : snap_points) {
      EXPECT_NEAR(point.position.x, expected.x, 0.0001f);
      EXPECT_NEAR(point.position.y, expected.y, 0.0001f);
      EXPECT_NEAR(point.position.z, expected.z, 0.0001f);
      EXPECT_EQ(point.object_id,
                competition.expect_curve_target ? -1 : 991);
      EXPECT_EQ(point.vertex_id,
                competition.expect_curve_target
                    ? quader_poly::kInvalidElementId
                    : quader_poly::ElementId{211});
    }
    EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  }
}

TEST(CurveToolServiceTests,
     ActiveCurveDraftKeepsSelectedMeshEligibleForVertexSnap) {
  CurveToolHarness harness;
  const auto mesh = harness.editor.modeling().create_box(
      {.min = {5.0f, -1.0f, -1.0f}, .max = {7.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(mesh.value()).ok());
  ASSERT_TRUE(harness.editor.modeling()
                  .set_selection_mode(QuaderSelectionMode::Object)
                  .ok());

  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, true);
  ASSERT_TRUE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  (void)harness.settings.set_bool_value(ViewportBoolSettingId::VertexSnap,
                                        true);
  ASSERT_TRUE(
      harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(QuaderTransformSpace::Global)
                  .ok());
  const double vertex_radius = harness.settings.float_value(
      ViewportFloatSettingId::PickVertexRadius);

  constexpr std::array<QVec3, 8u> box_corners{
      QVec3{5.0f, -1.0f, -1.0f}, QVec3{5.0f, -1.0f, 1.0f},
      QVec3{5.0f, 1.0f, -1.0f},  QVec3{5.0f, 1.0f, 1.0f},
      QVec3{7.0f, -1.0f, -1.0f}, QVec3{7.0f, -1.0f, 1.0f},
      QVec3{7.0f, 1.0f, -1.0f},  QVec3{7.0f, 1.0f, 1.0f},
  };
  std::optional<ViewportRay> eligible_ray;
  std::optional<ModelingMeshVertexSnapTarget> eligible_target;
  for (const QVec3 corner : box_corners) {
    const ViewportScreenPoint screen =
        harness.camera.project_world_point(corner);
    if (!screen.visible) {
      continue;
    }
    const ViewportRay ray =
        harness.camera.ray_from_viewport_point(screen.position);
    const ModelingMeshSnapQueryResult query =
        harness.session.modeling().pick_mesh_snap_query(
            {.origin = ray.origin, .direction = ray.direction}, vertex_radius,
            false);
    if (query.vertex_target.has_value()) {
      eligible_ray = ray;
      eligible_target = query.vertex_target;
      break;
    }
  }
  ASSERT_TRUE(eligible_ray.has_value());
  ASSERT_TRUE(eligible_target.has_value());
  EXPECT_EQ(eligible_target->modeling_object_id,
            mesh.value().modeling_object_id);
  const ModelingMeshSnapQueryResult excluded =
      harness.session.modeling().pick_mesh_snap_query(
          {.origin = eligible_ray->origin, .direction = eligible_ray->direction},
          vertex_radius, true);
  EXPECT_FALSE(excluded.vertex_target.has_value())
      << "The selected mesh is excluded only for a moving mesh selection.";

  harness.activate_curve_edit();
  const QVec3 target_world = eligible_target->world_position;
  harness.place({0.0f, target_world.y, target_world.z});
  harness.place({2.0f, target_world.y, target_world.z});
  CurveToolService &curve = harness.session.curve();
  const CurveToolPreviewState before = curve.preview_state();
  ASSERT_TRUE(before.active);
  ASSERT_TRUE(before.drawing);
  ASSERT_TRUE(before.document.has_value());
  ASSERT_TRUE(harness.editor.query().entity(mesh.value().entity)->selected);
  const CurveControlTarget source_anchor{before.document->points()[0].id,
                                         CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, source_anchor));

  std::size_t real_mesh_queries = 0u;
  harness.mesh_snap_query =
      [&](const ViewportRay &ray,
          double radius) -> ViewportMeshSnapQueryResult {
    ++real_mesh_queries;
    const QuaderMeshSnapQueryResult query =
        harness.editor.query().mesh_snap_query(
            {.origin = ray.origin, .direction = ray.direction}, radius, false);
    ViewportMeshSnapQueryResult result{
        .nearest_surface_ray_distance = query.nearest_surface_ray_distance,
    };
    if (query.vertex_target.has_value()) {
      result.vertex_target = ViewportMeshVertexSnapTarget{
          .modeling_object_id = query.vertex_target->modeling_object_id,
          .vertex_id =
              static_cast<quader_poly::ElementId>(query.vertex_target->vertex_id),
          .world_position = query.vertex_target->world_position,
          .ray_distance = query.vertex_target->ray_distance,
          .pick_distance = query.vertex_target->pick_distance,
      };
    }
    return result;
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  const ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_world);
  ASSERT_TRUE(target_screen.visible);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  EXPECT_EQ(real_mesh_queries, 1u);
  const std::optional<QVec3> snapped = curve.selected_controls_world_pivot();
  ASSERT_TRUE(snapped.has_value());
  EXPECT_NEAR(snapped->x, target_world.x, 0.0001f);
  EXPECT_NEAR(snapped->y, target_world.y, 0.0001f);
  EXPECT_NEAR(snapped->z, target_world.z, 0.0001f);
  const auto &snap_points =
      harness.controller.transform_gizmo_snap_overlay_points();
  ASSERT_EQ(snap_points.size(), 2u);
  for (const auto &point : snap_points) {
    EXPECT_EQ(point.object_id, eligible_target->modeling_object_id);
    EXPECT_EQ(point.vertex_id, eligible_target->vertex_id);
    EXPECT_NEAR(point.position.x, target_world.x, 0.0001f);
    EXPECT_NEAR(point.position.y, target_world.y, 0.0001f);
    EXPECT_NEAR(point.position.z, target_world.z, 0.0001f);
  }
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(
      *before.document));
}

TEST(CurveToolServiceTests,
     DraftMultiAnchorGizmoSnapsToUnselectedAnchorWithFrozenExclusion) {
  CurveToolHarness harness;
  harness.activate_curve_edit();
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, true);
  ASSERT_TRUE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::VertexSnap, true);
  ASSERT_TRUE(harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(QuaderTransformSpace::Global)
                  .ok());

  constexpr std::size_t kSelectedPointCount = 16u;
  for (std::size_t index = 0; index <= kSelectedPointCount; ++index) {
    harness.place({-4.0f + static_cast<float>(index) * 0.5f, 0.0f, 0.0f});
  }
  CurveToolService &curve = harness.session.curve();
  const CurveToolPreviewState before = curve.preview_state();
  ASSERT_TRUE(before.active);
  ASSERT_TRUE(before.drawing);
  ASSERT_TRUE(before.document.has_value());
  ASSERT_EQ(before.document->point_count(), kSelectedPointCount + 1u);
  ASSERT_TRUE(harness.curve_snap_candidates.empty())
      << "Uncommitted drafts never appear in scene Curve candidates.";

  std::vector<CurveControlTarget> selected;
  selected.reserve(kSelectedPointCount);
  for (std::size_t index = 0; index < kSelectedPointCount; ++index) {
    selected.push_back({before.document->points()[index].id,
                        CurveControlKind::Anchor});
  }
  ASSERT_TRUE(curve.edit_control_selection(
      selected, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      selected.back()));
  const modeling::CurveControlPoint target_point =
      before.document->points().back();
  const QVec3 target_world = transform_point(
      before.world_transform,
      {target_point.position.x, target_point.position.y,
       target_point.position.z});

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  const ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_world);
  ASSERT_TRUE(target_screen.visible);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  const std::optional<QVec3> snapped_pivot =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(snapped_pivot.has_value());
  EXPECT_NEAR(snapped_pivot->x, target_world.x, 0.0001f);
  EXPECT_NEAR(snapped_pivot->y, target_world.y, 0.0001f);
  EXPECT_NEAR(snapped_pivot->z, target_world.z, 0.0001f);
  ASSERT_EQ(harness.controller.transform_gizmo_snap_overlay_points().size(),
            2u);
  EXPECT_EQ(curve.control_selection().controls.size(), kSelectedPointCount);

  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, false, true, start), &curve)
                  .gizmo_consumed);
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());
  EXPECT_FALSE(harness.session.history().can_undo())
      << "Draft control edits stay inside the uncommitted Curve preview.";
}

TEST(CurveToolServiceTests,
     HandleAndPlaneGizmosDoNotQueryMeshVertexSnap) {
  struct IneligibleCase {
    CurveControlKind control;
    quader_editor_core::TransformAxis axis;
    bool plane;
    const char *label;
  };
  constexpr std::array cases{
      IneligibleCase{CurveControlKind::OutgoingHandle,
                     quader_editor_core::TransformAxis::X, false,
                     "handle on X axis"},
      IneligibleCase{CurveControlKind::Anchor,
                     quader_editor_core::TransformAxis::Xz, true,
                     "anchor on XZ plane"},
  };

  for (const IneligibleCase &ineligible : cases) {
    SCOPED_TRACE(ineligible.label);
    CurveToolHarness harness;
    harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
    harness.make_curve();
    harness.activate_curve_edit();
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::SnappingEnabled, true);
    ASSERT_TRUE(harness.settings.bool_value(
        ViewportBoolSettingId::SnappingEnabled));
    ASSERT_TRUE(harness.settings.set_bool_value(
        ViewportBoolSettingId::GridSnap, false));
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::VertexSnap, true);
    ASSERT_TRUE(
        harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
    ASSERT_TRUE(harness.editor.viewports()
                    .set_transform_space(QuaderTransformSpace::Global)
                    .ok());

    CurveToolService &curve = harness.session.curve();
    if (ineligible.plane) {
      const auto initial = curve.preview_state().document;
      ASSERT_TRUE(initial.has_value());
      ASSERT_TRUE(
          curve.toggle_point_smoothness(initial->points()[0].id).changed);
    }
    const auto before = *curve.preview_state().document;
    const CurveControlTarget target{before.points()[0].id,
                                    ineligible.control};
    if (!curve.control_selection().contains(target)) {
      ASSERT_TRUE(replace_curve_control_selection(curve, target));
    }
    ASSERT_TRUE(curve.control_selection().contains(target));
    int snap_query_count = 0;
    harness.mesh_snap_query =
        [&](const ViewportRay &, double)
        -> ViewportMeshSnapQueryResult {
      ++snap_query_count;
      return {
          .vertex_target = ViewportMeshVertexSnapTarget{
              .modeling_object_id = 81,
              .vertex_id = 23,
              .world_position = {20.0f, 20.0f, 20.0f},
              .ray_distance = 4.0,
              .pick_distance = 0.0,
          },
          .nearest_surface_ray_distance = 4.0,
      };
    };

    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), &curve);
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    QVec2 start;
    QVec2 update;
    if (ineligible.plane) {
      const auto *plane = find_curve_gizmo_plane(*gizmo, ineligible.axis);
      ASSERT_NE(plane, nullptr);
      start = plane_screen_center(*plane);
      update = start + QVec2{72.0f, -54.0f};
    } else {
      const auto *axis = find_curve_gizmo_axis(*gizmo, ineligible.axis);
      ASSERT_NE(axis, nullptr);
      const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
      const float screen_axis_length =
          std::sqrt(screen_axis.x * screen_axis.x +
                    screen_axis.y * screen_axis.y);
      ASSERT_GT(screen_axis_length, 1.0f);
      start = axis_screen_point(*axis, 0.65f);
      update = start + screen_axis * (96.0f / screen_axis_length);
    }

    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(true, true, false, start), &curve)
                    .gizmo_consumed);
    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, true, false, start), &curve)
                    .gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    ASSERT_TRUE(curve.dragging());
    EXPECT_FALSE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_EQ(snap_query_count, 0);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());

    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, false, true, start), &curve)
                    .gizmo_consumed);
    EXPECT_EQ(snap_query_count, 0);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());
  }
}

TEST(CurveToolServiceTests,
     CancelingSelectedControlMoveGizmoRestoresGeometryAndSelection) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.router.set_active_tool(ViewportToolId::Curve);
  harness.input.active_tool_owns_viewport = true;
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(replace_curve_control_selection(curve, target));
  harness.session.history().clear();
  harness.session.mark_clean();

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
  const float screen_axis_length =
      std::sqrt(screen_axis.x * screen_axis.x +
                screen_axis.y * screen_axis.y);
  ASSERT_GT(screen_axis_length, 1.0f);
  const QVec2 start = axis_screen_point(*axis, 0.3f);
  const QVec2 update =
      start + screen_axis * (96.0f / screen_axis_length);

  // Escape/cancel must consume a below-threshold gizmo press without
  // canceling the Curve tool session that owns the selected control.
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  EXPECT_TRUE(curve.active());
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
}

TEST(CurveToolServiceTests,
     PointerCancellationRestoresSelectedControlGizmoPreviewWithoutHistory) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.router.set_active_tool(ViewportToolId::Curve);
  harness.input.active_tool_owns_viewport = true;
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(replace_curve_control_selection(curve, target));
  harness.session.history().clear();
  harness.session.mark_clean();

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
  const float screen_axis_length =
      std::sqrt(screen_axis.x * screen_axis.x +
                screen_axis.y * screen_axis.y);
  ASSERT_GT(screen_axis_length, 1.0f);
  const QVec2 start = axis_screen_point(*axis, 0.3f);
  const QVec2 update =
      start + screen_axis * (96.0f / screen_axis_length);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  harness.input.pointer_interaction_canceled = true;
  const auto canceled = harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(false, false, true, start), &curve);
  harness.input.pointer_interaction_canceled = false;
  EXPECT_TRUE(canceled.gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
}

TEST(CurveToolServiceTests,
     CameraViewAndNavigationInvalidationCancelPendingAndActiveControlGizmos) {
  enum class Invalidation { Camera, View, Navigation };
  /// Associates each invalidation source with its diagnostic trace label.
  struct InvalidationCase {
    Invalidation kind;
    const char *label;
  };
  constexpr std::array invalidations{
      InvalidationCase{Invalidation::Camera, "camera"},
      InvalidationCase{Invalidation::View, "view"},
      InvalidationCase{Invalidation::Navigation, "navigation"},
  };

  for (const InvalidationCase &invalidation : invalidations) {
    for (const bool cross_threshold : {false, true}) {
      SCOPED_TRACE(invalidation.label);
      SCOPED_TRACE(cross_threshold ? "active preview" : "pending press");
      CurveToolHarness harness;
      harness.make_curve();
      ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
      harness.router.set_active_tool(ViewportToolId::Curve);
      harness.input.active_tool_owns_viewport = true;
      ASSERT_TRUE(harness.settings.set_bool_value(
          ViewportBoolSettingId::GridSnap, false));
      CurveToolService &curve = harness.session.curve();
      const auto before = *curve.preview_state().document;
      const CurveControlTarget target{.point = before.points()[0].id};
      ASSERT_TRUE(replace_curve_control_selection(curve, target));
      harness.session.history().clear();
      harness.session.mark_clean();

      (void)harness.controller.handle_before_active_tool(
          harness.editor, harness.gizmo_frame(), &curve);
      const auto *gizmo = harness.controller.transform_gizmo_frame();
      ASSERT_NE(gizmo, nullptr);
      const auto *axis = find_curve_gizmo_axis(
          *gizmo, quader_editor_core::TransformAxis::X);
      ASSERT_NE(axis, nullptr);
      const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
      const float screen_axis_length =
          std::sqrt(screen_axis.x * screen_axis.x +
                    screen_axis.y * screen_axis.y);
      ASSERT_GT(screen_axis_length, 1.0f);
      const QVec2 start = axis_screen_point(*axis, 0.3f);
      const QVec2 update =
          start + screen_axis * (96.0f / screen_axis_length);

      harness.input.mouse_local = start;
      harness.input.mouse_screen = start;
      ASSERT_TRUE(harness.controller
                      .handle_before_active_tool(
                          harness.editor,
                          harness.gizmo_frame(true, true, false, start),
                          &curve)
                      .gizmo_consumed);
      if (cross_threshold) {
        harness.input.mouse_local = update;
        harness.input.mouse_screen = update;
        ASSERT_TRUE(harness.controller
                        .handle_before_active_tool(
                            harness.editor,
                            harness.gizmo_frame(false, true, false, start),
                            &curve)
                        .gizmo_consumed);
        ASSERT_TRUE(harness.controller.dragging());
        ASSERT_TRUE(curve.dragging());
        ASSERT_FALSE(
            curve.preview_state().document->has_same_geometry(before));
      } else {
        ASSERT_FALSE(harness.controller.dragging());
        ASSERT_FALSE(curve.dragging());
        ASSERT_TRUE(curve.preview_state().document->has_same_geometry(before));
      }

      switch (invalidation.kind) {
      case Invalidation::Camera:
        harness.input_result.camera_changed = true;
        break;
      case Invalidation::View:
        harness.input_result.view_changed = true;
        break;
      case Invalidation::Navigation:
        harness.input_result.navigation_active = true;
        break;
      }
      const auto canceled = harness.controller.handle_before_active_tool(
          harness.editor, harness.gizmo_frame(false, true, false, start),
          &curve);
      harness.input_result = {};

      EXPECT_TRUE(canceled.gizmo_consumed);
      EXPECT_FALSE(harness.controller.dragging());
      EXPECT_FALSE(curve.dragging());
      EXPECT_FALSE(harness.controller.cancel_active_drag(harness.editor,
                                                          &curve));
      EXPECT_TRUE(curve.active());
      ASSERT_TRUE(curve.control_selection().active.has_value());
      EXPECT_EQ(*curve.control_selection().active, target);
      EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
      EXPECT_FALSE(harness.session.history().can_undo());
      EXPECT_FALSE(harness.session.dirty());
    }
  }
}

TEST(CurveToolServiceTests,
     ControllerResetRollsBackSelectedControlGizmoAndRetainsSelection) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.router.set_active_tool(ViewportToolId::Curve);
  harness.input.active_tool_owns_viewport = true;
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(replace_curve_control_selection(curve, target));
  harness.session.history().clear();
  harness.session.mark_clean();

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
  const float screen_axis_length =
      std::sqrt(screen_axis.x * screen_axis.x +
                screen_axis.y * screen_axis.y);
  ASSERT_GT(screen_axis_length, 1.0f);
  const QVec2 start = axis_screen_point(*axis, 0.3f);
  const QVec2 update =
      start + screen_axis * (96.0f / screen_axis_length);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  harness.controller.reset(&harness.editor, &curve);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(harness.controller.transform_gizmo_frame(), nullptr);
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(curve.active());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
}

TEST(CurveToolServiceTests,
     SelectedAnchorSemanticConversionRemainsOwnedByCurveInput) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.router.set_active_tool(ViewportToolId::Curve);
  harness.input.active_tool_owns_viewport = true;
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(replace_curve_control_selection(curve, target));
  harness.session.history().clear();

  // This proves the selected anchor has an eligible Move gizmo before the
  // semantic conversion edge reserves the same pointer press for Curve.
  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);
  const QVec2 anchor = harness.camera.project_world_point(transform_point(
      curve.preview_state().world_transform,
      curve_control_local_position(before.points()[0],
                                   CurveControlKind::Anchor))).position;
  harness.input.mouse_local = anchor;
  harness.input.mouse_screen = anchor;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, anchor}};
  harness.resolve_chord("Alt+LeftMouse");
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
  EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::Trigger);

  const auto converted = harness.frame(true, true, false);
  EXPECT_TRUE(converted.curve_consumed);
  EXPECT_TRUE(converted.claim_interaction_pane);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  expect_point(curve.preview_state().document->points()[0].incoming_handle,
               {});
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  EXPECT_TRUE(harness.session.history().can_undo());
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);

  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
}

TEST(CurveToolServiceTests,
     DeferredNonLeftConversionReleaseIsRetiredWhileCurveGizmoConsumesInput) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.router.set_active_tool(ViewportToolId::Curve);
  harness.input.active_tool_owns_viewport = true;
  const QuaderKeybindingActionKey action{
      "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
  ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
      action, "Alt+RightMouseClick").ok());
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget target{.point = before.points()[0].id};
  ASSERT_TRUE(replace_curve_control_selection(curve, target));
  harness.session.history().clear();
  harness.session.mark_clean();

  const QVec2 anchor = harness.camera.project_world_point(transform_point(
      curve.preview_state().world_transform,
      curve_control_local_position(before.points()[0],
                                   CurveControlKind::Anchor))).position;
  harness.input.mouse_local = anchor;
  harness.input.mouse_screen = anchor;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Right, anchor, 1u}};
  harness.resolve_chords({"Alt+RightMouse", "Alt+RightMouseDrag"});
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
  ASSERT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::ReservePress);
  ASSERT_TRUE(harness.dispatch().claim_interaction_pane);
  ASSERT_TRUE(harness.interaction.dragging());
  harness.input.pointer_pressed_edges.clear();
  harness.resolve_chord();

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 gizmo_press = axis_screen_point(*axis, 0.3f);
  harness.input.mouse_local = gizmo_press;
  harness.input.mouse_screen = gizmo_press;
  const auto pressed = harness.controller.handle_before_active_tool(
      harness.editor,
      harness.gizmo_frame(true, true, false, gizmo_press), &curve);
  ASSERT_TRUE(pressed.gizmo_consumed);
  ASSERT_FALSE(harness.controller.dragging());
  ASSERT_FALSE(curve.dragging());

  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Right, anchor, 2u}};
  harness.resolve_chord("Alt+RightMouseClick");
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
  ASSERT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::Trigger);
  const auto held = harness.controller.handle_before_active_tool(
      harness.editor,
      harness.gizmo_frame(false, true, false, gizmo_press), &curve);
  ASSERT_TRUE(held.gizmo_consumed);
  const auto released = harness.dispatch(held.gizmo_consumed);

  EXPECT_TRUE(released.curve_consumed);
  EXPECT_FALSE(released.claim_interaction_pane);
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, &curve));
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  ASSERT_TRUE(harness.editor.preferences().reset_action_shortcut(action).ok());
}

TEST(CurveToolServiceTests, RouterWheelZoomIsInertForASelectedControl) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.modeling().set_curve_closed(object, true).ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.session.mark_clean();
  CurveToolService &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const auto history_before = harness.editor.history().snapshot();
  harness.input.mouse_local = harness.camera.project_world_point(
      transform_point(curve.preview_state().world_transform,
          curve_control_local_position(before.points()[0],
                                        CurveControlKind::Anchor))).position;
  EXPECT_TRUE(harness.curve_native_selection_frame(
      true, true, false, harness.input.mouse_local).curve_consumed);
  EXPECT_TRUE(harness.curve_native_selection_frame(
      false, false, true, harness.input.mouse_local).curve_consumed);
  ASSERT_TRUE(curve.control_selection().active.has_value());
  const CurveControlTarget selected = *curve.control_selection().active;
  EXPECT_FALSE(curve.dragging());
  harness.input.mouse_local.x += 60.0f;
  (void)harness.curve_native_selection_frame(
      false, false, false, harness.input.mouse_local);
  ASSERT_TRUE(curve.preview_state().document->has_same_geometry(before));

  ViewportInputRouter router;
  router.set_active_tool(ViewportToolId::Curve);
  harness.input.mouse_wheel = 1.0f;
  harness.input.navigation_bindings.zoom_wheel = true;
  harness.input_result = router.handle_frame(harness.input, harness.camera);
  ASSERT_TRUE(harness.input_result.camera_changed);
  ASSERT_FALSE(harness.input_result.navigation_active);
  (void)harness.curve_native_selection_frame(
      false, false, false, harness.input.mouse_local);
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(harness.interaction.dragging());
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, selected);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.dirty());
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);
}

TEST(CurveToolServiceTests, DraftDragSurvivesAnUnrelatedOwnerRevisionRefresh) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  (void)harness.frame(true, true, false);
  harness.input.mouse_local.x += 40.0f;
  (void)harness.frame(false, true, false);
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.dragging());
  const auto before = *curve.preview_state().document;
  // An unrelated scene-owner publication must not sever the native pointer
  // ownership of a draft, which has no persisted source to invalidate.
  ASSERT_NE(harness.session.scene().create_entity("Unrelated entity"),
            kInvalidEntityId);
  EXPECT_TRUE(curve.refresh().changed);
  ASSERT_TRUE(curve.dragging());
  ASSERT_TRUE(harness.interaction.dragging());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  harness.input.mouse_local.x += 40.0f;
  (void)harness.frame(false, true, false);
  EXPECT_FALSE(curve.preview_state().document->has_same_geometry(before));
  (void)harness.frame(false, false, true);
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_EQ(curve.point_count(), 1u);
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests, DraftConversionIsTransientAndRejectsAnActiveDrag) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  auto &curve = harness.session.curve();
  harness.place({-2, 0, 0});
  const auto point = curve.preview_state().document->points()[0].id;
  const auto revision = curve.preview_state().revision;
  EXPECT_FALSE(curve.toggle_point_smoothness(point).changed);
  EXPECT_FALSE(curve.toggle_point_smoothness({}).ok);
  EXPECT_EQ(curve.preview_state().revision, revision);
  harness.place({2, 0, 0});
  ASSERT_TRUE(curve.toggle_point_smoothness(point).changed);
  const auto converted = *curve.preview_state().document;
  expect_point(converted.points()[0].position, {-2, 0, 0});
  expect_point(converted.points()[0].outgoing_handle, {4.0f / 3.0f, 0, 0});
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
  ASSERT_TRUE(begin_curve_control_selection_drag(curve, {.point = point}).ok);
  EXPECT_FALSE(curve.toggle_point_smoothness(point).ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(converted));
  ASSERT_TRUE(curve.cancel_drag().ok);
  ASSERT_TRUE(curve.apply().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(converted));
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests, CanonicalAltClickConvertsOnceWithoutDraggingOrSelecting) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.scene().set_transform(object.entity,
      {.position = {3, 0, -2}, .rotation_euler = {0, 35, 0},
       .scale = {2, 1, -0.75f}}).ok());
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.session.history().clear();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const auto world = curve.preview_state().world_transform;
  harness.input.mouse_local = harness.camera.project_world_point(transform_point(
      world, curve_control_local_position(before.points()[0],
                                           CurveControlKind::Anchor))).position;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouse");
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
  EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::Trigger);
  EXPECT_FALSE(harness.input.transfer_face_texture_pressed);
  const auto press = harness.frame(true, true, false);
  EXPECT_TRUE(press.claim_interaction_pane);
  EXPECT_TRUE(press.curve_consumed);
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(harness.interaction.dragging());
  const auto corner = *curve.preview_state().document;
  expect_point(corner.points()[0].incoming_handle, {});
  expect_point(corner.points()[0].outgoing_handle, {});
  EXPECT_EQ(corner.points()[0].id, before.points()[0].id);
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);

  harness.input.mouse_local.x += 90;
  harness.resolve_chord();
  (void)harness.frame(false, true, false);
  harness.resolve_chord();
  (void)harness.frame(false, false, true);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(corner));
  EXPECT_FALSE(harness.interaction.dragging());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());

  // A visible handle is not an anchor, and a conversion miss cannot append a point.
  harness.input.mouse_local = harness.camera.project_world_point(transform_point(
      world, curve_control_local_position(before.points()[0],
                                           CurveControlKind::OutgoingHandle))).position;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouse");
  EXPECT_FALSE(harness.frame(true, true, false).claim_interaction_pane);
  EXPECT_FALSE(curve.dragging());
  harness.resolve_chord();
  (void)harness.frame(false, false, true);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_TRUE(harness.session.history().can_redo());
  ASSERT_TRUE(curve.new_curve().ok);
  harness.input.mouse_local = {400, 300};
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouse");
  EXPECT_FALSE(harness.frame(true, true, false).claim_interaction_pane);
  harness.resolve_chord();
  (void)harness.frame(false, false, true);
  EXPECT_EQ(curve.point_count(), 0u);
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(harness.session.history().can_redo());
}

TEST(CurveToolServiceTests,
     ForegroundMeshDepthPreventsHiddenAltConversionFromChangingCurve) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  harness.session.history().clear();
  CurveToolService &curve = harness.session.curve();
  const modeling::CurveDocument before = *curve.preview_state().document;
  const QVec2 anchor_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const auto control_hit = pick_viewport_curve_control(
      harness.camera, curve.preview_state(), anchor_screen, true);
  ASSERT_TRUE(control_hit.has_value());
  ASSERT_GT(control_hit->ray_distance, 0.0);
  std::size_t mesh_depth_queries = 0u;
  harness.mesh_surface_distance =
      [&](const ViewportRay &) -> std::optional<double> {
    ++mesh_depth_queries;
    return control_hit->ray_distance * 0.5;
  };
  (void)harness.frame(false, false, false);
  EXPECT_EQ(mesh_depth_queries, 0u)
      << "idle Curve frames must not query mesh occlusion";

  harness.input.mouse_local = anchor_screen;
  harness.input.mouse_screen = anchor_screen;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, anchor_screen}};
  harness.resolve_chord("Alt+LeftMouse");
  const ViewportNativeToolInteractionResult press =
      harness.frame(true, true, false);

  EXPECT_TRUE(press.curve_consumed);
  EXPECT_FALSE(press.claim_interaction_pane);
  EXPECT_EQ(mesh_depth_queries, 1u)
      << "mesh depth is queried once only after the projected anchor hit";
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     NavigationButtonPressRebindingsSuppressTheirCompetingDrag) {
  /// Describes one press binding that competes with a navigation drag.
  struct BindingCase {
    std::string_view press;
    std::string_view drag;
    QuaderMouseButton button;
    ViewportInputActionKind navigation;
  };
  constexpr std::array cases{
      BindingCase{"MiddleMouse", "MiddleMouseDrag",
                  QuaderMouseButton::Middle, ViewportInputActionKind::Orbit},
      BindingCase{"Alt+MiddleMouse", "Alt+MiddleMouseDrag",
                  QuaderMouseButton::Middle,
                  ViewportInputActionKind::OrbitSnap},
      BindingCase{"Shift+MiddleMouse", "Shift+MiddleMouseDrag",
                  QuaderMouseButton::Middle, ViewportInputActionKind::Pan},
      BindingCase{"RightMouse", "RightMouseDrag", QuaderMouseButton::Right,
                  ViewportInputActionKind::Fly},
  };
  for (const BindingCase &entry : cases) {
    SCOPED_TRACE(entry.press);
    CurveToolHarness harness;
    const auto object = harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    const QuaderKeybindingActionKey action{
        "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
    ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
        action, std::string(entry.press)).ok());
    harness.session.history().clear();
    auto &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const auto first_id = before.points()[0].id;
    const auto second_id = before.points()[1].id;
    const QVec2 first = harness.camera.project_world_point(
        curve_control_local_position(before.points()[0],
                                     CurveControlKind::Anchor)).position;
    harness.input.mouse_local = first;
    harness.input.pointer_pressed_edges = {{entry.button, first}};
    harness.resolve_chords({entry.press, entry.drag});
    ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
    const auto &conversion = harness.input.curve_conversion_edges.front();
    EXPECT_EQ(conversion.kind, ViewportCurveConversionEdgeKind::Trigger);
    EXPECT_EQ(conversion.semantic_chord.gesture, QuaderInputGesture::Press);
    ASSERT_TRUE(conversion.pointer_edge.has_value());
    EXPECT_EQ(conversion.pointer_edge->button, entry.button);
    EXPECT_FLOAT_EQ(conversion.pointer_edge->local.x, first.x);
    EXPECT_FLOAT_EQ(conversion.pointer_edge->local.y, first.y);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.pan_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_pressed);

    ViewportInputRouter router;
    router.set_active_tool(ViewportToolId::Curve);
    harness.input_result = router.handle_frame(harness.input, harness.camera);
    EXPECT_FALSE(harness.input_result.navigation_active);
    EXPECT_TRUE(harness.dispatch().claim_interaction_pane);
    expect_point(curve.preview_state().document->points()[0].outgoing_handle,
                 {});
    EXPECT_TRUE(harness.session.history().can_undo());

    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_drag_started_edges = {{entry.button, first}};
    harness.input.active_input_chords = {
        quader_input_parse_shortcut(entry.drag)->front()};
    harness.resolve_chord();
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_down);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_down);
    EXPECT_FALSE(harness.input.navigation_bindings.pan_down);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_down);
    harness.input_result = router.handle_frame(harness.input, harness.camera);
    EXPECT_FALSE(harness.input_result.navigation_active)
        << "drag threshold crossing must not release the suppressed chord";

    harness.input.pointer_drag_started_edges.clear();
    harness.input.active_input_chords.clear();
    harness.input.released_input_chords = {
        quader_input_parse_shortcut(entry.drag)->front()};
    harness.input.pointer_released_edges = {{entry.button, first}};
    harness.resolve_chord();
    harness.input_result = {};
    (void)harness.dispatch();
    EXPECT_FALSE(harness.interaction.dragging());
    EXPECT_TRUE(harness.suppressed_chords.empty());

    ASSERT_TRUE(harness.editor.history().undo().ok());
    ASSERT_TRUE(curve.refresh().ok);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    ASSERT_EQ(curve.preview_state().document->points().size(), 2u);
    EXPECT_EQ(curve.preview_state().document->points()[0].id, first_id);
    EXPECT_EQ(curve.preview_state().document->points()[1].id, second_id);
    ASSERT_TRUE(harness.editor.query().entity(object.entity).has_value());
    EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);

    ASSERT_TRUE(harness.editor.preferences().reset_action_shortcut(action).ok());
    harness.input.released_input_chords.clear();
    harness.input.pointer_released_edges.clear();
    harness.input.pointer_pressed_edges = {{entry.button, first}};
    harness.resolve_chords({entry.press, entry.drag});
    switch (entry.navigation) {
    case ViewportInputActionKind::Orbit:
      EXPECT_TRUE(harness.input.navigation_bindings.orbit_pressed);
      break;
    case ViewportInputActionKind::OrbitSnap:
      EXPECT_TRUE(harness.input.navigation_bindings.orbit_snap_pressed);
      break;
    case ViewportInputActionKind::Pan:
      EXPECT_TRUE(harness.input.navigation_bindings.pan_pressed);
      break;
    case ViewportInputActionKind::Fly:
      EXPECT_TRUE(harness.input.navigation_bindings.fly_pressed);
      break;
    default:
      FAIL() << "unexpected navigation action";
      break;
    }
  }
}

TEST(CurveToolServiceTests,
     CoalescedPhysicalPressesConvertEveryAnchorInDispatchOrder) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  harness.session.history().clear();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const auto first_id = before.points()[0].id;
  const auto second_id = before.points()[1].id;
  const QVec2 first = harness.camera.project_world_point(
      curve_control_local_position(before.points()[0],
                                   CurveControlKind::Anchor)).position;
  const QVec2 second = harness.camera.project_world_point(
      curve_control_local_position(before.points()[1],
                                   CurveControlKind::Anchor)).position;
  harness.input.mouse_local = second;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, first},
      {QuaderMouseButton::Left, second},
  };
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, first},
  };
  harness.resolve_chords(
      {"Alt+LeftMouse", "Alt+LeftMouseClick",
       "Alt+LeftMouseDoubleClick"});
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 2u);
  EXPECT_EQ(harness.input.curve_conversion_edges[0].semantic_chord.gesture,
            QuaderInputGesture::Press);
  EXPECT_EQ(harness.input.curve_conversion_edges[1].semantic_chord.gesture,
            QuaderInputGesture::Press);
  ASSERT_TRUE(harness.input.curve_conversion_edges[0].pointer_edge.has_value());
  ASSERT_TRUE(harness.input.curve_conversion_edges[1].pointer_edge.has_value());
  EXPECT_FLOAT_EQ(harness.input.curve_conversion_edges[0].pointer_edge->local.x,
                  first.x);
  EXPECT_FLOAT_EQ(harness.input.curve_conversion_edges[1].pointer_edge->local.x,
                  second.x);
  EXPECT_TRUE(harness.dispatch().claim_interaction_pane);
  EXPECT_FALSE(harness.interaction.dragging());
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  expect_point(curve.preview_state().document->points()[1].incoming_handle,
               {});

  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, second},
  };
  harness.resolve_chord();
  (void)harness.dispatch();
  harness.input.pointer_released_edges.clear();

  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.preview_state().document->points()[0].id, first_id);
  EXPECT_EQ(curve.preview_state().document->points()[1].id, second_id);
  ASSERT_TRUE(harness.editor.query().entity(object.entity).has_value());
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  expect_point(curve.preview_state().document->points()[1].incoming_handle,
               {before.points()[1].incoming_handle.x,
                before.points()[1].incoming_handle.y,
                before.points()[1].incoming_handle.z});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_EQ(curve.preview_state().document->points()[0].id, first_id);
  EXPECT_EQ(curve.preview_state().document->points()[1].id, second_id);
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);
}

TEST(CurveToolServiceTests,
     DeferredNavigationButtonClicksOwnTheirCompetingDragThroughRelease) {
  /// Describes one deferred click that shares a button with navigation.
  struct BindingCase {
    std::string_view click;
    std::string_view press;
    std::string_view drag;
    QuaderMouseButton button;
    ViewportInputActionKind navigation;
  };
  constexpr std::array cases{
      BindingCase{"MiddleMouseClick", "MiddleMouse", "MiddleMouseDrag",
                  QuaderMouseButton::Middle, ViewportInputActionKind::Orbit},
      BindingCase{"Alt+MiddleMouseClick", "Alt+MiddleMouse",
                  "Alt+MiddleMouseDrag", QuaderMouseButton::Middle,
                  ViewportInputActionKind::OrbitSnap},
      BindingCase{"Shift+MiddleMouseClick", "Shift+MiddleMouse",
                  "Shift+MiddleMouseDrag", QuaderMouseButton::Middle,
                  ViewportInputActionKind::Pan},
      BindingCase{"RightMouseClick", "RightMouse", "RightMouseDrag",
                  QuaderMouseButton::Right, ViewportInputActionKind::Fly},
  };
  for (const BindingCase &entry : cases) {
    SCOPED_TRACE(entry.click);
    CurveToolHarness harness;
    const auto object = harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    const QuaderKeybindingActionKey action{
        "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
    for (const auto &candidate :
         harness.editor.commands().keybinding_catalog()) {
      if (candidate.action_id != action.action_id &&
          candidate.shortcut == entry.click) {
        ASSERT_TRUE(harness.editor.preferences()
                        .remove_action_shortcut(candidate.key)
                        .ok());
      }
    }
    ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
        action, std::string(entry.click)).ok());
    harness.session.history().clear();
    auto &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const QVec2 first = harness.camera.project_world_point(
        curve_control_local_position(before.points()[0],
                                     CurveControlKind::Anchor)).position;
    harness.input.mouse_local = first;
    harness.input.pointer_pressed_edges = {{entry.button, first, 1u}};
    harness.resolve_chords({entry.press, entry.drag});
    ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
    EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
              ViewportCurveConversionEdgeKind::ReservePress);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.pan_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_pressed);
    EXPECT_TRUE(harness.dispatch().claim_interaction_pane);
    EXPECT_TRUE(harness.interaction.dragging());
    EXPECT_FALSE(harness.session.history().can_undo());

    ViewportInputRouter router;
    router.set_active_tool(ViewportToolId::Curve);
    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_drag_started_edges = {{entry.button, first}};
    harness.input.active_input_chords = {
        quader_input_parse_shortcut(entry.drag)->front()};
    harness.resolve_chord();
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_down);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_down);
    EXPECT_FALSE(harness.input.navigation_bindings.pan_down);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_down);
    harness.input_result = router.handle_frame(harness.input, harness.camera);
    EXPECT_FALSE(harness.input_result.navigation_active);
    EXPECT_TRUE(harness.interaction.dragging());

    harness.input.pointer_drag_started_edges.clear();
    harness.input.active_input_chords.clear();
    harness.input.released_input_chords = {
        quader_input_parse_shortcut(entry.drag)->front()};
    harness.input.pointer_released_edges = {{entry.button, first, 2u}};
    harness.resolve_chord(entry.click);
    ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
    EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
              ViewportCurveConversionEdgeKind::Trigger);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_released);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_released);
    EXPECT_FALSE(harness.input.navigation_bindings.pan_released);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_released);
    harness.input_result = {};
    (void)harness.dispatch();
    EXPECT_FALSE(harness.interaction.dragging());
    expect_point(curve.preview_state().document->points()[0].outgoing_handle,
                 {});
    EXPECT_TRUE(harness.session.history().can_undo());
    EXPECT_TRUE(harness.suppressed_chords.empty());

    ASSERT_TRUE(harness.editor.history().undo().ok());
    ASSERT_TRUE(curve.refresh().ok);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);

    ASSERT_TRUE(harness.editor.preferences().reset_action_shortcut(action).ok());
    harness.input.released_input_chords.clear();
    harness.input.pointer_released_edges.clear();
    harness.input.pointer_pressed_edges = {{entry.button, first, 3u}};
    harness.resolve_chords({entry.press, entry.drag});
    switch (entry.navigation) {
    case ViewportInputActionKind::Orbit:
      EXPECT_TRUE(harness.input.navigation_bindings.orbit_pressed);
      break;
    case ViewportInputActionKind::OrbitSnap:
      EXPECT_TRUE(harness.input.navigation_bindings.orbit_snap_pressed);
      break;
    case ViewportInputActionKind::Pan:
      EXPECT_TRUE(harness.input.navigation_bindings.pan_pressed);
      break;
    case ViewportInputActionKind::Fly:
      EXPECT_TRUE(harness.input.navigation_bindings.fly_pressed);
      break;
    default:
      FAIL() << "unexpected navigation action";
      break;
    }
  }
}

TEST(CurveToolServiceTests,
     CoalescedCompletedClickCannotReleaseALaterReservedPress) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  const QuaderKeybindingActionKey action{
      "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
  ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
      action, "Alt+LeftMouseClick").ok());
  harness.session.history().clear();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const QVec2 first = harness.camera.project_world_point(
      curve_control_local_position(before.points()[0],
                                   CurveControlKind::Anchor)).position;
  const QVec2 second = harness.camera.project_world_point(
      curve_control_local_position(before.points()[1],
                                   CurveControlKind::Anchor)).position;

  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, first, 1u},
      {QuaderMouseButton::Left, second, 3u},
  };
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, first, 2u},
  };
  harness.resolve_chords(
      {"Alt+LeftMouse", "Alt+LeftMouseClick", "Alt+LeftMouse"});
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 3u);
  EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::ReservePress);
  EXPECT_EQ(harness.input.curve_conversion_edges[1].kind,
            ViewportCurveConversionEdgeKind::Trigger);
  EXPECT_EQ(harness.input.curve_conversion_edges[2].kind,
            ViewportCurveConversionEdgeKind::ReservePress);
  (void)harness.dispatch();
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  expect_point(curve.preview_state().document->points()[1].incoming_handle,
               {before.points()[1].incoming_handle.x,
                before.points()[1].incoming_handle.y,
                before.points()[1].incoming_handle.z});
  EXPECT_TRUE(harness.interaction.dragging())
      << "the earlier release must not erase the later sequence-3 reserve";

  harness.input.pointer_pressed_edges.clear();
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, second, 4u},
  };
  harness.resolve_chord("Alt+LeftMouseClick");
  (void)harness.dispatch();
  EXPECT_FALSE(harness.interaction.dragging());
  expect_point(curve.preview_state().document->points()[1].incoming_handle,
               {});

  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));

  // A thresholded drag emits no Click chord, but its sequenced release must
  // still retire the coalesced reserve instead of leaving pointer ownership.
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, first, 5u},
  };
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, first, 6u},
  };
  harness.resolve_chord("Alt+LeftMouse");
  ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
  EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
            ViewportCurveConversionEdgeKind::ReservePress);
  (void)harness.dispatch();
  EXPECT_FALSE(harness.interaction.dragging());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests, ReboundClicksReserveThePressAndOnlyConvertTheSameAnchor) {
  const QuaderKeybindingActionKey action{
      "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
  for (const std::string_view binding : {"Alt+LeftMouseClick", "Alt+LeftMouseDoubleClick"}) {
    SCOPED_TRACE(binding);
    CurveToolHarness harness;
    harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(action, std::string(binding)).ok());
    harness.session.history().clear();
    auto &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    harness.input.mouse_local = harness.camera.project_world_point(
        curve_control_local_position(before.points()[0], CurveControlKind::Anchor)).position;
    harness.input.pointer_pressed_edges = {
        {QuaderMouseButton::Left, harness.input.mouse_local}};
    harness.resolve_chord("Alt+LeftMouse");
    ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
    EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
              ViewportCurveConversionEdgeKind::ReservePress);
    EXPECT_TRUE(harness.frame(true, true, false).claim_interaction_pane);
    EXPECT_FALSE(curve.dragging());
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_FALSE(harness.session.history().can_undo());
    const bool double_click = binding == "Alt+LeftMouseDoubleClick";
    if (double_click) {
      harness.resolve_chord("Alt+LeftMouseClick");
      (void)harness.frame(false, false, true);
      EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    }
    if (double_click) {
      harness.input.pointer_pressed_edges = {
          {QuaderMouseButton::Left, harness.input.mouse_local}};
    } else {
      harness.input.pointer_released_edges = {
          {QuaderMouseButton::Left, harness.input.mouse_local}};
    }
    harness.resolve_chord(binding);
    (void)harness.frame(double_click, double_click, !double_click);
    EXPECT_FALSE(curve.dragging());
    expect_point(curve.preview_state().document->points()[0].outgoing_handle, {});
    harness.resolve_chord();
    (void)harness.frame(false, false, true);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    ASSERT_TRUE(curve.refresh().ok);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_FALSE(harness.session.history().can_undo());
  }

  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(action, "Alt+LeftMouseClick").ok());
  harness.session.history().clear();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  harness.input.mouse_local = harness.camera.project_world_point(
      curve_control_local_position(before.points()[0], CurveControlKind::Anchor)).position;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouse");
  (void)harness.frame(true, true, false);
  harness.input.mouse_local = harness.camera.project_world_point(
      curve_control_local_position(before.points()[1], CurveControlKind::Anchor)).position;
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouseClick");
  (void)harness.frame(false, false, true);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());

  harness.input.mouse_local = harness.camera.project_world_point(
      curve_control_local_position(before.points()[0],
                                   CurveControlKind::Anchor)).position;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouse");
  (void)harness.frame(true, true, false);
  ASSERT_TRUE(curve.set_closed(true).ok);
  const auto externally_changed = *curve.preview_state().document;
  harness.input.pointer_released_edges = {
      {QuaderMouseButton::Left, harness.input.mouse_local}};
  harness.resolve_chord("Alt+LeftMouseClick");
  (void)harness.frame(false, false, true);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(externally_changed));
}

TEST(CurveToolServiceTests, InterruptedDeferredConversionLeavesNoAuthoredEdit) {
  for (const int interruption : {0, 1, 2, 3}) {
    SCOPED_TRACE(interruption);
    CurveToolHarness harness;
    harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
        {"viewport.curve.toggle_point_smoothness", "viewport.tool.curve"},
        "Alt+LeftMouseClick").ok());
    harness.session.history().clear();
    auto &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    harness.input.mouse_local = harness.camera.project_world_point(
        curve_control_local_position(before.points()[0], CurveControlKind::Anchor)).position;
    harness.input.pointer_pressed_edges = {
        {QuaderMouseButton::Left, harness.input.mouse_local}};
    harness.resolve_chord("Alt+LeftMouse");
    (void)harness.frame(true, true, false);
    harness.resolve_chord();
    if (interruption == 0) {
      ViewportInputRouter router;
      router.set_active_tool(ViewportToolId::Curve);
      harness.input.mouse_wheel = 1.0f;
      harness.input.navigation_bindings.zoom_wheel = true;
      harness.input_result = router.handle_frame(harness.input, harness.camera);
      ASSERT_TRUE(harness.input_result.camera_changed);
    }
    harness.input.text_input_active = interruption == 1;
    harness.input.pointer_interaction_canceled = interruption == 3;
    (void)harness.frame(false, interruption != 3, interruption == 3,
                        interruption == 2);
    harness.input_result = {};
    harness.input.text_input_active = false;
    harness.input.pointer_interaction_canceled = false;
    harness.input.pointer_released_edges = {
        {QuaderMouseButton::Left, harness.input.mouse_local}};
    harness.resolve_chord("Alt+LeftMouseClick");
    (void)harness.frame(false, false, true);
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_FALSE(harness.session.history().can_undo());
    EXPECT_FALSE(harness.interaction.dragging());
    EXPECT_FALSE(curve.dragging());
  }
}

TEST(CurveToolServiceTests, NonLeftClickConversionKeepsPressTargetProvenance) {
  /// Describes a remapped non-left click and its retained button provenance.
  struct BindingCase {
    std::string_view click;
    std::string_view press;
    std::string_view drag;
    QuaderMouseButton button;
  };
  constexpr std::array cases{
      BindingCase{"Alt+RightMouseClick", "Alt+RightMouse",
                  "Alt+RightMouseDrag", QuaderMouseButton::Right},
      BindingCase{"Alt+Mouse4Click", "Alt+Mouse4",
                  "Alt+Mouse4Drag", QuaderMouseButton::Back},
  };
  for (const auto &entry : cases) {
    SCOPED_TRACE(entry.click);
    CurveToolHarness harness;
    harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
        {"viewport.curve.toggle_point_smoothness", "viewport.tool.curve"},
        std::string(entry.click)).ok());
    harness.session.history().clear();
    auto &curve = harness.session.curve();
    const auto before = *curve.preview_state().document;
    const QVec2 first = harness.camera.project_world_point(
        curve_control_local_position(before.points()[0],
                                     CurveControlKind::Anchor)).position;
    const QVec2 second = harness.camera.project_world_point(
        curve_control_local_position(before.points()[1],
                                     CurveControlKind::Anchor)).position;
    harness.input.mouse_local = first;
    harness.input.pointer_pressed_edges = {{entry.button, first}};
    harness.resolve_chords({entry.press, entry.drag});
    ASSERT_EQ(harness.input.curve_conversion_edges.size(), 1u);
    EXPECT_EQ(harness.input.curve_conversion_edges[0].kind,
              ViewportCurveConversionEdgeKind::ReservePress);
    ASSERT_TRUE(harness.input.curve_conversion_edges[0].pointer_edge.has_value());
    EXPECT_EQ(harness.input.curve_conversion_edges[0].pointer_edge->button,
              entry.button);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.orbit_snap_pressed);
    EXPECT_FALSE(harness.input.navigation_bindings.fly_pressed);
    EXPECT_TRUE(harness.dispatch().claim_interaction_pane);
    harness.input.pointer_pressed_edges.clear();

    harness.input.mouse_local = second;
    harness.input.pointer_released_edges = {{entry.button, second}};
    harness.resolve_chord(entry.click);
    (void)harness.dispatch();
    EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
    EXPECT_FALSE(harness.session.history().can_undo());
    harness.input.pointer_released_edges.clear();

    harness.input.mouse_local = first;
    harness.input.pointer_pressed_edges = {{entry.button, first}};
    harness.resolve_chords({entry.press, entry.drag});
    (void)harness.dispatch();
    harness.input.pointer_pressed_edges.clear();
    harness.input.pointer_released_edges = {{entry.button, first}};
    harness.resolve_chord(entry.click);
    (void)harness.dispatch();
    expect_point(curve.preview_state().document->points()[0].outgoing_handle, {});
    EXPECT_TRUE(harness.session.history().can_undo());
  }
}

TEST(CurveToolServiceTests, CoalescedClickUsesReleaseHitAndPressUsesPressHit) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  const QuaderKeybindingActionKey action{
      "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
  ASSERT_TRUE(harness.editor.preferences().set_action_shortcut(
      action, "Alt+LeftMouseClick").ok());
  harness.session.history().clear();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const QVec2 first = harness.camera.project_world_point(
      curve_control_local_position(before.points()[0], CurveControlKind::Anchor)).position;
  const QVec2 second = harness.camera.project_world_point(
      curve_control_local_position(before.points()[1], CurveControlKind::Anchor)).position;
  harness.input.pointer_pressed_edges = {{QuaderMouseButton::Left, first}};
  harness.input.pointer_released_edges = {{QuaderMouseButton::Left, second}};
  harness.input.mouse_local = second;
  harness.resolve_chords({"Alt+LeftMouse", "Alt+LeftMouseClick"});
  (void)harness.dispatch();
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());

  harness.input.pointer_pressed_edges = {{QuaderMouseButton::Left, first}};
  harness.input.pointer_released_edges = {{QuaderMouseButton::Left, first}};
  harness.input.mouse_local = first;
  harness.resolve_chords({"Alt+LeftMouse", "Alt+LeftMouseClick"});
  (void)harness.dispatch();
  expect_point(curve.preview_state().document->points()[0].outgoing_handle, {});
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));

  ASSERT_TRUE(harness.editor.preferences().reset_action_shortcut(action).ok());
  harness.input.pointer_pressed_edges = {{QuaderMouseButton::Left, first}};
  harness.input.pointer_released_edges = {{QuaderMouseButton::Left, second}};
  harness.input.mouse_local = second;
  harness.resolve_chord("Alt+LeftMouse");
  (void)harness.dispatch();
  expect_point(curve.preview_state().document->points()[0].outgoing_handle, {});
  EXPECT_TRUE(harness.session.history().can_undo())
      << "the default Press action must keep using the press position";
}

TEST(CurveToolServiceTests,
     ControlSelectionUsesSharedEditAlgebraWithoutGeometryOrHistory) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_anchor{before.points()[1].id,
                                         CurveControlKind::Anchor};
  const CurveControlTarget hidden_first_incoming{
      before.points()[0].id, CurveControlKind::IncomingHandle};
  harness.session.history().clear();
  harness.session.mark_clean();

  const std::array initial{first_anchor, first_outgoing};
  EXPECT_TRUE(curve.edit_control_selection(
      initial, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      first_outgoing));
  expect_control_selection(curve.control_selection(), initial, first_outgoing);

  const std::array toggle_hits{first_outgoing, second_anchor};
  EXPECT_TRUE(curve.edit_control_selection(
      toggle_hits, quader_editor_core::SelectionEditOperation::Toggle,
      nullptr, second_anchor));
  expect_control_selection(curve.control_selection(),
                           {first_anchor, second_anchor}, second_anchor);

  const std::array remove_active{second_anchor};
  EXPECT_TRUE(curve.edit_control_selection(
      remove_active, quader_editor_core::SelectionEditOperation::Remove));
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);

  const std::uint64_t selection_revision =
      curve.control_selection_revision();
  const std::array invisible_hit{hidden_first_incoming};
  EXPECT_FALSE(curve.edit_control_selection(
      invisible_hit, quader_editor_core::SelectionEditOperation::Add, nullptr,
      hidden_first_incoming));
  EXPECT_EQ(curve.control_selection_revision(), selection_revision);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
}

TEST(CurveToolServiceTests,
     ViewportControlClicksUseCanonicalReplaceAddAndRemovePolicyOnRelease) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_anchor{before.points()[1].id,
                                         CurveControlKind::Anchor};
  const QVec2 first_anchor_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const QVec2 first_outgoing_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::OutgoingHandle);
  const QVec2 second_anchor_screen = curve_control_screen(
      harness, curve.preview_state(), 1u, CurveControlKind::Anchor);
  harness.session.history().clear();
  harness.session.mark_clean();
  const auto history_before = harness.editor.history().snapshot();

  harness.input.mouse_local = first_anchor_screen;
  harness.input.mouse_screen = first_anchor_screen;
  harness.curve_selection_frame(true, true, false, first_anchor_screen);
  EXPECT_TRUE(curve.control_selection().empty())
      << "a click uses the shared press/release selection lifecycle";
  harness.curve_selection_frame(false, false, true, first_anchor_screen);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);

  harness.click_curve_control(first_outgoing_screen, {.shift = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing}, first_outgoing);
  harness.click_curve_control(second_anchor_screen, {.shift = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing, second_anchor},
                           second_anchor);

  harness.click_curve_control(first_outgoing_screen, {.primary = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, second_anchor}, second_anchor);
  harness.click_curve_control(second_anchor_screen,
                              {.primary = true, .shift = true});
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);

  harness.click_curve_control(first_outgoing_screen, {.shift = true});
  harness.click_curve_control(first_anchor_screen, {.shift = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing}, first_anchor);
  const QVec2 blank{64.0f, 64.0f};
  harness.click_curve_control(blank, {.shift = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing}, first_anchor);
  harness.click_curve_control(blank, {.primary = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing}, first_anchor);
  harness.click_curve_control(blank);
  EXPECT_TRUE(curve.control_selection().empty());
  EXPECT_FALSE(curve.control_selection().active.has_value());

  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);
}

TEST(CurveToolServiceTests,
     CurveControlMarqueeUsesSharedDirectionBaselineAndReleaseLifecycle) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_anchor{before.points()[1].id,
                                         CurveControlKind::Anchor};
  const QVec2 first_anchor_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const QVec2 first_outgoing_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::OutgoingHandle);
  const std::array initial{second_anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      initial, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      second_anchor));
  harness.session.history().clear();

  const QVec2 contain_start = first_anchor_screen - QVec2{7.0f, 7.0f};
  const QVec2 contain_end = first_anchor_screen + QVec2{7.0f, 7.0f};
  harness.input.mouse_local = contain_start;
  harness.input.mouse_screen = contain_start;
  harness.curve_selection_frame(true, true, false, contain_start);
  expect_control_selection(curve.control_selection(), {second_anchor},
                           second_anchor);
  harness.input.mouse_local = contain_end;
  harness.input.mouse_screen = contain_end;
  harness.curve_selection_frame(false, true, false, contain_start);
  expect_control_selection(curve.control_selection(), {second_anchor},
                           second_anchor);
  EXPECT_TRUE(harness.controller.selection_marquee().active);
  harness.curve_selection_frame(false, false, true, contain_start);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);

  const QVec2 crossing_start =
      first_outgoing_screen + QVec2{7.0f, 7.0f};
  const QVec2 crossing_end = first_outgoing_screen - QVec2{7.0f, 7.0f};
  harness.marquee_curve_controls(crossing_start, crossing_end,
                                  {.shift = true});
  expect_control_selection(curve.control_selection(),
                           {first_anchor, first_outgoing}, first_anchor);

  harness.marquee_curve_controls(contain_start, contain_end,
                                  {.primary = true});
  expect_control_selection(curve.control_selection(), {first_outgoing},
                           first_outgoing);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     DraftConstructionAndControlSelectionSharePointerArbitrationAndThreshold) {
  {
    CurveToolHarness harness;
    harness.activate_curve_edit();
    auto &curve = harness.session.curve();
    const QVec2 blank{320.0f, 260.0f};
    harness.input.mouse_local = blank;
    harness.input.mouse_screen = blank;
    const auto pressed =
        harness.curve_native_selection_frame(true, true, false, blank);
    EXPECT_TRUE(pressed.curve_consumed);
    EXPECT_TRUE(curve.point_construction_dragging());
    EXPECT_EQ(curve.point_count(), 1u);
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    (void)harness.curve_native_selection_frame(false, false, true, blank);
    EXPECT_FALSE(curve.dragging());
    EXPECT_EQ(curve.point_count(), 1u);
  }

  CurveToolHarness harness;
  harness.activate_curve_edit();
  harness.place({-2.0f, 0.0f, 0.0f});
  harness.place({2.0f, 0.0f, 0.0f});
  auto &curve = harness.session.curve();
  ASSERT_TRUE(curve.drawing());
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget second_anchor{before.points()[1].id,
                                         CurveControlKind::Anchor};
  const QVec2 first_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const QVec2 second_screen = curve_control_screen(
      harness, curve.preview_state(), 1u, CurveControlKind::Anchor);
  ASSERT_TRUE(replace_curve_control_selection(curve, first_anchor));
  const std::size_t point_count = curve.point_count();

  const QVec2 add_start = second_screen - QVec2{12.0f, 12.0f};
  const QVec2 add_end = second_screen + QVec2{12.0f, 12.0f};
  harness.input.modifiers = {.shift = true};
  harness.input.mouse_local = add_start;
  harness.input.mouse_screen = add_start;
  (void)harness.curve_native_selection_frame(true, true, false, add_start);
  EXPECT_FALSE(curve.dragging());
  EXPECT_EQ(curve.point_count(), point_count);
  harness.input.mouse_local = add_end;
  harness.input.mouse_screen = add_end;
  (void)harness.curve_native_selection_frame(false, true, false, add_start);
  EXPECT_TRUE(harness.controller.selection_marquee().active);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);
  (void)harness.curve_native_selection_frame(false, false, true, add_start);
  expect_control_selection(curve.control_selection(),
                           {first_anchor, second_anchor}, first_anchor);
  EXPECT_EQ(curve.point_count(), point_count);

  const QVec2 remove_start = first_screen - QVec2{12.0f, 12.0f};
  const QVec2 remove_end = first_screen + QVec2{12.0f, 12.0f};
  harness.input.modifiers = {.primary = true};
  harness.input.mouse_local = remove_start;
  harness.input.mouse_screen = remove_start;
  (void)harness.curve_native_selection_frame(true, true, false, remove_start);
  EXPECT_FALSE(curve.dragging());
  harness.input.mouse_local = remove_end;
  harness.input.mouse_screen = remove_end;
  (void)harness.curve_native_selection_frame(false, true, false,
                                               remove_start);
  (void)harness.curve_native_selection_frame(false, false, true,
                                               remove_start);
  expect_control_selection(curve.control_selection(), {second_anchor},
                           second_anchor);
  EXPECT_EQ(curve.point_count(), point_count);

  harness.input.modifiers = {};
  harness.input.mouse_local = first_screen;
  harness.input.mouse_screen = first_screen;
  (void)harness.curve_native_selection_frame(true, true, false, first_screen);
  harness.input.mouse_local = first_screen + QVec2{3.0f, 0.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  (void)harness.curve_native_selection_frame(false, true, false,
                                               first_screen);
  EXPECT_FALSE(harness.controller.selection_marquee().active);
  (void)harness.curve_native_selection_frame(false, false, true,
                                               first_screen);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);
  ASSERT_TRUE(replace_curve_control_selection(curve, second_anchor));

  harness.input.mouse_local = first_screen;
  harness.input.mouse_screen = first_screen;
  (void)harness.curve_native_selection_frame(true, true, false, first_screen);
  harness.input.mouse_local = first_screen + QVec2{4.0f, 0.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  (void)harness.curve_native_selection_frame(false, true, false,
                                               first_screen);
  EXPECT_TRUE(harness.controller.selection_marquee().active);
  expect_control_selection(curve.control_selection(), {second_anchor},
                           second_anchor);
  (void)harness.curve_native_selection_frame(false, false, true,
                                               first_screen);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);
  EXPECT_EQ(curve.point_count(), point_count);
}

TEST(CurveToolServiceTests,
     MixedControlGizmoUsesBoundsPivotMovesEndpointsOnceAndHasOneHistoryEdit) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::GridSnap, false));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::VertexSnap, false));
  ASSERT_TRUE(harness.editor.viewports()
                  .set_transform_space(QuaderTransformSpace::Global)
                  .ok());
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_incoming{
      before.points()[1].id, CurveControlKind::IncomingHandle};
  const std::array selected{first_anchor, first_outgoing, second_incoming};
  ASSERT_TRUE(curve.edit_control_selection(
      selected, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      second_incoming));
  const CurveControlSelection selection_before = curve.control_selection();
  const auto pivot_before = curve.selected_controls_world_pivot();
  ASSERT_TRUE(pivot_before.has_value());
  EXPECT_NEAR(pivot_before->x, -0.5f, 0.0001f);
  EXPECT_NEAR(pivot_before->y, 0.0f, 0.0001f);
  EXPECT_NEAR(pivot_before->z, 0.5f, 0.0001f);
  harness.session.history().clear();

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), &curve);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  EXPECT_NEAR(gizmo->info.pivot.x, pivot_before->x, 0.0001f);
  EXPECT_NEAR(gizmo->info.pivot.y, pivot_before->y, 0.0001f);
  EXPECT_NEAR(gizmo->info.pivot.z, pivot_before->z, 0.0001f);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 screen_axis = axis->screen_tip - axis->screen_start;
  const float screen_length = std::sqrt(screen_axis.x * screen_axis.x +
                                        screen_axis.y * screen_axis.y);
  ASSERT_GT(screen_length, 1.0f);
  const QVec2 screen_direction = screen_axis * (1.0f / screen_length);
  const QVec2 start = axis_screen_point(*axis, 0.3f);
  const QVec2 update = start + screen_direction * 96.0f;

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), &curve)
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), &curve)
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, false, true, start), &curve)
                  .gizmo_consumed);
  ASSERT_FALSE(curve.dragging());

  const auto edited = *curve.preview_state().document;
  const auto pivot_after = curve.selected_controls_world_pivot();
  ASSERT_TRUE(pivot_after.has_value());
  const QVec3 delta = *pivot_after - *pivot_before;
  EXPECT_GT(std::abs(delta.x), 0.01f);
  EXPECT_NEAR(delta.y, 0.0f, 0.001f);
  EXPECT_NEAR(delta.z, 0.0f, 0.001f);
  expect_point(edited.points()[0].position,
               {before.points()[0].position.x + delta.x,
                before.points()[0].position.y + delta.y,
                before.points()[0].position.z + delta.z});
  expect_point(edited.points()[0].outgoing_handle,
               {before.points()[0].outgoing_handle.x,
                before.points()[0].outgoing_handle.y,
                before.points()[0].outgoing_handle.z});
  expect_point(edited.points()[1].position,
               {before.points()[1].position.x,
                before.points()[1].position.y,
                before.points()[1].position.z});
  expect_point(edited.points()[1].incoming_handle,
               {before.points()[1].incoming_handle.x + delta.x,
                before.points()[1].incoming_handle.y + delta.y,
                before.points()[1].incoming_handle.z + delta.z});
  EXPECT_EQ(curve.control_selection(), selection_before);
  ASSERT_TRUE(harness.session.history().can_undo());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_EQ(curve.control_selection(), selection_before);
  EXPECT_FALSE(harness.session.history().can_undo())
      << "one multi-control gizmo gesture must create one history item";
  ASSERT_TRUE(harness.editor.history().redo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(edited));
  EXPECT_EQ(curve.control_selection(), selection_before);
}

TEST(CurveToolServiceTests,
     CancelingMultiControlMoveRestoresGeometryAndRetainsSelection) {
  CurveToolHarness harness;
  harness.make_curve();
  harness.activate_curve_edit();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_anchor{before.points()[1].id,
                                         CurveControlKind::Anchor};
  const std::array selected{first_outgoing, second_anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      selected, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      second_anchor));
  const CurveControlSelection selection_before = curve.control_selection();
  const auto pivot = curve.selected_controls_world_pivot();
  ASSERT_TRUE(pivot.has_value());
  harness.session.history().clear();

  ASSERT_TRUE(curve.begin_selected_control_drag().ok);
  ASSERT_TRUE(curve.update_drag(*pivot + QVec3{3.0f, -2.0f, 1.0f}).ok);
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));
  ASSERT_TRUE(curve.cancel_drag().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_EQ(curve.control_selection(), selection_before);
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     ExternalCurveReplacementPrunesInvalidControlsAndRepairsActiveControl) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  harness.activate_curve_edit();
  auto &curve = harness.session.curve();
  const auto before = *curve.preview_state().document;
  const CurveControlTarget first_anchor{before.points()[0].id,
                                        CurveControlKind::Anchor};
  const CurveControlTarget first_outgoing{
      before.points()[0].id, CurveControlKind::OutgoingHandle};
  const CurveControlTarget second_incoming{
      before.points()[1].id, CurveControlKind::IncomingHandle};
  const std::array selected{first_anchor, first_outgoing, second_incoming};
  ASSERT_TRUE(curve.edit_control_selection(
      selected, quader_editor_core::SelectionEditOperation::Replace, nullptr,
      second_incoming));

  modeling::CurveDocument replacement = before;
  const std::array point_edits{
      modeling::CurvePointEdit{
          .id = before.points()[0].id,
          .point = {.position = before.points()[0].position},
      },
      modeling::CurvePointEdit{
          .id = before.points()[1].id,
          .point = {.position = before.points()[1].position},
      },
  };
  ASSERT_TRUE(replacement.set_points(point_edits).ok());
  const auto replaced = harness.session.modeling().replace_curve_document(
      object.modeling_object_id, replacement);
  ASSERT_TRUE(replaced.ok) << replaced.message;
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_EQ(curve.preview_state().document->point_count(), 2u);
  expect_control_selection(curve.control_selection(), {first_anchor},
                           first_anchor);
  EXPECT_FALSE(curve.control_selection().contains(first_outgoing));
  EXPECT_FALSE(curve.control_selection().contains(second_incoming));
}

TEST(CurveToolServiceTests,
     DirectControlPickRanksScreenDistanceThenDepthAndStableTarget) {
  ViewportCameraController camera;
  camera.set_viewport_size(800.0f, 600.0f);
  camera.focus_origin(8.0f);
  camera.set_view_direction({0.0f, 0.0f, -1.0f});
  constexpr QVec2 center{400.0f, 300.0f};
  const ViewportRay center_ray = camera.ray_from_viewport_point(center);
  const ViewportRay offset_ray =
      camera.ray_from_viewport_point(center + QVec2{4.0f, 0.0f});
  const auto point_spec = [](QVec3 position) {
    return modeling::CurvePointSpec{
        .position = {position.x, position.y, position.z},
    };
  };

  const QVec3 offset_near =
      offset_ray.origin + offset_ray.direction * 3.0f;
  const QVec3 centered_far =
      center_ray.origin + center_ray.direction * 6.0f;
  const std::array screen_ranked_points{
      point_spec(offset_near),
      point_spec(centered_far),
  };
  CurveToolPreviewState preview{
      .active = true,
      .document = modeling::CurveDocument::make_bezier(screen_ranked_points)
                      .or_throw(),
  };
  auto hit = pick_viewport_curve_control(camera, preview, center, true);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target.point, preview.document->points()[1].id)
      << "the closest projected endpoint wins before depth";
  EXPECT_NEAR(hit->distance_pixels, 0.0, 0.0001);

  const QVec3 centered_near =
      center_ray.origin + center_ray.direction * 3.0f;
  const std::array depth_ranked_points{
      point_spec(centered_near),
      point_spec(centered_far),
  };
  preview.document =
      modeling::CurveDocument::make_bezier(depth_ranked_points).or_throw();
  hit = pick_viewport_curve_control(camera, preview, center, true);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target.point, preview.document->points()[0].id)
      << "ray depth breaks coincident screen-space ties";
  EXPECT_LT(hit->ray_distance, 6.0);
  EXPECT_TRUE(pick_viewport_curve_control(
                  camera, preview, center, true, 9.0,
                  hit->ray_distance - 0.0005)
                  .has_value())
      << "control occlusion shares the mesh-component 0.001 tolerance";
  EXPECT_FALSE(pick_viewport_curve_control(
                   camera, preview, center, true, 9.0,
                   hit->ray_distance - 0.002)
                   .has_value());

  hit = pick_viewport_curve_control(camera, preview, center, false);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target.kind, CurveControlKind::Anchor)
      << "stable kind ordering keeps a coincident zero handle behind its anchor";
}

TEST(CurveToolServiceTests, HandleVisibilityAndHitTestingShareEndpointPolicy) {
  CurveToolHarness harness;
  harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  const auto &preview = harness.session.curve().preview_state();
  ASSERT_TRUE(preview.document.has_value());
  EXPECT_TRUE(curve_control_is_visible(preview, 0u, CurveControlKind::Anchor));
  EXPECT_FALSE(curve_control_is_visible(preview, 0u, CurveControlKind::IncomingHandle));
  EXPECT_FALSE(curve_control_is_visible(preview, 1u, CurveControlKind::OutgoingHandle));
  const auto &point = preview.document->points()[0];
  const auto screen = harness.camera.project_world_point(
      transform_point(preview.world_transform,
          curve_control_local_position(point, CurveControlKind::OutgoingHandle)));
  const auto hit = pick_viewport_curve_control(harness.camera, preview,
                                               screen.position);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target.point, point.id);
  EXPECT_EQ(hit->target.kind, CurveControlKind::OutgoingHandle);
  EXPECT_NEAR(hit->world_position.x,
              transform_point(preview.world_transform,
                              curve_control_local_position(
                                  point, CurveControlKind::OutgoingHandle))
                  .x,
              0.0001f);
  EXPECT_GT(hit->ray_distance, 0.0);
  EXPECT_NEAR(hit->distance_pixels, 0.0, 0.0001);

  const auto box_hits = box_pick_viewport_curve_controls(
      harness.camera, preview, {0.0f, 0.0f}, harness.camera.viewport_size());
  EXPECT_EQ(box_hits.size(), 4u);
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[0].id,
                                     CurveControlKind::Anchor}) !=
              box_hits.end());
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[0].id,
                                     CurveControlKind::OutgoingHandle}) !=
              box_hits.end());
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[1].id,
                                     CurveControlKind::IncomingHandle}) !=
              box_hits.end());
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[1].id,
                                     CurveControlKind::Anchor}) !=
              box_hits.end());
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[0].id,
                                     CurveControlKind::IncomingHandle}) ==
              box_hits.end());
  EXPECT_TRUE(std::ranges::find(
                  box_hits,
                  CurveControlTarget{preview.document->points()[1].id,
                                     CurveControlKind::OutgoingHandle}) ==
              box_hits.end());
}

} // namespace
} // namespace quader
