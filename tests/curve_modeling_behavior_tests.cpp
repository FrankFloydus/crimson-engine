////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "support/curve_modeling_test_support.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <optional>
#include <vector>

namespace {

using namespace quader;
using namespace quader::test::curve;

/// Exercises curve editing and history exclusively through the public editor API.
class CurveModelingBehaviorTests : public testing::Test {
protected:
  CurveModelingBehaviorTests()
      : standalone_(create_standalone_editor()), editor_(standalone_.editor()) {}

  QuaderStandaloneEditor standalone_;
  QuaderEditorApi &editor_;
};

TEST_F(CurveModelingBehaviorTests,
       CreateEditCloseAndHistoryRetainAuthoredIdsAndCurveEntity) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  ASSERT_EQ(original.point_count(), 3U);
  ASSERT_EQ(original.segment_count(), 2U);
  const auto identity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(identity.has_value());
  EXPECT_NE(identity->curve, kInvalidAssetId);
  EXPECT_EQ(editor_.query().assets().curve_count, 1U);
  EXPECT_EQ(editor_.query().assets().mesh_count, 0U);
  ASSERT_EQ(editor_.query().modeling_objects().size(), 1U);
  EXPECT_EQ(editor_.query().modeling_objects().front().mesh, kInvalidAssetId);

  auto point = point_spec(original.points()[1]);
  point.position.y = 3.0F;
  point.incoming_handle = {-1.0F, -0.25F, 0.5F};
  ASSERT_TRUE(editor_.modeling().set_curve_point(
      curve, original.points()[1].id, point).ok());
  const auto edited = read_curve(editor_, curve);
  EXPECT_EQ(edited.points()[1].id, original.points()[1].id);
  EXPECT_FLOAT_EQ(edited.points()[1].incoming_handle.z, 0.5F);
  ASSERT_TRUE(editor_.modeling().set_curve_closed(curve, true).ok());
  const auto closed = read_curve(editor_, curve);
  EXPECT_TRUE(closed.closed());
  EXPECT_EQ(closed.segment_count(), 3U);
  for (std::size_t index = 0; index < original.segment_count(); ++index) {
    EXPECT_EQ(closed.segments()[index], original.segments()[index]);
  }

  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, edited);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, original);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(curve.entity).has_value());
  EXPECT_EQ(editor_.query().assets().curve_count, 0U);
  EXPECT_FALSE(editor_.history().can_undo());

  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, original);
  const auto restored = editor_.query().entity(curve.entity);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->uuid, identity->uuid);
  EXPECT_EQ(restored->editor_object_id, identity->editor_object_id);
  EXPECT_EQ(restored->curve, identity->curve);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, closed);
}

TEST_F(CurveModelingBehaviorTests,
       PointSmoothnessIsOneUndoableEditWithUnchangedSelectionAndIds) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto point = original.points()[1].id;
  const auto selection = selected_entities(editor_);
  const auto active = active_entity(editor_);
  const auto converted = editor_.modeling().toggle_curve_point_smoothness(curve, point);
  ASSERT_TRUE(converted.ok()) << converted.error_message();
  EXPECT_TRUE(converted.changed());
  const auto corner = read_curve(editor_, curve);
  auto expected = original;
  auto spec = point_spec(original.points()[1]);
  spec.incoming_handle = {};
  spec.outgoing_handle = {};
  ASSERT_TRUE(expected.set_point(point, spec).ok());
  EXPECT_TRUE(corner.has_same_geometry(expected));
  EXPECT_EQ(selected_entities(editor_), selection);
  EXPECT_EQ(active_entity(editor_), active);

  ASSERT_TRUE(editor_.modeling().toggle_curve_point_smoothness(curve, point).ok());
  const auto smooth = read_curve(editor_, curve);
  EXPECT_GT(smooth.points()[1].outgoing_handle.x, 0.0f);
  EXPECT_LT(smooth.points()[1].incoming_handle.x, 0.0f);
  EXPECT_EQ(smooth.points()[1].id, point);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, corner);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, original);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, corner);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, smooth);
  EXPECT_EQ(selected_entities(editor_), selection);
  EXPECT_EQ(active_entity(editor_), active);
}

