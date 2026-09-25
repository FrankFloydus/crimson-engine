////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/session/scene_session.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace quader;

constexpr float kEpsilon = 0.001f;

/** Owns a native scene and its public editor facade for compensated edits. */
struct CompensatedTransformHarness {
  SceneSession session;
  QuaderEditorApi editor{
      QuaderApiSessionInternalAccess::from_scene_session(session)};

  QuaderModelingObjectRef box(std::string name) {
    const auto created = editor.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        std::move(name));
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.value();
  }
};

void expect_matrix_near(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], kEpsilon) << index;
  }
}

void expect_vector_near(QVec3 actual, QVec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, kEpsilon);
  EXPECT_NEAR(actual.y, expected.y, kEpsilon);
  EXPECT_NEAR(actual.z, expected.z, kEpsilon);
}

QMat4 affine_offset(float direction = 1.0f) {
  QMat4 offset;
  offset.values[0] = -1.5f * direction;
  offset.values[4] = 0.45f;
  offset.values[5] = 0.75f;
  offset.values[9] = -0.35f;
  offset.values[10] = 2.0f;
  offset.values[12] = 3.0f * direction;
  offset.values[13] = -2.0f;
  return offset;
}

std::vector<QVec3> world_vertices(const CompensatedTransformHarness &harness,
                                 EntityId entity) {
  const auto &scene = harness.session.scene();
  const auto *mesh = harness.session.assets().mesh(scene.mesh(entity)->mesh);
  const QMat4 world = scene.world_transform_matrix(entity);
  std::vector<QVec3> result;
  for (const auto &vertex : mesh->geometry().vertices) {
    result.push_back(transform_point(world, vertex.position));
  }
  return result;
}

TEST(CompensatedTransformConsumerTests,
     SiblingMoveUsesDistinctCompensatedFramesAndRestoresHistory) {
  CompensatedTransformHarness harness;
  auto &scene = harness.session.scene();
  const EntityId parent = scene.create_empty("Parent");
  ASSERT_TRUE(scene.set_transform(parent, {
      .position = {-2.0f, 4.0f, 1.0f},
      .rotation_euler = {0.2f, 0.4f, -0.3f},
      .scale = {2.0f, 0.6f, -1.25f},
  }));
  const std::array children{scene.create_empty("First"),
                            scene.create_empty("Second")};
  std::array<TransformComponent, 2> captured;
  std::array<QMat4, 2> before;
  for (std::size_t index = 0; index < children.size(); ++index) {
    captured[index].position = {1.0f, -3.0f, 0.5f};
    captured[index].rotation_euler = {0.3f, -0.1f, 0.2f};
    captured[index].parenting_offset = affine_offset(index == 0u ? 1.0f : -1.0f);
    ASSERT_TRUE(scene.set_transform(children[index], captured[index]));
    ASSERT_TRUE(scene.set_parent(children[index], parent, false));
    ASSERT_TRUE(scene.set_selected(children[index], true));
    before[index] = scene.world_transform_matrix(children[index]);
  }
  harness.session.history().clear();
  SceneTransformDragService drag;
  ASSERT_TRUE(drag.begin(scene, {}, true));
  const QVec3 delta{2.5f, -1.25f, 3.0f};
  ASSERT_TRUE(drag.preview_world_translation_delta(scene, delta).ok);
  const auto expect_moved = [&] {
    for (std::size_t index = 0; index < children.size(); ++index) {
      QMat4 expected = before[index];
      expected.values[12] += delta.x;
      expected.values[13] += delta.y;
      expected.values[14] += delta.z;
      expect_matrix_near(scene.world_transform_matrix(children[index]), expected);
      EXPECT_EQ(scene.transform(children[index])->parenting_offset.values,
                captured[index].parenting_offset.values);
    }
  };
  expect_moved();
  ASSERT_TRUE(drag.commit(harness.editor).ok());
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  for (std::size_t index = 0; index < children.size(); ++index) {
    expect_matrix_near(scene.world_transform_matrix(children[index]), before[index]);
  }
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_moved();
}

