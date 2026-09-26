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
#include "editor/placement/scene_surface_query.hpp"
#include "editor/selection/checked_selection_publication_command.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "editor/viewport/viewport_curve_picking.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/tools/viewport_native_tool_interaction.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_settings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

using SceneScalarFingerprint = std::array<std::uint64_t, 12>;

[[nodiscard]] SceneScalarFingerprint
scene_scalar_fingerprint(const SceneDocument &scene) {
  return {
      scene.revision(),
      scene.content_revision(),
      scene.selection_revision(),
      scene.hierarchy_presentation_revision(),
      scene.visibility_revision(),
      scene.transform_revision(),
      scene.texture_mapping_revision(),
      scene.mesh_binding_revision(),
      scene.curve_binding_revision(),
      scene.material_binding_revision(),
      scene.render_component_revision(),
      scene.layer_revision(),
  };
}

class IncorrectSelectionRollbackCommand final : public IUndoableCommand {
public:
  IncorrectSelectionRollbackCommand(SceneDocument &scene, EntityId after,
                                    EntityId rollback,
                                    bool rollback_reports_changed)
      : scene_(scene), after_(after), rollback_(rollback),
        rollback_reports_changed_(rollback_reports_changed) {}

  [[nodiscard]] const std::string &label() const override { return label_; }

  CommandResult redo() override {
    (void)scene_.replace_selection({&after_, 1u}, after_);
    return command_changed_result();
  }

  CommandResult undo() override {
    (void)scene_.replace_selection({&rollback_, 1u}, rollback_);
    return rollback_reports_changed_ ? command_changed_result()
                                     : command_unchanged_result();
  }

private:
  SceneDocument &scene_;
  EntityId after_ = kInvalidEntityId;
  EntityId rollback_ = kInvalidEntityId;
  bool rollback_reports_changed_ = true;
  std::string label_ = "Incorrect Selection Rollback";
};

/** Supplies inert lifecycle completion to direct native-pointer tests. */
class InertNativeToolLifecycle final
    : public ViewportNativeToolLifecyclePort {
public:
  [[nodiscard]] CommandResult commit_active() override {
    return command_unchanged_result();
  }
};

/// Provides the real public editor and its pointer-driven Curve service.
struct CurveToolHarness {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};
  ViewportCameraController camera;
  ViewportNativeToolInteraction interaction;
  InertNativeToolLifecycle lifecycle;
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
  bool handles_visible = true;
  std::function<ViewportMeshSnapQueryResult(
      const ViewportRay &, double, std::optional<double>)>
      mesh_snap_query;
  std::vector<ViewportSceneCurvePickCandidate> curve_snap_candidates;
  std::deque<SceneSurfaceQuerySnapshot> scene_surface_snapshots;

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
    const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
    if (session.curve().controls_editable() &&
        (pressed || down || released ||
         !input.curve_conversion_edges.empty())) {
      scene_surface_snapshots.push_back(
          session.scene_surface_query().snapshot());
      scene_surfaces = &scene_surface_snapshots.back();
    }
    const ViewportCurveControlPickingAdapter curve_control_picker(
        camera, scene_surfaces);
    const auto result = interaction.handle_frame(
        lifecycle, session.scene(), session.assets(), nullptr, nullptr, nullptr,
        nullptr, nullptr, &session.point_placement(), camera,
        {.input = input,
         .input_result = input_result,
         .suppress_hover_work = suppress_hover,
         .handles_visible = handles_visible,
         .scene_surface_revision = scene_surfaces != nullptr
                                       ? scene_surfaces->revision()
                                       : 0u,
         .left_pressed = pressed,
         .left_down = down,
         .left_released = released,
         .left_click_local = input.mouse_local,
         .scene_snap_enabled = scene_snap_enabled,
         .grid_snap_enabled = grid_snap_enabled,
         .curve_control_picker = &curve_control_picker},
        &session.curve());
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    return result;
  }

  ViewportNativeToolInteractionResult dispatch(
      bool transform_gizmo_consumed = false) {
    const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
    if (session.curve().controls_editable() &&
        !input.curve_conversion_edges.empty()) {
      scene_surface_snapshots.push_back(
          session.scene_surface_query().snapshot());
      scene_surfaces = &scene_surface_snapshots.back();
    }
    const ViewportCurveControlPickingAdapter curve_control_picker(
        camera, scene_surfaces);
    return interaction.handle_frame(
        lifecycle, session.scene(), session.assets(), nullptr, nullptr, nullptr,
        nullptr, nullptr, &session.point_placement(), camera,
        {.input = input,
         .input_result = input_result,
         .transform_gizmo_consumed = transform_gizmo_consumed,
         .handles_visible = handles_visible,
         .scene_surface_revision = scene_surfaces != nullptr
                                       ? scene_surfaces->revision()
                                       : 0u,
         .scene_snap_enabled = scene_snap_enabled,
         .grid_snap_enabled = grid_snap_enabled,
         .curve_control_picker = &curve_control_picker},
        &session.curve());
  }

  [[nodiscard]] ViewportInteractionFrame gizmo_frame(
      bool pressed = false, bool down = false, bool released = false,
      QVec2 click_local = {}) {
    const SceneSurfaceQuerySnapshot *scene_surfaces = nullptr;
    if (session.curve().controls_editable() &&
        (pressed || down || released)) {
      scene_surface_snapshots.push_back(
          session.scene_surface_query().snapshot());
      scene_surfaces = &scene_surface_snapshots.back();
    }
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
         .scene_surfaces = scene_surfaces,
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
        session.native_tools(), nullptr);
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
                                                   harness.session.native_tools());
  harness.controller.handle_after_active_tool(
      harness.editor, frame, {}, before, harness.session.native_tools(),
      nullptr);
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

QuaderModelingObjectRef create_curve_pick_occluder(
    CurveToolHarness &harness, QVec2 local_mouse,
    double curve_ray_distance) {
  const ViewportRay ray =
      harness.camera.ray_from_viewport_point(local_mouse);
  const QVec3 center =
      ray.origin + ray.direction * static_cast<float>(curve_ray_distance * 0.5);
  const float half_extent = std::clamp(
      static_cast<float>(curve_ray_distance * 0.05), 0.1f, 0.5f);
  const auto created = harness.editor.modeling().create_box({
      .min = center - QVec3{half_extent, half_extent, half_extent},
      .max = center + QVec3{half_extent, half_extent, half_extent},
  });
  EXPECT_TRUE(created.ok()) << created.error_message();
  if (created.ok()) {
    EXPECT_TRUE(harness.session.scene().set_selectable(created.value().entity,
                                                       false));
    harness.session.scene_surface_query().invalidate();
  }
  return created.value();
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

class FailingSubSelectionProvider final : public EditorSubSelectionProvider {
public:
  explicit FailingSubSelectionProvider(EntityId entity, int modeling_object_id)
      : state{.kind = EditorSubSelectionKind::CurveControl,
               .entity = entity,
               .modeling_object_id = modeling_object_id,
               .selected = {{.index = 3,
                             .generation = 7,
                             .role = EditorSubSelectionElementRole::Primary}},
               .active = EditorSubSelectionTarget{
                   .index = 3,
                   .generation = 7,
                   .role = EditorSubSelectionElementRole::Primary,
               },
               .hover = EditorSubSelectionTarget{
                   .index = 3,
                   .generation = 7,
                   .role = EditorSubSelectionElementRole::SecondaryB,
               },
               .revision = 41,
               .owner_revision = 17,
               .presentation_revision = 23} {}

  EditorSubSelectionSnapshot sub_selection_snapshot() const override {
    return {.kind = state.kind,
            .participates_in_bulk_selection = true,
            .entity = state.entity,
            .modeling_object_id = state.modeling_object_id,
            .selected_count =
                static_cast<std::uint32_t>(state.selected.size()),
            .active = state.active,
            .revision = state.revision};
  }

  EditorSubSelectionBaseline
  capture_sub_selection_baseline() const override {
    return state;
  }

  void synchronize_sub_selection_scope() override {}

  ModelingCommandResult edit_sub_elements(
      std::span<const EditorSubSelectionTarget> hits,
      EditorSubSelectionEdit,
      const EditorSubSelectionBaseline *,
      std::optional<EditorSubSelectionTarget> direct_hit) override {
    state.selected.assign(hits.begin(), hits.end());
    state.active = direct_hit;
    state.hover.reset();
    ++state.revision;
    ++state.owner_revision;
    ++state.presentation_revision;
    ++failed_edit_count;
    return {.ok = false,
            .changed = true,
            .message = "Injected provider failure after mutation."};
  }

  ModelingCommandResult restore_sub_selection_baseline(
      const EditorSubSelectionBaseline &baseline) override {
    if (fail_restore) {
      ++restore_count;
      return {.ok = false,
              .changed = true,
              .message = "Injected provider rollback failure."};
    }
    const bool changed = state.selected != baseline.selected ||
                         state.active != baseline.active ||
                         state.hover != baseline.hover ||
                         state.revision != baseline.revision ||
                         state.owner_revision != baseline.owner_revision ||
                         state.presentation_revision !=
                             baseline.presentation_revision;
    state = baseline;
    ++restore_count;
    return {.ok = true, .changed = changed};
  }

  ModelingCommandResult preview_sub_element_hover(
      std::optional<EditorSubSelectionTarget> target) override {
    const bool changed = state.hover != target;
    state.hover = target;
    return {.ok = true, .changed = changed};
  }

  ModelingCommandResult select_all_sub_elements() override {
    return {.ok = true};
  }
  ModelingCommandResult invert_sub_elements() override {
    return {.ok = true};
  }
  ModelingCommandResult clear_sub_elements() override {
    return {.ok = true};
  }
  ModelingCommandResult delete_selected_sub_elements() override {
    return {.ok = true};
  }

  EditorSubSelectionBaseline state;
  int failed_edit_count = 0;
  int restore_count = 0;
  bool fail_restore = false;
};

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

TEST(CurveToolServiceTests,
     NativeControlCursorReusesStationaryPicksAndClearsOnBlockedFrames) {
  CurveToolHarness harness;
  const auto object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.activate_curve_edit();
  const SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();

  /// Counts real owner picks without replacing their geometry or occlusion logic.
  class CountingPicker final : public ViewportCurveControlPickingPort {
  public:
    CountingPicker(const ViewportCameraController &camera,
                   const SceneSurfaceQuerySnapshot &surfaces)
        : picker_(camera, &surfaces) {}

    std::optional<CurveControlTarget> pick(
        const CurveToolPreviewState &preview, QVec2 local,
        bool anchors_only) const override {
      ++calls;
      return picker_.pick(preview, local, anchors_only);
    }

    mutable std::size_t calls = 0u;

  private:
    ViewportCurveControlPickingAdapter picker_;
  } picker(harness.camera, surfaces);

  bool handles_visible = true;
  const auto frame = [&]() {
    return harness.interaction.handle_frame(
        harness.lifecycle, harness.session.scene(), harness.session.assets(),
        nullptr, nullptr, nullptr, nullptr, nullptr,
        &harness.session.point_placement(), harness.camera,
        {.input = harness.input,
         .input_result = harness.input_result,
         .handles_visible = handles_visible,
         .scene_surface_revision = surfaces.revision(),
         .curve_control_picker = &picker},
        &harness.session.curve());
  };
  for (const CurveControlKind kind :
       {CurveControlKind::Anchor, CurveControlKind::OutgoingHandle}) {
    harness.input.mouse_local = curve_control_screen(
        harness, harness.session.curve().preview_state(), 0u, kind);
    (void)frame();
    EXPECT_EQ(harness.interaction.pointer_cursor(),
              ViewportPointerCursor::OpenHand);
    const std::size_t picked_calls = picker.calls;
    const std::uint64_t preview_revision =
        harness.session.curve().preview_state().revision;
    for (int unchanged = 0; unchanged < 4; ++unchanged) {
      (void)frame();
      EXPECT_EQ(harness.interaction.pointer_cursor(),
                ViewportPointerCursor::OpenHand);
    }
    EXPECT_EQ(picker.calls, picked_calls);
    EXPECT_EQ(harness.session.curve().preview_state().revision,
              preview_revision);
  }
  const std::size_t picked_calls = picker.calls;
  handles_visible = false;
  (void)frame();
  EXPECT_EQ(harness.interaction.pointer_cursor(),
            ViewportPointerCursor::Default);
  EXPECT_EQ(picker.calls, picked_calls);
  handles_visible = true;
  harness.input.viewport_focused = false;
  (void)frame();
  EXPECT_EQ(harness.interaction.pointer_cursor(),
            ViewportPointerCursor::Default);
  harness.input.viewport_focused = true;
  harness.input_result.navigation_active = true;
  (void)frame();
  EXPECT_EQ(harness.interaction.pointer_cursor(),
            ViewportPointerCursor::Default);
  harness.input_result.navigation_active = false;
  harness.input.mouse_local = {-100.0f, -100.0f};
  (void)frame();
  EXPECT_EQ(harness.interaction.pointer_cursor(),
            ViewportPointerCursor::Default);
  EXPECT_EQ(picker.calls, picked_calls + 1u);
  harness.interaction.clear_drag_state();
  EXPECT_EQ(harness.interaction.pointer_cursor(),
            ViewportPointerCursor::Default);
}

TEST(CurveToolServiceTests, HiddenGizmosPreserveDrawAndPresetConstruction) {
  for (const auto kind : {CurveCreationKind::Draw, CurveCreationKind::Circle,
                          CurveCreationKind::Oval}) {
    SCOPED_TRACE(static_cast<int>(kind));
    CurveToolHarness harness;
    const std::size_t expected_points = kind == CurveCreationKind::Draw ? 1u
        : kind == CurveCreationKind::Circle ? 4u : 6u;
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    auto &curve = harness.session.curve();
    ASSERT_TRUE(curve.set_creation_kind(kind).ok);
    harness.handles_visible = false;
    EXPECT_TRUE(harness.frame(false, false, false).consumed);
    EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::Default);
    const auto &hover = curve.preview_state();
    if (kind == CurveCreationKind::Draw) {
      EXPECT_TRUE(hover.hover_world.has_value());
    } else {
      EXPECT_TRUE(hover.preset_hover_document.has_value());
    }
    EXPECT_TRUE(harness.frame(true, true, false).claim_interaction_pane);
    if (kind == CurveCreationKind::Draw) {
      ASSERT_TRUE(curve.point_construction_dragging());
      harness.input.mouse_local.x += 35.0f;
      EXPECT_TRUE(harness.frame(false, true, false).consumed);
      EXPECT_TRUE(curve.point_construction_dragging());
      EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::Default);
      harness.handles_visible = true;
      EXPECT_TRUE(harness.frame(false, true, false).consumed);
      EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::ClosedHand);
      harness.handles_visible = false;
    }
    EXPECT_TRUE(harness.frame(false, false, true).consumed);
    EXPECT_FALSE(curve.dragging());
    EXPECT_EQ(curve.point_count(), expected_points);
    EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::Default);
    harness.handles_visible = true;
    harness.input.mouse_local = curve_control_screen(
        harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
    EXPECT_TRUE(harness.frame(false, false, false).consumed);
    EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::OpenHand);
    harness.handles_visible = false;
    EXPECT_TRUE(harness.frame(false, false, false).consumed);
    EXPECT_EQ(harness.interaction.pointer_cursor(), ViewportPointerCursor::Default);
    EXPECT_EQ(curve.point_count(), expected_points);
  }
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
     DisabledSceneSnappingSkipsSharedSurfaceQueryDuringDrawingAndDrag) {
  CurveToolHarness harness;
  for (int index = 0; index < 24; ++index) {
    SCOPED_TRACE(index);
    const auto mesh = harness.editor.modeling().create_box({
        .min = {static_cast<float>(index), 0.0f, 4.0f},
        .max = {static_cast<float>(index + 1), 1.0f, 5.0f},
    });
    ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  }

  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  const auto surface_build_count = [&]() {
    return harness.session.point_placement()
        .surface_query_stats()
        .snapshot_build_count;
  };
  const std::uint64_t cold_build_count = surface_build_count();

  // A cold or explicitly invalidated cache must remain untouched whenever
  // scene landmark snapping is disabled.
  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_TRUE(harness.session.curve().preview_state().hover_world.has_value());
  EXPECT_EQ(surface_build_count(), cold_build_count);

  harness.scene_snap_enabled = true;
  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_GT(surface_build_count(), cold_build_count);

  const std::uint64_t warm_build_count = surface_build_count();
  harness.scene_snap_enabled = false;
  harness.grid_snap_enabled = false;
  harness.session.scene_surface_query().invalidate();

  EXPECT_TRUE(harness.frame(false, false, false).curve_consumed);
  EXPECT_TRUE(harness.session.curve().preview_state().hover_world.has_value());
  EXPECT_EQ(surface_build_count(), warm_build_count);
  EXPECT_TRUE(harness.frame(true, true, false).curve_consumed);
  const std::uint64_t pointer_surface_build_count = surface_build_count();
  EXPECT_GT(pointer_surface_build_count, warm_build_count);
  harness.input.mouse_local.x += 24.0f;
  EXPECT_TRUE(harness.frame(false, true, false).curve_consumed);
  EXPECT_EQ(surface_build_count(), pointer_surface_build_count);
  EXPECT_TRUE(harness.frame(false, false, true).curve_consumed);
  EXPECT_EQ(surface_build_count(), pointer_surface_build_count);
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
     PassiveExistingCurveSolidifySettingsAreValidatedAndUndoable) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_FALSE(curve.active());
  ASSERT_TRUE(curve.has_editable_existing_curve());
  EXPECT_FALSE(curve.solidify_enabled());
  EXPECT_EQ(curve.solidify_resolution(), kCurveSolidifyDefaultResolution);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), kCurveSolidifyDefaultRadius);

  harness.session.history().clear();
  const CurveComponent baseline = *harness.session.scene().curve(object.entity);
  const std::uint64_t revision_before_invalid =
      curve.tool_settings_revision();
  EXPECT_FALSE(curve
                   .set_solidify_resolution(static_cast<std::uint32_t>(
                       kCurveSolidifyMinimumResolution - 1))
                   .ok);
  EXPECT_FALSE(curve
                   .set_solidify_resolution(static_cast<std::uint32_t>(
                       kCurveSolidifyMaximumResolution + 1))
                   .ok);
  EXPECT_FALSE(curve.set_solidify_radius(
                        std::numeric_limits<double>::quiet_NaN())
                   .ok);
  EXPECT_FALSE(
      curve.set_solidify_radius(kCurveSolidifyMinimumRadius * 0.5).ok);
  EXPECT_EQ(*harness.session.scene().curve(object.entity), baseline);
  EXPECT_EQ(curve.tool_settings_revision(), revision_before_invalid);
  EXPECT_FALSE(harness.session.history().can_undo());

  EXPECT_FALSE(curve.set_solidify(false).changed);
  EXPECT_FALSE(
      curve.set_solidify_resolution(kCurveSolidifyDefaultResolution).changed);
  EXPECT_FALSE(
      curve.set_solidify_radius(kCurveSolidifyDefaultRadius).changed);
  EXPECT_FALSE(harness.session.history().can_undo());

  ASSERT_TRUE(curve.set_solidify_resolution(24U).changed);
  EXPECT_EQ(curve.solidify_resolution(), 24U);
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(), "Set Curve Component");
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.solidify_resolution(), kCurveSolidifyDefaultResolution);
  ASSERT_TRUE(harness.session.history().redo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.solidify_resolution(), 24U);

  harness.session.history().clear();
  ASSERT_TRUE(curve.set_solidify_radius(0.75).changed);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), kCurveSolidifyDefaultRadius);
  ASSERT_TRUE(harness.session.history().redo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);

  ASSERT_TRUE(curve.set_solidify(true).changed);
  ASSERT_TRUE(curve.solidify_enabled());
  harness.session.history().clear();
  ASSERT_TRUE(curve.set_solidify(false).changed);
  EXPECT_FALSE(curve.solidify_enabled());
  EXPECT_EQ(curve.solidify_resolution(), 24U);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.solidify_enabled());
  EXPECT_EQ(curve.solidify_resolution(), 24U);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);
  ASSERT_TRUE(harness.session.history().redo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_FALSE(curve.solidify_enabled());
  EXPECT_EQ(curve.solidify_resolution(), 24U);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);
}

