////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_operation_input_context.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/interactions/transform_gizmo.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/placement/scene_surface_query.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/viewport/viewport_interaction_controller.hpp"
#include "editor/viewport/viewport_selection_interaction.hpp"
#include "viewport/viewport_camera_controller.hpp"

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <vector>

namespace {

using ElementKey = std::array<int, 3>;

struct PaintQuad final {
  quader::QuaderModelingObjectRef object;
  quader::QVec3 center;
  std::array<quader_poly::ElementId, 4> vertices;
  quader_poly::ElementId face;
  std::vector<quader_poly::ElementId> perimeter;
};

struct PaintModeCase final {
  quader::QuaderSelectionMode api_mode;
  quader::ViewportSelectionMode viewport_mode;
};

constexpr std::array kComponentModes{
    PaintModeCase{quader::QuaderSelectionMode::Vertex,
                  quader::ViewportSelectionMode::Vertex},
    PaintModeCase{quader::QuaderSelectionMode::Edge,
                  quader::ViewportSelectionMode::Edge},
    PaintModeCase{quader::QuaderSelectionMode::Face,
                  quader::ViewportSelectionMode::Face}};

/// Drives the production selection owner with press-scoped event samples.
class PaintSelectionHarness final {
public:
  quader::SceneSession session;
  quader::QuaderEditorApi editor{
      quader::QuaderApiSessionInternalAccess::from_scene_session(session)};
  quader::ViewportInputRouter router;
  quader::ViewportCameraController camera;
  quader::ViewportSettings settings;
  quader::ViewportInteractionController controller;
  quader::ViewportSelectionInteraction direct_selection;
  quader::ViewportInputFrame input;
  quader::ViewportInputResult result;
  quader::ViewportBeforeToolInteractionResult before;
  quader::QuaderEditorApi *bound_editor = &editor;
  quader::SceneSession *bound_session = &session;
  quader::QVec2 press_point{};
  std::uint64_t sequence = 0;
  std::uint64_t press_sequence = 0;
  bool direct = false;
  quader::QuaderOperationInputContext operation_context;

  PaintSelectionHarness() {
    camera.set_viewport_size(800.0f, 600.0f);
    camera.focus_origin(8.0f);
    camera.set_view_direction({0.0f, 0.0f, -1.0f});
    input.viewport_hovered = true;
    input.viewport_focused = true;
    input.viewport_size = {800.0f, 600.0f};
    activate();
  }

  void activate() {
    router.set_active_tool(quader::ViewportToolId::PaintSelect);
    EXPECT_TRUE(editor.tools().activate("paint_select").ok());
  }

  PaintQuad quad(quader::QVec3 center, bool ngon = false) {
    quader_poly::Document document;
    PaintQuad quad{.center = center};
    constexpr std::array<quader::QVec3, 4> kCorners{
        quader::QVec3{-0.6f, -0.6f, 0.0f}, quader::QVec3{0.6f, -0.6f, 0.0f},
        quader::QVec3{0.6f, 0.6f, 0.0f}, quader::QVec3{-0.6f, 0.6f, 0.0f}};
    for (std::size_t index = 0; index < quad.vertices.size(); ++index) {
      quad.vertices[index] =
          quader_poly::add_vertex(document, center + kCorners[index]);
    }
    quad.perimeter.assign(quad.vertices.begin(), quad.vertices.end());
    if (ngon) {
      const auto midpoint = quader_poly::add_vertex(
          document, center + quader::QVec3{0.0f, -0.6f, 0.0f});
      quad.perimeter.insert(quad.perimeter.begin() + 1, midpoint);
    }
    quad.face = quader_poly::add_face(document, quad.perimeter);
    const auto created = editor.modeling().create_mesh(
        quader::QuaderPolygonDocumentInternalAccess::from_backend(
            std::move(document)),
        {.name = "Paint selection quad"});
    EXPECT_TRUE(created.ok()) << created.error_message();
    quad.object = created.value();
    EXPECT_TRUE(editor.selection().clear().ok());
    return quad;
  }

  void
  enter_edit(const PaintModeCase &mode,
             std::initializer_list<quader::QuaderModelingObjectRef> objects) {
    ASSERT_TRUE(editor.selection().select_objects(objects).ok());
    ASSERT_TRUE(editor.mode().set(quader::QuaderEditorMode::Edit).ok());
    ASSERT_TRUE(editor.selection().set_mode(mode.api_mode).ok());
    router.set_selection_mode(mode.viewport_mode);
    activate();
  }

  quader::QVec2 point(const PaintQuad &quad,
                      quader::QuaderSelectionMode mode =
                          quader::QuaderSelectionMode::Object) const {
    auto world = quad.center;
    if (mode == quader::QuaderSelectionMode::Vertex) {
      world = world + quader::QVec3{0.6f, 0.6f, 0.0f};
    } else if (mode == quader::QuaderSelectionMode::Edge) {
      world = world + quader::QVec3{0.0f, 0.6f, 0.0f};
    }
    const auto projected = camera.project_world_point(world);
    EXPECT_TRUE(projected.visible);
    return projected.position;
  }

  static ElementKey element(const PaintQuad &quad,
                            quader::QuaderSelectionMode mode) {
    if (mode == quader::QuaderSelectionMode::Vertex) {
      return {quad.object.modeling_object_id,
              static_cast<int>(quad.vertices[2]), 0};
    }
    if (mode == quader::QuaderSelectionMode::Edge) {
      return {quad.object.modeling_object_id,
              static_cast<int>(quad.vertices[2]),
              static_cast<int>(quad.vertices[3])};
    }
    return {quad.object.modeling_object_id, static_cast<int>(quad.face), 0};
  }

  void dispatch(bool pressed = false, bool down = false,
                bool released = false) {
    input.active_transform_drag = controller.dragging();
    const auto surfaces = bound_session->scene_surface_query().snapshot();
    const quader::ViewportInteractionFrame frame{
        .input_router = router,
        .camera = camera,
        .settings = settings,
        .input = input,
        .input_result = result,
        .left_click_local = press_point,
        .left_pressed = pressed,
        .left_down = down,
        .left_released = released,
        .monotonic_time_seconds = 10.0,
        .active_tool_allows_selection_fallback =
            controller.active_tool_allows_selection_fallback(
                router.state().active_tool),
        .preferred_selection_tool =
            bound_editor->viewports().preferred_selection_tool(),
        .scene_surfaces = &surfaces,
        .selection_state =
            {
                .summary = bound_editor->selection().summary(),
                .scene_revision = bound_session->scene().revision(),
                .asset_revision = bound_session->assets().revision(),
            },
    };
    if (direct) {
      direct_selection.handle_frame(*bound_editor, frame, {}, {}, nullptr);
      direct_selection.populate_operation_input_context(operation_context,
                                                        frame);
    } else {
      before = controller.handle_before_active_tool(*bound_editor, frame);
      controller.handle_after_active_tool(*bound_editor, frame, {}, before);
      controller.populate_operation_input_context(operation_context, frame);
    }
    input.pointer_pressed_edges.clear();
    input.pointer_released_edges.clear();
    input.pointer_motion_samples.clear();
  }

