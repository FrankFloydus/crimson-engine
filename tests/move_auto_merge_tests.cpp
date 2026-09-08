////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modeling/services/modeling_sdk_projection_adapter.hpp"
#include "scene/scene_document.hpp"
#include "support/modeling_test_events.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <gtest/gtest.h>

#include <array>

namespace quader {
namespace {

/** Keeps two disconnected triangles and their authored selection identities. */
class MoveAutoMergeTests : public ::testing::Test {
protected:
  void create(float target_x = 0.0F,
              ModelingSelectionMode mode = ModelingSelectionMode::Vertex) {
    quader_poly::Document document;
    source_ = quader_poly::add_vertex(document, {0.0F, 0.0F, 0.0F});
    edge_end_ = quader_poly::add_vertex(document, {0.0F, -2.0F, 0.0F});
    const auto a = quader_poly::add_vertex(document, {-1.0F, -2.0F, 0.0F});
    target_ = quader_poly::add_vertex(document, {target_x, 0.0F, 0.0F});
    const auto b = quader_poly::add_vertex(document, {2.0F, 2.0F, 0.0F});
    const auto c = quader_poly::add_vertex(document, {1.0F, 2.0F, 0.0F});
    face_ = quader_poly::add_face(document, std::array{source_, edge_end_, a});
    ASSERT_NE(face_, quader_poly::kInvalidElementId);
    ASSERT_NE(quader_poly::add_face(document, std::array{target_, b, c}),
              quader_poly::kInvalidElementId);
    const auto created = modeling_.create_mesh(
        {.document = std::move(document), .name = "Move Auto Merge"});
    ASSERT_TRUE(created.ok) << created.message;
    object_ = created.object;
    ModelingComponentSelectionRequest selection{
        .modeling_object_id = object_.modeling_object_id, .mode = mode};
    if (mode == ModelingSelectionMode::Vertex) {
      selection.vertices = {static_cast<int>(source_)};
      selection.active_kind = ModelingActiveSelectionKind::Vertex;
      selection.active_vertex = static_cast<int>(source_);
    } else if (mode == ModelingSelectionMode::Edge) {
      selection.edges = {{static_cast<int>(source_), static_cast<int>(edge_end_)}};
      selection.active_kind = ModelingActiveSelectionKind::Edge;
      selection.active_edge = selection.edges.front();
    } else {
      selection.faces = {static_cast<int>(face_)};
      selection.active_kind = ModelingActiveSelectionKind::Face;
      selection.active_face = static_cast<int>(face_);
    }
    const auto selected = modeling_.set_component_selection(selection);
    ASSERT_TRUE(selected.ok) << selected.message;
  }

  void set_auto_merge(bool enabled) {
    auto settings = modeling_.interaction_settings();
    settings.move_auto_merge_vertices = enabled;
    const auto set = modeling_.set_interaction_settings(settings);
    ASSERT_TRUE(set.ok) << set.message;
  }

  void begin() {
    auto request = test::x_move_drag_begin();
    request.grid_snap_enabled = false;
    const auto begun = modeling_.begin_transform_drag(request);
    ASSERT_TRUE(begun.ok) << begun.message;
  }

  void move_one_unit() {
    auto request = test::x_move_drag_update(60.0F);
    request.ray.origin.x = 1.0F;
    request.grid_snap_enabled = false;
    const auto updated = modeling_.update_transform_drag(request);
    ASSERT_TRUE(updated.ok) << updated.message;
    ASSERT_NEAR(updated.move_delta.x, 1.0F, 0.00001F);
  }

  [[nodiscard]] quader_poly::Document document() const {
    const auto *binding = scene_.mesh(object_.entity);
    if (binding == nullptr) {
      ADD_FAILURE() << "Missing scene mesh binding.";
      return {};
    }
    const auto *mesh = assets_.mesh(binding->mesh);
    if (mesh == nullptr || !mesh->authored_polygon_document) {
      ADD_FAILURE() << "Missing authored mesh document.";
      return {};
    }
    return *mesh->authored_polygon_document;
  }