TEST(CurveToolServiceTests,
     ApplySolidifyBakesMeshKeepsGeneratorAndRestoresUndoRedo) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  const EntityId parent =
      harness.session.scene().create_entity("Pipe Parent", EntityKind::Empty);
  ASSERT_NE(parent, kInvalidEntityId);
  ASSERT_TRUE(harness.session.scene().set_parent(object.entity, parent, false));
  TransformComponent transform;
  transform.position = {2.0f, 3.0f, -1.0f};
  transform.rotation_euler = {0.25f, 0.5f, -0.125f};
  transform.scale = {1.25f, 0.75f, 2.0f};
  ASSERT_TRUE(harness.session.scene().set_transform(object.entity, transform));
  const SceneLayerId layer =
      harness.session.scene().create_layer("Pipe Layer");
  ASSERT_NE(layer, kInvalidSceneLayerId);
  ASSERT_TRUE(harness.session.scene().set_entity_layer(object.entity, layer));
  const MaterialComponent material{
      .material = MaterialReference(42u),
      .material_slots = {{7u, MaterialReference(77u)}},
  };
  ASSERT_TRUE(harness.session.scene().set_material(object.entity, material));
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.set_solidify(true).ok);
  ASSERT_TRUE(curve.set_solidify_resolution(12U).ok);
  ASSERT_TRUE(curve.set_solidify_radius(0.35).ok);
  ASSERT_TRUE(curve.can_apply_solidify());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget selected_anchor{
      curve.preview_state().document->points().front().id,
      CurveControlKind::Anchor};
  const std::array selected_controls{selected_anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      selected_controls,
      quader_editor_core::SelectionEditOperation::Replace, nullptr,
      selected_anchor));
  expect_control_selection(curve.control_selection(), {selected_anchor},
                           selected_anchor);

  const NameComponent *source_name =
      harness.session.scene().name(object.entity);
  ASSERT_NE(source_name, nullptr);
  const std::string baked_base_name = source_name->name + " Mesh";
  const EntityId existing_name = harness.session.scene().create_entity(
      baked_base_name, EntityKind::Generic);
  ASSERT_NE(existing_name, kInvalidEntityId);

  harness.session.history().clear();
  const CurveComponent source_before =
      *harness.session.scene().curve(object.entity);
  const std::size_t entity_count_before =
      harness.session.scene().entities().size();
  const std::size_t mesh_count_before = harness.session.assets().mesh_count();
  const std::uint64_t material_revision_before =
      harness.session.scene().material_binding_revision();
  const CommandResult applied = curve.apply_solidify();
  ASSERT_TRUE(applied.ok) << applied.message;
  ASSERT_TRUE(applied.changed);
  ASSERT_FALSE(curve.active());
  ASSERT_FALSE(curve.has_editable_existing_curve());
  EXPECT_FALSE(curve.has_selected_controls());
  ASSERT_TRUE(harness.session.scene().exists(object.entity));
  const CurveComponent *source_after =
      harness.session.scene().curve(object.entity);
  ASSERT_NE(source_after, nullptr);
  EXPECT_FALSE(source_after->solidify);
  EXPECT_EQ(source_after->curve, source_before.curve);
  EXPECT_EQ(source_after->solidify_resolution,
            source_before.solidify_resolution);
  EXPECT_EQ(source_after->solidify_radius, source_before.solidify_radius);
  EXPECT_EQ(harness.session.scene().entities().size(),
            entity_count_before + 1u);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before + 1u);
  ASSERT_EQ(harness.session.scene().selected_entities().size(), 1u);
  const EntityId baked = harness.session.scene().selected_entities().front();
  ASSERT_NE(baked, object.entity);
  ASSERT_TRUE(harness.session.scene().exists(baked));
  ASSERT_NE(harness.session.scene().name(baked), nullptr);
  EXPECT_EQ(harness.session.scene().name(baked)->name,
            baked_base_name + "_1");
  EXPECT_EQ(harness.session.scene().parent(baked), parent);
  ASSERT_NE(harness.session.scene().transform(baked), nullptr);
  const TransformComponent *baked_transform =
      harness.session.scene().transform(baked);
  ASSERT_NE(baked_transform, nullptr);
  EXPECT_FLOAT_EQ(baked_transform->position.x, transform.position.x);
  EXPECT_FLOAT_EQ(baked_transform->position.y, transform.position.y);
  EXPECT_FLOAT_EQ(baked_transform->position.z, transform.position.z);
  EXPECT_FLOAT_EQ(baked_transform->rotation_euler.x,
                  transform.rotation_euler.x);
  EXPECT_FLOAT_EQ(baked_transform->rotation_euler.y,
                  transform.rotation_euler.y);
  EXPECT_FLOAT_EQ(baked_transform->rotation_euler.z,
                  transform.rotation_euler.z);
  EXPECT_FLOAT_EQ(baked_transform->scale.x, transform.scale.x);
  EXPECT_FLOAT_EQ(baked_transform->scale.y, transform.scale.y);
  EXPECT_FLOAT_EQ(baked_transform->scale.z, transform.scale.z);
  EXPECT_EQ(baked_transform->custom_pivot, transform.custom_pivot);
  EXPECT_EQ(harness.session.scene().entity_layer(baked), layer);
  ASSERT_NE(harness.session.scene().mesh(baked), nullptr);
  const AssetId baked_mesh = harness.session.scene().mesh(baked)->mesh;
  ASSERT_NE(baked_mesh, kInvalidAssetId);
  const MeshAsset *mesh = harness.session.assets().mesh(baked_mesh);
  ASSERT_NE(mesh, nullptr);
  EXPECT_TRUE(mesh->has_authored_polygon_document);
  ASSERT_NE(mesh->authored_polygon_document, nullptr);
  EXPECT_FALSE(mesh->authored_polygon_document->vertices.empty());
  EXPECT_FALSE(mesh->authored_polygon_document->faces.empty());
  EXPECT_FALSE(mesh->geometry().vertices.empty());
  EXPECT_FALSE(mesh->geometry().indices.empty());
  ASSERT_NE(harness.session.scene().material(baked), nullptr);
  EXPECT_EQ(*harness.session.scene().material(baked), material);
  const ModelingObjectComponent *baked_object =
      harness.session.scene().modeling_object(baked);
  ASSERT_NE(baked_object, nullptr);
  EXPECT_TRUE(baked_object->sdk_owned);
  const int baked_modeling_object = baked_object->object_id;
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(), "Apply Solidify");
  EXPECT_GT(harness.session.scene().material_binding_revision(),
            material_revision_before);

  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(harness.session.scene().exists(object.entity));
  ASSERT_NE(harness.session.scene().curve(object.entity), nullptr);
  EXPECT_EQ(*harness.session.scene().curve(object.entity), source_before);
  EXPECT_FALSE(harness.session.scene().exists(baked));
  EXPECT_EQ(harness.session.assets().mesh(baked_mesh), nullptr);
  EXPECT_EQ(harness.session.assets().mesh_count(), mesh_count_before);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{object.entity});
  EXPECT_EQ(harness.session.scene().primary_selection(), object.entity);
  EXPECT_TRUE(curve.has_editable_existing_curve());
  EXPECT_TRUE(curve.can_apply_solidify());
  EXPECT_TRUE(curve.has_selected_controls());
  expect_control_selection(curve.control_selection(), {selected_anchor},
                           selected_anchor);

  ASSERT_TRUE(harness.session.history().redo().ok);
  EXPECT_FALSE(curve.has_selected_controls())
      << "Redo must synchronize the shared Curve sub-selection owner before "
         "the next viewport refresh";
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(harness.session.scene().exists(object.entity));
  ASSERT_NE(harness.session.scene().curve(object.entity), nullptr);
  EXPECT_FALSE(harness.session.scene().curve(object.entity)->solidify);
  ASSERT_TRUE(harness.session.scene().exists(baked));
  ASSERT_NE(harness.session.scene().mesh(baked), nullptr);
  EXPECT_EQ(harness.session.scene().mesh(baked)->mesh, baked_mesh);
  ASSERT_NE(harness.session.scene().modeling_object(baked), nullptr);
  EXPECT_EQ(harness.session.scene().modeling_object(baked)->object_id,
            baked_modeling_object);
  ASSERT_NE(harness.session.assets().mesh(baked_mesh), nullptr);
  ASSERT_NE(harness.session.scene().name(baked), nullptr);
  EXPECT_EQ(harness.session.scene().name(baked)->name,
            baked_base_name + "_1");
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{baked});
  EXPECT_EQ(harness.session.scene().primary_selection(), baked);
}

TEST(CurveToolServiceTests,
     PassiveSolidifyContinuousEditsMergeAndRespectControlDragBlocking) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.has_editable_existing_curve());

  harness.session.history().clear();
  constexpr std::string_view kResolutionGesture =
      "test.curve.solidify.resolution.drag";
  ASSERT_TRUE(curve
                  .set_solidify_resolution(
                      12U, {.merge_key = std::string(kResolutionGesture)})
                  .changed);
  ASSERT_TRUE(curve
                  .set_solidify_resolution(
                      24U, {.merge_key = std::string(kResolutionGesture)})
                  .changed);
  EXPECT_EQ(curve.solidify_resolution(), 24U);
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.solidify_resolution(), kCurveSolidifyDefaultResolution);
  EXPECT_FALSE(harness.session.history().can_undo())
      << "one continuous Resolution gesture must create one history item";
  ASSERT_TRUE(harness.session.history().redo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.solidify_resolution(), 24U);

  harness.session.history().clear();
  constexpr std::string_view kRadiusGesture =
      "test.curve.solidify.radius.drag";
  ASSERT_TRUE(curve
                  .set_solidify_radius(
                      0.25, {.merge_key = std::string(kRadiusGesture)})
                  .changed);
  ASSERT_TRUE(curve
                  .set_solidify_radius(
                      0.75, {.merge_key = std::string(kRadiusGesture)})
                  .changed);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), 0.75);
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_DOUBLE_EQ(curve.solidify_radius(), kCurveSolidifyDefaultRadius);
  EXPECT_FALSE(harness.session.history().can_undo())
      << "one continuous Radius gesture must create one history item";

  const CurveControlTarget anchor{
      .point = curve.preview_state().document->points().front().id,
      .kind = CurveControlKind::Anchor,
  };
  ASSERT_TRUE(begin_curve_control_selection_drag(curve, anchor).ok);
  ASSERT_TRUE(curve.dragging());
  const CurveComponent before_blocked =
      *harness.session.scene().curve(object.entity);
  const CommandResult blocked = curve.set_solidify_radius(
      1.0, {.merge_key = "test.curve.solidify.blocked"});
  EXPECT_FALSE(blocked.ok);
  EXPECT_FALSE(blocked.changed);
  EXPECT_EQ(*harness.session.scene().curve(object.entity), before_blocked);
  EXPECT_FALSE(harness.session.history().can_undo());
  ASSERT_TRUE(curve.cancel_drag().ok);
}

TEST(CurveToolServiceTests, PassiveExistingCurveClosureUsesModelingHistory) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.has_editable_existing_curve());
  ASSERT_FALSE(curve.closed());
  harness.session.history().clear();

  ASSERT_TRUE(curve.set_closed(true).changed);
  EXPECT_TRUE(curve.closed());
  ASSERT_NE(harness.session.history().undo_label(), nullptr);
  EXPECT_EQ(*harness.session.history().undo_label(), "Edit Curve");
  ASSERT_TRUE(harness.session.history().undo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_FALSE(curve.closed());
  ASSERT_TRUE(harness.session.history().redo().ok);
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.closed());
  EXPECT_TRUE(curve.has_editable_existing_curve());
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

  (void)create_curve_pick_occluder(harness, anchor_screen,
                                   control_hit->ray_distance);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  ASSERT_TRUE(curve.refresh().ok);
  const SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();
  const ViewportRay ray =
      harness.camera.ray_from_viewport_point(anchor_screen);
  const auto surface_hit = surfaces.nearest_hit({
      .world_origin = ray.origin,
      .world_direction = ray.direction,
      .eligibility = SceneSurfaceEligibility::RenderVisible,
  });
  ASSERT_TRUE(surface_hit.has_value());
  ASSERT_LT(surface_hit->distance, control_hit->ray_distance);
  click_passive_curve_selection(harness, anchor_screen);

  EXPECT_TRUE(curve.control_selection().empty());
  const auto snapshot = harness.editor.query().entity(object.entity);
  ASSERT_TRUE(snapshot.has_value());
  EXPECT_FALSE(snapshot->selected)
      << "the hidden control must fall through to canonical scene picking";
  EXPECT_FALSE(harness.editor.selection().summary().has_selection);
}

