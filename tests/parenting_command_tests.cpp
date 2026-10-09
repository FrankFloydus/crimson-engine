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

#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace quader;

QuaderBounds unit_box_bounds() {
  return {.min = {-0.5f, -0.5f, -0.5f}, .max = {0.5f, 0.5f, 0.5f}};
}

/// Exercises public hierarchy transactions against the authoritative matrices.
class ParentingCommandTests : public ::testing::Test {
protected:
  EntityId create(std::string name, QuaderEntityKind kind = QuaderEntityKind::Empty,
                  EntityId parent = kInvalidEntityId,
                  QuaderTransform transform = {}) {
    const auto result = editor_.scene().create_entity(
        {.name = std::move(name), .kind = kind, .parent = parent,
         .transform = transform});
    EXPECT_TRUE(result.ok()) << result.error_message();
    return result.ok() ? result.value().id : kInvalidEntityId;
  }

  void select(std::initializer_list<EntityId> entities, EntityId active) {
    ASSERT_TRUE(editor_.selection()
                    .select_objects_for_inspection(
                        std::vector<EntityId>(entities), active).ok());
  }

  void expect_matrix(EntityId entity, const QMat4 &expected) {
    const QMat4 actual = session_.scene().world_transform_matrix(entity);
    for (std::size_t index = 0; index < expected.values.size(); ++index) {
      EXPECT_NEAR(actual.values[index], expected.values[index], 0.0001F)
          << "Entity " << entity << ", matrix value " << index;
    }
  }

  void expect_channels(QVec3 actual, QVec3 expected) {
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.z, expected.z);
  }

  SceneSession session_;
  QuaderEditorApi editor_{
      QuaderApiSessionInternalAccess::from_scene_session(session_)};
};

TEST_F(ParentingCommandTests,
       ActiveLastMeshParentFlattensExplicitSelectionAndRetainsChannelsAndWorld) {
  const auto mesh = editor_.modeling().create_box(unit_box_bounds());
  ASSERT_TRUE(mesh.ok()) << mesh.error_message();
  const EntityId target = mesh.value().entity;
  ASSERT_TRUE(editor_.scene().set_transform(
      target, {.location = {10, -3, 2}, .rotation_euler = {0.2F, 0.8F, -0.3F},
               .scale = {-2, 3, 0.75F}}).ok());
  const EntityId first = create("First", QuaderEntityKind::Empty,
      kInvalidEntityId, {.location = {1, 2, 3},
                         .rotation_euler = {0.3F, 0.4F, 0.7F}, .scale = {2, 1, 3}});
  const EntityId nested = create("Nested", QuaderEntityKind::Generic, first,
      {.location = {4, -1, 2}, .rotation_euler = {0.6F, 0.3F, 0.2F}});
  const EntityId existing = create("Existing", QuaderEntityKind::Empty, target);
  const auto light = editor_.scene().create_point_light({.name = "Light"});
  ASSERT_TRUE(light.ok()) << light.error_message();
  const auto curve = editor_.modeling().create_curve({
      .points = {{.position = {-1, 0, 0}}, {.position = {1, 2, 0}}},
      .name = "Curve"});
  ASSERT_TRUE(curve.ok()) << curve.error_message();
  const std::vector<EntityId> moved{first, nested, light.value().id,
                                   curve.value().entity};
  std::vector<QMat4> worlds;
  std::vector<TransformComponent> channels;
  for (const EntityId entity : moved) {
    worlds.push_back(session_.scene().world_transform_matrix(entity));
    channels.push_back(*session_.scene().transform(entity));
  }
  select({nested, existing, light.value().id, curve.value().entity, first, target},
          target);
  const auto selected = session_.scene().selected_entities();
  const auto result = editor_.scene().parent_selection_to_active();
  ASSERT_TRUE(result.ok()) << result.error_message();
  EXPECT_TRUE(result.changed());
  EXPECT_EQ(session_.scene().children(target),
            (std::vector<EntityId>{existing, first, nested, light.value().id,
                                   curve.value().entity}));
  EXPECT_EQ(session_.scene().selected_entities(), selected);
  EXPECT_EQ(session_.scene().primary_selection(), target);
  for (std::size_t index = 0; index < moved.size(); ++index) {
    const auto *after = session_.scene().transform(moved[index]);
    ASSERT_NE(after, nullptr);
    expect_channels(after->location, channels[index].location);
    expect_channels(after->rotation_euler, channels[index].rotation_euler);
    expect_channels(after->scale, channels[index].scale);
    expect_matrix(moved[index], worlds[index]);
    EXPECT_TRUE(editor_.query().entity(moved[index])->parenting_offset.has_value());
  }
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().parent(nested), first);
  EXPECT_EQ(session_.scene().parent(first), kInvalidEntityId);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(session_.scene().parent(nested), target);
  for (std::size_t index = 0; index < moved.size(); ++index) {
    expect_matrix(moved[index], worlds[index]);
  }
  const auto revision = session_.scene().revision();
  const auto unchanged = editor_.scene().parent_selection_to_active();
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(session_.scene().revision(), revision);
  EXPECT_EQ(editor_.history().snapshot().undo_label, "Parent to Active");
}

