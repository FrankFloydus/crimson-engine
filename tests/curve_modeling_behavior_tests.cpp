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
       NestedGroupDuplicateKeepsEditableCurveAndMeshBindingsThroughHistory) {
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
  const auto duplicated =
      editor_.commands().execute(quader_command::kDuplicateSelection);
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
       CancelAfterExternalSceneEditsDoesNotRevertTransformVisibilityOrSelection) {
  const auto mesh = create_box(editor_);
  const auto curve = create_curve(editor_);
  const auto original = read_curve(editor_, curve);
  ASSERT_TRUE(editor_.modeling().begin_curve_preview(curve).ok());
  ASSERT_TRUE(editor_.modeling().update_curve_preview(
      with_middle_height(original, 4.0F)).ok());
  QuaderTransform transform;
  transform.position = {7.0F, 2.0F, 1.0F};
  ASSERT_TRUE(editor_.scene().set_transform(curve.entity, transform).ok());
  ASSERT_TRUE(editor_.scene().set_visible(curve.entity, false).ok());
  ASSERT_TRUE(editor_.selection().select_object(mesh).ok());
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