TEST(CurveToolServiceTests,
     PassiveCurveControlModifierMissPreservesCanonicalSelection) {
  constexpr std::array kModifiers{
      ViewportModifiers{.shift = true},
      ViewportModifiers{.primary = true},
  };
  for (const ViewportModifiers modifier : kModifiers) {
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

TEST(CurveToolServiceTests,
     CreationPreferencesSurviveCanceledDraftsAndFollowSharedAppLifetime) {
  CurveSessionDefaults remembered;
  SceneSession first_session;
  SceneSession second_session;
  CurveToolService first(
      first_session.scene(), first_session.assets(), first_session.history(),
      first_session.modeling(), first_session.selection(),
      first_session.document_commands(), &remembered);
  CurveToolService second(
      second_session.scene(), second_session.assets(), second_session.history(),
      second_session.modeling(), second_session.selection(),
      second_session.document_commands(), &remembered);

  ASSERT_TRUE(first.activate().ok);
  ASSERT_TRUE(first.set_closed(true).changed);
  ASSERT_TRUE(first.set_creation_kind(CurveCreationKind::Circle).changed);
  ASSERT_TRUE(first.set_preset_radius(2.75).changed);
  ASSERT_TRUE(first.set_creation_kind(CurveCreationKind::Oval).changed);
  ASSERT_TRUE(first.set_oval_length(12.0).changed);
  ASSERT_TRUE(first.set_preset_radius(3.5).changed);
  EXPECT_FALSE(first.set_preset_radius(7.0).ok);
  ASSERT_TRUE(first.place_preset({.center = {8.0f, 2.0f, 4.0f}}).changed);
  ASSERT_TRUE(first.cancel().changed);

  ASSERT_TRUE(second.activate().changed);
  EXPECT_EQ(second.creation_kind(), CurveCreationKind::Oval);
  EXPECT_DOUBLE_EQ(second.preset_radius(), 3.5);
  EXPECT_DOUBLE_EQ(second.oval_length(), 12.0);
  EXPECT_EQ(second.point_count(), 0u);
  EXPECT_FALSE(second.preview_state().preset_hover_document.has_value());
  ASSERT_TRUE(second.set_creation_kind(CurveCreationKind::Circle).changed);
  EXPECT_DOUBLE_EQ(second.preset_radius(), 2.75);
  ASSERT_TRUE(second.cancel().changed);

  ASSERT_TRUE(first.activate().changed);
  EXPECT_EQ(first.creation_kind(), CurveCreationKind::Circle);
  EXPECT_DOUBLE_EQ(first.preset_radius(), 2.75);
  ASSERT_TRUE(first.set_creation_kind(CurveCreationKind::Draw).changed);
  EXPECT_TRUE(first.closed());
  EXPECT_EQ(first.point_count(), 0u);
  EXPECT_TRUE(first_session.scene().entities().empty());
  EXPECT_TRUE(second_session.scene().entities().empty());

  SceneSession fresh_session;
  CurveToolService fresh(
      fresh_session.scene(), fresh_session.assets(), fresh_session.history(),
      fresh_session.modeling(), fresh_session.selection(),
      fresh_session.document_commands());
  ASSERT_TRUE(fresh.activate().changed);
  EXPECT_EQ(fresh.creation_kind(), CurveCreationKind::Draw);
  EXPECT_FALSE(fresh.closed());
  EXPECT_DOUBLE_EQ(fresh.preset_radius(), kCurvePresetDefaultRadius);
  EXPECT_DOUBLE_EQ(fresh.oval_length(), kCurveOvalDefaultLength);
}

TEST(CurveToolServiceTests,
     ConcurrentCreationOwnersRememberAValidPairOfOvalDimensions) {
  CurveSessionDefaults remembered;
  SceneSession first_session;
  SceneSession second_session;
  CurveToolService first(
      first_session.scene(), first_session.assets(), first_session.history(),
      first_session.modeling(), first_session.selection(),
      first_session.document_commands(), &remembered);
  CurveToolService second(
      second_session.scene(), second_session.assets(), second_session.history(),
      second_session.modeling(), second_session.selection(),
      second_session.document_commands(), &remembered);
  ASSERT_TRUE(first.activate().ok);
  ASSERT_TRUE(second.activate().ok);
  ASSERT_TRUE(first.set_creation_kind(CurveCreationKind::Oval).changed);
  ASSERT_TRUE(second.set_creation_kind(CurveCreationKind::Oval).changed);
  ASSERT_TRUE(first.set_oval_length(12.0).changed);
  ASSERT_TRUE(first.set_preset_radius(3.5).changed);
  ASSERT_TRUE(second.set_oval_length(3.0).changed);
  EXPECT_DOUBLE_EQ(remembered.oval_radius, 1.0);
  EXPECT_DOUBLE_EQ(remembered.oval_length, 3.0);
  ASSERT_TRUE(first.cancel().changed);
  ASSERT_TRUE(first.activate().changed);
  EXPECT_DOUBLE_EQ(first.preset_radius(), 1.0);
  EXPECT_DOUBLE_EQ(first.oval_length(), 3.0);
  EXPECT_TRUE(first.place_preset({}).ok);
}

TEST(CurveToolServiceTests,
     RememberedCreationPreferencesDoNotOverwriteAttachedCurveProperties) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &authored = harness.session.curve();
  ASSERT_TRUE(authored.refresh().ok);
  ASSERT_TRUE(authored.set_solidify(true).changed);
  ASSERT_TRUE(authored.set_solidify_radius(0.35).changed);
  ASSERT_TRUE(authored.set_solidify_resolution(12u).changed);
  ASSERT_FALSE(authored.closed());

  CurveSessionDefaults remembered{.closed = true, .circle_radius = 3.0};
  CurveToolService editing(
      harness.session.scene(), harness.session.assets(),
      harness.session.history(), harness.session.modeling(),
      harness.session.selection(), harness.session.document_commands(),
      &remembered);
  ASSERT_TRUE(editing.activate().changed);
  EXPECT_TRUE(editing.has_editable_existing_curve());
  EXPECT_FALSE(editing.closed());
  EXPECT_TRUE(editing.solidify_enabled());
  EXPECT_NEAR(editing.solidify_radius(), 0.35, 1e-7);
  EXPECT_EQ(editing.solidify_resolution(), 12u);
  EXPECT_TRUE(remembered.closed);
  ASSERT_TRUE(editing.begin_new_curve().changed);
  EXPECT_TRUE(editing.closed());
  EXPECT_DOUBLE_EQ(editing.preset_radius(), 3.0);
  const CurveComponent *component = harness.session.scene().curve(object.entity);
  ASSERT_NE(component, nullptr);
  EXPECT_TRUE(component->solidify);
  EXPECT_FLOAT_EQ(component->solidify_radius, 0.35f);
  EXPECT_EQ(component->solidify_resolution, 12u);
}

TEST(
    CurveToolServiceTests,
    PresetSettingsRetainPerTypeValuesAndRejectInvalidCoupledDimensionsAtomically) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();

  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
  EXPECT_TRUE(curve.can_change_creation_kind());
  EXPECT_TRUE(curve.can_reset_settings());
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), kCurvePresetDefaultRadius);
  ASSERT_TRUE(curve.set_preset_radius(3.0).changed);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), 3.0);

  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Oval).changed);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), kCurvePresetDefaultRadius);
  EXPECT_DOUBLE_EQ(curve.oval_length(), kCurveOvalDefaultLength);
  const std::uint64_t revision_before_invalid = curve.tool_settings_revision();
  EXPECT_FALSE(curve.set_preset_radius(2.001).ok);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), kCurvePresetDefaultRadius);
  EXPECT_DOUBLE_EQ(curve.oval_length(), kCurveOvalDefaultLength);
  EXPECT_EQ(curve.tool_settings_revision(), revision_before_invalid);

  ASSERT_TRUE(curve.set_oval_length(10.0).changed);
  ASSERT_TRUE(curve.set_preset_radius(3.0).changed);
  EXPECT_FALSE(curve.set_oval_length(5.999).ok);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), 3.0);
  EXPECT_DOUBLE_EQ(curve.oval_length(), 10.0);
  EXPECT_FALSE(
      curve.set_preset_radius(std::numeric_limits<double>::infinity()).ok);
  EXPECT_FALSE(
      curve.set_oval_length(std::numeric_limits<double>::quiet_NaN()).ok);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), 3.0);
  EXPECT_DOUBLE_EQ(curve.oval_length(), 10.0);

  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), 3.0);
  ASSERT_TRUE(curve.reset_settings().changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), kCurvePresetDefaultRadius);

  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Oval).changed);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), 3.0);
  EXPECT_DOUBLE_EQ(curve.oval_length(), 10.0);
  ASSERT_TRUE(curve.reset_settings().changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Oval);
  EXPECT_DOUBLE_EQ(curve.preset_radius(), kCurvePresetDefaultRadius);
  EXPECT_DOUBLE_EQ(curve.oval_length(), kCurveOvalDefaultLength);
}

TEST(CurveToolServiceTests,
     PresetHoverIsSeparateAndPlacementAuthorsOneNormalizedClosedDraft) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);

  const CurvePresetPlacementFrame frame{
      .center = {3.0f, 4.0f, 5.0f},
      .axis_u = {2.0f, 0.0f, 0.0f},
      .axis_v = {1.0f, 3.0f, 0.0f},
  };
  ASSERT_TRUE(curve.set_preset_hover(frame).changed);
  const CurveToolPreviewState hover = curve.preview_state();
  ASSERT_TRUE(hover.document.has_value());
  EXPECT_EQ(hover.document->point_count(), 0u);
  ASSERT_TRUE(hover.preset_hover_document.has_value());
  EXPECT_EQ(hover.preset_hover_document->point_count(), 4u);
  ASSERT_TRUE(hover.hover_world.has_value());
  EXPECT_EQ(hover.hover_world->x, frame.center.x);
  EXPECT_EQ(hover.hover_world->y, frame.center.y);
  EXPECT_EQ(hover.hover_world->z, frame.center.z);
  const QVec3 hover_origin = transform_point(hover.preset_hover_transform, {});
  const QVec3 hover_x =
      transform_point(hover.preset_hover_transform, {1.0f, 0.0f, 0.0f});
  expect_point({hover_origin.x, hover_origin.y, hover_origin.z}, frame.center);
  expect_point({hover_x.x, hover_x.y, hover_x.z}, {4.0f, 4.0f, 5.0f});
  EXPECT_FALSE(curve.set_preset_hover(frame).changed);

  const std::uint64_t settings_before = curve.tool_settings_revision();
  ASSERT_TRUE(curve.place_preset(frame).changed);
  EXPECT_GT(curve.tool_settings_revision(), settings_before);
  const CurveToolPreviewState placed = curve.preview_state();
  ASSERT_TRUE(placed.document.has_value());
  EXPECT_EQ(placed.document->point_count(), 4u);
  EXPECT_TRUE(placed.document->closed());
  EXPECT_FALSE(placed.preset_hover_document.has_value());
  EXPECT_FALSE(placed.hover_world.has_value());
  EXPECT_TRUE(curve.control_selection().empty());
  EXPECT_TRUE(curve.can_apply());
  expect_point(placed.document->points().front().position, {4.0f, 4.0f, 5.0f});

  const auto before_second_click = *placed.document;
  EXPECT_FALSE(curve.place_preset({.axis_u = {}, .axis_v = {}}).changed);
  EXPECT_TRUE(
      curve.preview_state().document->has_same_geometry(before_second_click));
}

TEST(CurveToolServiceTests,
     ProceduralPresetRegeneratesAtPinnedFrameAndDrawFreezesItsGeometry) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
  ASSERT_TRUE(curve.set_preset_radius(2.0).changed);
  ASSERT_TRUE(curve.place_preset({.center = {7.0f, 2.0f, -3.0f}}).changed);
  ASSERT_EQ(curve.preview_state().document->point_count(), 4u);
  const CurveControlTarget selected{
      .point = curve.preview_state().document->points().front().id,
      .kind = CurveControlKind::Anchor,
  };
  ASSERT_TRUE(replace_curve_control_selection(curve, selected));

  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Oval).changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Oval);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_EQ(curve.preview_state().document->point_count(), 6u);
  EXPECT_TRUE(curve.control_selection().empty());
  ASSERT_TRUE(curve.set_oval_length(8.0).changed);
  const auto regenerated = *curve.preview_state().document;
  EXPECT_TRUE(regenerated.closed());
  const auto bounds = regenerated.bounds();
  ASSERT_TRUE(bounds.valid);
  EXPECT_NEAR(bounds.min.x, 3.0f, 0.0001f);
  EXPECT_NEAR(bounds.max.x, 11.0f, 0.0001f);
  EXPECT_NEAR(bounds.min.y, 1.0f, 0.0001f);
  EXPECT_NEAR(bounds.max.y, 3.0f, 0.0001f);

  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Draw).changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(regenerated));
  EXPECT_FALSE(curve.can_change_creation_kind());
  EXPECT_FALSE(curve.set_creation_kind(CurveCreationKind::Circle).ok);
  EXPECT_FALSE(curve.set_preset_radius(4.0).ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(regenerated));
}

TEST(CurveToolServiceTests,
     OnlyCommittedPresetControlGeometryEditsConvertTheDraftToDraw) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
  ASSERT_TRUE(curve.place_preset({}).changed);
  const CurveControlTarget anchor{
      .point = curve.preview_state().document->points().front().id,
      .kind = CurveControlKind::Anchor,
  };
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);

  ASSERT_TRUE(curve.begin_selected_control_drag().changed);
  ASSERT_TRUE(curve.commit_drag().changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle)
      << "a no-op gizmo commit is not an authored geometry edit";

  ASSERT_TRUE(curve.begin_selected_control_drag().changed);
  const QVec3 canceled_origin = *curve.drag_origin_world();
  ASSERT_TRUE(
      curve.update_drag(canceled_origin + QVec3{2.0f, 0.0f, 0.0f}).changed);
  ASSERT_TRUE(curve.cancel_drag().changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);

  ASSERT_TRUE(curve.begin_selected_control_drag().changed);
  const QVec3 committed_origin = *curve.drag_origin_world();
  ASSERT_TRUE(
      curve.update_drag(committed_origin + QVec3{2.0f, 0.0f, 0.0f}).changed);
  ASSERT_TRUE(curve.commit_drag().changed);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
  EXPECT_FALSE(curve.can_change_creation_kind());
}

TEST(CurveToolServiceTests,
     PresetPointConversionAndDeletionEndParametricRegeneration) {
  {
    CurveToolHarness harness;
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    CurveToolService &curve = harness.session.curve();
    ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
    ASSERT_TRUE(curve.place_preset({}).changed);
    const auto anchor = curve.preview_state().document->points().front().id;
    const std::uint64_t revision = curve.tool_settings_revision();
    ASSERT_TRUE(curve.toggle_point_smoothness(anchor).changed);
    EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
    EXPECT_GT(curve.tool_settings_revision(), revision);
    EXPECT_FALSE(curve.can_change_creation_kind());
  }

  {
    CurveToolHarness harness;
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    CurveToolService &curve = harness.session.curve();
    ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
    ASSERT_TRUE(curve.place_preset({}).changed);
    const CurveControlTarget handle{
        .point = curve.preview_state().document->points().front().id,
        .kind = CurveControlKind::OutgoingHandle,
    };
    ASSERT_TRUE(replace_curve_control_selection(curve, handle));
    const ModelingCommandResult removed = curve.delete_selected_sub_elements();
    ASSERT_TRUE(removed.ok) << removed.message;
    ASSERT_TRUE(removed.changed);
    EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
    EXPECT_FALSE(curve.can_change_creation_kind());
  }
}

