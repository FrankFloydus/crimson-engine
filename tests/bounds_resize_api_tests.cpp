////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_transform.hpp"
#include "support/command_observable_state.hpp"


#include <gtest/gtest.h>
#include <QTemporaryDir>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

/// Owns one native document and exposes test intent through the public facade.
struct BoundsResizeHarness {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};

  QuaderModelingObjectRef box(QVec3 minimum, QVec3 maximum, std::string name) {
    const auto created = editor.modeling().create_box({.min = minimum, .max = maximum}, std::move(name));
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }
};

float coordinate(QVec3 point, int axis) {
  return axis == 0 ? point.x : axis == 1 ? point.y : point.z;
}

void set_coordinate(QVec3 &point, int axis, float value) {
  if (axis == 0) point.x = value;
  else if (axis == 1) point.y = value;
  else point.z = value;
}

QVec3 frame_point(const QuaderBoundsResizeState &state, QVec3 local) {
  return state.world_origin + state.world_x * local.x +
         state.world_y * local.y + state.world_z * local.z;
}

QuaderTransformDragBeginRequest resize_request(
    const QuaderBoundsResizeState &state, int axis = 0, bool positive = true,
    QuaderTransformSpace space = QuaderTransformSpace::Global) {
  QVec3 moving = state.local_min + (state.local_max - state.local_min) * 0.5f;
  QVec3 anchor = moving;
  set_coordinate(moving, axis, coordinate(positive ? state.local_max : state.local_min, axis));
  set_coordinate(anchor, axis, coordinate(positive ? state.local_min : state.local_max, axis));
  return {
      .tool = QuaderTransformTool::Scale,
      .axis = axis == 0 ? QuaderTransformAxis::X : axis == 1 ? QuaderTransformAxis::Y : QuaderTransformAxis::Z,
      .space = space,
      .orientation = {.x_axis = state.world_x, .y_axis = state.world_y, .z_axis = state.world_z},
      .pivot = frame_point(state, anchor),
      .screen_position = {100, 100}, .screen_pivot = {90, 100}, .screen_tip = {110, 100},
      .ray = {.origin = {0, 0, 10}, .direction = {0, 0, -1}},
      .camera = {.orthographic = true, .origin = {0, 0, 10},
                 .viewport_size = {800, 600}, .orthographic_size = 10},
      .grid_snap_enabled = false,
      .bounds_resize_reference = QuaderBoundsResizeReference{.moving_point = frame_point(state, moving)}};
}

QuaderTransformDragUpdateRequest resize_update(double displacement, bool snap = false, double grid = 1.0) {
  return {.grid_size = grid, .grid_snap_enabled = snap,
          .bounds_resize_displacement = displacement};
}

QMat4 world_matrix(const QuaderEditorApi &editor, EntityId entity) {
  const auto object = editor.query().entity(entity);
  EXPECT_TRUE(object);
  if (!object) return {};
  const TransformComponent local{.location = object->transform.location,
                                 .rotation_euler = object->transform.rotation_euler,
                                 .scale = object->transform.scale,
                                 .parenting_offset = object->parenting_offset.value_or(QMat4{})};
  const auto local_matrix = scene_local_transform_matrix(local);
  return object->parent == kInvalidEntityId ? local_matrix
      : scene_transform_multiply(world_matrix(editor, object->parent), local_matrix);
}

std::vector<EntityId> selected_entities(const QuaderEditorApi &editor) {
  std::vector<EntityId> selected;
  for (const auto &entity : editor.query().entities()) {
    if (entity.selected) selected.push_back(entity.id);
  }
  return selected;
}

std::vector<QVec3> world_points(const QuaderEditorApi &editor, EntityId entity) {
  const auto object = editor.query().modeling_object_for_entity(entity);
  EXPECT_TRUE(object);
  const auto mesh = object ? editor.query().compiled_object_mesh(*object) : std::nullopt;
  EXPECT_TRUE(mesh);
  std::vector<QVec3> points;
  if (mesh) {
    const auto world = world_matrix(editor, entity);
    for (const auto &vertex : mesh->vertices) {
      points.push_back(transform_point(world, vertex.position));
    }
  }
  return points;
}