TEST_F(CurveModelingBehaviorTests,
       InvalidAndDegeneratePointConversionsPreserveRevisionsAndRedo) {
  const auto created = editor_.modeling().create_curve({
      .points = {{.position = {1, 2, 3}}, {.position = {1, 2, 3}}},
  });
  ASSERT_TRUE(created.ok());
  const auto curve = created.value();
  ASSERT_TRUE(editor_.modeling().set_curve_closed(curve, true).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto original = read_curve(editor_, curve);
  const auto history = editor_.query().history();
  const auto revisions = editor_.revisions();
  const auto unchanged = editor_.modeling().toggle_curve_point_smoothness(
      curve, original.points()[0].id);
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());
  EXPECT_FALSE(editor_.modeling().toggle_curve_point_smoothness(curve, {}).ok());
  expect_curve(editor_, curve, original);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(read_curve(editor_, curve).closed());
}

TEST_F(CurveModelingBehaviorTests,
       UnchangedAndInvalidEditsLeaveGeometryRevisionsAndHistoryUntouched) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  ASSERT_EQ(original.point_count(), 3U);
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();

  const auto same_point = editor_.modeling().set_curve_point(
      curve, original.points()[1].id, point_spec(original.points()[1]));
  EXPECT_TRUE(same_point.ok());
  EXPECT_FALSE(same_point.changed());
  const auto same_closure = editor_.modeling().set_curve_closed(curve, false);
  EXPECT_TRUE(same_closure.ok());
  EXPECT_FALSE(same_closure.changed());
  const auto invalid = editor_.modeling().replace_curve(curve, {});
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  expect_curve(editor_, curve, original);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
}

TEST_F(CurveModelingBehaviorTests,
       MultiplePreviewUpdatesCommitExactlyOneHistoryEntry) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto first = with_middle_height(original, 2.0F);
  const auto final = with_middle_height(original, 4.0F);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(first).ok());
  expect_curve(editor_, curve, first);
  expect_curve(editor_, curve, original, false);
  ASSERT_TRUE(editor_.modeling().update_curve_preview(final).ok());
  expect_curve(editor_, curve, final);
  expect_curve(editor_, curve, original, false);
  const auto committed = editor_.modeling().commit_curve_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.changed());
  expect_curve(editor_, curve, final, false);

  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, original);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(curve.entity).has_value());
  EXPECT_FALSE(editor_.history().can_undo());
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, final);
  EXPECT_FALSE(editor_.history().can_redo());
}

TEST_F(CurveModelingBehaviorTests,
       PreviewReturnedToOriginalCommitsNoHistoryAndKeepsRedo) {
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto edited = with_middle_height(original, 3.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(curve, edited).ok());
  ASSERT_TRUE(editor_.history().undo().ok());
  const auto revisions = editor_.revisions();
  const auto history = editor_.query().history();

  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 5.0F)).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(original).ok());
  const auto committed = editor_.modeling().commit_curve_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.changed());
  expect_curve(editor_, curve, original);
  expect_same_revisions(editor_.revisions(), revisions);
  expect_same_history(editor_.query().history(), history);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
}

TEST_F(CurveModelingBehaviorTests,
       MixedMeshAndCurveTransformsAndEditsUseOneOrderedHistory) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto mesh_before = editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(mesh_before.has_value());
  const auto edited = with_middle_height(original, 3.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(curve, edited).ok());
  ASSERT_TRUE(editor_.selection().select_objects({mesh, curve}).ok());
  ASSERT_EQ(editor_.selection().summary().selected_object_count, 2U);
  ASSERT_TRUE(editor_.modeling().move_selection({3.0F, 2.0F, -1.0F}).ok());
  for (const auto entity : {mesh.entity, curve.entity}) {
    const auto moved = editor_.query().entity(entity);
    ASSERT_TRUE(moved.has_value());
    EXPECT_FLOAT_EQ(moved->transform.position.x, 3.0F);
    EXPECT_FLOAT_EQ(moved->transform.position.y, 2.0F);
    EXPECT_FLOAT_EQ(moved->transform.position.z, -1.0F);
  }
  expect_curve(editor_, curve, edited);

  EXPECT_EQ(editor_.query().history().undo_label, "Move Selection");
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, edited);
  EXPECT_EQ(editor_.query().history().undo_label, "Select Entities");
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, edited);
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{curve.entity});
  EXPECT_EQ(editor_.query().history().undo_label, "Edit Curve");
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, original);
  for (const auto entity : {mesh.entity, curve.entity}) {
    const auto restored = editor_.query().entity(entity);
    ASSERT_TRUE(restored.has_value());
    EXPECT_FLOAT_EQ(restored->transform.position.x, 0.0F);
    EXPECT_FLOAT_EQ(restored->transform.position.y, 0.0F);
    EXPECT_FLOAT_EQ(restored->transform.position.z, 0.0F);
  }
  const auto mesh_after = editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(mesh_after.has_value());
  expect_same_mesh_geometry(*mesh_after, *mesh_before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(editor_.selection().summary().selected_object_count, 2U);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, curve, edited);
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
}