TEST(CurveToolServiceTests,
     FirstDrawPointDisablesTypeChangesAndPublishesTheSettingsRevision) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_EQ(curve.creation_kind(), CurveCreationKind::Draw);
  ASSERT_TRUE(curve.can_change_creation_kind());
  const std::uint64_t before = curve.tool_settings_revision();

  ASSERT_TRUE(curve.begin_point({1.0f, 2.0f, 3.0f}).changed);
  EXPECT_FALSE(curve.can_change_creation_kind());
  EXPECT_GT(curve.tool_settings_revision(), before);
  const std::uint64_t during_drag = curve.tool_settings_revision();
  ASSERT_TRUE(curve.commit_drag().changed);
  EXPECT_FALSE(curve.can_change_creation_kind());
  EXPECT_GT(curve.tool_settings_revision(), during_drag);
  EXPECT_FALSE(curve.set_creation_kind(CurveCreationKind::Circle).ok);
}

TEST(CurveToolServiceTests,
     AppliedPresetBecomesAnOrdinaryCurveAndNewCurveRetainsThePresetType) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Oval).changed);
  ASSERT_TRUE(curve.set_oval_length(9.0).changed);
  ASSERT_TRUE(curve.place_preset({.center = {2.0f, 0.0f, 6.0f}}).changed);
  const modeling::CurveDocument authored = *curve.preview_state().document;

  ASSERT_TRUE(curve.apply().changed);
  EXPECT_TRUE(curve.active());
  EXPECT_FALSE(curve.drawing());
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Oval);
  EXPECT_FALSE(curve.can_change_creation_kind());
  EXPECT_FALSE(curve.can_reset_settings());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(authored));
  EXPECT_TRUE(curve.preview_state().document->closed());

  ASSERT_TRUE(curve.new_curve().changed);
  EXPECT_TRUE(curve.drawing());
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Oval);
  EXPECT_DOUBLE_EQ(curve.oval_length(), 9.0);
  EXPECT_EQ(curve.point_count(), 0u);
  EXPECT_TRUE(curve.can_change_creation_kind());
  EXPECT_TRUE(curve.can_reset_settings());
}

TEST(CurveToolServiceTests,
     PresetCancelIsTransientAndApplyUndoRedoReplayOneOrdinarySelectedCurve) {
  {
    CurveToolHarness canceled;
    canceled.activate_curve_edit();
    CurveToolService &curve = canceled.session.curve();
    const std::vector<EntityId> baseline_entities =
        canceled.session.scene().entities();
    const std::vector<EntityId> baseline_selection =
        canceled.session.scene().selected_entities();
    const EntityId baseline_active =
        canceled.session.scene().primary_selection();
    canceled.session.history().clear();
    canceled.session.mark_clean();
    const bool baseline_can_undo = canceled.session.history().can_undo();
    const bool baseline_can_redo = canceled.session.history().can_redo();
    const auto baseline_history = canceled.editor.history().snapshot();
    ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
    ASSERT_TRUE(curve.place_preset({.center = {2.0f, 0.0f, -3.0f},
                                    .axis_v = {0.0f, 0.0f, 1.0f}})
                    .changed);
    ASSERT_TRUE(curve.can_apply());
    EXPECT_EQ(canceled.session.scene().entities(), baseline_entities);
    EXPECT_EQ(canceled.session.history().can_undo(), baseline_can_undo);
    EXPECT_EQ(canceled.session.history().can_redo(), baseline_can_redo);
    EXPECT_TRUE(canceled.editor.history().can_undo())
        << "the public history API exposes the cancellable native preview";

    ASSERT_TRUE(canceled.editor.tools().cancel_active_tool_preview().ok());
    EXPECT_FALSE(curve.active());
    EXPECT_EQ(canceled.session.scene().entities(), baseline_entities);
    EXPECT_EQ(canceled.session.scene().selected_entities(),
              baseline_selection);
    EXPECT_EQ(canceled.session.scene().primary_selection(), baseline_active);
    EXPECT_EQ(canceled.session.history().can_undo(), baseline_can_undo);
    EXPECT_EQ(canceled.session.history().can_redo(), baseline_can_redo);
    const auto canceled_history = canceled.editor.history().snapshot();
    EXPECT_EQ(canceled_history.can_undo, baseline_history.can_undo);
    EXPECT_EQ(canceled_history.can_redo, baseline_history.can_redo);
    EXPECT_EQ(canceled_history.undo_label, baseline_history.undo_label);
    EXPECT_EQ(canceled_history.redo_label, baseline_history.redo_label);
  }

  CurveToolHarness applied;
  applied.activate_curve_edit();
  CurveToolService &curve = applied.session.curve();
  const std::vector<EntityId> baseline_entities =
      applied.session.scene().entities();
  const std::vector<EntityId> baseline_selection =
      applied.session.scene().selected_entities();
  const EntityId baseline_active = applied.session.scene().primary_selection();
  applied.session.history().clear();
  applied.session.mark_clean();
  const bool baseline_can_undo = applied.session.history().can_undo();
  const bool baseline_can_redo = applied.session.history().can_redo();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Oval).changed);
  ASSERT_TRUE(curve.set_oval_length(7.0).changed);
  ASSERT_TRUE(curve.place_preset({.center = {-4.0f, 1.0f, 6.0f},
                                  .axis_v = {0.0f, 0.0f, 1.0f}})
                  .changed);
  const modeling::CurveDocument authored = *curve.preview_state().document;
  EXPECT_EQ(applied.session.scene().entities(), baseline_entities);
  EXPECT_EQ(applied.session.history().can_undo(), baseline_can_undo);
  EXPECT_EQ(applied.session.history().can_redo(), baseline_can_redo);
  EXPECT_TRUE(applied.editor.history().can_undo())
      << "the public history API exposes the cancellable native preview";

  ASSERT_TRUE(applied.editor.tools().commit_active_tool_preview().ok());
  EXPECT_FALSE(curve.active());
  ASSERT_FALSE(curve.drawing());
  EXPECT_EQ(applied.editor.tools().active_tool(), "select");
  const EntityId created = curve.preview_state().entity;
  ASSERT_NE(created, kInvalidEntityId);
  const std::vector<EntityId> applied_entities =
      applied.session.scene().entities();
  EXPECT_EQ(applied_entities.size(), baseline_entities.size() + 1u);
  EXPECT_EQ(std::count(applied_entities.begin(), applied_entities.end(),
                       created),
            1);
  const auto created_snapshot = applied.editor.query().entity(created);
  ASSERT_TRUE(created_snapshot.has_value());
  EXPECT_EQ(created_snapshot->name, "Curve");
  EXPECT_EQ(created_snapshot->kind, QuaderEntityKind::Curve);
  EXPECT_TRUE(created_snapshot->has_curve);
  EXPECT_TRUE(created_snapshot->has_modeling_object);
  EXPECT_TRUE(created_snapshot->selected);
  EXPECT_TRUE(created_snapshot->active);
  EXPECT_EQ(applied.session.scene().selected_entities(),
            std::vector<EntityId>{created});
  EXPECT_TRUE(curve.controls_editable());
  EXPECT_EQ(applied.editor.query().history().undo_label, "Create Curve");

  ASSERT_TRUE(applied.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(applied.editor.tools().active_tool(), "select");
  EXPECT_FALSE(applied.session.scene().exists(created));
  EXPECT_EQ(applied.session.scene().entities(), baseline_entities);
  EXPECT_EQ(applied.session.scene().selected_entities(), baseline_selection);
  EXPECT_EQ(applied.session.scene().primary_selection(), baseline_active);
  EXPECT_FALSE(applied.editor.history().can_undo())
      << "preset Apply must publish exactly one history item";
  EXPECT_TRUE(applied.editor.history().can_redo());
  EXPECT_EQ(applied.editor.query().history().redo_label, "Create Curve");

  ASSERT_TRUE(applied.editor.history().redo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(applied.editor.tools().active_tool(), "select");
  ASSERT_TRUE(applied.session.scene().exists(created));
  const std::vector<EntityId> replayed_entities =
      applied.session.scene().entities();
  EXPECT_EQ(replayed_entities.size(), baseline_entities.size() + 1u);
  EXPECT_EQ(std::count(replayed_entities.begin(), replayed_entities.end(),
                       created),
            1);
  const auto replayed_snapshot = applied.editor.query().entity(created);
  ASSERT_TRUE(replayed_snapshot.has_value());
  EXPECT_EQ(replayed_snapshot->name, "Curve");
  EXPECT_EQ(replayed_snapshot->kind, QuaderEntityKind::Curve);
  EXPECT_TRUE(replayed_snapshot->selected);
  EXPECT_TRUE(replayed_snapshot->active);
  EXPECT_EQ(applied.session.scene().selected_entities(),
            std::vector<EntityId>{created});
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(authored));
  EXPECT_TRUE(curve.controls_editable());
}

TEST(CurveToolServiceTests,
     PresetControlsReuseClickModifierAndMarqueeSelectionWithoutHistory) {
  CurveToolHarness harness;
  harness.activate_curve_edit();
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.set_creation_kind(CurveCreationKind::Circle).changed);
  ASSERT_TRUE(curve.place_preset({.axis_u = {1.0f, 0.0f, 0.0f},
                                  .axis_v = {0.0f, 0.0f, 1.0f}})
                  .changed);
  const modeling::CurveDocument before = *curve.preview_state().document;
  ASSERT_EQ(before.point_count(), 4u);
  const CurveControlTarget first{before.points()[0].id,
                                 CurveControlKind::Anchor};
  const CurveControlTarget second{before.points()[1].id,
                                  CurveControlKind::Anchor};
  const CurveControlTarget third{before.points()[2].id,
                                 CurveControlKind::Anchor};
  const QVec2 first_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const QVec2 second_screen = curve_control_screen(
      harness, curve.preview_state(), 1u, CurveControlKind::Anchor);
  const QVec2 third_screen = curve_control_screen(
      harness, curve.preview_state(), 2u, CurveControlKind::Anchor);
  const auto history_before = harness.editor.history().snapshot();

  harness.click_curve_control(first_screen);
  expect_control_selection(curve.control_selection(), {first}, first);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);

  harness.click_curve_control(second_screen, {.shift = true});
  expect_control_selection(curve.control_selection(), {first, second},
                           second);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);

  harness.marquee_curve_controls(third_screen - QVec2{7.0f, 7.0f},
                                 third_screen + QVec2{7.0f, 7.0f});
  expect_control_selection(curve.control_selection(), {third}, third);
  EXPECT_EQ(curve.creation_kind(), CurveCreationKind::Circle);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);
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
  EXPECT_FALSE(curve.active());
  EXPECT_FALSE(curve.drawing());
  EXPECT_TRUE(curve.controls_editable());
  EXPECT_TRUE(curve.has_editable_existing_curve());
  EXPECT_EQ(harness.editor.tools().active_tool(), "select");
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

TEST(CurveToolServiceTests,
     PublicFinalizeAlwaysExitsToSelectAndKeepsPassiveCurveEditing) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef existing = harness.make_curve();
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Move).ok());
  ASSERT_EQ(harness.editor.tools().active_tool(), "move");
  ASSERT_TRUE(harness.editor.selection().clear().ok());

  ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.active());
  ASSERT_TRUE(curve.drawing());
  harness.session.history().clear();
  harness.session.mark_clean();
  harness.place({-3.0f, 0.0f, 0.0f});
  harness.place({3.0f, 0.0f, 0.0f});

  const QuaderApiVoidResult finalized =
      harness.editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(finalized.ok()) << finalized.error_message();
  EXPECT_TRUE(finalized.changed());
  EXPECT_FALSE(curve.active());
  EXPECT_FALSE(curve.drawing());
  EXPECT_TRUE(curve.controls_editable());
  EXPECT_TRUE(curve.has_editable_existing_curve());
  EXPECT_EQ(harness.editor.tools().active_tool(), "select");
  EXPECT_FALSE(harness.session.native_tools().has_pending_tool());
  EXPECT_EQ(harness.editor.query().history().undo_label, "Create Curve");

  const EntityId created = harness.session.scene().primary_selection();
  ASSERT_NE(created, kInvalidEntityId);
  ASSERT_NE(created, existing.entity);
  ASSERT_NE(harness.session.scene().curve(created), nullptr);
  const QuaderToolSettingsTarget passive_settings =
      harness.editor.tools().active_settings();
  EXPECT_TRUE(passive_settings.active);
  EXPECT_EQ(passive_settings.tool_id, "curve");
  EXPECT_FALSE(passive_settings.apply_visible);
  EXPECT_FALSE(passive_settings.can_cancel);
  EXPECT_FALSE(passive_settings.can_reset);
  EXPECT_EQ(std::ranges::find_if(passive_settings.controls,
                                [](const auto &control) {
                                  return control.id == kCurveNewAction;
                                }),
            passive_settings.controls.end());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_FALSE(harness.session.scene().exists(created));
  EXPECT_FALSE(curve.controls_editable());
  EXPECT_EQ(harness.editor.tools().active_tool(), "select");
  EXPECT_FALSE(harness.editor.history().can_undo());

  ASSERT_TRUE(harness.editor.history().redo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(harness.session.scene().exists(created));
  EXPECT_EQ(harness.session.scene().primary_selection(), created);
  EXPECT_TRUE(curve.controls_editable());
  EXPECT_EQ(curve.preview_state().entity, created);
  EXPECT_EQ(harness.editor.tools().active_tool(), "select");
}

TEST(CurveToolServiceTests,
     PublicBeginNewCurveDetachesSelectedCurveIntoAnEmptyDrawingSession) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef existing = harness.make_curve();
  ASSERT_EQ(harness.session.scene().primary_selection(), existing.entity);
  ASSERT_NE(harness.session.scene().curve(existing.entity), nullptr);

  const QuaderApiVoidResult started =
      harness.editor.tools().begin_new_curve_creation();

  ASSERT_TRUE(started.ok()) << started.error_message();
  EXPECT_TRUE(started.changed());
  CurveToolService &curve = harness.session.curve();
  EXPECT_TRUE(curve.active());
  EXPECT_TRUE(curve.drawing());
  EXPECT_EQ(curve.point_count(), 0u);
  EXPECT_FALSE(curve.has_editable_existing_curve());
  EXPECT_EQ(curve.preview_state().entity, kInvalidEntityId);
  EXPECT_TRUE(curve.preview_state().document.has_value());
  EXPECT_EQ(harness.session.scene().primary_selection(), existing.entity)
      << "starting a fresh draft must not rewrite object selection";
  EXPECT_EQ(harness.editor.tools().active_tool(), "curve");
  EXPECT_FALSE(curve.refresh().changed);
  EXPECT_TRUE(curve.drawing());
  EXPECT_FALSE(curve.has_editable_existing_curve());
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
}

TEST(CurveToolServiceTests,
     PublicBeginNewCurveRejectsAndPreservesNonemptyDraftAndActiveDrag) {
  CurveToolHarness harness;
  ASSERT_TRUE(harness.editor.tools().begin_new_curve_creation().ok());
  CurveToolService &curve = harness.session.curve();
  harness.place({-2.0f, 0.0f, 0.0f});
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const modeling::CurveDocument draft = *curve.preview_state().document;

  const QuaderApiVoidResult nonempty =
      harness.editor.tools().begin_new_curve_creation();
  EXPECT_FALSE(nonempty.ok());
  EXPECT_FALSE(nonempty.changed());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(draft));
  EXPECT_EQ(curve.point_count(), 1u);
  EXPECT_FALSE(harness.session.history().can_undo());

  ASSERT_TRUE(curve.begin_point({2.0f, 0.0f, 0.0f}).ok);
  const modeling::CurveDocument dragging = *curve.preview_state().document;
  const QuaderApiVoidResult active_drag =
      harness.editor.tools().begin_new_curve_creation();
  EXPECT_FALSE(active_drag.ok());
  EXPECT_FALSE(active_drag.changed());
  ASSERT_TRUE(curve.preview_state().document.has_value());
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(dragging));
  EXPECT_TRUE(curve.dragging());
  EXPECT_FALSE(harness.session.history().can_undo());
  ASSERT_TRUE(curve.cancel_drag().ok);
  ASSERT_TRUE(harness.editor.tools().cancel_active_tool_preview().ok());
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
                                                     select_frame, harness.session.native_tools());
  EXPECT_NE(harness.controller.transform_gizmo_frame(), nullptr);

  harness.router.set_active_tool(ViewportToolId::Poly);
  harness.input.active_tool_owns_viewport = true;
  harness.input.modal_tool_active = true;
  ViewportInteractionFrame modal_frame = harness.gizmo_frame();
  modal_frame.active_tool_allows_selection_fallback = false;
  (void)harness.controller.handle_before_active_tool(harness.editor,
                                                     modal_frame, harness.session.native_tools());
  EXPECT_EQ(harness.controller.transform_gizmo_frame(), nullptr);
  EXPECT_TRUE(curve.controls_editable())
      << "The modal tool suspends passive interaction without detaching the "
         "canonical Curve selection.";
}