std::vector<QVec3> world_points(const BoundsResizeHarness &harness, EntityId entity) {
  return world_points(harness.editor, entity);
}

void expect_point(QVec3 actual, QVec3 expected, float epsilon = 0.002f) {
  EXPECT_NEAR(actual.x, expected.x, epsilon);
  EXPECT_NEAR(actual.y, expected.y, epsilon);
  EXPECT_NEAR(actual.z, expected.z, epsilon);
}

void expect_points(const std::vector<QVec3> &actual, const std::vector<QVec3> &expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t index = 0; index < actual.size(); ++index) {
    expect_point(actual[index], expected[index]);
  }
}

TEST(BoundsResizeApiTests, CommonBoundsResizeKeepsOppositeSideAndRelativePlacementThroughHistory) {
  BoundsResizeHarness harness;
  const auto first = harness.box({-1, -1, -1}, {1, 1, 1}, "First");
  const auto second = harness.box({3, -1, -1}, {5, 1, 1}, "Second");
  const auto child = harness.box({-0.25f, -0.25f, -0.25f}, {0.25f, 0.25f, 0.25f}, "Child");
  ASSERT_TRUE(harness.editor.scene().reparent(child.entity, first.entity).ok());
  ASSERT_TRUE(harness.editor.selection().select_objects({first, second}).ok());
  const auto before_first = world_points(harness, first.entity);
  const auto before_second = world_points(harness, second.entity);
  const auto before_child = world_points(harness, child.entity);
  const auto selected = selected_entities(harness.editor);
  const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  EXPECT_FLOAT_EQ(state->local_min.x, -1);
  EXPECT_FLOAT_EQ(state->local_max.x, 5);
  const auto retained = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(retained);
  EXPECT_EQ(retained->revision, state->revision);
  const auto history_before = harness.editor.history().undo_history(100).entries.size();
  const auto begun = harness.editor.modeling().begin_transform_drag(resize_request(*state));
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  const auto updated = harness.editor.modeling().update_transform_drag(resize_update(6));
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  const auto expect_scaled = [&](EntityId entity, const std::vector<QVec3> &before) {
    auto expected = before;
    for (auto &point : expected) point.x = 2 * point.x + 1;
    expect_points(world_points(harness, entity), expected);
  };
  expect_scaled(first.entity, before_first);
  expect_scaled(second.entity, before_second);
  expect_scaled(child.entity, before_child);
  const auto resized = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(resized);
  EXPECT_FLOAT_EQ(resized->local_min.x, -1);
  EXPECT_FLOAT_EQ(resized->local_max.x, 11);
  EXPECT_NE(resized->revision, state->revision);
  ASSERT_TRUE(harness.editor.modeling().commit_transform_drag().ok());
  EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before + 1);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_points(world_points(harness, first.entity), before_first);
  expect_points(world_points(harness, second.entity), before_second);
  expect_points(world_points(harness, child.entity), before_child);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_scaled(first.entity, before_first);
  expect_scaled(second.entity, before_second);
  EXPECT_EQ(selected_entities(harness.editor), selected);
}