TEST_F(CurveModelingBehaviorTests,
       DuplicateEditsDetachAndDeleteUndoRestoresCurveIdentity) {
  const auto original = create_curve(editor_);
  const auto document = read_curve(editor_, original);
  ASSERT_TRUE(editor_.selection().select_object(original).ok());
  const auto duplicate =
      editor_.commands().execute(quader_command::kDuplicateSelection);
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  const auto copy = editor_.query().modeling_object_for_entity(
      active_entity(editor_));
  ASSERT_TRUE(copy.has_value());
  EXPECT_NE(copy->modeling_object_id, original.modeling_object_id);
  expect_curve(editor_, *copy, document);
  const auto edited = with_middle_height(document, 8.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(*copy, edited).ok());
  expect_curve(editor_, original, document);
  expect_curve(editor_, *copy, edited);
  const auto source_entity = editor_.query().entity(original.entity);
  const auto copy_entity = editor_.query().entity(copy->entity);
  ASSERT_TRUE(source_entity.has_value());
  ASSERT_TRUE(copy_entity.has_value());
  EXPECT_NE(source_entity->curve, copy_entity->curve);

  ASSERT_TRUE(editor_.selection().select_object(*copy).ok());
  ASSERT_TRUE(editor_.commands().execute(quader_command::kDeleteSelection).ok());
  EXPECT_FALSE(editor_.query().entity(copy->entity).has_value());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, *copy, edited);
  const auto restored = editor_.query().entity(copy->entity);
  ASSERT_TRUE(restored.has_value());
  EXPECT_EQ(restored->uuid, copy_entity->uuid);
  EXPECT_EQ(restored->editor_object_id, copy_entity->editor_object_id);
  EXPECT_EQ(restored->curve, copy_entity->curve);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, *copy, document);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(copy->entity).has_value());
  expect_curve(editor_, original, document);
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, *copy, edited);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_FALSE(editor_.query().entity(copy->entity).has_value());
  expect_curve(editor_, original, document);
}