TEST(CurveToolServiceTests,
     ToolPointerLifecycleOwnsCurveGizmoAndConversionPrecedence) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.router.set_active_tool(ViewportToolId::Select);
  harness.router.set_selection_mode(ViewportSelectionMode::Object);
  harness.input.active_tool_owns_viewport = false;

  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const CurveControlTarget anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));

  ViewportInteractionFrame interaction = harness.gizmo_frame();
  interaction.active_tool_allows_selection_fallback = true;
  const ViewportNativeToolInteractionFrame native{
      .input = harness.input,
      .input_result = harness.input_result,
  };
  const auto dispatch = [&]() {
    return harness.controller.handle_tool_pointer_lifecycle(
        harness.editor, harness.session.scene(), harness.session.assets(),
        harness.session.native_tools(), &harness.session.point_placement(),
        harness.camera, {.interaction = interaction, .native_tool = native});
  };

  const ViewportToolPointerLifecycleResult gizmo = dispatch();
  EXPECT_TRUE(gizmo.curve_gizmo_prepass);
  EXPECT_FALSE(gizmo.native_tool.consumed);
  EXPECT_NE(harness.controller.transform_gizmo_frame(), nullptr);

  harness.input.curve_conversion_edges.push_back({});
  const ViewportToolPointerLifecycleResult conversion = dispatch();
  EXPECT_FALSE(conversion.curve_gizmo_prepass);
  EXPECT_TRUE(conversion.native_tool.curve_consumed);
  EXPECT_TRUE(conversion.native_tool.blocks_transform_gizmo);
  EXPECT_TRUE(conversion.before_tool.gizmo_consumed);
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
  constexpr std::array kToolCases{
      ToolCase{ViewportToolId::Move,
               quader_editor_core::ViewportInteractionTool::Move},
      ToolCase{ViewportToolId::Rotate,
               quader_editor_core::ViewportInteractionTool::Rotate},
      ToolCase{ViewportToolId::Scale,
               quader_editor_core::ViewportInteractionTool::Scale},
  };
  for (const ToolCase &tool_case : kToolCases) {
    harness.router.set_active_tool(tool_case.tool);
    ViewportInteractionFrame frame = harness.gizmo_frame();
    frame.active_tool_allows_selection_fallback = true;
    (void)harness.controller.handle_before_active_tool(harness.editor, frame,
                                                       harness.session.native_tools());
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
      harness.editor, control_frame, harness.session.native_tools());
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
      harness.editor, control_frame, harness.session.native_tools());
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
      harness.editor, passive_frame(), harness.session.native_tools());
  const auto *object_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(object_gizmo, nullptr);
  const CurveToolPreviewState preview = curve.preview_state();
  ASSERT_TRUE(preview.document.has_value());
  const QVec2 middle_anchor =
      curve_control_screen(harness, preview, 1, CurveControlKind::Anchor);
  (void)harness.controller.handle_before_active_tool(
      harness.editor, passive_frame(), harness.session.native_tools());
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
      harness.editor, passive_frame(), harness.session.native_tools());
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
      harness.editor, passive_frame(), harness.session.native_tools());
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
      harness.editor, passive_frame(), harness.session.native_tools());
  object_gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(object_gizmo, nullptr);
  const auto *object_z = find_curve_gizmo_axis(
      *object_gizmo, quader_editor_core::TransformAxis::Z);
  ASSERT_NE(object_z, nullptr);
  const QVec2 object_press = axis_screen_point(*object_z, 0.65f);
  const SceneSurfaceQuerySnapshot surfaces =
      harness.session.scene_surface_query().snapshot();
  EXPECT_FALSE(pick_unoccluded_viewport_curve_control(
                   harness.camera, curve.preview_state(), object_press,
                   surfaces)
                   .has_value());
  harness.input.mouse_local = object_press;
  harness.input.mouse_screen = object_press;
  const auto object_press_result =
      harness.controller.handle_before_active_tool(
          harness.editor, passive_frame(true, true, false, object_press),
          harness.session.native_tools());
  EXPECT_TRUE(object_press_result.gizmo_consumed);
  EXPECT_TRUE(harness.controller.transform_gizmo_pointer_active());
  harness.input.mouse_local = object_press + QVec2{0.0f, -40.0f};
  harness.input.mouse_screen = harness.input.mouse_local;
  EXPECT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      passive_frame(false, true, false, object_press), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_TRUE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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
  constexpr std::array kCases{
      MoveCase{CurveControlKind::Anchor,
               quader_editor_core::TransformAxis::X, false, "anchor x"},
      MoveCase{CurveControlKind::OutgoingHandle,
               quader_editor_core::TransformAxis::Xz, true, "handle xz"},
  };

  for (const MoveCase &move_case : kCases) {
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
        harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
        harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
        harness.editor, harness.gizmo_frame(true, true, false, start), harness.session.native_tools());
    ASSERT_TRUE(press.gizmo_consumed);
    EXPECT_FALSE(harness.controller.dragging());

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    const auto drag = harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(false, true, false, start), harness.session.native_tools());
    ASSERT_TRUE(drag.gizmo_consumed);
    ASSERT_TRUE(harness.controller.dragging());
    ASSERT_TRUE(curve.dragging());
    ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

    const auto release = harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(false, false, true, start), harness.session.native_tools());
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
  constexpr std::array kCases{
      AxisCase{quader_editor_core::TransformAxis::X, {7.0f, 0.0f, 0.0f},
               "X"},
      AxisCase{quader_editor_core::TransformAxis::Y, {0.0f, 7.0f, 0.0f},
               "Y"},
      AxisCase{quader_editor_core::TransformAxis::Z, {0.0f, 0.0f, 7.0f},
               "Z"},
  };

  for (const AxisCase &axis_case : kCases) {
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
        [&](const ViewportRay &, double vertex_radius,
            std::optional<double>)
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
      };
    };

    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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

    harness.input.transform_vertex_snap_down = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                    .gizmo_consumed);
    EXPECT_EQ(snap_query_count, 0);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());

    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
                        harness.gizmo_frame(false, false, true, start), harness.session.native_tools())
                    .gizmo_consumed);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_FALSE(curve.dragging());
    EXPECT_EQ(snap_query_count, 1);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty());
  }
}

TEST(CurveToolServiceTests,
     AnchorCentralGizmoHeldSnapMatchesVertexExactlyDespiteCoarseGrid) {
  CurveToolHarness harness;
  harness.camera.set_view_direction({-0.45f, -0.35f, -0.82f});
  harness.make_curve();
  harness.activate_curve_edit();
  (void)harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, true);
  (void)harness.settings.set_bool_value(ViewportBoolSettingId::GridSnap,
                                        true);
  (void)harness.settings.set_bool_value(ViewportBoolSettingId::VertexSnap,
                                        false);
  ASSERT_TRUE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_TRUE(
      harness.settings.bool_value(ViewportBoolSettingId::GridSnap));
  ASSERT_FALSE(
      harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  harness.input.grid_preset = 10;
  harness.input.transform_vertex_snap_down = true;

  CurveToolService &curve = harness.session.curve();
  const modeling::CurveDocument before = *curve.preview_state().document;
  const CurveControlTarget anchor{before.points()[0].id,
                                  CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
  const std::optional<QVec3> pivot_before =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(pivot_before.has_value());
  const QVec3 snap_world = *pivot_before + QVec3{5.35f, -2.45f, 3.2f};
  int snap_query_count = 0;
  harness.mesh_snap_query =
      [&](const ViewportRay &, double,
          std::optional<double>) -> ViewportMeshSnapQueryResult {
    ++snap_query_count;
    return {
        .vertex_target = ViewportMeshVertexSnapTarget{
            .modeling_object_id = 117,
            .vertex_id = 63,
            .world_position = snap_world,
            .ray_distance = 4.0,
            .pick_distance = 0.0,
        },
    };
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *center = find_curve_gizmo_plane(
      *gizmo, quader_editor_core::TransformAxis::All);
  ASSERT_NE(center, nullptr);
  const QVec2 start = plane_screen_center(*center);
  const ViewportScreenPoint target_screen =
      harness.camera.project_world_point(snap_world);
  ASSERT_TRUE(target_screen.visible);

  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start),
                      harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_EQ(snap_query_count, 0);

  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start),
                      harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  EXPECT_EQ(snap_query_count, 1);
  const std::optional<QVec3> snapped_pivot =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(snapped_pivot.has_value());
  EXPECT_NEAR(snapped_pivot->x, snap_world.x, 0.0001f);
  EXPECT_NEAR(snapped_pivot->y, snap_world.y, 0.0001f);
  EXPECT_NEAR(snapped_pivot->z, snap_world.z, 0.0001f);

  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, false, true, start),
                      harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
}

TEST(CurveToolServiceTests,
     AnchorSnapRequiresHeldActionAndReusesOverlayStorage) {
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
      [&](const ViewportRay &, double,
          std::optional<double>) -> ViewportMeshSnapQueryResult {
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_EQ(snap_query_count, 0);
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty())
      << "Persistent snapping settings must not activate gizmo vertex snap.";

  harness.input.transform_vertex_snap_down = true;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  const auto &reused_overlay =
      harness.controller.transform_gizmo_snap_overlay_points();
  ASSERT_EQ(reused_overlay.size(), 2u);
  EXPECT_EQ(reused_overlay.data(), overlay_storage);
  EXPECT_EQ(reused_overlay.capacity(), overlay_capacity);

  harness.input.transform_vertex_snap_down = false;
  const int query_count_before_release = snap_query_count;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_EQ(snap_query_count, query_count_before_release);
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());

  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::SnappingEnabled, false));
  ASSERT_TRUE(harness.settings.set_bool_value(
      ViewportBoolSettingId::VertexSnap, false));
  ASSERT_FALSE(harness.settings.bool_value(
      ViewportBoolSettingId::SnappingEnabled));
  ASSERT_FALSE(
      harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));
  harness.input.transform_vertex_snap_down = true;
  const int query_count_before_override = snap_query_count;
  update.x += 7.0f;
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_EQ(snap_query_count, query_count_before_override + 1);
  ASSERT_EQ(harness.controller.transform_gizmo_snap_overlay_points().size(),
            2u);
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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
  harness.mesh_snap_query =
      [&](const ViewportRay &, double,
          std::optional<double>) -> ViewportMeshSnapQueryResult {
    ++mesh_snap_queries;
    return {};
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
      harness.editor, passive_frame(), harness.session.native_tools());
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

  harness.input.transform_vertex_snap_down = true;
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor, passive_frame(true, true, false, start),
                      harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor, passive_frame(false, true, false, start),
                      harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  EXPECT_EQ(mesh_snap_queries, 1u);
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
                      passive_frame(false, false, true, start), harness.session.native_tools())
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
     LockedAndNonselectableMeshesOccludeCurveAnchorSnapCandidates) {
  struct OccluderCase {
    bool locked = false;
    const char *label = "";
  };
  constexpr std::array kCases{
      OccluderCase{true, "locked"},
      OccluderCase{false, "nonselectable"},
  };

  for (const OccluderCase occluder_case : kCases) {
    SCOPED_TRACE(occluder_case.label);
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
    (void)harness.settings.set_bool_value(
        ViewportBoolSettingId::VertexSnap, true);
    ASSERT_TRUE(
        harness.settings.bool_value(ViewportBoolSettingId::VertexSnap));

    CurveToolService &curve = harness.session.curve();
    ASSERT_TRUE(curve.refresh().ok);
    const CurveToolPreviewState preview = curve.preview_state();
    ASSERT_TRUE(preview.document.has_value());
    ASSERT_EQ(preview.document->point_count(), 2u);
    const CurveControlTarget source{preview.document->points()[0].id,
                                    CurveControlKind::Anchor};
    const QVec3 target_world = transform_point(
        preview.world_transform,
        curve_control_local_position(preview.document->points()[1],
                                     CurveControlKind::Anchor));
    const ViewportScreenPoint projected_target =
        harness.camera.project_world_point(target_world);
    ASSERT_TRUE(projected_target.visible);
    const ViewportRay target_ray =
        harness.camera.ray_from_viewport_point(projected_target.position);
    const double target_ray_distance = static_cast<double>(
        dot(target_world - target_ray.origin, target_ray.direction));
    ASSERT_GT(target_ray_distance, 0.0);

    const QuaderModelingObjectRef occluder = create_curve_pick_occluder(
        harness, projected_target.position, target_ray_distance);
    ASSERT_NE(occluder.entity, kInvalidEntityId);
    if (occluder_case.locked) {
      ASSERT_TRUE(
          harness.session.scene().set_selectable(occluder.entity, true));
      ASSERT_TRUE(
          harness.session.scene().set_locked(occluder.entity, true));
      harness.session.scene_surface_query().invalidate();
    }
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
    ASSERT_TRUE(curve.refresh().ok);
    ASSERT_TRUE(replace_curve_control_selection(curve, source));
    harness.curve_snap_candidates.push_back({
        .entity = object.entity,
        .world_transform = preview.world_transform,
        .document =
            std::make_shared<const modeling::CurveDocument>(*preview.document),
    });

    std::size_t mesh_snap_queries = 0u;
    std::optional<double> forwarded_surface_cutoff;
    harness.mesh_snap_query =
        [&](const ViewportRay &, double,
            std::optional<double> maximum_ray_distance)
        -> ViewportMeshSnapQueryResult {
      ++mesh_snap_queries;
      forwarded_surface_cutoff = maximum_ray_distance;
      return {};
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
        harness.editor, passive_frame(), harness.session.native_tools());
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto *axis = find_curve_gizmo_axis(
        *gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const QVec2 start = axis_screen_point(*axis, 0.65f);
    harness.input.transform_vertex_snap_down = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(true, true, false, start),
                        harness.session.native_tools())
                    .gizmo_consumed);
    harness.input.mouse_local = projected_target.position;
    harness.input.mouse_screen = projected_target.position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(false, true, false, start),
                        harness.session.native_tools())
                    .gizmo_consumed);

    EXPECT_EQ(mesh_snap_queries, 1u);
    ASSERT_TRUE(forwarded_surface_cutoff.has_value());
    EXPECT_LT(*forwarded_surface_cutoff, target_ray_distance);
    EXPECT_TRUE(
        harness.controller.transform_gizmo_snap_overlay_points().empty())
        << "The render-visible foreground must reject the hidden Curve "
           "anchor even when that mesh cannot itself become a snap target.";
    EXPECT_TRUE(harness.controller.cancel_active_drag(
        harness.editor, harness.session.native_tools()));
  }
}