  SceneDocument scene_;
  AssetRegistry assets_;
  ModelingSessionService modeling_{scene_, assets_};
  EditorHistoryService history_;
  ModelingObjectRecord object_;
  quader_poly::ElementId source_ = quader_poly::kInvalidElementId;
  quader_poly::ElementId target_ = quader_poly::kInvalidElementId;
  quader_poly::ElementId edge_end_ = quader_poly::kInvalidElementId;
  quader_poly::ElementId face_ = quader_poly::kInvalidElementId;
};

TEST_F(MoveAutoMergeTests, MoveAndWeldCommitAsOneUndoStepAndPreserveTarget) {
  create(1.0F);
  ASSERT_TRUE(modeling_.interaction_settings().move_auto_merge_vertices);
  begin();
  const auto scene_revision = scene_.revision();
  const auto asset_revision = assets_.revision();
  move_one_unit();
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(scene_.revision(), scene_revision);
  EXPECT_EQ(assets_.revision(), asset_revision);
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_TRUE(committed.changed);
  EXPECT_EQ(history_.undo_count(), 1U);
  EXPECT_EQ(document().vertices.size(), 5U);
  const auto exported = modeling_.export_selection();
  ASSERT_EQ(exported.objects.size(), 1U);
  ASSERT_EQ(exported.objects.front().selected_vertices.size(), 1U);
  EXPECT_EQ(exported.objects.front().selected_vertices.front().id, target_);
  EXPECT_FLOAT_EQ(exported.objects.front().selected_vertices.front().position.x,
                  1.0F);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.export_selection().objects.front().selected_vertices.front().id,
            source_);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_EQ(modeling_.export_selection().objects.front().selected_vertices.front().id,
            target_);
}

TEST_F(MoveAutoMergeTests, AcceptedZeroDistanceWeldsAndCapturesSettingForRepeat) {
  create();
  begin();
  set_auto_merge(false);
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_TRUE(committed.changed);
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_EQ(history_.undo_count(), 1U);
  ASSERT_TRUE(history_.undo().ok);
  ASSERT_EQ(document().vertices.size(), 6U);
  const auto repeated = modeling_.repeat_last_action();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_TRUE(repeated.changed);
  EXPECT_EQ(document().vertices.size(), 5U);
}

TEST_F(MoveAutoMergeTests, MixedMeshMoveKeepsUnweldedParticipantsInOneHistoryStep) {
  create(1.0F);
  quader_poly::Document other_document;
  const auto a = quader_poly::add_vertex(other_document, {10.0F, 0.0F, 0.0F});
  const auto b = quader_poly::add_vertex(other_document, {10.0F, -2.0F, 0.0F});
  const auto c = quader_poly::add_vertex(other_document, {9.0F, -2.0F, 0.0F});
  ASSERT_NE(quader_poly::add_face(other_document, std::array{a, b, c}),
            quader_poly::kInvalidElementId);
  const auto other = modeling_.create_mesh(
      {.document = std::move(other_document), .name = "Unwelded Move Participant"});
  ASSERT_TRUE(other.ok) << other.message;
  const std::array<ModelingComponentSelectionRequest, 2> selections{
      ModelingComponentSelectionRequest{
          .modeling_object_id = object_.modeling_object_id,
          .mode = ModelingSelectionMode::Vertex,
          .vertices = {static_cast<int>(source_)}},
      ModelingComponentSelectionRequest{
          .modeling_object_id = other.object.modeling_object_id,
          .mode = ModelingSelectionMode::Vertex,
          .vertices = {static_cast<int>(a)}}};
  ASSERT_TRUE(modeling_.set_component_selections(selections).ok);
  const auto other_x = [&] {
    const auto exported = modeling_.export_selection();
    for (const auto &object : exported.objects) {
      if (object.modeling_object_id == other.object.modeling_object_id &&
          !object.selected_vertices.empty()) {
        return object.selected_vertices.front().position.x;
      }
    }
    ADD_FAILURE() << "Unwelded participant selection was lost.";
    return 0.0F;
  };
  begin();
  move_one_unit();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_FLOAT_EQ(other_x(), 11.0F);
  EXPECT_EQ(history_.undo_count(), 1U);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_FLOAT_EQ(other_x(), 10.0F);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_FLOAT_EQ(other_x(), 11.0F);
}

TEST_F(MoveAutoMergeTests, DisabledAtPressRemainsDisabledAndNoOpHasNoHistory) {
  create();
  set_auto_merge(false);
  begin();
  set_auto_merge(true);
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_FALSE(committed.changed);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(history_.undo_count(), 0U);
}

TEST_F(MoveAutoMergeTests, UndoKeepsAllocatorHighWaterAfterLaterTopologyEdits) {
  create();
  begin();
  ASSERT_TRUE(modeling_.commit_transform_drag(history_).ok);
  const auto merged = document();
  auto later = merged;
  const auto allocated = quader_poly::add_vertex(later, {20.0F, 0.0F, 0.0F});
  ASSERT_NE(allocated, quader_poly::kInvalidElementId);
  ASSERT_TRUE(modeling_.replace_mesh_document(
      object_.modeling_object_id, std::move(later), true).ok);
  ASSERT_TRUE(modeling_.replace_mesh_document(
      object_.modeling_object_id, merged, true).ok);
  const auto undone = history_.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_GT(document().next_vertex_id, allocated);
  const auto redone = history_.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_GT(document().next_vertex_id, allocated);
}

TEST_F(MoveAutoMergeTests, NonmanifoldWeldRedoPreservesLaterAllocatorHighWater) {
  quader_poly::Document source;
  const auto a = quader_poly::add_vertex(source, {0.0F, 0.0F, 0.0F});
  const auto b = quader_poly::add_vertex(source, {1.0F, 0.0F, 0.0F});
  const auto c = quader_poly::add_vertex(source, {0.0F, 1.0F, 0.0F});
  const auto d = quader_poly::add_vertex(source, {0.0F, -1.0F, 0.0F});
  const auto duplicate_a = quader_poly::add_vertex(source, {0.0F, 0.0F, 0.0F});
  const auto duplicate_b = quader_poly::add_vertex(source, {1.0F, 0.0F, 0.0F});
  const auto e = quader_poly::add_vertex(source, {0.0F, 0.0F, 1.0F});
  ASSERT_NE(quader_poly::add_face(source, std::array{a, b, c}),
            quader_poly::kInvalidElementId);
  ASSERT_NE(quader_poly::add_face(source, std::array{b, a, d}),
            quader_poly::kInvalidElementId);
  ASSERT_NE(quader_poly::add_face(source, std::array{duplicate_a, duplicate_b, e}),
            quader_poly::kInvalidElementId);
  const auto created = modeling_.create_mesh(
      {.document = std::move(source), .name = "Nonmanifold Auto Merge History"});
  ASSERT_TRUE(created.ok) << created.message;
  object_ = created.object;
  const auto selected = modeling_.set_component_selection({
      .modeling_object_id = object_.modeling_object_id,
      .mode = ModelingSelectionMode::Vertex,
      .vertices = {static_cast<int>(duplicate_a), static_cast<int>(duplicate_b)},
      .active_kind = ModelingActiveSelectionKind::Vertex,
      .active_vertex = static_cast<int>(duplicate_b),
  });
  ASSERT_TRUE(selected.ok) << selected.message;
  begin();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_EQ(document().vertices.size(), 5U);
  ASSERT_EQ(document().faces.size(), 3U);
  ASSERT_TRUE(history_.undo().ok);
  ASSERT_EQ(document().vertices.size(), 7U);

  const auto before = document();
  auto later = before;
  const auto allocated = quader_poly::add_vertex(later, {20.0F, 0.0F, 0.0F});
  ASSERT_TRUE(modeling_.replace_mesh_document(
      object_.modeling_object_id, std::move(later), true).ok);
  ASSERT_TRUE(modeling_.replace_mesh_document(
      object_.modeling_object_id, before, true).ok);
  const auto redone = history_.redo();
  ASSERT_TRUE(redone.ok) << redone.message;
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_EQ(document().faces.size(), 3U);
  EXPECT_GT(document().next_vertex_id, allocated);
  EXPECT_EQ(modeling_.selection_summary().selected_vertex_count, 2U);
  const auto undone = history_.undo();
  ASSERT_TRUE(undone.ok) << undone.message;
  EXPECT_EQ(document().vertices.size(), 7U);
  EXPECT_GT(document().next_vertex_id, allocated);
}

TEST_F(MoveAutoMergeTests, CancellationNeverWelds) {
  create(1.0F);
  begin();
  move_one_unit();
  const auto canceled = modeling_.cancel_transform_drag();
  ASSERT_TRUE(canceled.ok) << canceled.message;
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(history_.undo_count(), 0U);
  EXPECT_FLOAT_EQ(modeling_.export_selection()
                      .objects.front().selected_vertices.front().position.x,
                  0.0F);
}

TEST_F(MoveAutoMergeTests, GenericTranslationsDoNotApplyTheMoveToolOption) {
  create(1.0F);
  ASSERT_TRUE(modeling_.move_selection({1.0F, 0.0F, 0.0F}).ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  ASSERT_TRUE(modeling_.begin_transform_preview(false).ok);
  ASSERT_TRUE(modeling_.update_transform_translate_preview({}).ok);
  const auto committed = modeling_.commit_transform_preview();
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(document().vertices.size(), 6U);
  const auto exported = modeling_.export_selection();
  ASSERT_EQ(exported.objects.size(), 1U);
  ASSERT_EQ(exported.objects.front().selected_vertices.size(), 1U);
  EXPECT_EQ(exported.objects.front().selected_vertices.front().id, source_);
  EXPECT_FLOAT_EQ(exported.objects.front().selected_vertices.front().position.x,
                  1.0F);
}

TEST_F(MoveAutoMergeTests, EdgeModeWeldsDerivedVerticesAndPreservesSelectionMode) {
  create(0.0F, ModelingSelectionMode::Edge);
  begin();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_EQ(modeling_.selection_summary().selected_edge_count, 1U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Edge);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_edge_count, 1U);
}

TEST_F(MoveAutoMergeTests, FaceModeWeldsDerivedVerticesAndPreservesSelectionMode) {
  create(0.0F, ModelingSelectionMode::Face);
  begin();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(document().vertices.size(), 5U);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Face);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
}