TEST(BoundsResizeApiTests, LocalRotatedAndReflectedSelectionUsesOneCapturedBoundsFrame) {
  for (const bool reflected : {false, true}) {
    SCOPED_TRACE(reflected);
    BoundsResizeHarness harness;
    const auto first = harness.box({-1, -1, -1}, {1, 1, 1}, "First");
    const auto second = harness.box({-1, -1, -1}, {1, 1, 1}, "Second");
    const QuaderTransform first_transform{
        .rotation_euler = {0, 0, std::numbers::pi_v<float> * 0.25f},
        .scale = {reflected ? -1.0f : 1.0f, 1, 1}};
    auto second_transform = first_transform;
    second_transform.location = {3, 3, 0};
    ASSERT_TRUE(harness.editor.scene().set_transform(first.entity, first_transform).ok());
    ASSERT_TRUE(harness.editor.scene().set_transform(second.entity, second_transform).ok());
    ASSERT_TRUE(harness.editor.selection().select_objects({first, second}).ok());
    const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Local);
    ASSERT_TRUE(state);
    const auto request = resize_request(*state, 0, true, QuaderTransformSpace::Local);
    const float extent = state->local_max.x - state->local_min.x;
    const auto begun = harness.editor.modeling().begin_transform_drag(request);
    ASSERT_TRUE(begun.ok()) << begun.error_message();
    const auto updated = harness.editor.modeling().update_transform_drag(resize_update(extent));
    ASSERT_TRUE(updated.ok()) << updated.error_message();
    const auto after = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Local);
    ASSERT_TRUE(after);
    EXPECT_NEAR(after->local_min.x, state->local_min.x, 0.002f);
    EXPECT_NEAR(after->local_max.x - after->local_min.x, extent * 2, 0.002f);
    EXPECT_NEAR(after->local_min.y, state->local_min.y, 0.002f);
    EXPECT_NEAR(after->local_max.y, state->local_max.y, 0.002f);
    ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
    const auto restored = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Local);
    ASSERT_TRUE(restored);
    expect_point(restored->local_min, state->local_min);
    expect_point(restored->local_max, state->local_max);
  }
}

TEST(BoundsResizeApiTests, LocalBoundsUseActualRotatedGeometryAndRetainExistingShear) {
  BoundsResizeHarness harness;
  const auto target = harness.box({-1, -1, -1}, {1, 1, 1}, "Rotated");
  ASSERT_TRUE(harness.editor.scene().set_transform(
      target.entity, {.rotation_euler = {0, 0, std::numbers::pi_v<float> * 0.25f}}).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const auto global = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  const auto local = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Local);
  ASSERT_TRUE(global);
  ASSERT_TRUE(local);
  EXPECT_NEAR(local->local_max.x - local->local_min.x, 2, 0.001f);
  EXPECT_GT(global->local_max.x - global->local_min.x, 2.8f);

  // The public editable TRS excludes authored affine prefixes; establish the
  // existing-shear fixture through its storage owner, then use public intent.
  TransformComponent placement;
  placement.parenting_offset.values[4] = 0.5f;
  ASSERT_TRUE(harness.session.scene().set_transform(target.entity, placement));
  const auto sheared = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Local);
  ASSERT_TRUE(sheared);
  const auto original_points = world_points(harness, target.entity);
  const auto begun = harness.editor.modeling().begin_transform_drag(
      resize_request(*sheared, 0, true, QuaderTransformSpace::Local));
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  const auto updated = harness.editor.modeling().update_transform_drag(resize_update(2));
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  const auto accepted_entity = harness.editor.query().entity(target.entity);
  ASSERT_TRUE(accepted_entity);
  ASSERT_TRUE(accepted_entity->parenting_offset);
  EXPECT_EQ(accepted_entity->parenting_offset->values, placement.parenting_offset.values);
  ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
  expect_points(world_points(harness, target.entity), original_points);
}