TEST(CurveToolServiceTests,
     AuthoredOtherCurveAndMeshTargetsUseDeterministicCrossDomainDepthOrdering) {
  struct CompetitionCase {
    double mesh_depth_delta;
    bool expect_curve_target;
    const char *label;
  };
  constexpr std::array kCases{
      CompetitionCase{0.0, false, "exact depth and pick tie prefers mesh"},
      CompetitionCase{2.0e-8, true,
                      "Curve closer beyond depth epsilon prefers Curve"},
  };

  for (const CompetitionCase &competition : kCases) {
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
            double,
            std::optional<double>) -> ViewportMeshSnapQueryResult {
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
        harness.editor, passive_frame(), harness.session.native_tools());
    const auto *gizmo = harness.controller.transform_gizmo_frame();
    ASSERT_NE(gizmo, nullptr);
    const auto *axis = find_curve_gizmo_axis(
        *gizmo, quader_editor_core::TransformAxis::X);
    ASSERT_NE(axis, nullptr);
    const QVec2 start = axis_screen_point(*axis, 0.65f);
    const ViewportScreenPoint target_screen =
        harness.camera.project_world_point(target_world);
    ASSERT_TRUE(target_screen.visible);

    harness.input.transform_vertex_snap_down = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(true, true, false, start), harness.session.native_tools())
                    .gizmo_consumed);
    harness.input.mouse_local = target_screen.position;
    harness.input.mouse_screen = target_screen.position;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        passive_frame(false, true, false, start), harness.session.native_tools())
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
    EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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

  constexpr std::array<QVec3, 8u> kBoxCorners{
      QVec3{5.0f, -1.0f, -1.0f}, QVec3{5.0f, -1.0f, 1.0f},
      QVec3{5.0f, 1.0f, -1.0f},  QVec3{5.0f, 1.0f, 1.0f},
      QVec3{7.0f, -1.0f, -1.0f}, QVec3{7.0f, -1.0f, 1.0f},
      QVec3{7.0f, 1.0f, -1.0f},  QVec3{7.0f, 1.0f, 1.0f},
  };
  std::optional<ViewportRay> eligible_ray;
  std::optional<ModelingMeshVertexSnapTarget> eligible_target;
  for (const QVec3 corner : kBoxCorners) {
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
          double radius,
          std::optional<double> maximum_ray_distance)
          -> ViewportMeshSnapQueryResult {
    ++real_mesh_queries;
    const std::optional<QuaderMeshVertexSnapTarget> target =
        harness.editor.query().mesh_vertex_snap_target(
            {.origin = ray.origin, .direction = ray.direction}, radius, false,
            maximum_ray_distance);
    ViewportMeshSnapQueryResult result;
    if (target.has_value()) {
      result.vertex_target = ViewportMeshVertexSnapTarget{
          .modeling_object_id = target->modeling_object_id,
          .vertex_id =
              static_cast<quader_poly::ElementId>(target->vertex_id),
          .world_position = target->world_position,
          .ray_distance = target->ray_distance,
          .pick_distance = target->pick_distance,
      };
    }
    return result;
  };

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  const ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_world);
  ASSERT_TRUE(target_screen.visible);

  harness.input.transform_vertex_snap_down = true;
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto *axis = find_curve_gizmo_axis(
      *gizmo, quader_editor_core::TransformAxis::X);
  ASSERT_NE(axis, nullptr);
  const QVec2 start = axis_screen_point(*axis, 0.65f);
  const ViewportScreenPoint target_screen =
      harness.camera.project_world_point(target_world);
  ASSERT_TRUE(target_screen.visible);

  harness.input.transform_vertex_snap_down = true;
  harness.input.mouse_local = start;
  harness.input.mouse_screen = start;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = target_screen.position;
  harness.input.mouse_screen = target_screen.position;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
                      harness.gizmo_frame(false, false, true, start), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_FALSE(curve.dragging());
  EXPECT_TRUE(
      harness.controller.transform_gizmo_snap_overlay_points().empty());
  EXPECT_FALSE(harness.session.history().can_undo())
      << "Draft control edits stay inside the uncommitted Curve preview.";
}

TEST(CurveToolServiceTests,
     HandleAndOrdinaryAxisPlaneGizmosDoNotQueryMeshVertexSnap) {
  struct IneligibleCase {
    CurveControlKind control;
    quader_editor_core::TransformAxis axis;
    bool plane;
    const char *label;
  };
  constexpr std::array kCases{
      IneligibleCase{CurveControlKind::OutgoingHandle,
                     quader_editor_core::TransformAxis::X, false,
                     "handle on X axis"},
      IneligibleCase{CurveControlKind::Anchor,
                     quader_editor_core::TransformAxis::Xz, true,
                     "anchor on XZ plane"},
  };

  for (const IneligibleCase &ineligible : kCases) {
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
        [&](const ViewportRay &, double, std::optional<double>)
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
      };
    };

    (void)harness.controller.handle_before_active_tool(
        harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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

    harness.input.transform_vertex_snap_down = true;
    harness.input.mouse_local = start;
    harness.input.mouse_screen = start;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                    .gizmo_consumed);
    harness.input.mouse_local = update;
    harness.input.mouse_screen = update;
    ASSERT_TRUE(harness.controller
                    .handle_before_active_tool(
                        harness.editor,
                        harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
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
                        harness.gizmo_frame(false, false, true, start), harness.session.native_tools())
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(curve.dragging());
  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
  EXPECT_TRUE(curve.active());
  ASSERT_TRUE(curve.control_selection().active.has_value());
  EXPECT_EQ(*curve.control_selection().active, target);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());

  (void)harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
  ASSERT_NE(harness.controller.transform_gizmo_frame(), nullptr);
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  harness.input.pointer_interaction_canceled = true;
  const auto canceled = harness.controller.handle_before_active_tool(
      harness.editor, harness.gizmo_frame(false, false, true, start), harness.session.native_tools());
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
  constexpr std::array kInvalidations{
      InvalidationCase{Invalidation::Camera, "camera"},
      InvalidationCase{Invalidation::View, "view"},
      InvalidationCase{Invalidation::Navigation, "navigation"},
  };

  for (const InvalidationCase &invalidation : kInvalidations) {
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
          harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                          harness.session.native_tools())
                      .gizmo_consumed);
      if (cross_threshold) {
        harness.input.mouse_local = update;
        harness.input.mouse_screen = update;
        ASSERT_TRUE(harness.controller
                        .handle_before_active_tool(
                            harness.editor,
                            harness.gizmo_frame(false, true, false, start),
                            harness.session.native_tools())
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
          harness.session.native_tools());
      harness.input_result = {};

      EXPECT_TRUE(canceled.gizmo_consumed);
      EXPECT_FALSE(harness.controller.dragging());
      EXPECT_FALSE(curve.dragging());
      EXPECT_FALSE(harness.controller.cancel_active_drag(harness.editor,
                                                          harness.session.native_tools()));
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(harness.controller.dragging());
  ASSERT_TRUE(curve.dragging());
  ASSERT_FALSE(curve.preview_state().document->has_same_geometry(before));

  harness.controller.reset(&harness.editor, harness.session.native_tools());
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
      harness.gizmo_frame(true, true, false, gizmo_press), harness.session.native_tools());
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
      harness.gizmo_frame(false, true, false, gizmo_press), harness.session.native_tools());
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
  EXPECT_TRUE(harness.controller.cancel_active_drag(harness.editor, harness.session.native_tools()));
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
  EXPECT_FALSE(harness.input.material_transfer_pressed);
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
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const QVec2 anchor_screen = curve_control_screen(
      harness, curve.preview_state(), 0u, CurveControlKind::Anchor);
  const auto control_hit = pick_viewport_curve_control(
      harness.camera, curve.preview_state(), anchor_screen, true);
  ASSERT_TRUE(control_hit.has_value());
  ASSERT_GT(control_hit->ray_distance, 0.0);
  (void)create_curve_pick_occluder(harness, anchor_screen,
                                   control_hit->ray_distance);
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  harness.activate_curve_edit();
  ASSERT_TRUE(curve.refresh().ok);
  const modeling::CurveDocument before = *curve.preview_state().document;
  harness.session.history().clear();
  const std::uint64_t surface_builds_before_idle =
      harness.session.scene_surface_query().stats().snapshot_build_count;
  (void)harness.frame(false, false, false);
  EXPECT_EQ(harness.session.scene_surface_query().stats().snapshot_build_count,
            surface_builds_before_idle)
      << "idle Curve frames must not freeze mesh occlusion";

  harness.input.mouse_local = anchor_screen;
  harness.input.mouse_screen = anchor_screen;
  harness.input.pointer_pressed_edges = {
      {QuaderMouseButton::Left, anchor_screen}};
  harness.resolve_chord("Alt+LeftMouse");
  const ViewportNativeToolInteractionResult press =
      harness.frame(true, true, false);

  EXPECT_TRUE(press.curve_consumed);
  EXPECT_FALSE(press.claim_interaction_pane);
  EXPECT_GT(harness.session.scene_surface_query().stats().snapshot_build_count,
            surface_builds_before_idle);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(before));
  EXPECT_FALSE(curve.dragging());
  EXPECT_FALSE(harness.session.history().can_undo());
}

TEST(CurveToolServiceTests,
     FixedNavigationPressBindingsRejectReassignmentWhileFlyRemainsEditable) {
  /// Describes one press binding that competes with a navigation drag.
  struct BindingCase {
    std::string_view press;
    std::string_view drag;
    QuaderMouseButton button;
    ViewportInputActionKind navigation;
  };
  constexpr std::array kCases{
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
  for (const BindingCase &entry : kCases) {
    SCOPED_TRACE(entry.press);
    CurveToolHarness harness;
    const auto object = harness.make_curve();
    ASSERT_TRUE(harness.editor.tools().activate(kCurveToolId).ok());
    const QuaderKeybindingActionKey action{
        "viewport.curve.toggle_point_smoothness", "viewport.tool.curve"};
    const auto assigned = harness.editor.preferences().set_action_shortcut(
        action, std::string(entry.press));
    if (entry.navigation != ViewportInputActionKind::Fly) {
      EXPECT_FALSE(assigned.ok());
      EXPECT_FALSE(assigned.changed());
      harness.resolve_chords({entry.press, entry.drag});
      EXPECT_TRUE(harness.input.curve_conversion_edges.empty());
      EXPECT_EQ(harness.input.navigation_bindings.orbit_pressed,
                entry.navigation == ViewportInputActionKind::Orbit);
      EXPECT_EQ(harness.input.navigation_bindings.orbit_snap_pressed,
                entry.navigation == ViewportInputActionKind::OrbitSnap);
      EXPECT_EQ(harness.input.navigation_bindings.pan_pressed,
                entry.navigation == ViewportInputActionKind::Pan);
      continue;
    }
    ASSERT_TRUE(assigned.ok()) << assigned.error_message();
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
     FixedNavigationClickBindingsRejectReassignmentWhileFlyRemainsEditable) {
  /// Describes one deferred click that shares a button with navigation.
  struct BindingCase {
    std::string_view click;
    std::string_view press;
    std::string_view drag;
    QuaderMouseButton button;
    ViewportInputActionKind navigation;
  };
  constexpr std::array kCases{
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
  for (const BindingCase &entry : kCases) {
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
    const auto assigned = harness.editor.preferences().set_action_shortcut(
        action, std::string(entry.click));
    if (entry.navigation != ViewportInputActionKind::Fly) {
      EXPECT_FALSE(assigned.ok());
      EXPECT_FALSE(assigned.changed());
      harness.resolve_chords({entry.press, entry.drag});
      EXPECT_TRUE(harness.input.curve_conversion_edges.empty());
      EXPECT_EQ(harness.input.navigation_bindings.orbit_pressed,
                entry.navigation == ViewportInputActionKind::Orbit);
      EXPECT_EQ(harness.input.navigation_bindings.orbit_snap_pressed,
                entry.navigation == ViewportInputActionKind::OrbitSnap);
      EXPECT_EQ(harness.input.navigation_bindings.pan_pressed,
                entry.navigation == ViewportInputActionKind::Pan);
      continue;
    }
    ASSERT_TRUE(assigned.ok()) << assigned.error_message();
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
  for (const int interruption : {0, 1, 2}) {
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
    harness.input.pointer_interaction_canceled = interruption == 2;
    (void)harness.frame(false, interruption != 2, interruption == 2);
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
  constexpr std::array kCases{
      BindingCase{"Alt+RightMouseClick", "Alt+RightMouse",
                  "Alt+RightMouseDrag", QuaderMouseButton::Right},
      BindingCase{"Alt+Mouse4Click", "Alt+Mouse4",
                  "Alt+Mouse4Drag", QuaderMouseButton::Back},
  };
  for (const auto &entry : kCases) {
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
     CanonicalSelectionProjectsCurveControlsAndRoutesBulkOperations) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const std::uint64_t modeling_selection_revision =
      harness.session.modeling().selection_revision();
  const CurveControlTarget first_anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, first_anchor));

  const QuaderSelectionSummary selected = harness.editor.selection().summary();
  EXPECT_EQ(selected.sub_selection_kind,
            QuaderSubSelectionKind::CurveControl);
  EXPECT_EQ(selected.selected_sub_element_count, 1u);
  EXPECT_EQ(selected.sub_selection_revision,
            curve.control_selection_revision());
  EXPECT_EQ(selected.selection_revision, modeling_selection_revision);
  EXPECT_EQ(selected.selection_revision,
            harness.session.modeling().selection_revision());
  const EditorSelectionState editor_state = harness.session.selection().state();
  EXPECT_EQ(editor_state.target.kind, EditorSelectionTargetKind::SubElement);
  ASSERT_TRUE(editor_state.target.sub_element.has_value());
  EXPECT_EQ(editor_state.target.sub_element->index, first_anchor.point.index);
  EXPECT_EQ(editor_state.target.sub_element->generation,
            first_anchor.point.generation);
  EXPECT_EQ(editor_state.target.sub_element->role,
            EditorSubSelectionElementRole::Primary);

  const std::uint64_t first_sub_selection_revision =
      selected.sub_selection_revision;
  const CurveControlTarget second_anchor{
      curve.preview_state().document->points()[1].id,
      CurveControlKind::Anchor};
  const std::array added_control{second_anchor};
  ASSERT_TRUE(curve.edit_control_selection(
      added_control, quader_editor_core::SelectionEditOperation::Add, nullptr,
      second_anchor));
  const QuaderSelectionSummary extended =
      harness.editor.selection().summary();
  EXPECT_GT(extended.sub_selection_revision, first_sub_selection_revision);
  EXPECT_EQ(extended.sub_selection_revision,
            curve.control_selection_revision());
  EXPECT_EQ(extended.selection_revision, modeling_selection_revision);
  EXPECT_EQ(harness.session.modeling().selection_revision(),
            modeling_selection_revision);

  const auto cleared = harness.editor.selection().clear();
  ASSERT_TRUE(cleared.ok()) << cleared.error_message();
  EXPECT_TRUE(cleared.changed());
  EXPECT_TRUE(curve.control_selection().empty());
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected)
      << "clearing controls must preserve their selected Curve owner";

  const auto selected_all = harness.editor.selection().select_all();
  ASSERT_TRUE(selected_all.ok()) << selected_all.error_message();
  EXPECT_TRUE(selected_all.changed());
  EXPECT_EQ(curve.control_selection().controls.size(), 4u);
  EXPECT_EQ(harness.editor.selection().summary().selected_sub_element_count,
            4u);

  const auto inverted_empty = harness.editor.selection().select_inverse();
  ASSERT_TRUE(inverted_empty.ok()) << inverted_empty.error_message();
  EXPECT_TRUE(inverted_empty.changed());
  EXPECT_TRUE(curve.control_selection().empty());
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);
  const QuaderSelectionSummary empty_controls =
      harness.editor.selection().summary();
  EXPECT_EQ(empty_controls.sub_selection_kind,
            QuaderSubSelectionKind::CurveControl);
  EXPECT_EQ(empty_controls.selected_sub_element_count, 0u);
  EXPECT_EQ(empty_controls.selection_revision,
            harness.session.modeling().selection_revision());

  ASSERT_TRUE(harness.editor.selection().select_inverse().ok());
  EXPECT_EQ(curve.control_selection().controls.size(), 4u);
}

TEST(CurveToolServiceTests,
     CurveProviderReportsCheckedFailuresAndRestoresExactBaseline) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  ASSERT_GE(curve.preview_state().document->point_count(), 2u);
  harness.session.history().clear();
  harness.session.mark_clean();

  const auto sub_target = [](modeling::CurvePointId point) {
    return EditorSubSelectionTarget{
        .index = point.index,
        .generation = point.generation,
        .role = EditorSubSelectionElementRole::Primary,
    };
  };
  const EditorSubSelectionTarget first =
      sub_target(curve.preview_state().document->points()[0].id);
  const EditorSubSelectionTarget second =
      sub_target(curve.preview_state().document->points()[1].id);
  const std::array first_hit{first};
  ASSERT_TRUE(curve
                  .edit_sub_elements(first_hit, EditorSubSelectionEdit::Replace,
                                     nullptr, first)
                  .ok);
  ASSERT_TRUE(curve.preview_sub_element_hover(first).ok);
  const std::uint64_t published_revision = curve.preview_state().revision;
  const std::optional<QVec3> baseline_pivot =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(baseline_pivot.has_value());
  const EditorSubSelectionBaseline baseline =
      curve.capture_sub_selection_baseline();
  ASSERT_TRUE(baseline.valid());
  EXPECT_EQ(baseline.presentation_revision, published_revision);

  const ModelingCommandResult hover_only_change = curve.edit_sub_elements(
      first_hit, EditorSubSelectionEdit::Replace, &baseline, first);
  ASSERT_TRUE(hover_only_change.ok) << hover_only_change.message;
  EXPECT_TRUE(hover_only_change.changed)
      << "clearing the provider hover is an observable provider mutation";
  EXPECT_EQ(curve.control_selection_revision(), baseline.revision);
  EXPECT_FALSE(curve.capture_sub_selection_baseline().hover.has_value());

  const ModelingCommandResult hover_restored =
      curve.restore_sub_selection_baseline(baseline);
  ASSERT_TRUE(hover_restored.ok) << hover_restored.message;
  EXPECT_TRUE(hover_restored.changed);
  EXPECT_EQ(curve.capture_sub_selection_baseline(), baseline);
  EXPECT_EQ(curve.preview_state().revision, published_revision);

  const std::array second_hit{second};
  const ModelingCommandResult edited = curve.edit_sub_elements(
      second_hit, EditorSubSelectionEdit::Replace, &baseline, second);
  ASSERT_TRUE(edited.ok) << edited.message;
  ASSERT_TRUE(edited.changed);
  EXPECT_NE(curve.preview_state().revision, published_revision);
  ASSERT_TRUE(curve.selected_controls_world_pivot().has_value());
  const ModelingCommandResult restored =
      curve.restore_sub_selection_baseline(baseline);
  ASSERT_TRUE(restored.ok) << restored.message;
  EXPECT_TRUE(restored.changed);
  EXPECT_EQ(curve.capture_sub_selection_baseline(), baseline);
  EXPECT_EQ(curve.preview_state().revision, published_revision);
  const std::optional<QVec3> restored_pivot =
      curve.selected_controls_world_pivot();
  ASSERT_TRUE(restored_pivot.has_value());
  EXPECT_NEAR(restored_pivot->x, baseline_pivot->x, 0.0001f);
  EXPECT_NEAR(restored_pivot->y, baseline_pivot->y, 0.0001f);
  EXPECT_NEAR(restored_pivot->z, baseline_pivot->z, 0.0001f);

  EditorSubSelectionTarget stale = first;
  ++stale.generation;
  const EditorSubSelectionBaseline before_stale =
      curve.capture_sub_selection_baseline();
  const ModelingCommandResult stale_hover =
      curve.preview_sub_element_hover(stale);
  EXPECT_FALSE(stale_hover.ok);
  EXPECT_FALSE(stale_hover.changed);
  EXPECT_EQ(curve.capture_sub_selection_baseline(), before_stale);
  const std::array stale_hit{stale};
  const ModelingCommandResult stale_edit = curve.edit_sub_elements(
      stale_hit, EditorSubSelectionEdit::Replace, &before_stale, stale);
  EXPECT_FALSE(stale_edit.ok);
  EXPECT_FALSE(stale_edit.changed);
  EXPECT_EQ(curve.capture_sub_selection_baseline(), before_stale);

  ASSERT_TRUE(curve.begin_selected_control_drag().ok);
  const ModelingCommandResult dragging_edit = curve.edit_sub_elements(
      first_hit, EditorSubSelectionEdit::Replace, nullptr, first);
  EXPECT_FALSE(dragging_edit.ok);
  EXPECT_FALSE(dragging_edit.changed);
  ASSERT_TRUE(curve.cancel_drag().ok);
  EXPECT_FALSE(harness.session.history().can_undo());
  EXPECT_FALSE(harness.session.dirty());

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(curve.control_selection().empty());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  const ModelingCommandResult detached_edit = curve.edit_sub_elements(
      first_hit, EditorSubSelectionEdit::Replace, nullptr, first);
  EXPECT_FALSE(detached_edit.ok);
  EXPECT_FALSE(detached_edit.changed);
}