TEST(CompensatedTransformConsumerTests,
     LocalRotationAndScalePinVisiblePivotInCompensatedFrame) {
  SceneDocument scene;
  const EntityId entity = scene.create_empty("Compensated");
  TransformComponent before{
      .position = {1.0f, 2.0f, -3.0f},
      .rotation_euler = {0.1f, 0.4f, -0.2f},
      .scale = {1.5f, 0.7f, 2.0f},
  };
  before.parenting_offset = affine_offset();
  ASSERT_TRUE(scene.set_transform(entity, before));
  ASSERT_TRUE(scene.set_selected(entity, true));
  const QVec3 local_pivot{0.6f, -0.3f, 0.8f};
  const QVec3 pivot = transform_point(scene.world_transform_matrix(entity), local_pivot);
  expect_vector_near(local_pivot_for_world_point(before, pivot), local_pivot);
  TransformComponent standalone = before;
  standalone.rotation_euler = {0.8f, 0.1f, -0.6f};
  pin_local_pivot_to_world_point(standalone, local_pivot, pivot);
  expect_vector_near(transform_point(scene_local_transform_matrix(standalone), local_pivot),
                     pivot);
  for (const bool rotate : {true, false}) {
    SceneTransformDragService drag;
    ASSERT_TRUE(drag.begin(scene, pivot, true));
    const auto result = rotate
        ? drag.preview_local_rotation_delta(scene, {0.2f, 0.3f, -0.1f})
        : drag.preview_local_scale_delta(scene, {0.5f, 1.5f, 2.0f});
    ASSERT_TRUE(result.ok) << result.message;
    expect_vector_near(transform_point(scene.world_transform_matrix(entity), local_pivot),
                       pivot);
    EXPECT_EQ(scene.transform(entity)->parenting_offset.values,
              before.parenting_offset.values);
    ASSERT_TRUE(drag.cancel().ok);
    expect_matrix_near(scene.local_transform_matrix(entity),
                       scene_local_transform_matrix(before));
  }
}

TEST(CompensatedTransformConsumerTests,
     WorldRotationAfterKeepTransformParentingUsesCompensatedParent) {
  SceneDocument scene;
  const EntityId parent = scene.create_empty("Nonuniform Parent");
  const EntityId child = scene.create_empty("Child");
  ASSERT_TRUE(scene.set_transform(parent, {
      .rotation_euler = {0.2f, 0.35f, -0.1f},
      .scale = {3.0f, 0.5f, -2.0f},
  }));
  ASSERT_TRUE(scene.set_transform(child, {.position = {2.0f, 3.0f, 1.0f}}));
  ASSERT_TRUE(scene.set_parent(child, parent, true));
  ASSERT_TRUE(scene.set_selected(child, true));
  const QMat4 before = scene.world_transform_matrix(child);
  const QMat4 offset = scene.transform(child)->parenting_offset;
  const QMat4 delta = compose_transform(QTransform{
      .rotation = quat_from_euler_radians({0.0f, 0.0f, 0.4f})});
  SceneTransformDragService drag;
  ASSERT_TRUE(drag.begin(scene, {}, true));
  const auto preview = drag.preview_world_transform_delta(
      scene, delta, SceneTextureTransformKind::Rotation);
  ASSERT_TRUE(preview.ok) << preview.message;
  expect_matrix_near(scene.world_transform_matrix(child),
                     scene_transform_multiply(delta, before));
  EXPECT_EQ(scene.transform(child)->parenting_offset.values, offset.values);
  ASSERT_TRUE(drag.cancel().ok);
}