TEST(BoundsResizeApiTests, NegativeSideSnapsMovingBoundAndClampsCrossingWithoutReflection) {
  BoundsResizeHarness harness;
  const auto target = harness.box({-2, -1, -1}, {3, 1, 1}, "Snap");
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  const auto history_before = harness.editor.history().undo_history(100).entries.size();
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*state, 0, false)).ok());
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(0.6, true)).ok());
  const auto snapped = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(snapped);
  EXPECT_NEAR(snapped->local_min.x, -3, 0.001f);
  EXPECT_NEAR(snapped->local_max.x, 3, 0.001f);
  const auto repeated = harness.editor.modeling().update_transform_drag(resize_update(0.6, true));
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(1.0e9)).ok());
  const auto enlarged = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(enlarged);
  EXPECT_NEAR(enlarged->local_max.x - enlarged->local_min.x, 50000, 0.01f);
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(-100)).ok());
  const auto clamped = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(clamped);
  EXPECT_NEAR(clamped->local_max.x, 3, 0.001f);
  EXPECT_GT(clamped->local_max.x, clamped->local_min.x);
  EXPECT_NEAR(clamped->local_max.x - clamped->local_min.x, 0.005f, 0.0001f);
  ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
  EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before);
  const auto restored = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(restored);
  expect_point(restored->local_min, state->local_min);
  expect_point(restored->local_max, state->local_max);
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*state)).ok());
  ASSERT_TRUE(harness.editor.modeling().commit_transform_drag().ok());
  EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before);
}