  void press(quader::QVec2 point, quader::ViewportModifiers modifiers = {}) {
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
        .modifiers = modifiers,
    });
  }

  void move(quader::QVec2 point, quader::ViewportModifiers modifiers = {}) {
    input.mouse_local = point;
    input.modifiers = modifiers;
    input.pointer_motion_samples.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = ++sequence,
        .press_sequence = press_sequence,
        .modifiers = modifiers,
    });
  }

  void release(quader::QVec2 point, quader::ViewportModifiers modifiers = {}) {
    move(point, modifiers);
    input.pointer_released_edges.push_back({
        .button = quader::QuaderMouseButton::Left,
        .local = point,
        .sequence = sequence,
        .modifiers = modifiers,
    });
  }

  void move_above(quader::QVec2 from, quader::QVec2 to,
                  quader::ViewportModifiers modifiers = {}) {
    move({from.x, 100.0f}, modifiers);
    move({to.x, 100.0f}, modifiers);
    move(to, modifiers);
  }

  std::vector<quader::EntityId> selected_entities() const {
    std::vector<quader::EntityId> result;
    for (const auto &entity : editor.query().entities()) {
      if (entity.selected)
        result.push_back(entity.id);
    }
    std::ranges::sort(result);
    return result;
  }

  std::vector<ElementKey> selected_components() const {
    std::vector<ElementKey> selected;
    const auto overlay = editor.query().selection_overlay();
    const auto mode = editor.selection().summary().mode;
    if (mode == quader::QuaderSelectionMode::Vertex) {
      for (const auto &point : overlay.points) {
        if (point.layer == quader::QuaderOverlayLayer::SelectedVertex) {
          selected.push_back(
              {point.element.object_id, point.element.vertex_id, 0});
        }
      }
    } else if (mode == quader::QuaderSelectionMode::Edge) {
      for (const auto &line : overlay.lines) {
        if (line.layer == quader::QuaderOverlayLayer::SelectedEdge) {
          selected.push_back(
              {line.element.object_id,
               std::min(line.element.edge_a, line.element.edge_b),
               std::max(line.element.edge_a, line.element.edge_b)});
        }
      }
    } else {
      for (const auto &triangle : overlay.triangles) {
        if (triangle.layer == quader::QuaderOverlayLayer::SelectedVisibleFace ||
            triangle.layer == quader::QuaderOverlayLayer::SelectedXrayFace) {
          selected.push_back(
              {triangle.element.object_id, triangle.element.face_id, 0});
        }
      }
    }
    std::ranges::sort(selected);
    selected.erase(std::unique(selected.begin(), selected.end()),
                   selected.end());
    return selected;
  }
};

TEST(PaintSelectionInteractionTests,
     ReplaceClearsOnceAndSparseMotionAccumulatesMeshesAsOneUndoStep) {
  PaintSelectionHarness harness;
  const auto first = harness.quad({-3.0f, 0.0f, 0.0f});
  const auto middle = harness.quad({0.0f, 0.0f, 0.0f});
  const auto last = harness.quad({3.0f, 0.0f, 0.0f});
  const auto prior = harness.quad({0.0f, 3.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(prior.object).ok());
  harness.session.history().clear();
  const auto content_revision = harness.session.modeling().content_revision();
  harness.press(harness.point(first));
  harness.dispatch(true, true, false);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{first.object.entity});
  harness.move(harness.point(last));
  harness.dispatch(false, true, false);
  const std::vector<quader::EntityId> expected{
      first.object.entity, middle.object.entity, last.object.entity};
  EXPECT_EQ(harness.selected_entities(), expected);
  EXPECT_TRUE(harness.controller.dragging());
  EXPECT_FALSE(harness.controller.selection_marquee().active);
  harness.release(harness.point(last));
  harness.dispatch(false, false, true);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(harness.selected_entities(), expected);
  ASSERT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{prior.object.entity});
  ASSERT_TRUE(harness.editor.history().redo().ok());
  EXPECT_EQ(harness.selected_entities(), expected);
  EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
}

TEST(PaintSelectionInteractionTests,
     CoalescedGroupStrokeSelectsHierarchyRootsAndUndoRestoresTheBaseline) {
  PaintSelectionHarness harness;
  const auto left = harness.quad({-3.0f, 0.0f, 0.0f});
  const auto right = harness.quad({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection().select_object(left.object).ok());
  const auto left_group = harness.editor.scene().group_selection("Left group");
  ASSERT_TRUE(left_group.ok());
  ASSERT_TRUE(harness.editor.selection().select_object(right.object).ok());
  const auto right_group =
      harness.editor.scene().group_selection("Right group");
  ASSERT_TRUE(right_group.ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  ASSERT_TRUE(harness.editor.selection()
                  .set_mode(quader::QuaderSelectionMode::Group)
                  .ok());
  harness.router.set_selection_mode(quader::ViewportSelectionMode::Group);
  harness.session.history().clear();
  harness.press(harness.point(left));
  harness.release(harness.point(right));
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities(),
            (std::vector<quader::EntityId>{left_group.value().id,
                                           right_group.value().id}));
  ASSERT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  EXPECT_TRUE(harness.selected_entities().empty());
}

TEST(PaintSelectionInteractionTests,
     ComponentStrokeAccumulatesExactElementsWithinTheFixedEditParticipants) {
  for (const auto &mode : kComponentModes) {
    SCOPED_TRACE(static_cast<int>(mode.api_mode));
    PaintSelectionHarness harness;
    const auto left = harness.quad({-3.0f, 0.0f, 0.0f});
    const auto outsider = harness.quad({0.0f, 0.0f, 0.0f});
    const auto right = harness.quad({3.0f, 0.0f, 0.0f});
    harness.enter_edit(mode, {left.object, right.object});
    const auto generation = harness.editor.mode().state().generation;
    harness.session.history().clear();
    const auto content_revision = harness.session.modeling().content_revision();
    const auto left_point = harness.point(left, mode.api_mode);
    const auto outsider_point = harness.point(outsider, mode.api_mode);
    const auto right_point = harness.point(right, mode.api_mode);
    harness.press(left_point);
    harness.dispatch(true, true, false);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{harness.element(left, mode.api_mode)}));
    harness.move_above(left_point, outsider_point);
    harness.move_above(outsider_point, right_point);
    harness.release(right_point);
    harness.dispatch(false, false, true);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{harness.element(left, mode.api_mode),
                                       harness.element(right, mode.api_mode)}));
    EXPECT_EQ(harness.editor.mode().state().participants.size(), 2u);
    EXPECT_EQ(harness.editor.mode().state().generation, generation);
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
  }
}