TEST_F(CurveModelingBehaviorTests,
       ModelingDuplicateMeshesKeepsNestedCurveAndMeshBindingsThroughHistory) {
  const auto mesh = create_box(editor_);
  const auto inner_curve = create_curve(editor_);
  const auto outer_curve = create_curve(editor_);
  ASSERT_TRUE(editor_.modeling().set_curve_closed(outer_curve, true).ok());
  const auto inner_document = read_curve(editor_, inner_curve);
  const auto outer_document = read_curve(editor_, outer_curve);
  ASSERT_TRUE(editor_.selection().select_objects({mesh, inner_curve}).ok());
  const auto inner_group = editor_.scene().group_selection("Inner Kit");
  ASSERT_TRUE(inner_group.ok()) << inner_group.error_message();
  ASSERT_TRUE(editor_.scene().select(
      {inner_group.value().id, outer_curve.entity}, inner_group.value().id).ok());
  const auto outer_group = editor_.scene().group_selection("Outer Kit");
  ASSERT_TRUE(outer_group.ok()) << outer_group.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor_.scene().select(outer_group.value().id).ok());
  const auto duplicated = editor_.modeling().duplicate_meshes();
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  const EntityId copy_root = active_entity(editor_);
  ASSERT_NE(copy_root, outer_group.value().id);
  ASSERT_NE(copy_root, kInvalidEntityId);

  std::optional<QuaderEntitySnapshot> copy_inner_group;
  std::optional<QuaderModelingObjectRef> copy_outer_curve;
  for (const auto &entity : editor_.query().entities()) {
    if (entity.parent != copy_root) {
      continue;
    }
    if (entity.has_curve) {
      copy_outer_curve = editor_.query().modeling_object_for_entity(entity.id);
    } else if (!entity.has_modeling_object) {
      copy_inner_group = entity;
    }
  }
  ASSERT_TRUE(copy_inner_group.has_value());
  ASSERT_TRUE(copy_outer_curve.has_value());
  EXPECT_NE(copy_outer_curve->modeling_object_id, outer_curve.modeling_object_id);
  expect_curve(editor_, *copy_outer_curve, outer_document);

  std::optional<QuaderModelingObjectRef> copy_inner_curve;
  std::optional<QuaderModelingObjectRef> copy_mesh;
  for (const auto &entity : editor_.query().entities()) {
    if (entity.parent != copy_inner_group->id) {
      continue;
    }
    if (entity.has_curve) {
      copy_inner_curve = editor_.query().modeling_object_for_entity(entity.id);
    } else if (entity.has_mesh) {
      copy_mesh = editor_.query().modeling_object_for_entity(entity.id);
    }
  }
  ASSERT_TRUE(copy_inner_curve.has_value());
  ASSERT_TRUE(copy_mesh.has_value());
  EXPECT_NE(copy_inner_curve->modeling_object_id, inner_curve.modeling_object_id);
  EXPECT_NE(copy_inner_curve->modeling_object_id,
            copy_outer_curve->modeling_object_id);
  EXPECT_NE(copy_mesh->modeling_object_id, mesh.modeling_object_id);
  EXPECT_EQ(editor_.query().modeling_objects().size(), 6U);
  expect_curve(editor_, *copy_inner_curve, inner_document);
  const auto source_mesh = editor_.query().compiled_object_mesh(mesh);
  const auto cloned_mesh = editor_.query().compiled_object_mesh(*copy_mesh);
  ASSERT_TRUE(source_mesh.has_value());
  ASSERT_TRUE(cloned_mesh.has_value());
  expect_same_mesh_geometry(*cloned_mesh, *source_mesh);

  const auto edited = with_middle_height(inner_document, 9.0F);
  ASSERT_TRUE(editor_.modeling().replace_curve(*copy_inner_curve, edited).ok());
  expect_curve(editor_, inner_curve, inner_document);
  expect_curve(editor_, outer_curve, outer_document);
  expect_curve(editor_, *copy_outer_curve, outer_document);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, *copy_inner_curve, inner_document);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(copy_root).has_value());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 3U);
  expect_curve(editor_, inner_curve, inner_document);
  expect_curve(editor_, outer_curve, outer_document);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, *copy_inner_curve, inner_document);
  expect_curve(editor_, *copy_outer_curve, outer_document);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_curve(editor_, *copy_inner_curve, edited);
  expect_curve(editor_, inner_curve, inner_document);
  const auto restored_curve = editor_.query().entity(copy_inner_curve->entity);
  ASSERT_TRUE(restored_curve.has_value());
  EXPECT_EQ(restored_curve->parent, copy_inner_group->id);
}

TEST_F(CurveModelingBehaviorTests,
       DirectMixedHierarchyLifecyclePreservesBindingsAndUnrelatedSelection) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto curve_document = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.selection().select_objects({mesh, curve}).ok());
  const auto group = editor_.scene().group_selection("Direct Mixed Kit");
  ASSERT_TRUE(group.ok()) << group.error_message();

  const auto unrelated = create_curve(editor_);
  ASSERT_EQ(selected_entities(editor_),
            std::vector<EntityId>{unrelated.entity});
  const auto duplicated = editor_.scene().duplicate(group.value().id);
  ASSERT_TRUE(duplicated.ok()) << duplicated.error_message();
  ASSERT_TRUE(duplicated.changed());
  const EntityId copy_root = duplicated.value().id;
  ASSERT_NE(copy_root, group.value().id);
  EXPECT_EQ(editor_.query().history().undo_label, "Duplicate Entity");

  std::optional<QuaderModelingObjectRef> copy_curve;
  std::optional<QuaderModelingObjectRef> copy_mesh;
  for (const auto &entity : editor_.query().entities()) {
    if (entity.parent != copy_root) {
      continue;
    }
    if (entity.has_curve) {
      copy_curve = editor_.query().modeling_object_for_entity(entity.id);
    } else if (entity.has_mesh) {
      copy_mesh = editor_.query().modeling_object_for_entity(entity.id);
    }
  }
  ASSERT_TRUE(copy_curve.has_value());
  ASSERT_TRUE(copy_mesh.has_value());
  EXPECT_NE(copy_curve->modeling_object_id, curve.modeling_object_id);
  EXPECT_NE(copy_mesh->modeling_object_id, mesh.modeling_object_id);
  expect_curve(editor_, *copy_curve, curve_document);
  ASSERT_TRUE(editor_.query().compiled_object_mesh(*copy_mesh).has_value());

  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(copy_root).has_value());
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{unrelated.entity});
  expect_curve(editor_, curve, curve_document);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(editor_.query().entity(copy_root).has_value());
  expect_curve(editor_, *copy_curve, curve_document);

  ASSERT_TRUE(editor_.selection().select_object(unrelated).ok());
  const auto destroyed = editor_.scene().destroy(group.value().id);
  ASSERT_TRUE(destroyed.ok()) << destroyed.error_message();
  ASSERT_TRUE(destroyed.changed());
  EXPECT_EQ(editor_.query().history().undo_label, "Destroy Entity");
  EXPECT_FALSE(editor_.query().entity(group.value().id).has_value());
  EXPECT_FALSE(editor_.query().entity(mesh.entity).has_value());
  EXPECT_FALSE(editor_.query().entity(curve.entity).has_value());
  EXPECT_TRUE(editor_.query().entity(copy_root).has_value());
  expect_curve(editor_, *copy_curve, curve_document);
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{unrelated.entity});

  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_TRUE(editor_.query().entity(group.value().id).has_value());
  EXPECT_TRUE(editor_.query().entity(mesh.entity).has_value());
  expect_curve(editor_, curve, curve_document);
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{unrelated.entity});
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_FALSE(editor_.query().entity(group.value().id).has_value());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_curve(editor_, curve, curve_document);
}