TEST(BoundsResizeApiTests, RejectedSamplesPreserveAcceptedPreviewAndInvalidBeginDoesNotSettleIt) {
  BoundsResizeHarness harness;
  const auto target = harness.box({-1, -1, -1}, {1, 1, 1}, "Accepted");
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*state)).ok());
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(1)).ok());
  const auto accepted = test::capture_command_observable_state(harness.editor);
  const auto expect_accepted = [&] {
    const auto difference = test::diff_command_observable_state(
        accepted, test::capture_command_observable_state(harness.editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
  };
  const auto invalid = harness.editor.modeling().update_transform_drag(
      resize_update(std::numeric_limits<double>::quiet_NaN()));
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  expect_accepted();
  const auto missing = harness.editor.modeling().update_transform_drag({});
  EXPECT_FALSE(missing.ok());
  EXPECT_FALSE(missing.changed());
  expect_accepted();
  auto invalid_begin = resize_request(*state);
  invalid_begin.bounds_resize_reference->moving_point = invalid_begin.pivot;
  const auto rejected = harness.editor.modeling().begin_transform_drag(invalid_begin);
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_accepted();
  ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
}

TEST(BoundsResizeApiTests, GlobalAffineResizesRotatedReflectedHierarchiesFromCapturedGeometryThroughHistory) {
  for (const bool reflected : {false, true}) {
    SCOPED_TRACE(reflected);
    BoundsResizeHarness harness;
    harness.editor.preferences().set_runtime_hooks({
        .texture_lock_enabled = [] { return true; },
        .texture_lock_scale_enabled = [reflected] { return reflected; }});
    const auto parent = harness.editor.scene().create_empty();
    ASSERT_TRUE(parent.ok());
    ASSERT_TRUE(harness.editor.scene().set_transform(parent.value().id,
        {.location = {3, -2, 1}, .rotation_euler = {0.1f, 0.2f, -0.3f},
         .scale = {1.5f, 0.75f, 2}}).ok());
    const auto first = harness.box({-1, -1, -1}, {1, 1, 1}, "Rotated Child");
    const auto second = harness.box({-1, -1, -1}, {1, 1, 1}, "Rotated Root");
    const auto child = harness.box({-0.25f, -0.25f, -0.25f}, {0.25f, 0.25f, 0.25f}, "Selected Descendant");
    ASSERT_TRUE(harness.editor.scene().reparent(first.entity, parent.value().id).ok());
    ASSERT_TRUE(harness.editor.scene().set_transform(first.entity,
        {.location = {-2, 1, 0}, .rotation_euler = {0, 0, std::numbers::pi_v<float> * 0.25f},
         .scale = {reflected ? -1.25f : 1.25f, 0.8f, 1.5f}}).ok());
    ASSERT_TRUE(harness.editor.scene().set_transform(second.entity,
        {.location = {6, 2, -1}, .rotation_euler = {0.3f, -0.2f, 0.6f},
         .scale = {0.7f, 1.4f, 1}}).ok());
    ASSERT_TRUE(harness.editor.scene().reparent(child.entity, first.entity).ok());
    ASSERT_TRUE(harness.editor.selection().select_objects({first, child, second}).ok());
    const std::array<EntityId, 3> targets{first.entity, child.entity, second.entity};
    std::array<std::vector<QVec3>, 3> before;
    std::array<QuaderEntitySnapshot, 3> captured;
    std::array<QMat4, 3> captured_world;
    for (std::size_t index = 0; index < targets.size(); ++index) {
      before[index] = world_points(harness, targets[index]);
      captured[index] = *harness.editor.query().entity(targets[index]);
      captured_world[index] = world_matrix(harness.editor, targets[index]);
    }
    const auto assets_before = harness.editor.query().assets();
    const auto selection_before = selected_entities(harness.editor);
    const auto parent_before = world_matrix(harness.editor, parent.value().id);
    const auto history_before = harness.editor.history().undo_history(100).entries.size();
    const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
    ASSERT_TRUE(state);
    const double span = state->local_max.x - state->local_min.x;
    const auto expect_factor = [&](float factor) {
      for (std::size_t index = 0; index < targets.size(); ++index) {
        auto expected = before[index];
        for (auto &point : expected) {
          point.x = state->local_min.x + (point.x - state->local_min.x) * factor;
        }
        expect_points(world_points(harness, targets[index]), expected);
        const auto object = harness.editor.query().modeling_object_for_entity(targets[index]);
        ASSERT_TRUE(object);
        const auto mesh = harness.editor.query().compiled_object_mesh(*object);
        const auto entity = harness.editor.query().entity(targets[index]);
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(entity);
        const auto mapping = entity->texture_sampling_from_local.value_or(QMat4{});
        const auto captured_mapping = captured[index].texture_sampling_from_local.value_or(QMat4{});
        ASSERT_EQ(mesh->vertices.size(), expected.size());
        for (std::size_t vertex_index = 0; vertex_index < mesh->vertices.size(); ++vertex_index) {
          const auto local = mesh->vertices[vertex_index].position;
          const auto expected_sample = reflected ? transform_point(captured_mapping, local)
              : transform_point(captured_mapping,
                    transform_point(inverse(captured_world[index]), expected[vertex_index]));
          expect_point(transform_point(mapping, local), expected_sample);
        }
      }
      EXPECT_EQ(world_matrix(harness.editor, parent.value().id).values, parent_before.values);
      EXPECT_EQ(harness.editor.query().assets().revision, assets_before.revision);
      EXPECT_EQ(selected_entities(harness.editor), selection_before);
    };
    const auto begun = harness.editor.modeling().begin_transform_drag(resize_request(*state));
    ASSERT_TRUE(begun.ok()) << begun.error_message();
    for (const float factor : {2.0f, 1.5f, 1.0f, 2.0f}) {
      const auto updated = harness.editor.modeling().update_transform_drag(resize_update(span * (factor - 1)));
      ASSERT_TRUE(updated.ok()) << updated.error_message();
      expect_factor(factor);
      if (factor == 1) {
        for (std::size_t index = 0; index < targets.size(); ++index) {
          const auto restored = harness.editor.query().entity(targets[index]);
          ASSERT_TRUE(restored);
          EXPECT_EQ(restored->parenting_offset.value_or(QMat4{}).values,
                    captured[index].parenting_offset.value_or(QMat4{}).values);
          expect_point(restored->transform.rotation_euler, captured[index].transform.rotation_euler, 0);
          expect_point(restored->transform.scale, captured[index].transform.scale, 0);
        }
      }
    }
    const auto repeated = harness.editor.modeling().update_transform_drag(resize_update(span));
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.changed());
    const auto accepted = test::capture_command_observable_state(harness.editor);
    const auto invalid = harness.editor.modeling().update_transform_drag(
        resize_update(std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(invalid.ok());
    EXPECT_FALSE(invalid.changed());
    const auto difference = test::diff_command_observable_state(
        accepted, test::capture_command_observable_state(harness.editor));
    EXPECT_TRUE(difference.empty()) << difference.summary;
    const auto committed = harness.editor.modeling().commit_transform_drag();
    ASSERT_TRUE(committed.ok()) << committed.error_message();
    EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before + 1);
    ASSERT_TRUE(harness.editor.history().undo().ok());
    expect_factor(1);
    ASSERT_TRUE(harness.editor.history().redo().ok());
    expect_factor(2);
    const auto resized = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
    ASSERT_TRUE(resized);
    ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*resized)).ok());
    ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(1)).ok());
    ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
    expect_factor(2);
    EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before + 1);
  }
}