TEST(PaintSelectionInteractionTests,
     CoalescedComponentSamplesUseLiveModifiersWithControlTakingPrecedence) {
  for (const auto &mode : kComponentModes) {
    SCOPED_TRACE(static_cast<int>(mode.api_mode));
    PaintSelectionHarness harness;
    const auto left = harness.quad({-2.0f, 0.0f, 0.0f});
    const auto right = harness.quad({2.0f, 0.0f, 0.0f});
    harness.enter_edit(mode, {left.object, right.object});
    const auto left_point = harness.point(left, mode.api_mode);
    const auto right_point = harness.point(right, mode.api_mode);
    harness.press(left_point);
    harness.move_above(left_point, right_point, {.shift = true});
    harness.move({right_point.x, 100.0f}, {.shift = true});
    harness.move({left_point.x, 100.0f}, {.primary = true, .shift = true});
    harness.release(left_point, {.primary = true, .shift = true});
    harness.input.modifiers = {};
    harness.dispatch(true, false, true);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{harness.element(right, mode.api_mode)}));
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(PaintSelectionInteractionTests,
     ReleaseBoundsTheStrokeBeforeUnrelatedAndLaterCoalescedMotion) {
  PaintSelectionHarness harness;
  const auto first = harness.quad({-3.0f, 0.0f, 0.0f});
  const auto unrelated = harness.quad({3.0f, 0.0f, 0.0f});
  harness.press(harness.point(first));
  harness.dispatch(true, true, false);
  harness.move(harness.point(unrelated));
  harness.input.pointer_motion_samples.back().press_sequence += 100u;
  harness.dispatch(false, true, false);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{first.object.entity});
  harness.release(harness.point(first));
  harness.move(harness.point(unrelated));
  harness.dispatch(false, false, true);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{first.object.entity});
  EXPECT_FALSE(harness.controller.dragging());
  harness.dispatch();
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{first.object.entity});
  harness.press(harness.point(unrelated));
  harness.release(harness.point(unrelated));
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{unrelated.object.entity});
}

TEST(PaintSelectionInteractionTests,
     InitialNoOpDoesNotPreventTheFirstChangedSampleStartingOneUndoStep) {
  for (const bool shift_on_selected : {false, true}) {
    SCOPED_TRACE(shift_on_selected);
    PaintSelectionHarness harness;
    const auto first = harness.quad({-2.0f, 0.0f, 0.0f});
    const auto last = harness.quad({2.0f, 0.0f, 0.0f});
    const auto prior = harness.quad({0.0f, 2.5f, 0.0f});
    if (shift_on_selected) {
      ASSERT_TRUE(harness.editor.selection().select_object(prior.object).ok());
    }
    harness.session.history().clear();
    const quader::ViewportModifiers modifiers{.shift = shift_on_selected};
    const auto start = shift_on_selected ? harness.point(prior)
                                         : quader::QVec2{100.0f, 300.0f};
    harness.press(start, modifiers);
    harness.dispatch(true, true, false);
    EXPECT_EQ(harness.session.history().undo_count(), 0u);
    harness.move(harness.point(first), modifiers);
    harness.dispatch(false, true, false);
    harness.release(harness.point(last), modifiers);
    harness.dispatch(false, false, true);
    std::vector<quader::EntityId> expected{first.object.entity,
                                           last.object.entity};
    if (shift_on_selected)
      expected.push_back(prior.object.entity);
    EXPECT_EQ(harness.selected_entities(), expected);
    ASSERT_EQ(harness.session.history().undo_count(), 1u);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    EXPECT_EQ(harness.selected_entities(),
              shift_on_selected
                  ? std::vector<quader::EntityId>{prior.object.entity}
                  : std::vector<quader::EntityId>{});
    ASSERT_TRUE(harness.editor.history().redo().ok());
    EXPECT_EQ(harness.selected_entities(), expected);
  }
}

TEST(PaintSelectionInteractionTests,
     StationarySamplesGatePickingAndModifierChangesApplyAtTheSamePoint) {
  PaintSelectionHarness harness;
  harness.direct = true;
  const auto quad = harness.quad({});
  const auto point = harness.point(quad);
  harness.press(point);
  harness.dispatch(true, true, false);
  ASSERT_TRUE(harness.direct_selection.paint_pointer_active());
  ASSERT_TRUE(harness.direct_selection.pointer_interaction_active());
  EXPECT_EQ(harness.operation_context.selection_gesture,
            quader::QuaderInputSelectionGesture::Paint);
  const auto picks = harness.direct_selection.work_stats().paint_pick_count;
  const auto revision = harness.editor.selection().summary().selection_revision;
  ASSERT_GT(picks, 0u);
  for (int frame = 0; frame < 12; ++frame) {
    if ((frame & 1) != 0)
      harness.move(point);
    harness.dispatch(false, true, false);
    EXPECT_EQ(harness.direct_selection.work_stats().paint_pick_count, picks);
    EXPECT_EQ(harness.editor.selection().summary().selection_revision,
              revision);
  }
  harness.move(point, {.primary = true});
  harness.dispatch(false, true, false);
  EXPECT_TRUE(harness.selected_entities().empty());
  EXPECT_EQ(harness.operation_context.selection_operation,
            quader::QuaderInputSelectionOperation::Remove);
  harness.move(point, {.shift = true});
  harness.dispatch(false, true, false);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{quad.object.entity});
  harness.release(point, {.shift = true});
  harness.dispatch(false, false, true);
  EXPECT_FALSE(harness.direct_selection.paint_pointer_active());
}

TEST(PaintSelectionInteractionTests,
     HiddenAndLockedMeshesAreSkippedAndTheVisibleFrontSurfaceWins) {
  PaintSelectionHarness harness;
  const auto hidden = harness.quad({-3.0f, 0.0f, 0.0f});
  const auto locked = harness.quad({0.0f, 0.0f, 0.0f});
  const auto front = harness.quad({3.0f, 0.0f, 1.0f});
  // Keep the rear quad on the same camera ray. Equal world X would expose
  // its left edge in perspective, legitimately adding it earlier in the stroke.
  const auto eye = harness.camera.pose().eye;
  const auto back = harness.quad(eye + (front.center - eye) * (9.0f / 7.0f));
  const auto left_edge = [&](const PaintQuad &quad) {
    return harness.camera
        .project_world_point(quad.center + quader::QVec3{-0.6f, 0.0f, 0.0f})
        .position.x;
  };
  const auto right_edge = [&](const PaintQuad &quad) {
    return harness.camera
        .project_world_point(quad.center + quader::QVec3{0.6f, 0.0f, 0.0f})
        .position.x;
  };
  ASSERT_GT(left_edge(back), left_edge(front));
  ASSERT_LT(right_edge(back), right_edge(front));
  ASSERT_TRUE(
      harness.editor.scene().set_visible(hidden.object.entity, false).ok());
  ASSERT_TRUE(
      harness.editor.scene().set_locked(locked.object.entity, true).ok());
  harness.press({100.0f, 300.0f});
  harness.release(harness.point(front));
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{front.object.entity});
  ASSERT_TRUE(
      harness.editor.scene().set_visible(front.object.entity, false).ok());
  ASSERT_TRUE(harness.editor.selection().clear().ok());
  harness.press(harness.point(back));
  harness.release(harness.point(back));
  harness.dispatch(true, false, true);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{back.object.entity});
}