TEST(CurveToolServiceTests,
     GroupUndoRedoRestoresExactCurveControlSelectionBaseline) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  ASSERT_TRUE(curve.preview_state().document.has_value());
  const modeling::CurvePointId point =
      curve.preview_state().document->points().front().id;
  const EditorSubSelectionTarget anchor{
      .index = point.index,
      .generation = point.generation,
      .role = EditorSubSelectionElementRole::Primary,
  };
  const EditorSubSelectionTarget outgoing{
      .index = point.index,
      .generation = point.generation,
      .role = EditorSubSelectionElementRole::SecondaryB,
  };
  const std::array controls{anchor, outgoing};
  ASSERT_TRUE(curve
                  .edit_sub_elements(controls, EditorSubSelectionEdit::Replace,
                                     nullptr, outgoing)
                  .ok);
  ASSERT_TRUE(curve.preview_sub_element_hover(anchor).ok);
  const EditorSubSelectionBaseline before =
      curve.capture_sub_selection_baseline();
  ASSERT_TRUE(before.valid());

  const auto grouped = harness.editor.scene().group_selection("Curve Group");
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  EXPECT_FALSE(curve.capture_sub_selection_baseline().valid());

  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(curve.capture_sub_selection_baseline(), before);

  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_FALSE(curve.capture_sub_selection_baseline().valid());
}

TEST(CurveToolServiceTests,
     ResolvedSelectionProviderFailureRollsBackBeforeScenePublication) {
  CurveToolHarness harness;
  const auto first = harness.editor.modeling().create_box(
      {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}});
  const auto second = harness.editor.modeling().create_box(
      {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(first.value()).ok());

  FailingSubSelectionProvider provider(first.value().entity,
                                       first.value().modeling_object_id);
  harness.session.selection().set_sub_selection_provider(&provider);
  harness.session.history().clear();
  const EditorSubSelectionBaseline provider_before = provider.state;
  const ModelingSelectionSummary selection_before =
      harness.session.selection().summary();
  const std::vector<EntityId> scene_selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const std::uint64_t scene_revision_before =
      harness.session.scene().revision();
  const std::uint64_t scene_selection_revision_before =
      harness.session.scene().selection_revision();
  const std::uint64_t modeling_selection_revision_before =
      harness.session.modeling().selection_revision();
  const auto history_before = harness.editor.history().snapshot();

  const EditorSubSelectionTarget sub_target = provider_before.selected.front();
  const EditorSelectionBoxBaseline baseline =
      harness.session.selection().capture_box_selection_baseline();
  const ResolvedSelectionApplyResult cross_domain =
      harness.session.selection().apply_resolved_marquee({
          .targets = {
              ResolvedSelectionTarget{
                  .domain = ResolvedSelectionDomain::SceneEntity,
                  .entity = second.value().entity,
              },
              ResolvedSelectionTarget{
                  .domain = ResolvedSelectionDomain::ModelingComponent,
                  .entity = first.value().entity,
                  .modeling_object_id = first.value().modeling_object_id,
                  .component_kind = ModelingActiveSelectionKind::Vertex,
                  .vertex = 0,
              },
              ResolvedSelectionTarget{
                  .domain = ResolvedSelectionDomain::RegisteredSubElement,
                  .entity = first.value().entity,
                  .modeling_object_id = first.value().modeling_object_id,
                  .sub_selection_kind =
                      EditorSubSelectionKind::CurveControl,
                  .sub_element = sub_target,
              },
          },
          .edit = ModelingSelectionEdit::Replace,
          .mode = ModelingSelectionMode::Object,
          .baseline = baseline,
      });
  EXPECT_FALSE(cross_domain.command.ok);
  EXPECT_FALSE(cross_domain.command.changed);
  EXPECT_EQ(provider.failed_edit_count, 0);
  EXPECT_EQ(provider.restore_count, 0);
  EXPECT_EQ(provider.state, provider_before);

  const ResolvedSelectionApplyResult click =
      harness.session.selection().apply_resolved_click({
          .winner = ResolvedSelectionTarget{
              .domain = ResolvedSelectionDomain::RegisteredSubElement,
              .entity = first.value().entity,
              .modeling_object_id = first.value().modeling_object_id,
              .sub_selection_kind = EditorSubSelectionKind::CurveControl,
              .sub_element = sub_target,
          },
          .policy = {},
          .baseline = baseline,
      });
  EXPECT_FALSE(click.command.ok);
  EXPECT_FALSE(click.command.changed);
  EXPECT_EQ(provider.state, provider_before);
  EXPECT_EQ(provider.failed_edit_count, 1);
  EXPECT_EQ(provider.restore_count, 1);
  EXPECT_EQ(click.selection.selected_sub_element_count,
            selection_before.selected_sub_element_count);
  EXPECT_EQ(click.selection.sub_selection_revision,
            selection_before.sub_selection_revision);
  EXPECT_EQ(click.selection.selection_revision,
            selection_before.selection_revision);

  const ResolvedSelectionApplyResult marquee =
      harness.session.selection().apply_resolved_marquee({
          .targets = {
              ResolvedSelectionTarget{
                  .domain = ResolvedSelectionDomain::SceneEntity,
                  .entity = second.value().entity,
                  .modeling_object_id = second.value().modeling_object_id,
              },
              ResolvedSelectionTarget{
                  .domain = ResolvedSelectionDomain::RegisteredSubElement,
                  .entity = first.value().entity,
                  .modeling_object_id = first.value().modeling_object_id,
                  .sub_selection_kind =
                      EditorSubSelectionKind::CurveControl,
                  .sub_element = sub_target,
              },
          },
          .edit = ModelingSelectionEdit::Replace,
          .mode = ModelingSelectionMode::Object,
          .baseline = baseline,
      });
  EXPECT_FALSE(marquee.command.ok);
  EXPECT_FALSE(marquee.command.changed);
  EXPECT_EQ(provider.state, provider_before);
  EXPECT_EQ(provider.failed_edit_count, 2);
  EXPECT_EQ(provider.restore_count, 2);
  EXPECT_EQ(marquee.selection.selected_sub_element_count,
            selection_before.selected_sub_element_count);
  EXPECT_EQ(marquee.selection.sub_selection_revision,
            selection_before.sub_selection_revision);
  EXPECT_EQ(marquee.selection.selection_revision,
            selection_before.selection_revision);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            scene_selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
  EXPECT_EQ(harness.session.scene().selection_revision(),
            scene_selection_revision_before);
  EXPECT_EQ(harness.session.modeling().selection_revision(),
            modeling_selection_revision_before);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  provider.fail_restore = true;
  const ResolvedSelectionApplyResult failed_compensation =
      harness.session.selection().apply_resolved_click({
          .winner = ResolvedSelectionTarget{
              .domain = ResolvedSelectionDomain::RegisteredSubElement,
              .entity = first.value().entity,
              .modeling_object_id = first.value().modeling_object_id,
              .sub_selection_kind = EditorSubSelectionKind::CurveControl,
              .sub_element = sub_target,
          },
          .policy = {},
      });
  EXPECT_FALSE(failed_compensation.command.ok);
  EXPECT_TRUE(failed_compensation.command.changed);
  EXPECT_NE(failed_compensation.command.message.find("rollback failed"),
            std::string::npos);
  harness.session.selection().set_sub_selection_provider(nullptr);
}

TEST(CurveToolServiceTests,
     SceneSelectionProjectionFailureRestoresSceneModelingAndHistoryExactly) {
  CurveToolHarness harness;
  const auto first = harness.editor.modeling().create_box(
      {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}});
  const auto second = harness.editor.modeling().create_box(
      {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(first.value()).ok());
  harness.session.history().clear();

  const std::vector<EntityId> scene_selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const std::uint64_t scene_revision_before =
      harness.session.scene().revision();
  const std::uint64_t scene_selection_revision_before =
      harness.session.scene().selection_revision();
  const ModelingSelectionSummary modeling_before =
      harness.session.modeling().selection_summary();
  const auto history_before = harness.editor.history().snapshot();

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const QuaderApiVoidResult failed =
      harness.editor.selection().select_object(second.value());
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_NE(failed.error_message().find("Injected failure"),
            std::string::npos);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            scene_selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
  EXPECT_EQ(harness.session.scene().selection_revision(),
            scene_selection_revision_before);
  const ModelingSelectionSummary modeling_after =
      harness.session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const QuaderApiVoidResult selected =
      harness.editor.selection().select_object(second.value());
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected.changed());
}

TEST(CurveToolServiceTests,
     PickSceneEntityPropagatesProjectionFailureAndCompensatesExactly) {
  CurveToolHarness harness;
  const auto first = harness.editor.modeling().create_box(
      {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}});
  const auto second = harness.editor.modeling().create_box(
      {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}});
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(first.value()).ok());
  harness.session.history().clear();

  const std::vector<EntityId> scene_selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const std::uint64_t scene_revision_before =
      harness.session.scene().revision();
  const std::uint64_t scene_selection_revision_before =
      harness.session.scene().selection_revision();
  const ModelingSelectionSummary modeling_before =
      harness.session.modeling().selection_summary();
  const auto history_before = harness.editor.history().snapshot();

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const QuaderApiResult<QuaderSelectionSummary> failed =
      harness.editor.selection().pick_scene_entity(second.value().entity, {});
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_NE(failed.error_message().find("Injected failure"),
            std::string::npos);
  EXPECT_EQ(harness.session.scene().selected_entities(),
            scene_selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(harness.session.scene().revision(), scene_revision_before);
  EXPECT_EQ(harness.session.scene().selection_revision(),
            scene_selection_revision_before);
  const ModelingSelectionSummary modeling_after =
      harness.session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const auto selected =
      harness.editor.selection().pick_scene_entity(second.value().entity, {});
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  EXPECT_TRUE(selected.changed());
  EXPECT_EQ(harness.session.scene().selected_entities(),
            std::vector<EntityId>{second.value().entity});
}

TEST(CurveToolServiceTests,
     CheckedPublicationRejectsInexactOrUnreportedSelectionInverse) {
  const auto run = [](bool exact_selection, bool reports_changed) {
    SceneDocument scene;
    const EntityId before = scene.create_entity("Before");
    const EntityId after = scene.create_entity("After");
    const EntityId wrong = scene.create_entity("Wrong");
    EXPECT_NE(before, kInvalidEntityId);
    EXPECT_NE(after, kInvalidEntityId);
    EXPECT_NE(wrong, kInvalidEntityId);
    EXPECT_TRUE(scene.replace_selection({&before, 1u}, before));
    const std::uint64_t revision_before = scene.revision();

    std::unique_ptr<IUndoableCommand> command =
        with_checked_selection_publication(
            scene,
            std::make_unique<IncorrectSelectionRollbackCommand>(
                scene, after, exact_selection ? before : wrong,
                reports_changed),
            [] { return command_invalid_result("Injected publication failure"); });
    const CommandResult result = command->redo();
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(result.changed);
    EXPECT_NE(result.message.find("did not restore the exact scene selection"),
              std::string::npos);
    EXPECT_EQ(scene.selected_entities(),
              std::vector<EntityId>{exact_selection ? before : wrong});
    EXPECT_EQ(scene.primary_selection(), exact_selection ? before : wrong);
    EXPECT_GT(scene.revision(), revision_before)
        << "An invalid inverse must not authorize scalar rewinding.";
  };

  run(false, true);
  run(true, false);
}

TEST(CurveToolServiceTests,
     CheckedPublicationRejectsSceneMutationInsidePublisherBeforeInverse) {
  SceneDocument scene;
  const EntityId before = scene.create_entity("Before");
  const EntityId after = scene.create_entity("After");
  const EntityId concurrent = scene.create_entity("Concurrent");
  ASSERT_TRUE(scene.replace_selection({&before, 1u}, before));
  const std::uint64_t revision_before = scene.revision();
  bool inverse_called = false;

  class ObservableInverseCommand final : public IUndoableCommand {
  public:
    ObservableInverseCommand(SceneDocument &scene, EntityId before,
                             EntityId after, bool &inverse_called)
        : scene_(scene), before_(before), after_(after),
          inverse_called_(inverse_called) {}

    [[nodiscard]] const std::string &label() const override { return label_; }
    CommandResult redo() override {
      (void)scene_.replace_selection({&after_, 1u}, after_);
      return command_changed_result();
    }
    CommandResult undo() override {
      inverse_called_ = true;
      (void)scene_.replace_selection({&before_, 1u}, before_);
      return command_changed_result();
    }

  private:
    SceneDocument &scene_;
    EntityId before_ = kInvalidEntityId;
    EntityId after_ = kInvalidEntityId;
    bool &inverse_called_;
    std::string label_ = "Observable Selection Inverse";
  };

  std::unique_ptr<IUndoableCommand> command =
      with_checked_selection_publication(
          scene,
          std::make_unique<ObservableInverseCommand>(scene, before, after,
                                                     inverse_called),
          [&scene, concurrent] {
            (void)scene.replace_selection({&concurrent, 1u}, concurrent);
            return command_invalid_result("Injected publication failure");
          });
  const CommandResult result = command->redo();
  EXPECT_FALSE(result.ok);
  EXPECT_TRUE(result.changed);
  EXPECT_FALSE(inverse_called);
  EXPECT_NE(result.message.find("Scene changed during selection publication"),
            std::string::npos);
  EXPECT_EQ(scene.selected_entities(), std::vector<EntityId>{concurrent});
  EXPECT_EQ(scene.primary_selection(), concurrent);
  EXPECT_GT(scene.revision(), revision_before);
}

