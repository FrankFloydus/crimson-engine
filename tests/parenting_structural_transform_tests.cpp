////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_transform.hpp"

#include <gtest/gtest.h>

#include <string>
#include <utility>

namespace quader {
namespace {

void expect_world(const QMat4 &actual, const QMat4 &expected) {
  for (std::size_t index = 0; index < actual.values.size(); ++index) {
    EXPECT_NEAR(actual.values[index], expected.values[index], 0.0002f) << index;
  }
}

void expect_vector(QVec3 actual, QVec3 expected) {
  EXPECT_FLOAT_EQ(actual.x, expected.x);
  EXPECT_FLOAT_EQ(actual.y, expected.y);
  EXPECT_FLOAT_EQ(actual.z, expected.z);
}

void expect_channels(const TransformComponent &actual,
                     const TransformComponent &expected) {
  expect_vector(actual.position, expected.position);
  expect_vector(actual.rotation_euler, expected.rotation_euler);
  expect_vector(actual.scale, expected.scale);
  EXPECT_EQ(actual.custom_pivot, expected.custom_pivot);
}

/// Exercises complete affine placement through existing structural API owners.
class ParentingStructuralTransformTests : public ::testing::Test {
protected:
  ParentingStructuralTransformTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  EntityId empty(std::string name, QuaderTransform transform = {}) {
    const auto created = editor_.scene().create_entity(
        {.name = std::move(name), .kind = QuaderEntityKind::Empty,
         .transform = transform});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value().id : kInvalidEntityId;
  }