TEST(PaintSelectionInteractionTests,
     InterruptionsKeepPublishedSamplesAndReserveTheEventualRelease) {
  enum class Interruption {
    Escape,
    CaptureLoss,
    Navigation,
    FocusLoss,
    ToolChange,
    ModeRoundTrip,
    ExternalSelection,
  };
  constexpr std::array kInterruptions{
      Interruption::Escape,           Interruption::CaptureLoss,
      Interruption::Navigation,       Interruption::FocusLoss,
      Interruption::ToolChange,       Interruption::ModeRoundTrip,
      Interruption::ExternalSelection};
  for (const auto interruption : kInterruptions) {
    SCOPED_TRACE(static_cast<int>(interruption));
    PaintSelectionHarness harness;
    const auto first = harness.quad({-3.0f, 0.0f, 0.0f});
    const auto later = harness.quad({3.0f, 0.0f, 0.0f});
    const auto external = harness.quad({0.0f, 2.5f, 0.0f});
    harness.press(harness.point(first));
    harness.dispatch(true, true, false);
    ASSERT_TRUE(harness.controller.dragging());
    switch (interruption) {
    case Interruption::Escape:
      ASSERT_TRUE(harness.controller.cancel_active_drag(harness.editor));
      break;
    case Interruption::CaptureLoss:
      harness.input.pointer_interaction_canceled = true;
      break;
    case Interruption::Navigation:
      harness.result.navigation_active = true;
      break;
    case Interruption::FocusLoss:
      harness.input.viewport_focused = false;
      break;
    case Interruption::ToolChange:
      ASSERT_TRUE(harness.editor.tools().activate("select").ok());
      harness.router.set_active_tool(quader::ViewportToolId::Select);
      break;
    case Interruption::ModeRoundTrip:
      ASSERT_TRUE(
          harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      ASSERT_TRUE(
          harness.editor.mode().set(quader::QuaderEditorMode::Object).ok());
      break;
    case Interruption::ExternalSelection:
      ASSERT_TRUE(
          harness.editor.selection().select_object(external.object).ok());
      break;
    }
    harness.move(harness.point(later));
    harness.dispatch(false, true, false);
    EXPECT_FALSE(harness.controller.dragging());
    harness.result.navigation_active = false;
    harness.input.pointer_interaction_canceled = false;
    harness.input.viewport_focused = true;
    harness.release(harness.point(later));
    harness.dispatch(false, false, true);
    EXPECT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{
                  interruption == Interruption::ExternalSelection
                      ? external.object.entity
                      : first.object.entity});
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(PaintSelectionInteractionTests,
     SceneReplacementCannotApplyHeldSamplesToAnotherDocumentsMatchingIds) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
#ifdef _WIN32
  constexpr const char *kProfileVariable = "LOCALAPPDATA";
#else
  constexpr const char *kProfileVariable = "XDG_CONFIG_HOME";
#endif
  struct RestoreProfile final {
    const char *variable;
    bool present;
    QByteArray value;
    ~RestoreProfile() {
      if (present)
        qputenv(variable, value);
      else
        qunsetenv(variable);
    }
  } restore_profile{kProfileVariable,
                    qEnvironmentVariableIsSet(kProfileVariable),
                    qgetenv(kProfileVariable)};
  ASSERT_TRUE(qputenv(kProfileVariable, directory.path().toUtf8()));
  const auto root =
      std::filesystem::path(directory.path().toStdWString()) / "Project";
  ASSERT_TRUE(std::filesystem::create_directory(root));
  quader::ProjectSession project;
  project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR) /
                            "resources/modeling_assets");
  quader::QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(root).ok());
  const auto first_scene = editor.scene_documents().active_scene_id();
  const quader::QuaderBounds bounds{.min = {-0.6f, -0.6f, -0.6f},
                                    .max = {0.6f, 0.6f, 0.6f}};
  const auto original = editor.modeling().create_box(bounds, "Original scene");
  ASSERT_TRUE(original.ok());
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.tools().activate("paint_select").ok());
  PaintSelectionHarness harness;
  harness.bound_editor = &editor;
  harness.bound_session = project.active_scene()->session.get();
  harness.press({400.0f, 300.0f});
  harness.dispatch(true, true, false);
  ASSERT_EQ(editor.selection().summary().selected_object_count, 1u);
  const auto second_scene = editor.scene_documents().new_scene();
  ASSERT_TRUE(second_scene.ok());
  ASSERT_NE(first_scene, editor.scene_documents().active_scene_id());
  const auto replacement =
      editor.modeling().create_box(bounds, "Replacement scene");
  ASSERT_TRUE(replacement.ok());
  ASSERT_EQ(original.value().modeling_object_id,
            replacement.value().modeling_object_id);
  ASSERT_TRUE(editor.selection().clear().ok());
  ASSERT_TRUE(editor.tools().activate("paint_select").ok());
  harness.bound_session = project.active_scene()->session.get();
  harness.move({400.0f, 300.0f}, {.shift = true});
  harness.dispatch(false, true, false);
  harness.release({400.0f, 300.0f}, {.shift = true});
  harness.dispatch(false, false, true);
  EXPECT_EQ(editor.selection().summary().selected_object_count, 0u);
  EXPECT_FALSE(harness.controller.dragging());
  ASSERT_TRUE(first_scene.has_value());
  ASSERT_TRUE(editor.scene_documents().activate(*first_scene).ok());
  EXPECT_EQ(editor.selection().summary().selected_object_count, 1u);
}