TEST(CompensatedTransformConsumerTests,
     InspectorTextureLocksRetainOffsetsForAllFourPolicies) {
  for (const bool lock : {false, true}) {
    for (const bool scale_lock : {false, true}) {
      SCOPED_TRACE(::testing::Message() << lock << ", " << scale_lock);
      CompensatedTransformHarness harness;
      auto &scene = harness.session.scene();
      const auto object = harness.box("Texture");
      TransformComponent initial;
      initial.parenting_offset = affine_offset();
      ASSERT_TRUE(scene.set_transform(object.entity, initial));
      harness.session.document_commands().set_texture_lock_settings_provider(
          [=] { return TextureLockSettings{lock, scale_lock}; });
      QuaderTransform channels{.position = {3.0f, -2.0f, 1.0f}};
      ASSERT_TRUE(harness.editor.scene().set_transform(object.entity, channels).ok());
      channels.rotation_euler = {0.2f, 0.4f, -0.3f};
      ASSERT_TRUE(harness.editor.scene().set_transform(object.entity, channels).ok());
      const QMat4 before_scale = compose_transform(QTransform{
          .position = channels.position,
          .rotation = quat_from_euler_radians(channels.rotation_euler)});
      channels.scale = {-2.0f, 3.0f, 0.5f};
      ASSERT_TRUE(harness.editor.scene().set_transform(object.entity, channels).ok());
      const QMat4 scale = compose_transform(QTransform{.scale = channels.scale});
      const QMat4 expected = lock
          ? (scale_lock ? QMat4{} : scale)
          : (scale_lock ? before_scale : scene_transform_multiply(before_scale, scale));
      const auto *mapping = scene.texture_mapping(object.entity);
      expect_matrix_near(mapping != nullptr ? mapping->sampling_from_local : QMat4{},
                         expected);
      EXPECT_EQ(scene.transform(object.entity)->parenting_offset.values,
                initial.parenting_offset.values);
      const QMat4 world = scene.world_transform_matrix(object.entity);
      ASSERT_TRUE(harness.editor.history().undo().ok());
      ASSERT_TRUE(harness.editor.history().redo().ok());
      expect_matrix_near(scene.world_transform_matrix(object.entity), world);
    }
  }
}

TEST(CompensatedTransformConsumerTests,
     PivotRebasePreservesMeshAndChildWorldsAndChildChannels) {
  CompensatedTransformHarness harness;
  auto &scene = harness.session.scene();
  const auto source = harness.box("Pivot Source");
  const auto target = harness.box("Pivot Target");
  ASSERT_TRUE(scene.set_transform(source.entity, {
      .position = {7.0f, -2.0f, 4.0f}, .custom_pivot = true}));
  ASSERT_TRUE(harness.editor.selection().select_object(source).ok());
  ASSERT_TRUE(harness.editor.tools().activate(ViewportToolId::Pivot).ok());
  ASSERT_TRUE(harness.editor.tools().execute_action("pivot", "pivot.align.top_center").ok());
  ASSERT_TRUE(harness.editor.tools().commit_active_tool_preview().ok());
  TransformComponent target_transform{.position = {1.0f, 2.0f, 3.0f}};
  target_transform.parenting_offset = affine_offset();
  ASSERT_TRUE(scene.set_transform(target.entity, target_transform));
  const EntityId child = scene.create_empty("Child");
  TransformComponent child_transform{
      .position = {-2.0f, 1.0f, 0.5f},
      .rotation_euler = {0.3f, 0.2f, -0.4f},
      .scale = {0.6f, 2.0f, -1.0f},
  };
  child_transform.parenting_offset = affine_offset(-1.0f);
  ASSERT_TRUE(scene.set_transform(child, child_transform));
  ASSERT_TRUE(scene.set_parent(child, target.entity, false));
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({source.entity, target.entity}, source.entity).ok());
  const QMat4 child_world = scene.world_transform_matrix(child);
  const auto vertices = world_vertices(harness, target.entity);
  harness.session.history().clear();
  const auto result = harness.editor.modeling().copy_pivot_from_active();
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  const auto expect_preserved = [&] {
    const auto moved_vertices = world_vertices(harness, target.entity);
    ASSERT_EQ(moved_vertices.size(), vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      expect_vector_near(moved_vertices[index], vertices[index]);
    }
    expect_matrix_near(scene.world_transform_matrix(child), child_world);
    expect_vector_near(scene.transform(child)->position, child_transform.position);
    expect_vector_near(scene.transform(child)->rotation_euler, child_transform.rotation_euler);
    expect_vector_near(scene.transform(child)->scale, child_transform.scale);
    EXPECT_EQ(scene.transform(target.entity)->parenting_offset.values,
              target_transform.parenting_offset.values);
  };
  expect_preserved();
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_preserved();
  EXPECT_EQ(scene.transform(child)->parenting_offset.values,
            child_transform.parenting_offset.values);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_preserved();
}