  EntityId box(std::string name) {
    const auto created = editor_.modeling().create_box(
        {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        std::move(name));
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value().entity : kInvalidEntityId;
  }

  void compensated(EntityId entity) {
    TransformComponent transform{
        .position = {2.0f, -3.0f, 4.0f},
        .rotation_euler = {0.35f, -0.4f, 0.7f},
        .scale = {-1.25f, 0.8f, 2.5f},
        .custom_pivot = true};
    transform.parenting_offset.values[4] = 0.3f;
    transform.parenting_offset.values[9] = -0.2f;
    transform.parenting_offset.values[12] = 5.0f;
    ASSERT_TRUE(session_.scene().set_transform(entity, transform));
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(ParentingStructuralTransformTests,
       GroupUngroupAndDuplicatePreserveShearChannelsAndPivots) {
  const EntityId parent = empty("Scaled Parent",
      {.position = {3.0f, 5.0f, -2.0f},
       .rotation_euler = {-0.2f, 0.5f, 0.4f},
       .scale = {0.75f, 2.0f, -1.5f}});
  const EntityId first = empty("First");
  const EntityId second = empty("Second", {.position = {-4.0f, 1.0f, 2.0f}});
  compensated(first);
  ASSERT_TRUE(editor_.scene().reparent(first, parent, false).ok());
  ASSERT_TRUE(editor_.scene().reparent(second, parent, false).ok());
  const auto first_before = *session_.scene().transform(first);
  const auto second_before = *session_.scene().transform(second);
  const QMat4 first_world = session_.scene().world_transform_matrix(first);
  const QMat4 second_world = session_.scene().world_transform_matrix(second);
  ASSERT_TRUE(editor_.scene().select({first, second}, second).ok());
  const auto grouped = editor_.scene().group_selection("Compensated Group",
      {.position = {-1.0f, 2.0f, 3.0f},
       .rotation_euler = {0.3f, -0.25f, 0.6f},
       .scale = {1.5f, 0.5f, -2.0f}});
  ASSERT_TRUE(grouped.ok()) << grouped.error_message();
  expect_world(session_.scene().world_transform_matrix(first), first_world);
  expect_world(session_.scene().world_transform_matrix(second), second_world);
  expect_channels(*session_.scene().transform(first), first_before);
  expect_channels(*session_.scene().transform(second), second_before);

  const auto duplicated = editor_.scene().duplicate(grouped.value().id);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  const auto children = session_.scene().children(duplicated.value().id);
  ASSERT_EQ(children.size(), 2u);
  expect_world(session_.scene().world_transform_matrix(children[0]), first_world);
  expect_world(session_.scene().world_transform_matrix(children[1]), second_world);
  expect_channels(*session_.scene().transform(children[0]), first_before);
  EXPECT_EQ(session_.scene().transform(children[0])->parenting_offset.values,
            session_.scene().transform(first)->parenting_offset.values);

  ASSERT_TRUE(editor_.scene().select(grouped.value().id).ok());
  const auto ungrouped = editor_.scene().ungroup_selection();
  ASSERT_TRUE(ungrouped.ok()) << ungrouped.error_message();
  EXPECT_EQ(session_.scene().parent(first), parent);
  expect_world(session_.scene().world_transform_matrix(first), first_world);
  expect_world(session_.scene().world_transform_matrix(second), second_world);
  expect_channels(*session_.scene().transform(first), first_before);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_world(session_.scene().world_transform_matrix(first), first_world);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world(session_.scene().world_transform_matrix(first), first_world);
}

TEST_F(ParentingStructuralTransformTests,
       ClipboardRootRetainsCapturedAffineFrameWithoutSourceParent) {
  const EntityId parent = empty("Clipboard Parent",
      {.position = {3.0f, -1.0f, 2.0f},
       .rotation_euler = {0.5f, 0.2f, -0.4f},
       .scale = {1.5f, 0.75f, -2.0f}});
  const EntityId source = box("Clipboard Child");
  compensated(source);
  ASSERT_TRUE(editor_.scene().reparent(source, parent, false).ok());
  const TransformComponent before = *session_.scene().transform(source);
  const QMat4 source_world = session_.scene().world_transform_matrix(source);
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  ASSERT_TRUE(editor_.scene().destroy(parent).ok());
  ASSERT_TRUE(editor_.selection().clear().ok());
  const auto pasted = editor_.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  const auto selected = session_.scene().selected_entities();
  ASSERT_EQ(selected.size(), 1u);
  const EntityId clone = selected.front();
  EXPECT_EQ(session_.scene().parent(clone), kInvalidEntityId);
  QMat4 translated = source_world;
  translated.values[12] += 1.0f;
  translated.values[14] += 1.0f;
  expect_world(session_.scene().world_transform_matrix(clone), translated);
  expect_vector(session_.scene().transform(clone)->rotation_euler,
                before.rotation_euler);
  expect_vector(session_.scene().transform(clone)->scale, before.scale);
  EXPECT_EQ(session_.scene().transform(clone)->custom_pivot, before.custom_pivot);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_world(session_.scene().world_transform_matrix(clone), translated);
}

TEST_F(ParentingStructuralTransformTests,
       ClipboardRemapsCopiedMeshHierarchyInsteadOfParentingToOriginal) {
  const EntityId parent = box("Copied Parent");
  const EntityId child = box("Copied Child");
  compensated(parent);
  compensated(child);
  ASSERT_TRUE(editor_.scene().reparent(child, parent, false).ok());
  const QMat4 parent_world = session_.scene().world_transform_matrix(parent);
  const QMat4 child_world = session_.scene().world_transform_matrix(child);
  ASSERT_TRUE(editor_.selection().select_objects({parent, child}, child).ok());
  ASSERT_TRUE(editor_.modeling().copy_selection().ok());
  const auto pasted = editor_.modeling().paste_selection();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  const auto selected = session_.scene().selected_entities();
  ASSERT_EQ(selected.size(), 2u);
  EntityId copied_parent = kInvalidEntityId;
  EntityId copied_child = kInvalidEntityId;
  for (const EntityId entity : selected) {
    if (session_.scene().parent(entity) == kInvalidEntityId) {
      copied_parent = entity;
    } else {
      copied_child = entity;
    }
  }
  ASSERT_NE(copied_parent, kInvalidEntityId);
  ASSERT_NE(copied_child, kInvalidEntityId);
  EXPECT_EQ(session_.scene().parent(copied_child), copied_parent);
  expect_world(session_.scene().world_transform_matrix(copied_parent), parent_world);
  expect_world(session_.scene().world_transform_matrix(copied_child), child_world);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(session_.scene().parent(copied_child), copied_parent);
  expect_world(session_.scene().world_transform_matrix(copied_child), child_world);
}

} // namespace
} // namespace quader