TEST(PaintSelectionInteractionTests,
     NavigationRejectsANewStrokeAndPaintDoesNotStartATransformGizmo) {
  for (const bool navigation : {false, true}) {
    SCOPED_TRACE(navigation);
    PaintSelectionHarness harness;
    const auto first = harness.quad({});
    const auto second = harness.quad({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(harness.editor.selection().select_object(first.object).ok());
    const auto content_revision = harness.session.modeling().content_revision();
    const auto original_transform =
        harness.editor.query().entity(first.object.entity)->transform;
    harness.result.navigation_active = navigation;
    harness.press(harness.point(first));
    harness.dispatch(true, true, false);
    EXPECT_FALSE(harness.before.gizmo_consumed);
    EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());
    harness.release(harness.point(second));
    harness.dispatch(false, false, true);
    EXPECT_EQ(harness.selected_entities(),
              (navigation ? std::vector<quader::EntityId>{first.object.entity}
                          : std::vector<quader::EntityId>{
                                first.object.entity, second.object.entity}));
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    const auto transform =
        harness.editor.query().entity(first.object.entity)->transform;
    EXPECT_FLOAT_EQ(transform.position.x, original_transform.position.x);
    EXPECT_FLOAT_EQ(transform.position.y, original_transform.position.y);
    EXPECT_FLOAT_EQ(transform.position.z, original_transform.position.z);
  }
}

TEST(PaintSelectionInteractionTests,
     InsidePressKeepsItsClippedStrokeWhenFinalPointerIsOutsideTheViewport) {
  for (const bool release_in_first_frame : {false, true}) {
    SCOPED_TRACE(release_in_first_frame);
    PaintSelectionHarness harness;
    const auto first = harness.quad({-2.0f, 0.0f, 0.0f});
    const auto last = harness.quad({2.0f, 0.0f, 0.0f});
    constexpr quader::QVec2 kOutside{1000.0f, 300.0f};
    harness.press(harness.point(first));
    if (release_in_first_frame)
      harness.release(kOutside);
    else
      harness.move(kOutside);
    harness.input.viewport_hovered = false;
    harness.dispatch(true, !release_in_first_frame, release_in_first_frame);
    const std::vector<quader::EntityId> expected{first.object.entity,
                                                 last.object.entity};
    EXPECT_EQ(harness.selected_entities(), expected);
    if (!release_in_first_frame) {
      EXPECT_TRUE(harness.controller.dragging());
      harness.release(kOutside);
      harness.dispatch(false, false, true);
    }
    EXPECT_EQ(harness.selected_entities(), expected);
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(PaintSelectionInteractionTests,
     RememberedPaintAccumulatesUnderTransformToolsWithoutTransformingMeshes) {
  constexpr std::array kTools{quader::ViewportToolId::Move,
                              quader::ViewportToolId::Rotate,
                              quader::ViewportToolId::Scale};
  for (const auto tool : kTools) {
    SCOPED_TRACE(static_cast<int>(tool));
    PaintSelectionHarness harness;
    const auto first = harness.quad({-3.0f, 0.0f, 0.0f});
    const auto middle = harness.quad({0.0f, 0.0f, 0.0f});
    const auto last = harness.quad({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(harness.editor.tools().activate(tool).ok());
    harness.router.set_active_tool(harness.editor.viewports().selected_tool());
    ASSERT_EQ(harness.editor.viewports().preferred_selection_tool(),
              quader::ViewportToolId::PaintSelect);
    const auto content_revision = harness.session.modeling().content_revision();
    const auto transforms = [&] {
      std::vector<std::array<float, 9>> result;
      for (const auto &quad : {first, middle, last}) {
        const auto transform =
            harness.editor.query().entity(quad.object.entity)->transform;
        result.push_back({transform.position.x, transform.position.y,
                          transform.position.z, transform.rotation_euler.x,
                          transform.rotation_euler.y,
                          transform.rotation_euler.z, transform.scale.x,
                          transform.scale.y, transform.scale.z});
      }
      return result;
    };
    const auto before = transforms();
    harness.session.history().clear();
    harness.press({100.0f, 300.0f});
    harness.dispatch(true, true, false);
    harness.move(harness.point(first));
    harness.dispatch(false, true, false);
    EXPECT_EQ(harness.selected_entities(),
              std::vector<quader::EntityId>{first.object.entity});
    EXPECT_EQ(harness.operation_context.selection_gesture,
              quader::QuaderInputSelectionGesture::Paint);
    EXPECT_FALSE(harness.before.gizmo_consumed);
    harness.release(harness.point(last));
    harness.dispatch(false, false, true);
    EXPECT_EQ(harness.selected_entities(),
              (std::vector<quader::EntityId>{first.object.entity,
                                             middle.object.entity,
                                             last.object.entity}));
    EXPECT_EQ(harness.router.state().active_tool, tool);
    EXPECT_EQ(harness.editor.viewports().selected_tool(), tool);
    EXPECT_EQ(harness.editor.tools().active_tool(),
              quader::viewport_tool_id(tool));
    EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());
    EXPECT_EQ(transforms(), before);
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_EQ(harness.session.history().undo_count(), 1u);
  }
}

TEST(PaintSelectionInteractionTests,
     RememberedPaintUnderMoveKeepsExactComponentsAndFixedEditParticipants) {
  for (const auto &mode : kComponentModes) {
    SCOPED_TRACE(static_cast<int>(mode.api_mode));
    PaintSelectionHarness harness;
    const auto left = harness.quad({-3.0f, 0.0f, 0.0f});
    const auto outsider = harness.quad({0.0f, 0.0f, 0.0f});
    const auto right = harness.quad({3.0f, 0.0f, 0.0f});
    harness.enter_edit(mode, {left.object, right.object});
    ASSERT_TRUE(
        harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(harness.editor.viewports().selected_tool());
    ASSERT_EQ(harness.editor.viewports().preferred_selection_tool(),
              quader::ViewportToolId::PaintSelect);
    const auto generation = harness.editor.mode().generation();
    const auto content_revision = harness.session.modeling().content_revision();
    const auto left_point = harness.point(left, mode.api_mode);
    const auto outsider_point = harness.point(outsider, mode.api_mode);
    const auto right_point = harness.point(right, mode.api_mode);
    harness.press({left_point.x, 100.0f});
    harness.move(left_point);
    harness.dispatch(true, true, false);
    harness.move_above(left_point, outsider_point);
    harness.move_above(outsider_point, right_point);
    harness.release(right_point);
    harness.dispatch(false, false, true);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{harness.element(left, mode.api_mode),
                                       harness.element(right, mode.api_mode)}));
    EXPECT_EQ(harness.editor.mode().state().participants.size(), 2u);
    EXPECT_EQ(harness.editor.mode().generation(), generation);
    EXPECT_EQ(harness.editor.tools().active_tool(), "move");
    EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
    EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());
  }
}

TEST(PaintSelectionInteractionTests,
     MoveGizmoStillOwnsItsPressWhenPaintIsTheRememberedSelectionTool) {
  PaintSelectionHarness harness;
  const auto quad = harness.quad({});
  ASSERT_TRUE(harness.editor.selection().select_object(quad.object).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(harness.editor.viewports().selected_tool());
  ASSERT_EQ(harness.editor.viewports().preferred_selection_tool(),
            quader::ViewportToolId::PaintSelect);
  harness.settings.set_bool_value(quader::ViewportBoolSettingId::GridSnap,
                                  false);
  harness.settings.set_bool_value(quader::ViewportBoolSettingId::VertexSnap,
                                  false);
  harness.dispatch();
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto axis =
      std::ranges::find_if(gizmo->axes, [](const auto &candidate) {
        return candidate.axis == quader_editor_core::TransformAxis::X;
      });
  ASSERT_NE(axis, gizmo->axes.end());
  const auto start = axis->screen_tip;
  const auto before =
      harness.editor.query().entity(quad.object.entity)->transform;
  const auto selection_revision =
      harness.editor.selection().summary().selection_revision;
  harness.session.history().clear();
  harness.press(start);
  harness.dispatch(true, true, false);
  EXPECT_TRUE(harness.before.gizmo_consumed);
  const auto end = start + quader::QVec2{70.0f, 0.0f};
  harness.move(end);
  harness.dispatch(false, true, false);
  EXPECT_TRUE(harness.before.gizmo_consumed);
  EXPECT_TRUE(harness.controller.transform_gizmo_pointer_active());
  EXPECT_NE(harness.operation_context.selection_gesture,
            quader::QuaderInputSelectionGesture::Paint);
  harness.release(end);
  harness.dispatch(false, false, true);
  const auto after =
      harness.editor.query().entity(quad.object.entity)->transform;
  EXPECT_GT(after.position.x, before.position.x);
  EXPECT_EQ(harness.editor.selection().summary().selection_revision,
            selection_revision);
  EXPECT_EQ(harness.selected_entities(),
            std::vector<quader::EntityId>{quad.object.entity});
  EXPECT_EQ(harness.editor.tools().active_tool(), "move");
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
}

TEST(PaintSelectionInteractionTests,
     DeferredMoveGizmoClickRemovesTheComponentUsingCapturedControl) {
  PaintSelectionHarness harness;
  const auto quad = harness.quad({});
  harness.enter_edit(kComponentModes.front(), {quad.object});
  ASSERT_TRUE(harness.editor.selection()
                  .select_vertex(quad.object, quad.vertices[2])
                  .ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(harness.editor.viewports().selected_tool());
  ASSERT_EQ(harness.editor.viewports().preferred_selection_tool(),
            quader::ViewportToolId::PaintSelect);
  harness.dispatch();
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  const auto start = harness.point(quad, quader::QuaderSelectionMode::Vertex);
  ASSERT_NEAR(gizmo->info.screen_pivot.x, start.x, 0.01f);
  ASSERT_NEAR(gizmo->info.screen_pivot.y, start.y, 0.01f);
  const auto content_revision = harness.session.modeling().content_revision();
  const auto asset_revision = harness.session.assets().revision();
  harness.session.history().clear();
  harness.press(start, {.primary = true});
  harness.dispatch(true, true, false);
  ASSERT_TRUE(harness.before.gizmo_consumed);
  ASSERT_FALSE(harness.before.selection_click_requested);
  EXPECT_TRUE(harness.controller.transform_gizmo_pointer_active());
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 1u);

  // Releasing Ctrl before a sub-threshold mouse release must not convert the
  // deferred click into Replace, nor leave a paint stroke running afterwards.
  harness.release(start + quader::QVec2{1.0f, 0.0f});
  harness.dispatch(false, false, true);
  ASSERT_TRUE(harness.before.selection_click_requested);
  EXPECT_TRUE(harness.before.selection_click_modifiers.primary);
  EXPECT_FLOAT_EQ(harness.before.selection_click_local.x, start.x);
  EXPECT_FLOAT_EQ(harness.before.selection_click_local.y, start.y);
  EXPECT_TRUE(harness.selected_components().empty());
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 0u);
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_FALSE(harness.controller.transform_gizmo_pointer_active());
  harness.input.mouse_local =
      harness.camera.project_world_point({-0.6f, -0.6f, 0.0f}).position;
  harness.dispatch();
  EXPECT_TRUE(harness.selected_components().empty());
  EXPECT_EQ(harness.editor.tools().active_tool(), "move");
  EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
  EXPECT_EQ(harness.session.assets().revision(), asset_revision);
  EXPECT_EQ(harness.session.history().undo_count(), 0u);
}

TEST(PaintSelectionInteractionTests,
     PaneSelectionMemoryControlsMoveFallbackWithoutChangingTheActiveTool) {
  PaintSelectionHarness harness;
  const auto first = harness.quad({-2.0f, 0.0f, 0.0f});
  const auto last = harness.quad({2.0f, 0.0f, 0.0f});
  auto &viewports = harness.editor.viewports();
  ASSERT_TRUE(viewports.set_layout(quader::ViewportLayoutId::FourQuad).ok());
  ASSERT_TRUE(viewports.focus_pane(quader::ViewportPaneId::Perspective).ok());
  ASSERT_TRUE(harness.editor.tools()
                  .activate(quader::ViewportToolId::PaintSelect)
                  .ok());
  ASSERT_TRUE(viewports.focus_pane(quader::ViewportPaneId::Top).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Select).ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(viewports.selected_tool());
  for (const auto pane :
       {quader::ViewportPaneId::Perspective, quader::ViewportPaneId::Top}) {
    ASSERT_TRUE(viewports.focus_pane(pane).ok());
    ASSERT_TRUE(harness.editor.selection().clear().ok());
    harness.press({100.0f, 300.0f});
    harness.move({700.0f, 300.0f});
    harness.dispatch(true, true, false);
    if (pane == quader::ViewportPaneId::Perspective) {
      EXPECT_EQ(harness.selected_entities(),
                (std::vector<quader::EntityId>{first.object.entity,
                                               last.object.entity}));
      EXPECT_EQ(harness.operation_context.selection_gesture,
                quader::QuaderInputSelectionGesture::Paint);
    } else {
      EXPECT_TRUE(harness.selected_entities().empty());
      EXPECT_TRUE(harness.controller.selection_marquee().active);
      EXPECT_TRUE(harness.controller.selection_marquee().lasso_points.empty());
    }
    harness.release({700.0f, 300.0f});
    harness.dispatch(false, false, true);
    EXPECT_EQ(viewports.selected_tool(pane), quader::ViewportToolId::Move);
    EXPECT_EQ(harness.editor.tools().active_tool(), "move");
  }
  EXPECT_EQ(
      viewports.preferred_selection_tool(quader::ViewportPaneId::Perspective),
      quader::ViewportToolId::PaintSelect);
  EXPECT_EQ(viewports.preferred_selection_tool(quader::ViewportPaneId::Top),
            quader::ViewportToolId::Select);
}

TEST(PaintSelectionInteractionTests,
     RememberedPaintAddsTargetsWhileTheAlignOperationRemainsActive) {
  PaintSelectionHarness harness;
  const auto left = harness.quad({-3.0f, 0.0f, 0.0f});
  const auto center = harness.quad({0.0f, 0.0f, 0.0f});
  const auto right = harness.quad({3.0f, 0.0f, 0.0f});
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({left.object, center.object})
                  .ok());
  ASSERT_TRUE(
      harness.editor.tools().activate(quader::ViewportToolId::Align).ok());
  harness.router.set_active_tool(harness.editor.viewports().selected_tool());
  ASSERT_EQ(harness.editor.viewports().preferred_selection_tool(),
            quader::ViewportToolId::PaintSelect);
  const auto content_revision = harness.session.modeling().content_revision();
  harness.press({700.0f, 300.0f}, {.shift = true});
  harness.release(harness.point(right), {.shift = true});
  harness.dispatch(true, false, true);
  EXPECT_EQ(
      harness.selected_entities(),
      (std::vector<quader::EntityId>{left.object.entity, center.object.entity,
                                     right.object.entity}));
  EXPECT_EQ(harness.editor.tools().active_tool(), "align");
  EXPECT_EQ(harness.editor.viewports().selected_tool(),
            quader::ViewportToolId::Align);
  EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
}

std::vector<ElementKey> quad_loop_elements(const PaintQuad &quad,
                                         quader::QuaderSelectionMode mode) {
  std::vector<ElementKey> expected;
  if (mode == quader::QuaderSelectionMode::Face) {
    expected.push_back({quad.object.modeling_object_id,
                        static_cast<int>(quad.face), 0});
  } else {
    for (std::size_t index = 0; index < quad.perimeter.size(); ++index) {
      const int first = static_cast<int>(quad.perimeter[index]);
      const int second = static_cast<int>(
          quad.perimeter[(index + 1) % quad.perimeter.size()]);
      expected.push_back(mode == quader::QuaderSelectionMode::Vertex
          ? ElementKey{quad.object.modeling_object_id, first, 0}
          : ElementKey{quad.object.modeling_object_id, std::min(first, second),
                        std::max(first, second)});
    }
  }
  std::ranges::sort(expected);
  return expected;
}

TEST(PaintSelectionInteractionTests,
     AltTraversalOverridesEveryRememberedShapeAndKeepsTheExactPressComponent) {
  for (const auto &mode : kComponentModes) {
    for (const auto shape : {quader::ViewportToolId::Select,
                             quader::ViewportToolId::LassoSelect,
                             quader::ViewportToolId::PaintSelect}) {
      for (const bool move : {false, true}) {
        SCOPED_TRACE(static_cast<int>(mode.api_mode));
        SCOPED_TRACE(static_cast<int>(shape));
        SCOPED_TRACE(move);
        PaintSelectionHarness harness;
        // Blender stops at isolated quad corners. A boundary n-gon supplies
        // a real multi-element loop while retaining the same pick positions.
        const auto quad = harness.quad({}, mode.api_mode !=
            quader::QuaderSelectionMode::Face);
        const auto outsider = harness.quad({3.0f, 0.0f, 0.0f});
        harness.enter_edit(mode, {quad.object});
        ASSERT_TRUE(harness.editor.tools().activate(shape).ok());
        if (move) {
          ASSERT_TRUE(harness.editor.tools().activate(
              quader::ViewportToolId::Move).ok());
        }
        harness.router.set_active_tool(harness.editor.viewports().selected_tool());
        const auto content_revision = harness.session.modeling().content_revision();
        const auto generation = harness.editor.mode().generation();
        const auto point = harness.point(quad, mode.api_mode);
        harness.press(point, {.alt = true});
        harness.dispatch(true, true, false);
        EXPECT_TRUE(harness.selected_components().empty());
        EXPECT_TRUE(harness.controller.dragging());
        EXPECT_FALSE(harness.controller.selection_marquee().active);
        EXPECT_EQ(harness.operation_context.selection_gesture,
                  quader::QuaderInputSelectionGesture::EdgeLoop);
        // The native release is still on the captured component. Later
        // coalesced hover and released Alt must not redirect the click.
        harness.release(point);
        harness.input.mouse_local = harness.point(outsider, mode.api_mode);
        harness.input.modifiers = {.primary = true};
        harness.dispatch(false, false, true);
        EXPECT_EQ(harness.selected_components(), quad_loop_elements(quad, mode.api_mode));
        EXPECT_FALSE(harness.controller.dragging());
        EXPECT_FALSE(harness.controller.selection_marquee().active);
        EXPECT_EQ(harness.editor.mode().state().participants.size(), 1u);
        EXPECT_EQ(harness.editor.mode().generation(), generation);
        EXPECT_EQ(harness.editor.viewports().preferred_selection_tool(), shape);
        EXPECT_EQ(harness.editor.viewports().selected_tool(),
                  move ? quader::ViewportToolId::Move : shape);
        EXPECT_EQ(harness.session.modeling().content_revision(), content_revision);
        harness.dispatch();
        EXPECT_EQ(harness.selected_components(), quad_loop_elements(quad, mode.api_mode));
      }
    }
  }
}

TEST(PaintSelectionInteractionTests,
     ShiftAltTogglesCapturedLoopAndAnOrdinaryStrokeStillPaintsAfterward) {
  PaintSelectionHarness harness;
  const auto quad = harness.quad({}, true);
  harness.enter_edit(kComponentModes[1], {quad.object});
  const auto point = harness.point(quad, quader::QuaderSelectionMode::Edge);
  harness.press(point, {.alt = true});
  harness.release(point);
  harness.dispatch(true, false, true);
  ASSERT_EQ(harness.selected_components(),
            quad_loop_elements(quad, quader::QuaderSelectionMode::Edge));
  harness.press(point, {.shift = true, .alt = true});
  harness.dispatch(true, true, false);
  harness.release(point, {.primary = true});
  harness.dispatch(false, false, true);
  EXPECT_TRUE(harness.selected_components().empty());
  harness.press(point);
  harness.dispatch(true, true, false);
  EXPECT_EQ(harness.selected_components(),
            (std::vector<ElementKey>{harness.element(
                quad, quader::QuaderSelectionMode::Edge)}));
  EXPECT_EQ(harness.operation_context.selection_gesture,
            quader::QuaderInputSelectionGesture::Paint);
  harness.release(point);
  harness.dispatch(false, false, true);
  EXPECT_FALSE(harness.controller.dragging());
}

TEST(PaintSelectionInteractionTests,
     ControlAltUsesTypedRingIntentWithoutBecomingPaintRemoval) {
  for (const auto &mode : kComponentModes) {
    SCOPED_TRACE(static_cast<int>(mode.api_mode));
    PaintSelectionHarness harness;
    const auto quad = harness.quad({});
    harness.enter_edit(mode, {quad.object});
    const auto point = harness.point(quad, mode.api_mode);
    harness.press(point, {.primary = true, .alt = true});
    harness.dispatch(true, true, false);
    EXPECT_TRUE(harness.selected_components().empty());
    EXPECT_EQ(harness.operation_context.selection_gesture,
              mode.api_mode == quader::QuaderSelectionMode::Face
                  ? quader::QuaderInputSelectionGesture::EdgeLoop
                  : quader::QuaderInputSelectionGesture::EdgeRing);
    harness.release(point);
    harness.dispatch(false, false, true);
    auto expected = quad_loop_elements(quad, mode.api_mode);
    if (mode.api_mode == quader::QuaderSelectionMode::Edge) {
      expected = {{quad.object.modeling_object_id,
                    static_cast<int>(quad.vertices[0]),
                    static_cast<int>(quad.vertices[1])},
                  {quad.object.modeling_object_id,
                    static_cast<int>(quad.vertices[2]),
                    static_cast<int>(quad.vertices[3])}};
    }
    EXPECT_EQ(harness.selected_components(), expected);
    EXPECT_FALSE(harness.controller.dragging());
  }
}

TEST(PaintSelectionInteractionTests,
     FaceCenterAltClickUsesTheSelectedNeighborAsTheStripDirection) {
  for (const bool control : {false, true}) {
    PaintSelectionHarness harness;
    quader_poly::Document document;
    std::array<std::array<quader_poly::ElementId, 2>, 4> vertices;
    std::array<quader_poly::ElementId, 3> faces;
    for (std::size_t column = 0; column < vertices.size(); ++column) {
      const float x = static_cast<float>(column) - 1.5f;
      vertices[column][0] = quader_poly::add_vertex(document, {x, -0.6f, 0.0f});
      vertices[column][1] = quader_poly::add_vertex(document, {x, 0.6f, 0.0f});
    }
    for (std::size_t column = 0; column < faces.size(); ++column) {
      const std::array corners{vertices[column][0], vertices[column + 1][0],
                               vertices[column + 1][1], vertices[column][1]};
      faces[column] = quader_poly::add_face(document, corners);
    }
    const auto created = harness.editor.modeling().create_mesh(
        quader::QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Face traversal strip"});
    ASSERT_TRUE(created.ok());
    const auto object = created.value();
    harness.enter_edit(kComponentModes[2], {object});
    ASSERT_TRUE(harness.editor.selection().select_face(object, faces[0]).ok());
    ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
    harness.router.set_active_tool(harness.editor.viewports().selected_tool());
    const auto point = harness.camera.project_world_point({0.0f, 0.0f, 0.0f}).position;
    harness.press(point, {.primary = control, .alt = true});
    harness.dispatch(true, true, false);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{{object.modeling_object_id,
                                         static_cast<int>(faces[0]), 0}}));
    harness.release(point);
    harness.dispatch(false, false, true);
    std::vector<ElementKey> expected;
    for (const auto face : faces) {
      expected.push_back({object.modeling_object_id, static_cast<int>(face), 0});
    }
    EXPECT_EQ(harness.selected_components(), expected);
    EXPECT_EQ(harness.editor.tools().active_tool(), "move");
    EXPECT_FALSE(harness.controller.selection_marquee().active);
  }
}