TEST_F(CurveModelingBehaviorTests,
       DirectStructuralPreviewMovesMixedHierarchyAndReplaysExactIdentity) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto source_curve = read_curve(editor_, curve);
  const auto source_mesh = editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(source_mesh.has_value());
  ASSERT_TRUE(editor_.selection().select_objects({mesh, curve}).ok());
  const auto group = editor_.scene().group_selection("Preview Mixed Kit");
  ASSERT_TRUE(group.ok()) << group.error_message();
  const auto generic = editor_.scene().create_entity({
      .name = "Generic Marker",
      .kind = QuaderEntityKind::Generic,
      .parent = group.value().id,
      .transform = {.position = {0.0F, 4.0F, 0.0F}},
  });
  ASSERT_TRUE(generic.ok()) << generic.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor_.scene().select(group.value().id).ok());

  const auto children_of = [&](EntityId parent) {
    std::vector<QuaderEntitySnapshot> children;
    for (const QuaderEntitySnapshot &entity : editor_.query().entity_tree()) {
      if (entity.parent == parent) {
        children.push_back(entity);
      }
    }
    return children;
  };
  const std::vector<QuaderEntitySnapshot> source_children =
      children_of(group.value().id);
  ASSERT_EQ(source_children.size(), 3U);

  const auto begun = editor_.modeling().begin_transform_preview(true);
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  ASSERT_TRUE(begun.changed());
  const EntityId copy_root = active_entity(editor_);
  ASSERT_NE(copy_root, kInvalidEntityId);
  ASSERT_NE(copy_root, group.value().id);
  const auto copy_root_at_begin = editor_.query().entity(copy_root);
  ASSERT_TRUE(copy_root_at_begin.has_value());
  const std::vector<QuaderEntitySnapshot> copy_children_at_begin =
      children_of(copy_root);
  ASSERT_EQ(copy_children_at_begin.size(), source_children.size());

  std::optional<QuaderModelingObjectRef> copy_mesh;
  std::optional<QuaderModelingObjectRef> copy_curve;
  std::optional<QuaderEntitySnapshot> copy_generic;
  std::vector<EntityId> copy_child_order;
  for (const QuaderEntitySnapshot &child : copy_children_at_begin) {
    copy_child_order.push_back(child.id);
    if (child.has_mesh) {
      copy_mesh = editor_.query().modeling_object_for_entity(child.id);
    } else if (child.has_curve) {
      copy_curve = editor_.query().modeling_object_for_entity(child.id);
    } else {
      copy_generic = child;
    }
  }
  ASSERT_TRUE(copy_mesh.has_value());
  ASSERT_TRUE(copy_curve.has_value());
  ASSERT_TRUE(copy_generic.has_value());
  const auto copy_mesh_geometry =
      editor_.query().compiled_object_mesh(*copy_mesh);
  ASSERT_TRUE(copy_mesh_geometry.has_value());
  expect_same_mesh_geometry(*copy_mesh_geometry, *source_mesh);
  expect_curve(editor_, *copy_curve, source_curve);

  const QVec3 delta{3.0F, 2.0F, -1.0F};
  const auto updated = editor_.modeling().update_transform_translate(delta);
  ASSERT_TRUE(updated.ok()) << updated.error_message();
  ASSERT_TRUE(updated.changed());
  const auto moved_root = editor_.query().entity(copy_root);
  ASSERT_TRUE(moved_root.has_value());
  EXPECT_FLOAT_EQ(moved_root->transform.position.x,
                  copy_root_at_begin->transform.position.x + delta.x);
  EXPECT_FLOAT_EQ(moved_root->transform.position.y,
                  copy_root_at_begin->transform.position.y + delta.y);
  EXPECT_FLOAT_EQ(moved_root->transform.position.z,
                  copy_root_at_begin->transform.position.z + delta.z);
  const std::vector<QuaderEntitySnapshot> moved_children =
      children_of(copy_root);
  ASSERT_EQ(moved_children.size(), copy_children_at_begin.size());
  for (std::size_t index = 0; index < moved_children.size(); ++index) {
    EXPECT_EQ(moved_children[index].id, copy_children_at_begin[index].id);
    EXPECT_FLOAT_EQ(moved_children[index].transform.position.x,
                    copy_children_at_begin[index].transform.position.x);
    EXPECT_FLOAT_EQ(moved_children[index].transform.position.y,
                    copy_children_at_begin[index].transform.position.y);
    EXPECT_FLOAT_EQ(moved_children[index].transform.position.z,
                    copy_children_at_begin[index].transform.position.z);
  }
  const auto moved_mesh = editor_.query().compiled_object_mesh(*copy_mesh);
  ASSERT_TRUE(moved_mesh.has_value());
  expect_same_mesh_geometry(*moved_mesh, *copy_mesh_geometry);
  expect_curve(editor_, *copy_curve, source_curve);

  const auto committed = editor_.modeling().commit_transform_preview();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.changed());
  EXPECT_EQ(editor_.query().history().undo_label,
            "Commit Transform Preview");
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(copy_root).has_value());
  for (const EntityId child : copy_child_order) {
    EXPECT_FALSE(editor_.query().entity(child).has_value());
  }
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{group.value().id});
  EXPECT_TRUE(editor_.query().entity(generic.value().id).has_value());
  expect_curve(editor_, curve, source_curve);
  const auto source_mesh_after_undo =
      editor_.query().compiled_object_mesh(mesh);
  ASSERT_TRUE(source_mesh_after_undo.has_value());
  expect_same_mesh_geometry(*source_mesh_after_undo, *source_mesh);

  ASSERT_TRUE(editor_.history().redo().ok());
  const auto copy_root_after_redo = editor_.query().entity(copy_root);
  ASSERT_TRUE(copy_root_after_redo.has_value());
  EXPECT_EQ(copy_root_after_redo->uuid, copy_root_at_begin->uuid);
  EXPECT_EQ(copy_root_after_redo->editor_object_id,
            copy_root_at_begin->editor_object_id);
  EXPECT_FLOAT_EQ(copy_root_after_redo->transform.position.x,
                  moved_root->transform.position.x);
  const std::vector<QuaderEntitySnapshot> copy_children_after_redo =
      children_of(copy_root);
  ASSERT_EQ(copy_children_after_redo.size(), copy_children_at_begin.size());
  for (std::size_t index = 0; index < copy_children_after_redo.size(); ++index) {
    const QuaderEntitySnapshot &restored = copy_children_after_redo[index];
    const QuaderEntitySnapshot &captured = copy_children_at_begin[index];
    EXPECT_EQ(restored.id, captured.id);
    EXPECT_EQ(restored.uuid, captured.uuid);
    EXPECT_EQ(restored.editor_object_id, captured.editor_object_id);
    EXPECT_EQ(restored.modeling_object_id, captured.modeling_object_id);
    EXPECT_EQ(restored.parent, copy_root);
  }
  expect_curve(editor_, *copy_curve, source_curve);
  const auto restored_mesh = editor_.query().compiled_object_mesh(*copy_mesh);
  ASSERT_TRUE(restored_mesh.has_value());
  expect_same_mesh_geometry(*restored_mesh, *copy_mesh_geometry);

  ASSERT_TRUE(editor_.history().undo().ok());
  const QuaderHistorySnapshot history_before_cancel = editor_.query().history();
  const auto cancel_begin = editor_.modeling().begin_transform_preview(true);
  ASSERT_TRUE(cancel_begin.ok()) << cancel_begin.error_message();
  const EntityId canceled_root = active_entity(editor_);
  ASSERT_NE(canceled_root, group.value().id);
  const std::vector<QuaderEntitySnapshot> canceled_children =
      children_of(canceled_root);
  ASSERT_EQ(canceled_children.size(), source_children.size());
  ASSERT_TRUE(editor_.modeling().update_transform_translate(delta).ok());
  const auto canceled = editor_.modeling().cancel_transform_preview();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  ASSERT_TRUE(canceled.changed());
  EXPECT_FALSE(editor_.query().entity(canceled_root).has_value());
  for (const QuaderEntitySnapshot &child : canceled_children) {
    EXPECT_FALSE(editor_.query().entity(child.id).has_value());
  }
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{group.value().id});
  expect_same_history(editor_.query().history(), history_before_cancel);
  EXPECT_EQ(children_of(group.value().id).size(), source_children.size());
  expect_curve(editor_, curve, source_curve);
}