TEST(BoundsResizeApiTests, AffineRoundTripToZeroCreatesNoHistoryAndOrdinaryScaleKeepsItsRejectionPolicy) {
  BoundsResizeHarness harness;
  const auto target = harness.box({-1, -1, -1}, {1, 1, 1}, "Rotated");
  ASSERT_TRUE(harness.editor.scene().set_transform(target.entity,
      {.rotation_euler = {0, 0, std::numbers::pi_v<float> * 0.25f}}).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  const auto before = world_points(harness, target.entity);
  const auto history_before = harness.editor.history().undo_history(100).entries.size();
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*state)).ok());
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(1)).ok());
  ASSERT_TRUE(harness.editor.modeling().update_transform_drag(resize_update(0)).ok());
  const auto committed = harness.editor.modeling().commit_transform_drag();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.changed());
  EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before);
  expect_points(world_points(harness, target.entity), before);

  auto ordinary = resize_request(*state);
  ordinary.bounds_resize_reference.reset();
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(ordinary).ok());
  const auto rejected = harness.editor.modeling().update_transform_drag({
      .screen_position = {200, 100}, .grid_snap_enabled = false});
  EXPECT_FALSE(rejected.ok());
  EXPECT_FALSE(rejected.changed());
  expect_points(world_points(harness, target.entity), before);
  ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
}