TEST_F(MoveAutoMergeTests, ExtrudedCapSelectionSurvivesWeldIntoDuplicateSidewall) {
  const auto box = modeling_.create_box_from_bounds({
      .bounds = {.min = {-1.0F, -1.0F, -1.0F}, .max = {1.0F, 1.0F, 1.0F}},
      .name = "Extruded Cap Auto Merge",
  });
  ASSERT_TRUE(box.ok) << box.message;
  object_ = box.object;
  ASSERT_TRUE(modeling_.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_EQ(modeling_.pick_select(test::pick_at({0.0F, 0.0F, 5.0F}))
                .selected_face_count,
            1U);
  const auto center = modeling_.selection_center();
  ASSERT_TRUE(center.has_value());
  const auto begun = modeling_.begin_transform_drag(test::x_move_drag_begin({
      .operation = ModelingTransformOperation::Extrude,
      .pivot = *center,
  }));
  ASSERT_TRUE(begun.ok) << begun.message;
  const auto updated = modeling_.update_transform_drag(test::x_move_drag_update(120.0F));
  ASSERT_TRUE(updated.ok) << updated.message;
  ASSERT_NEAR(updated.move_delta.x, 2.0F, 0.00001F);
  ASSERT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(history_.undo_count(), 1U);
  EXPECT_GT(document().vertices.size(), 8U);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(document().vertices.size(), 8U);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_GT(document().vertices.size(), 8U);
}

TEST_F(MoveAutoMergeTests, FailedPublicationRestoresGeometrySelectionAndHistory) {
  create(1.0F);
  for (const auto failure : {
           ModelingSdkProjectionFailurePoint::BeforeCommit,
           ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
           ModelingSdkProjectionFailurePoint::CommittedSceneNotify,
           ModelingSdkProjectionFailurePoint::AfterSdkMutation}) {
    SCOPED_TRACE(static_cast<int>(failure));
    const auto scene_revision = scene_.revision();
    const auto asset_revision = assets_.revision();
    const auto content_revision = modeling_.content_revision();
    const auto selection_revision = modeling_.selection_summary().selection_revision;
    begin();
    move_one_unit();
    modeling_.set_modeling_sdk_projection_failure_for_tests(failure);
    const auto committed = modeling_.commit_transform_drag(history_);
    modeling_.set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(committed.ok);
    EXPECT_FALSE(committed.changed) << committed.message;
    EXPECT_FALSE(modeling_.transform_drag_active());
    EXPECT_EQ(document().vertices.size(), 6U);
    EXPECT_EQ(history_.undo_count(), 0U);
    EXPECT_EQ(scene_.revision(), scene_revision);
    EXPECT_EQ(assets_.revision(), asset_revision);
    EXPECT_EQ(modeling_.content_revision(), content_revision);
    EXPECT_EQ(modeling_.selection_summary().selection_revision, selection_revision);
    EXPECT_FLOAT_EQ(modeling_.export_selection()
                        .objects.front().selected_vertices.front().position.x,
                    0.0F);
  }
}

} // namespace
} // namespace quader