TEST(CurveToolServiceTests,
     SelectCreatedLightPublicationFailureRestoresSceneScalarsAllocationAndHistory) {
  CurveToolHarness harness;
  CurveToolHarness control;
  const auto prepare = [](CurveToolHarness &target) {
    const auto box = target.editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
    EXPECT_TRUE(box.ok()) << box.error_message();
    if (!box.ok()) {
      return QuaderModelingObjectRef{};
    }
    EXPECT_TRUE(target.editor.selection().clear().ok());
    EXPECT_TRUE(target.editor.selection().select_object(box.value()).ok());
    target.session.history().clear();
    return box.value();
  };
  const QuaderModelingObjectRef selected = prepare(harness);
  const QuaderModelingObjectRef control_selected = prepare(control);
  ASSERT_NE(selected.entity, kInvalidEntityId);
  ASSERT_NE(control_selected.entity, kInvalidEntityId);

  const std::vector<EntityId> entities_before = harness.session.scene().entities();
  const std::vector<EntityId> selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const auto scalars_before = scene_scalar_fingerprint(harness.session.scene());
  const ModelingSelectionSummary modeling_before =
      harness.session.modeling().selection_summary();
  const auto history_before = harness.editor.history().snapshot();

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  QuaderCreatePointLightArgs args;
  args.name = "Failed Selected Light";
  args.select_created = true;
  const QuaderApiResult<QuaderEntityRef> failed =
      harness.editor.scene().create_point_light(args);
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_NE(failed.error_message().find("Injected failure"),
            std::string::npos);
  EXPECT_EQ(harness.session.scene().entities(), entities_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(scene_scalar_fingerprint(harness.session.scene()), scalars_before);
  const ModelingSelectionSummary modeling_after =
      harness.session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  args.name = "Selected Light";
  const QuaderApiResult<QuaderEntityRef> created_after_failure =
      harness.editor.scene().create_point_light(args);
  const QuaderApiResult<QuaderEntityRef> control_created =
      control.editor.scene().create_point_light(args);
  ASSERT_TRUE(created_after_failure.ok())
      << created_after_failure.error_message();
  ASSERT_TRUE(control_created.ok()) << control_created.error_message();
  EXPECT_EQ(created_after_failure.value().id, control_created.value().id)
      << "Failed publication must restore the entity allocator high-water.";
  EXPECT_EQ(created_after_failure.value().editor_object_id,
            control_created.value().editor_object_id)
      << "Failed publication must restore the editor-object allocator "
         "high-water.";
}

TEST(CurveToolServiceTests,
     BatchLockSelectionPublicationFailureRestoresOuterSceneScalarsAndHistory) {
  CurveToolHarness harness;
  CurveToolHarness control;
  const auto prepare = [](CurveToolHarness &target) {
    const auto box = target.editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
    EXPECT_TRUE(box.ok()) << box.error_message();
    if (!box.ok()) {
      return QuaderModelingObjectRef{};
    }
    EXPECT_TRUE(target.editor.selection().clear().ok());
    EXPECT_TRUE(target.editor.selection().select_object(box.value()).ok());
    target.session.history().clear();
    return box.value();
  };
  const QuaderModelingObjectRef selected = prepare(harness);
  const QuaderModelingObjectRef control_selected = prepare(control);
  ASSERT_NE(selected.entity, kInvalidEntityId);
  ASSERT_NE(control_selected.entity, kInvalidEntityId);

  const std::vector<EntityId> entities_before = harness.session.scene().entities();
  const std::vector<EntityId> selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const SceneScalarFingerprint scalars_before =
      scene_scalar_fingerprint(harness.session.scene());
  const ModelingSelectionSummary modeling_before =
      harness.session.modeling().selection_summary();
  const auto history_before = harness.editor.history().snapshot();

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const DocumentCommandResult failed =
      harness.session.document_commands().apply_scene_batch(
          "Lock Selected In Batch",
          {DocumentSceneBatchOperation{
              .kind = DocumentSceneBatchOperationKind::SetLocked,
              .entity = selected.entity,
              .flag = true,
          }});
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_NE(failed.message.find("Injected failure"), std::string::npos);
  EXPECT_EQ(harness.session.scene().entities(), entities_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(scene_scalar_fingerprint(harness.session.scene()), scalars_before);
  EXPECT_FALSE(harness.session.scene().is_effectively_locked(selected.entity));
  const ModelingSelectionSummary modeling_after =
      harness.session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  const DocumentCreateEntityResult created_after_failure =
      harness.session.document_commands().create_point_light(
          "Allocator Probe", LightComponent{});
  const DocumentCreateEntityResult control_created =
      control.session.document_commands().create_point_light(
          "Allocator Probe", LightComponent{});
  ASSERT_TRUE(created_after_failure.ok) << created_after_failure.message;
  ASSERT_TRUE(control_created.ok) << control_created.message;
  EXPECT_EQ(created_after_failure.entity, control_created.entity);
  EXPECT_EQ(harness.session.scene().editor_object_id(
                created_after_failure.entity),
            control.session.scene().editor_object_id(control_created.entity));
}

TEST(CurveToolServiceTests,
     RepeatStructuralSelectionPublicationFailureRestoresOuterSceneAndHistory) {
  CurveToolHarness harness;
  CurveToolHarness control;
  const auto prepare_repeat = [](CurveToolHarness &target) {
    const auto box = target.editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}});
    EXPECT_TRUE(box.ok()) << box.error_message();
    if (!box.ok()) {
      return false;
    }
    EXPECT_TRUE(target.editor.selection().clear().ok());
    EXPECT_TRUE(target.editor.selection().select_object(box.value()).ok());
    const QuaderApiResult<QuaderEntityRef> group =
        target.editor.scene().group_selection("Repeat Group");
    EXPECT_TRUE(group.ok()) << group.error_message();
    if (!group.ok()) {
      return false;
    }
    EXPECT_TRUE(target.editor.selection()
                    .set_mode(QuaderSelectionMode::Group)
                    .ok());
    EXPECT_TRUE(target.editor.selection().select_object(group.value().id).ok());
    EXPECT_TRUE(target.editor.modeling().begin_transform_preview(true).ok());
    EXPECT_TRUE(target.editor.modeling()
                    .update_transform_translate({2.0f, 0.0f, 0.0f})
                    .ok());
    EXPECT_TRUE(target.editor.modeling().commit_transform_preview().ok());
    target.session.history().clear();
    return true;
  };
  ASSERT_TRUE(prepare_repeat(harness));
  ASSERT_TRUE(prepare_repeat(control));

  const std::vector<EntityId> entities_before = harness.session.scene().entities();
  const std::vector<EntityId> selection_before =
      harness.session.scene().selected_entities();
  const EntityId active_before = harness.session.scene().primary_selection();
  const SceneScalarFingerprint scalars_before =
      scene_scalar_fingerprint(harness.session.scene());
  const ModelingSelectionSummary modeling_before =
      harness.session.modeling().selection_summary();
  const auto history_before = harness.editor.history().snapshot();

  std::size_t publication_calls = 0u;
  harness.session.document_commands().set_selection_publication(
      [&harness, &publication_calls, expected_entity_count =
                                         entities_before.size() + 2u]() {
        ++publication_calls;
        EXPECT_EQ(harness.session.scene().entities().size(),
                  expected_entity_count);
        harness.session.modeling()
            .set_modeling_sdk_projection_failure_for_tests(
                ModelingSdkProjectionFailurePoint::AfterSdkMutation);
        CommandResult result =
            harness.session.selection().publish_scene_selection();
        harness.session.modeling()
            .set_modeling_sdk_projection_failure_for_tests(
                ModelingSdkProjectionFailurePoint::None);
        return result;
      });
  const QuaderApiVoidResult failed =
      harness.editor.history().repeat_last_action();
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(publication_calls, 1u) << failed.error_message();
  EXPECT_NE(failed.error_message().find("after SDK selection mutation"),
            std::string::npos);
  EXPECT_EQ(failed.error_message().find("during SDK duplicate batch"),
            std::string::npos);
  EXPECT_EQ(harness.session.scene().entities(), entities_before);
  EXPECT_EQ(harness.session.scene().selected_entities(), selection_before);
  EXPECT_EQ(harness.session.scene().primary_selection(), active_before);
  EXPECT_EQ(scene_scalar_fingerprint(harness.session.scene()), scalars_before);
  const ModelingSelectionSummary modeling_after =
      harness.session.modeling().selection_summary();
  EXPECT_EQ(modeling_after.mode, modeling_before.mode);
  EXPECT_EQ(modeling_after.active_object_id, modeling_before.active_object_id);
  EXPECT_EQ(modeling_after.selected_object_count,
            modeling_before.selected_object_count);
  EXPECT_EQ(modeling_after.selection_revision,
            modeling_before.selection_revision);
  EXPECT_EQ(modeling_after.content_revision, modeling_before.content_revision);
  const auto history_after = harness.editor.history().snapshot();
  EXPECT_EQ(history_after.can_undo, history_before.can_undo);
  EXPECT_EQ(history_after.can_redo, history_before.can_redo);
  EXPECT_EQ(history_after.undo_label, history_before.undo_label);
  EXPECT_EQ(history_after.redo_label, history_before.redo_label);

  harness.session.document_commands().set_selection_publication([&harness] {
    return harness.session.selection().publish_scene_selection();
  });
  const DocumentCreateEntityResult created_after_failure =
      harness.session.document_commands().create_point_light(
          "Allocator Probe", LightComponent{});
  const DocumentCreateEntityResult control_created =
      control.session.document_commands().create_point_light(
          "Allocator Probe", LightComponent{});
  ASSERT_TRUE(created_after_failure.ok) << created_after_failure.message;
  ASSERT_TRUE(control_created.ok) << control_created.message;
  EXPECT_EQ(created_after_failure.entity, control_created.entity);
  EXPECT_EQ(harness.session.scene().editor_object_id(
                created_after_failure.entity),
            control.session.scene().editor_object_id(control_created.entity));
}

TEST(CurveToolServiceTests,
     CanonicalDeleteMutatesSelectedCurveControlsThroughCurveHistoryOwner) {
  CurveToolHarness harness;
  const auto created = harness.editor.modeling().create_curve({
      .points = {{.position = {-2.0f, 0.0f, 0.0f},
                  .outgoing_handle = {1.0f, 0.0f, 1.0f}},
                 {.position = {0.0f, 0.0f, 2.0f}},
                 {.position = {2.0f, 0.0f, 0.0f},
                  .incoming_handle = {-1.0f, 0.0f, 1.0f}}},
  });
  ASSERT_TRUE(created.ok()) << created.error_message();
  const QuaderModelingObjectRef object = created.value();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  const modeling::CurveDocument original = *curve.preview_state().document;
  const CurveControlTarget outgoing{original.points()[0].id,
                                    CurveControlKind::OutgoingHandle};
  ASSERT_TRUE(replace_curve_control_selection(curve, outgoing));
  ASSERT_TRUE(harness.editor.scene()
                  .set_transform(object.entity,
                                 {.position = {3.0f, 0.0f, 0.0f}})
                  .ok());
  EXPECT_FALSE(harness.session.selection().has_sub_selection())
      << "cheap summary polling must not refresh a stale provider scope";

  const QuaderApiVoidResult deleted_handle = harness.editor.commands().execute(
      quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted_handle.ok()) << deleted_handle.error_message();
  EXPECT_TRUE(deleted_handle.changed());
  ASSERT_TRUE(curve.refresh().ok);
  expect_point(curve.preview_state().document->points()[0].outgoing_handle,
               {});
  EXPECT_TRUE(curve.control_selection().empty());
  EXPECT_TRUE(harness.editor.query().entity(object.entity)->selected);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(original));

  const CurveControlTarget anchor{original.points()[0].id,
                                  CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
  const QuaderApiVoidResult deleted_anchor = harness.editor.commands().execute(
      quader_command::kDeleteSelection);
  ASSERT_TRUE(deleted_anchor.ok()) << deleted_anchor.error_message();
  EXPECT_TRUE(deleted_anchor.changed());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_EQ(curve.preview_state().document->point_count(), 2u);
  EXPECT_TRUE(curve.control_selection().empty());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  ASSERT_TRUE(curve.refresh().ok);
  EXPECT_TRUE(curve.preview_state().document->has_same_geometry(original));
}

TEST(CurveToolServiceTests,
     CurveSubSelectionDisappearsOnOwnerDeselectionAndSessionRebind) {
  CurveToolHarness harness;
  const QuaderModelingObjectRef object = harness.make_curve();
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  CurveToolService &curve = harness.session.curve();
  ASSERT_TRUE(curve.refresh().ok);
  const CurveControlTarget anchor{
      curve.preview_state().document->points()[0].id,
      CurveControlKind::Anchor};
  ASSERT_TRUE(replace_curve_control_selection(curve, anchor));
  ASSERT_EQ(harness.editor.selection().summary().selected_sub_element_count,
            1u);

  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  const QuaderSelectionSummary deselected =
      harness.editor.selection().summary();
  EXPECT_EQ(deselected.sub_selection_kind, QuaderSubSelectionKind::None);
  EXPECT_EQ(deselected.selected_sub_element_count, 0u);
  EXPECT_EQ(deselected.sub_selection_revision, 0u);
  EXPECT_EQ(deselected.selection_revision,
            harness.session.modeling().selection_revision());
  EXPECT_TRUE(curve.control_selection().empty());

  harness.session.replace_document(SceneContentDocument{});
  const QuaderSelectionSummary rebound = harness.editor.selection().summary();
  EXPECT_EQ(rebound.sub_selection_kind, QuaderSubSelectionKind::None);
  EXPECT_EQ(rebound.selected_sub_element_count, 0u);
  EXPECT_EQ(rebound.sub_selection_revision, 0u);
  EXPECT_EQ(rebound.selection_revision,
            harness.session.modeling().selection_revision());
  EXPECT_FALSE(harness.session.selection().has_sub_selection());
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
      harness.editor, harness.gizmo_frame(), harness.session.native_tools());
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
                      harness.gizmo_frame(true, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  harness.input.mouse_local = update;
  harness.input.mouse_screen = update;
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, true, false, start), harness.session.native_tools())
                  .gizmo_consumed);
  ASSERT_TRUE(curve.dragging());
  ASSERT_TRUE(harness.controller
                  .handle_before_active_tool(
                      harness.editor,
                      harness.gizmo_frame(false, false, true, start), harness.session.native_tools())
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
  constexpr QVec2 kCenter{400.0f, 300.0f};
  const ViewportRay center_ray = camera.ray_from_viewport_point(kCenter);
  const ViewportRay offset_ray =
      camera.ray_from_viewport_point(kCenter + QVec2{4.0f, 0.0f});
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
  auto hit = pick_viewport_curve_control(camera, preview, kCenter, true);
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
  hit = pick_viewport_curve_control(camera, preview, kCenter, true);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->target.point, preview.document->points()[0].id)
      << "ray depth breaks coincident screen-space ties";
  EXPECT_LT(hit->ray_distance, 6.0);
  EXPECT_TRUE(pick_viewport_curve_control(
                  camera, preview, kCenter, true, 9.0,
                  hit->ray_distance - 0.0005)
                  .has_value())
      << "control occlusion shares the mesh-component 0.001 tolerance";
  EXPECT_FALSE(pick_viewport_curve_control(
                   camera, preview, kCenter, true, 9.0,
                   hit->ray_distance - 0.002)
                   .has_value());

  hit = pick_viewport_curve_control(camera, preview, kCenter, false);
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