TEST(BoundsResizeApiTests, AffineResizeKeepsLinkedGeometrySharedAndUsesTheActiveOccurrenceFrame) {
  BoundsResizeHarness harness;
  const auto member = harness.box({-1, -1, -1}, {1, 1, 1}, "Shared Rotated Member");
  ASSERT_TRUE(harness.editor.scene().set_transform(member.entity,
      {.rotation_euler = {0, 0, std::numbers::pi_v<float> * 0.25f}}).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(member).ok());
  const auto created = harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto definition = created.value().definition.root.id;
  const auto first = created.value().occurrence.root.id;
  ASSERT_TRUE(harness.editor.commands().execute(quader_command::kDuplicateSelection).ok());
  const auto duplicate_selection = selected_entities(harness.editor);
  ASSERT_EQ(duplicate_selection.size(), 1u);
  const auto second = duplicate_selection.front();
  ASSERT_NE(second, first);
  ASSERT_TRUE(harness.editor.scene().set_transform(second,
      {.location = {6, 2, 0}, .rotation_euler = {0, 0, -0.2f}}).ok());
  const auto mesh = harness.editor.query().compiled_object_mesh(member);
  ASSERT_TRUE(mesh);
  const auto assets_before = harness.editor.query().assets();
  const auto displayed_points = [&](EntityId occurrence) {
    const auto display = scene_transform_multiply(world_matrix(harness.editor, occurrence),
        scene_transform_multiply(inverse(world_matrix(harness.editor, definition)),
                                 world_matrix(harness.editor, member.entity)));
    std::vector<QVec3> result;
    for (const auto &vertex : mesh->vertices) result.push_back(transform_point(display, vertex.position));
    return result;
  };
  const auto before_first = displayed_points(first);
  const auto before_second = displayed_points(second);
  ASSERT_TRUE(harness.editor.selection().select_objects({first, second}, first).ok());
  const auto state = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  ASSERT_TRUE(harness.editor.modeling().begin_transform_drag(resize_request(*state)).ok());
  const auto updated = harness.editor.modeling().update_transform_drag(
      resize_update(state->local_max.x - state->local_min.x));
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  for (const auto &entry : {std::pair{first, before_first}, std::pair{second, before_second}}) {
    auto expected = entry.second;
    for (auto &point : expected) point.x = state->local_min.x + 2 * (point.x - state->local_min.x);
    expect_points(displayed_points(entry.first), expected);
  }
  EXPECT_EQ(harness.editor.query().assets().revision, assets_before.revision);
  ASSERT_TRUE(harness.editor.modeling().cancel_transform_drag().ok());
  expect_points(displayed_points(first), before_first);
  expect_points(displayed_points(second), before_second);

  ASSERT_TRUE(harness.editor.modeling().enter_instance_edit(first).ok());
  ASSERT_TRUE(harness.editor.scene().select(member.entity).ok());
  const auto scoped = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(scoped);
  const auto first_placement = world_matrix(harness.editor, first);
  const auto second_placement = world_matrix(harness.editor, second);
  const auto begun = harness.editor.modeling().begin_transform_drag(resize_request(*scoped));
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  const auto scoped_update = harness.editor.modeling().update_transform_drag(
      resize_update(scoped->local_max.x - scoped->local_min.x));
  ASSERT_TRUE(scoped_update.ok()) << scoped_update.error_message();
  auto expected_first = before_first;
  for (auto &point : expected_first) point.x = scoped->local_min.x + 2 * (point.x - scoped->local_min.x);
  expect_points(displayed_points(first), expected_first);
  EXPECT_EQ(world_matrix(harness.editor, first).values, first_placement.values);
  EXPECT_EQ(world_matrix(harness.editor, second).values, second_placement.values);
  EXPECT_EQ(harness.editor.query().assets().revision, assets_before.revision);
  const auto retained_mesh = harness.editor.query().compiled_object_mesh(member);
  ASSERT_TRUE(retained_mesh);
  ASSERT_EQ(retained_mesh->vertices.size(), mesh->vertices.size());
  EXPECT_EQ(retained_mesh->indices, mesh->indices);
  for (std::size_t index = 0; index < mesh->vertices.size(); ++index) {
    expect_point(retained_mesh->vertices[index].position, mesh->vertices[index].position, 0);
  }
  ASSERT_TRUE(harness.editor.modeling().commit_transform_drag().ok());
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_points(displayed_points(first), before_first);
  expect_points(displayed_points(second), before_second);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_points(displayed_points(first), expected_first);
  ASSERT_TRUE(harness.editor.modeling().exit_instance_edit().ok());
}