TEST_F(CurveModelingBehaviorTests,
       MixedHierarchyDragDuplicateUsesOneCancelableStructuralTransaction) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto curve_document = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.selection().select_objects({mesh, curve}).ok());
  const auto group = editor_.scene().group_selection("Drag Mixed Kit");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor_.scene().select(group.value().id).ok());

  const QuaderViewportCamera camera{
      .orthographic = true,
      .origin = {0.0F, 0.0F, 10.0F},
      .x_axis = {1.0F, 0.0F, 0.0F},
      .y_axis = {0.0F, 1.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 1.0F},
      .viewport_size = {100.0F, 100.0F},
      .orthographic_size = 100.0,
      .fov_degrees = 60.0,
  };
  const QuaderRay ray{
      .origin = {0.0F, 0.0F, 10.0F},
      .direction = {0.0F, 0.0F, -1.0F},
  };
  const auto begin = editor_.modeling().begin_transform_drag({
      .tool = QuaderTransformTool::Move,
      .operation = QuaderTransformOperation::Transform,
      .axis = QuaderTransformAxis::X,
      .screen_position = {0.0F, 0.0F},
      .screen_pivot = {0.0F, 0.0F},
      .screen_tip = {10.0F, 0.0F},
      .has_world_direction = true,
      .world_direction = {1.0F, 0.0F, 0.0F},
      .ray = ray,
      .camera = camera,
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .duplicate_selected = true,
  });
  ASSERT_TRUE(begin.ok()) << begin.error_message();
  ASSERT_TRUE(begin.value().active);
  EXPECT_FALSE(begin.value().preview_driven);
  EXPECT_TRUE(begin.value().scene_transform);
  ASSERT_EQ(selected_entities(editor_).size(), 1U);
  const EntityId copy_root = active_entity(editor_);
  ASSERT_NE(copy_root, group.value().id);
  ASSERT_NE(copy_root, kInvalidEntityId);

  std::optional<QuaderModelingObjectRef> copy_curve;
  std::optional<QuaderModelingObjectRef> copy_mesh;
  for (const auto &entity : editor_.query().entities()) {
    if (entity.parent != copy_root) {
      continue;
    }
    if (entity.has_curve) {
      copy_curve = editor_.query().modeling_object_for_entity(entity.id);
    } else if (entity.has_mesh) {
      copy_mesh = editor_.query().modeling_object_for_entity(entity.id);
    }
  }
  ASSERT_TRUE(copy_curve.has_value());
  ASSERT_TRUE(copy_mesh.has_value());
  expect_curve(editor_, *copy_curve, curve_document);
  ASSERT_TRUE(editor_.query().compiled_object_mesh(*copy_mesh).has_value());

  const auto update = editor_.modeling().update_transform_drag({
      .screen_position = {2.0F, 0.0F},
      .ray = ray,
      .camera = camera,
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
  });
  ASSERT_TRUE(update.ok()) << update.error_message();
  ASSERT_TRUE(update.value().changed);
  const auto committed = editor_.modeling().commit_transform_drag();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  ASSERT_TRUE(committed.value().changed);
  EXPECT_EQ(editor_.query().history().undo_label, "Duplicate Selection");

  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(copy_root).has_value());
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{group.value().id});
  expect_curve(editor_, curve, curve_document);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(editor_.query().entity(copy_root).has_value());
  expect_curve(editor_, *copy_curve, curve_document);
  ASSERT_TRUE(editor_.query().compiled_object_mesh(*copy_mesh).has_value());

  const auto repeated =
      editor_.modeling().execute_tool("repeat_last_action");
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  EXPECT_EQ(editor_.query().history().undo_label, "Repeat Last Action");
  const EntityId repeated_root = active_entity(editor_);
  ASSERT_NE(repeated_root, copy_root);
  ASSERT_NE(repeated_root, group.value().id);
  std::optional<QuaderModelingObjectRef> repeated_curve;
  std::optional<QuaderModelingObjectRef> repeated_mesh;
  for (const auto &entity : editor_.query().entities()) {
    if (entity.parent != repeated_root) {
      continue;
    }
    if (entity.has_curve) {
      repeated_curve = editor_.query().modeling_object_for_entity(entity.id);
    } else if (entity.has_mesh) {
      repeated_mesh = editor_.query().modeling_object_for_entity(entity.id);
    }
  }
  ASSERT_TRUE(repeated_curve.has_value());
  ASSERT_TRUE(repeated_mesh.has_value());
  expect_curve(editor_, *repeated_curve, curve_document);
  ASSERT_TRUE(editor_.query().compiled_object_mesh(*repeated_mesh).has_value());

  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(repeated_root).has_value());
  EXPECT_EQ(selected_entities(editor_), std::vector<EntityId>{copy_root});
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(editor_.query().entity(repeated_root).has_value());
  expect_curve(editor_, *repeated_curve, curve_document);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(repeated_root).has_value());

  ASSERT_TRUE(editor_.history().undo().ok());
  const QuaderHistorySnapshot history_before_cancel = editor_.query().history();
  const auto cancel_begin = editor_.modeling().begin_transform_drag({
      .tool = QuaderTransformTool::Move,
      .operation = QuaderTransformOperation::Transform,
      .axis = QuaderTransformAxis::X,
      .screen_position = {0.0F, 0.0F},
      .screen_pivot = {0.0F, 0.0F},
      .screen_tip = {10.0F, 0.0F},
      .has_world_direction = true,
      .world_direction = {1.0F, 0.0F, 0.0F},
      .ray = ray,
      .camera = camera,
      .grid_size = 1.0,
      .vertex_snap_radius = 0.0,
      .grid_snap_enabled = false,
      .duplicate_selected = true,
  });
  ASSERT_TRUE(cancel_begin.ok()) << cancel_begin.error_message();
  const EntityId canceled_root = active_entity(editor_);
  ASSERT_NE(canceled_root, group.value().id);
  const auto canceled = editor_.modeling().cancel_transform_drag();
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  ASSERT_TRUE(canceled.changed());
  EXPECT_FALSE(editor_.query().entity(canceled_root).has_value());
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{group.value().id});
  expect_same_history(editor_.query().history(), history_before_cancel);
  expect_curve(editor_, curve, curve_document);
}