TEST(PaintSelectionInteractionTests,
     DeferredMoveHandleAltClickTraversesInsteadOfStartingPaint) {
  PaintSelectionHarness harness;
  const auto quad = harness.quad({}, true);
  harness.enter_edit(kComponentModes[0], {quad.object});
  ASSERT_TRUE(harness.editor.selection().select_vertex(quad.object, quad.vertices[2]).ok());
  ASSERT_TRUE(harness.editor.tools().activate(quader::ViewportToolId::Move).ok());
  harness.router.set_active_tool(harness.editor.viewports().selected_tool());
  harness.dispatch();
  const auto point = harness.point(quad, quader::QuaderSelectionMode::Vertex);
  const auto *gizmo = harness.controller.transform_gizmo_frame();
  ASSERT_NE(gizmo, nullptr);
  ASSERT_NEAR(gizmo->info.screen_pivot.x, point.x, 0.01f);
  ASSERT_NEAR(gizmo->info.screen_pivot.y, point.y, 0.01f);
  const auto revision = harness.session.modeling().content_revision();
  harness.press(point, {.alt = true});
  harness.dispatch(true, true, false);
  ASSERT_TRUE(harness.before.gizmo_consumed);
  EXPECT_EQ(harness.editor.selection().summary().selected_vertex_count, 1u);
  harness.release(point + quader::QVec2{1.0f, 0.0f});
  harness.input.mouse_local = {20.0f, 20.0f};
  harness.dispatch(false, false, true);
  ASSERT_TRUE(harness.before.selection_click_requested);
  EXPECT_TRUE(harness.before.selection_click_modifiers.alt);
  EXPECT_EQ(harness.selected_components(),
            quad_loop_elements(quad, quader::QuaderSelectionMode::Vertex));
  EXPECT_FALSE(harness.controller.dragging());
  EXPECT_EQ(harness.session.modeling().content_revision(), revision);
}