TEST(CompensatedTransformConsumerTests,
     ArrangementTranslatesInWorldDespiteShearedCompensation) {
  CompensatedTransformHarness harness;
  auto &scene = harness.session.scene();
  const auto active = harness.box("Active");
  const auto other = harness.box("Other");
  ASSERT_TRUE(scene.set_transform(active.entity, {.position = {10.0f, 0.0f, 0.0f}}));
  TransformComponent before{.position = {-3.0f, 2.0f, 1.0f}};
  before.parenting_offset = affine_offset();
  ASSERT_TRUE(scene.set_transform(other.entity, before));
  const auto bounds_before = entity_world_bounds(scene, harness.session.assets(), other.entity);
  ASSERT_TRUE(bounds_before.has_value());
  ASSERT_TRUE(harness.editor.selection()
                  .select_objects({active.entity, other.entity}, active.entity).ok());
  const auto result = harness.editor.modeling().align_selected_meshes(
      QuaderTransformAxis::X, QuaderMeshAlignment::Center);
  ASSERT_TRUE(result.ok()) << result.error_message();
  const auto bounds_after = entity_world_bounds(scene, harness.session.assets(), other.entity);
  const auto active_bounds = entity_world_bounds(scene, harness.session.assets(), active.entity);
  ASSERT_TRUE(bounds_after.has_value());
  ASSERT_TRUE(active_bounds.has_value());
  EXPECT_NEAR(bounds_center(*bounds_after).x, bounds_center(*active_bounds).x, kEpsilon);
  EXPECT_NEAR(bounds_center(*bounds_after).y, bounds_center(*bounds_before).y, kEpsilon);
  EXPECT_NEAR(bounds_center(*bounds_after).z, bounds_center(*bounds_before).z, kEpsilon);
  EXPECT_EQ(scene.transform(other.entity)->parenting_offset.values,
            before.parenting_offset.values);
}

TEST(CompensatedTransformConsumerTests,
     InstanceConversionAndFlattenPreserveAffinePlacementAndChannels) {
  CompensatedTransformHarness harness;
  auto &scene = harness.session.scene();
  const auto object = harness.box("Affine Member");
  TransformComponent original{
      .position = {2.0f, -1.0f, 3.0f},
      .rotation_euler = {0.2f, -0.3f, 0.4f},
      .scale = {0.75f, 1.5f, -2.0f},
  };
  original.parenting_offset = affine_offset();
  ASSERT_TRUE(scene.set_transform(object.entity, original));
  ASSERT_TRUE(harness.editor.selection().select_object(object).ok());
  const QMat4 before = scene.world_transform_matrix(object.entity);
  const auto created = harness.editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(created.ok()) << created.error_message();
  const EntityId occurrence = scene.primary_selection();
  const auto *instance = scene.instance_occurrence(occurrence);
  ASSERT_NE(instance, nullptr);
  const EntityId definition = instance->definition;
  const auto visible_world = [&] {
    return scene_transform_multiply(scene.world_transform_matrix(occurrence),
        scene_transform_multiply(inverse(scene.world_transform_matrix(definition)),
                                 scene.world_transform_matrix(object.entity)));
  };
  expect_matrix_near(visible_world(), before);
  expect_vector_near(scene.transform(object.entity)->position, original.position);
  expect_vector_near(scene.transform(object.entity)->rotation_euler, original.rotation_euler);
  expect_vector_near(scene.transform(object.entity)->scale, original.scale);
  ASSERT_TRUE(harness.editor.scene().set_transform(occurrence, {
      .position = {5.0f, 1.0f, -3.0f},
      .rotation_euler = {0.4f, -0.15f, 0.3f},
      .scale = {-1.2f, 0.65f, 2.0f},
  }).ok());
  const QMat4 expected = visible_world();
  harness.session.history().clear();
  const auto flattened = harness.editor.modeling().flatten_instance();
  ASSERT_TRUE(flattened.ok()) << flattened.error_message();
  ASSERT_TRUE(flattened.changed());
  expect_matrix_near(scene.world_transform_matrix(object.entity), expected);
  expect_vector_near(scene.transform(object.entity)->position, original.position);
  expect_vector_near(scene.transform(object.entity)->rotation_euler, original.rotation_euler);
  expect_vector_near(scene.transform(object.entity)->scale, original.scale);
  EXPECT_EQ(harness.session.history().undo_count(), 1u);
  ASSERT_TRUE(harness.editor.history().undo().ok());
  expect_matrix_near(visible_world(), expected);
  ASSERT_TRUE(harness.editor.history().redo().ok());
  expect_matrix_near(scene.world_transform_matrix(object.entity), expected);
}

} // namespace