TEST_F(CurveModelingBehaviorTests,
       CancelAfterExternalSceneEditsDoesNotRevertTransformVisibilityOrSelection) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  const auto authored = with_middle_height(original, 4.0F);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(authored).ok());
  QuaderTransform transform;
  transform.position = {7.0F, 2.0F, 1.0F};
  ASSERT_TRUE(editor_.scene().set_transform(curve.entity, transform).ok());
  ASSERT_TRUE(editor_.scene().set_visible(curve.entity, false).ok());
  const auto selected = editor_.selection().select_object(mesh);
  ASSERT_TRUE(selected.ok()) << selected.error_message();
  expect_curve(editor_, curve, authored);
  const auto continued = with_middle_height(original, 5.0F);
  const auto continued_update =
      editor_.modeling().update_curve_preview(continued);
  ASSERT_TRUE(continued_update.ok()) << continued_update.error_message();
  expect_curve(editor_, curve, continued);
  ASSERT_TRUE(editor_.modeling().cancel_curve_preview().ok());
  expect_curve(editor_, curve, original);
  const auto entity = editor_.query().entity(curve.entity);
  ASSERT_TRUE(entity.has_value());
  EXPECT_FALSE(entity->visible);
  EXPECT_FLOAT_EQ(entity->transform.position.x, 7.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.y, 2.0F);
  EXPECT_FLOAT_EQ(entity->transform.position.z, 1.0F);
  EXPECT_EQ(active_entity(editor_), mesh.entity);
  EXPECT_EQ(selected_entities(editor_),
            std::vector<EntityId>{mesh.entity});
}

} // namespace