TEST(PaintSelectionInteractionTests,
     InterruptedTraversalNeverBecomesPaintAndReservesItsDelayedRelease) {
  for (int interruption = 0; interruption < 5; ++interruption) {
    SCOPED_TRACE(interruption);
    PaintSelectionHarness harness;
    const auto quad = harness.quad({});
    harness.enter_edit(kComponentModes[1], {quad.object});
    const auto point = harness.point(quad, quader::QuaderSelectionMode::Edge);
    const auto revision = harness.session.modeling().content_revision();
    harness.press(point, {.alt = true});
    harness.dispatch(true, true, false);
    ASSERT_TRUE(harness.selected_components().empty());
    switch (interruption) {
    case 0:
      harness.move(point + quader::QVec2{20.0f, 0.0f});
      harness.move(point);
      break;
    case 1:
      harness.input.pointer_interaction_canceled = true;
      break;
    case 2:
      ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Object).ok());
      ASSERT_TRUE(harness.editor.mode().set(quader::QuaderEditorMode::Edit).ok());
      break;
    case 3:
      harness.input.viewport_focused = false;
      break;
    case 4:
      harness.input.keybinding_input_consumed = true;
      break;
    }
    harness.dispatch(false, true, false);
    EXPECT_FALSE(harness.controller.dragging());
    EXPECT_TRUE(harness.selected_components().empty());
    EXPECT_FALSE(harness.controller.selection_marquee().active);
    harness.input.pointer_interaction_canceled = false;
    harness.input.viewport_focused = true;
    harness.input.keybinding_input_consumed = false;
    harness.release(point);
    harness.dispatch(false, false, true);
    EXPECT_TRUE(harness.selected_components().empty());
    EXPECT_EQ(harness.session.modeling().content_revision(), revision);
    harness.press(point);
    harness.release(point);
    harness.dispatch(true, false, true);
    EXPECT_EQ(harness.selected_components(),
              (std::vector<ElementKey>{harness.element(
                  quad, quader::QuaderSelectionMode::Edge)}));
  }
}

} // namespace