TEST_F(ParentingCommandTests,
       UnparentDetachesNestedSelectionsIndividuallyAndLeavesRootSelectionAlone) {
  const EntityId parent = create("Parent", QuaderEntityKind::Empty,
      kInvalidEntityId, {.location = {3, 2, 1}, .rotation_euler = {0.2F, 0.3F, 0.5F},
                         .scale = {3, -2, 1}});
  const EntityId child = create("Child", QuaderEntityKind::Empty, parent,
      {.location = {2, 1, 0}, .rotation_euler = {0.3F, 0.4F, 0.7F}});
  const EntityId grandchild = create("Grandchild", QuaderEntityKind::Empty, child,
      {.location = {1, 2, 3}, .scale = {0, 1, 2}});
  const EntityId root = create("Root");
  const auto child_world = session_.scene().world_transform_matrix(child);
  const auto grandchild_world = session_.scene().world_transform_matrix(grandchild);
  select({root, grandchild, child}, child);
  ASSERT_TRUE(editor_.scene().unparent_selection().ok());
  EXPECT_EQ(session_.scene().root_entities(),
            (std::vector<EntityId>{parent, root, child, grandchild}));
  expect_matrix(child, child_world);
  expect_matrix(grandchild, grandchild_world);
  EXPECT_EQ(session_.scene().primary_selection(), child);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(session_.scene().parent(child), parent);
  EXPECT_EQ(session_.scene().parent(grandchild), child);
  expect_matrix(child, child_world);
  expect_matrix(grandchild, grandchild_world);
  ASSERT_TRUE(editor_.history().redo().ok());
  const auto revision = session_.scene().revision();
  const auto unchanged = editor_.scene().unparent_selection();
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(session_.scene().revision(), revision);
}

TEST_F(ParentingCommandTests,
       RejectsCyclesLockedSubtreesAndSingularParentsBeforeAnyMutation) {
  const EntityId parent = create("Parent");
  const EntityId child = create("Child", QuaderEntityKind::Empty, parent);
  const EntityId other = create("Other");
  const EntityId singular = create("Singular", QuaderEntityKind::Empty,
      kInvalidEntityId, {.scale = {1, 0, 1}});
  const auto roots = session_.scene().root_entities();
  const auto rejected = [&] {
    const auto revision = session_.scene().revision();
    const auto history = editor_.history().snapshot();
    const auto result = editor_.scene().parent_selection_to_active();
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(session_.scene().revision(), revision);
    EXPECT_EQ(editor_.history().snapshot().undo_label, history.undo_label);
    EXPECT_EQ(session_.scene().root_entities(), roots);
    EXPECT_EQ(session_.scene().parent(child), parent);
  };
  select({other, parent, child}, child);
  rejected();
  select({other, parent, singular}, singular);
  rejected();
  ASSERT_TRUE(editor_.scene().set_selectable(child, false).ok());
  select({parent, other}, other);
  rejected();
}

TEST_F(ParentingCommandTests,
       InspectorChannelEditsRetainCompensationAndUnparentedRootOffset) {
  const EntityId target = create("Target", QuaderEntityKind::Empty,
      kInvalidEntityId, {.location = {4, 3, 2}, .rotation_euler = {0.3F, 0.6F, 0.2F},
                         .scale = {-2, 3, 1}});
  const EntityId child = create("Child", QuaderEntityKind::Empty,
      kInvalidEntityId, {.location = {1, 2, 3}, .rotation_euler = {0.4F, 0.3F, 0.7F}});
  select({child, target}, target);
  ASSERT_TRUE(editor_.scene().parent_selection_to_active().ok());
  const auto offset = session_.scene().transform(child)->parenting_offset;
  auto edited = editor_.query().entity(child)->transform;
  edited.location = {8, 2, 1};
  ASSERT_TRUE(editor_.scene().set_transform(child, edited).ok());
  EXPECT_EQ(session_.scene().transform(child)->parenting_offset.values,
            offset.values);
  select({child}, child);
  const auto world = session_.scene().world_transform_matrix(child);
  ASSERT_TRUE(editor_.scene().unparent_selection().ok());
  expect_matrix(child, world);
  const auto root_offset = session_.scene().transform(child)->parenting_offset;
  edited.scale = {2, 3, 4};
  ASSERT_TRUE(editor_.scene().set_transform(child, edited).ok());
  EXPECT_EQ(session_.scene().transform(child)->parenting_offset.values,
            root_offset.values);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_matrix(child, world);
}

} // namespace