TEST(BoundsResizeApiTests, CommittedAffineResizeSurvivesPublicSaveAndReopenWithoutBakingGeometry) {
  QTemporaryDir temporary;
  ASSERT_TRUE(temporary.isValid());
  ProjectSession project;
  QuaderEditorApi editor(project);
  ASSERT_TRUE(editor.project().create(std::filesystem::path(temporary.path().toStdWString())).ok());
  const auto created = editor.modeling().create_box(
      {.min = {-1, -1, -1}, .max = {1, 1, 1}}, "Persisted Affine Resize");
  ASSERT_TRUE(created.ok());
  const auto target = created.value();
  ASSERT_TRUE(editor.scene().set_transform(target.entity,
      {.location = {2, -1, 3}, .rotation_euler = {0.2f, -0.3f, 0.7f},
       .scale = {-1.5f, 0.75f, 2}}).ok());
  ASSERT_TRUE(editor.selection().select_object(target).ok());
  const auto original_mesh = editor.query().compiled_object_mesh(target);
  ASSERT_TRUE(original_mesh);
  const auto state = editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(state);
  ASSERT_TRUE(editor.modeling().begin_transform_drag(resize_request(*state)).ok());
  const auto updated = editor.modeling().update_transform_drag(resize_update(2));
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  ASSERT_TRUE(editor.modeling().commit_transform_drag().ok());
  const auto accepted = editor.query().entity(target.entity);
  ASSERT_TRUE(accepted);
  ASSERT_TRUE(accepted->parenting_offset);
  const auto expected = world_points(editor, target.entity);
  const auto path = ProjectRelativePath::parse_portable("Scenes/Affine Bounds Resize.qdr");
  ASSERT_TRUE(path.ok());
  const auto document = editor.scene_documents().active_scene();
  ASSERT_TRUE(document);
  const auto saved = editor.scene_documents().save_as(document->session_id, path.value());
  ASSERT_TRUE(saved.ok()) << saved.error_message();
  ASSERT_TRUE(editor.scene_documents().close_active().ok());
  const auto reopened = editor.scene_documents().open(path.value());
  ASSERT_TRUE(reopened.ok()) << reopened.error_message();
  const auto objects = editor.query().modeling_objects();
  ASSERT_EQ(objects.size(), 1u);
  const auto rebound = editor.query().modeling_object_for_id(objects.front().modeling_object_id);
  ASSERT_TRUE(rebound);
  const auto durable = editor.query().entity(rebound->entity);
  ASSERT_TRUE(durable);
  EXPECT_EQ(durable->parenting_offset.value_or(QMat4{}).values, accepted->parenting_offset->values);
  expect_point(durable->transform.location, accepted->transform.location, 0);
  expect_point(durable->transform.rotation_euler, accepted->transform.rotation_euler, 0);
  expect_point(durable->transform.scale, accepted->transform.scale, 0);
  expect_points(world_points(editor, rebound->entity), expected);
  const auto durable_mesh = editor.query().compiled_object_mesh(*rebound);
  ASSERT_TRUE(durable_mesh);
  ASSERT_EQ(durable_mesh->vertices.size(), original_mesh->vertices.size());
  EXPECT_EQ(durable_mesh->indices, original_mesh->indices);
  for (std::size_t index = 0; index < original_mesh->vertices.size(); ++index) {
    expect_point(durable_mesh->vertices[index].position, original_mesh->vertices[index].position, 0);
  }
}

TEST(BoundsResizeApiTests, IneligibleAndDegenerateTargetsDoNotStartAResize) {
  BoundsResizeHarness harness;
  EXPECT_FALSE(harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global));
  const auto target = harness.box({-1, -1, -1}, {1, 1, 1}, "Target");
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  ASSERT_TRUE(harness.editor.scene().set_visible(target.entity, false).ok());
  EXPECT_FALSE(harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global));
  ASSERT_TRUE(harness.editor.scene().set_visible(target.entity, true).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Group).ok());
  EXPECT_FALSE(harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global));
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Face).ok());
  EXPECT_FALSE(harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global));
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  EXPECT_FALSE(harness.editor.scene().set_transform(target.entity, {.scale = {0, 1, 1}}).ok());
  const auto original_bounds = harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global);
  ASSERT_TRUE(original_bounds);
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Edit).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Vertex).ok());
  ASSERT_TRUE(harness.editor.selection().select_vertex(target, 1).ok());
  ASSERT_TRUE(harness.editor.selection().select_all().ok());
  const auto collapsed_result = harness.editor.modeling().scale_selection({0, 0, 0});
  ASSERT_TRUE(collapsed_result.ok()) << collapsed_result.error_message();
  ASSERT_TRUE(harness.editor.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().set_mode(QuaderSelectionMode::Object).ok());
  ASSERT_TRUE(harness.editor.selection().select_object(target).ok());
  EXPECT_FALSE(harness.editor.modeling().selection_resize_bounds(QuaderTransformSpace::Global));
  const auto history_before = harness.editor.history().undo_history(100).entries.size();
  EXPECT_FALSE(harness.editor.modeling().begin_transform_drag(resize_request(*original_bounds)).ok());
  EXPECT_EQ(harness.editor.history().undo_history(100).entries.size(), history_before);
}

} // namespace
} // namespace quader
