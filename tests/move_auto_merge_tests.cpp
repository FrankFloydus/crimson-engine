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

#include <algorithm>
#include <array>
#include <vector>

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

  void create_box_face() {
    const auto created = modeling_.create_box_from_bounds({
        .bounds = {.min = {-1.0F, -1.0F, -1.0F},
                   .max = {1.0F, 1.0F, 1.0F}},
        .name = "Face Drag Rejection",
    });
    ASSERT_TRUE(created.ok) << created.message;
    object_ = created.object;
    const auto authored = document();
    for (const auto &face : authored.faces) {
      if (std::ranges::all_of(face.vertices, [&](auto id) {
            const auto *vertex = quader_poly::find_vertex(authored, id);
            return vertex != nullptr && vertex->position.x == 1.0F;
          })) {
        face_ = face.id;
        box_face_vertices_ = face.vertices;
        break;
      }
    }
    ASSERT_EQ(box_face_vertices_.size(), 4U);
    const auto selected = modeling_.set_component_selection({
        .modeling_object_id = object_.modeling_object_id,
        .mode = ModelingSelectionMode::Face,
        .faces = {static_cast<int>(face_)},
        .active_kind = ModelingActiveSelectionKind::Face,
        .active_face = static_cast<int>(face_),
    });
    ASSERT_TRUE(selected.ok) << selected.message;
  }

  void begin_box_face_drag() {
    auto request = test::x_move_drag_begin({
        .pivot = {1.0F, 0.0F, 0.0F},
        .grid_snap_enabled = false,
    });
    request.ray.origin.x = 1.0F;
    request.vertex_snap_radius = 0.0;
    const auto begun = modeling_.begin_transform_drag(request);
    ASSERT_TRUE(begun.ok) << begun.message;
  }

  [[nodiscard]] ModelingTransformDragPreview drag_box_face_to(float x) {
    auto request = test::x_move_drag_update((x - 1.0F) * 60.0F);
    // The pointer is outside the box silhouette but still in the viewport.
    // An acquired axis drag must not depend on hitting that moving mesh again.
    request.screen_position.y = 180.0F;
    request.ray.origin = {x, 3.0F, 5.0F};
    request.grid_snap_enabled = false;
    request.vertex_snap_radius = 0.0;
    return modeling_.update_transform_drag(request);
  }

  void expect_box_face_position(float x, bool preview) {
    const auto selected = modeling_.export_selection();
    ASSERT_EQ(selected.objects.size(), 1U);
    ASSERT_EQ(selected.objects.front().selected_faces.size(), 1U);
    EXPECT_EQ(selected.objects.front().selected_faces.front().id, face_);
    EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Face);
    const auto moved_vertex = [&](auto id) {
      return std::ranges::find(box_face_vertices_, id) != box_face_vertices_.end();
    };
    if (preview) {
      const auto meshes = modeling_.tool_preview_meshes();
      ASSERT_EQ(meshes.size(), 1U);
      const auto &mesh = meshes.front();
      ASSERT_EQ(mesh.source_entity, object_.entity);
      ASSERT_TRUE(mesh.suppress_source);
      const auto &vertices = mesh.mesh.geometry().vertices;
      ASSERT_FALSE(vertices.empty());
      if (mesh.dynamic_position_payload) {
        ASSERT_EQ(mesh.dynamic_position_payload->size(), vertices.size());
      }
      std::size_t moved_corners = 0U;
      for (std::size_t index = 0; index < vertices.size(); ++index) {
        const auto &vertex = vertices[index];
        const QVec3 position = mesh.dynamic_position_payload
            ? (*mesh.dynamic_position_payload)[index]
            : vertex.position;
        const bool moved = moved_vertex(vertex.source_vertex_id);
        if (moved) ++moved_corners;
        EXPECT_FLOAT_EQ(position.x, moved ? x : -1.0F);
        EXPECT_FLOAT_EQ(position.y, vertex.position.y);
        EXPECT_FLOAT_EQ(position.z, vertex.position.z);
      }
      EXPECT_GT(moved_corners, 0U);
    } else {
      EXPECT_TRUE(modeling_.tool_preview_meshes().empty());
      const auto authored = document();
      ASSERT_EQ(authored.vertices.size(), 8U);
      ASSERT_EQ(authored.faces.size(), 6U);
      for (const auto &vertex : authored.vertices) {
        EXPECT_FLOAT_EQ(vertex.position.x, moved_vertex(vertex.id) ? x : -1.0F);
      }
    }
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
  std::vector<quader_poly::ElementId> box_face_vertices_;
};

TEST_F(MoveAutoMergeTests,
       RejectedFaceCollapseKeepsLastPreviewAndDragCanContinueCommitAndUndo) {
  create_box_face();
  for (const bool auto_merge : {false, true}) {
    SCOPED_TRACE(auto_merge);
    set_auto_merge(auto_merge);
    begin_box_face_drag();
    const auto scene_revision = scene_.revision();
    const auto asset_revision = assets_.revision();
    const auto moved = drag_box_face_to(0.0F);
    ASSERT_TRUE(moved.ok) << moved.message;
    EXPECT_FLOAT_EQ(moved.move_delta.x, -1.0F);
    expect_box_face_position(0.0F, true);

    // The opposite face is x=-1: this sample collapses all four sidewalls.
    // Repeated rejected samples must retain the accepted half-width box.
    for (int sample = 0; sample < 2; ++sample) {
      const auto rejected = drag_box_face_to(-1.0F);
      EXPECT_FALSE(rejected.ok);
      EXPECT_FALSE(rejected.changed);
      EXPECT_TRUE(rejected.active);
      EXPECT_FALSE(rejected.message.empty());
      ASSERT_TRUE(modeling_.transform_drag_active());
      expect_box_face_position(0.0F, true);
      EXPECT_EQ(scene_.revision(), scene_revision);
      EXPECT_EQ(assets_.revision(), asset_revision);
      EXPECT_EQ(history_.undo_count(), 0U);
    }

    const auto continued = drag_box_face_to(-0.5F);
    ASSERT_TRUE(continued.ok) << continued.message;
    EXPECT_FLOAT_EQ(continued.move_delta.x, -1.5F);
    expect_box_face_position(-0.5F, true);
    const auto committed = modeling_.commit_transform_drag(history_);
    ASSERT_TRUE(committed.ok) << committed.message;
    ASSERT_TRUE(committed.changed);
    EXPECT_FALSE(modeling_.transform_drag_active());
    EXPECT_EQ(history_.undo_count(), 1U);
    expect_box_face_position(-0.5F, false);
    ASSERT_TRUE(history_.undo().ok);
    expect_box_face_position(1.0F, false);
    ASSERT_TRUE(history_.redo().ok);
    expect_box_face_position(-0.5F, false);
    ASSERT_TRUE(history_.undo().ok);
    history_.clear();
  }
}

TEST_F(MoveAutoMergeTests, RejectedFaceCollapseStillAllowsExplicitCancellation) {
  create_box_face();
  for (const bool auto_merge : {false, true}) {
    SCOPED_TRACE(auto_merge);
    set_auto_merge(auto_merge);
    begin_box_face_drag();
    const auto moved = drag_box_face_to(0.0F);
    ASSERT_TRUE(moved.ok) << moved.message;
    const auto rejected = drag_box_face_to(-1.0F);
    EXPECT_FALSE(rejected.ok);
    ASSERT_TRUE(modeling_.transform_drag_active());
    expect_box_face_position(0.0F, true);
    const auto canceled = modeling_.cancel_transform_drag();
    ASSERT_TRUE(canceled.ok) << canceled.message;
    EXPECT_FALSE(modeling_.transform_drag_active());
    EXPECT_EQ(history_.undo_count(), 0U);
    expect_box_face_position(1.0F, false);
  }
}

TEST_F(MoveAutoMergeTests, ReleaseAfterRejectedFaceCollapseCommitsLastValidSample) {
  create_box_face();
  for (const bool auto_merge : {false, true}) {
    SCOPED_TRACE(auto_merge);
    set_auto_merge(auto_merge);
    begin_box_face_drag();
    const auto moved = drag_box_face_to(0.0F);
    ASSERT_TRUE(moved.ok) << moved.message;
    const auto rejected = drag_box_face_to(-1.0F);
    EXPECT_FALSE(rejected.ok);
    ASSERT_TRUE(modeling_.transform_drag_active());
    expect_box_face_position(0.0F, true);

    // Mouse release accepts the retained candidate without another valid move.
    const auto committed = modeling_.commit_transform_drag(history_);
    ASSERT_TRUE(committed.ok) << committed.message;
    EXPECT_TRUE(committed.changed);
    EXPECT_FALSE(modeling_.transform_drag_active());
    EXPECT_EQ(history_.undo_count(), 1U);
    expect_box_face_position(0.0F, false);
    ASSERT_TRUE(history_.undo().ok);
    expect_box_face_position(1.0F, false);
    ASSERT_TRUE(history_.redo().ok);
    expect_box_face_position(0.0F, false);
    ASSERT_TRUE(history_.undo().ok);
    history_.clear();
  }
}

TEST_F(MoveAutoMergeTests, FaceCollapseRestoresEarlierParticipantPreviewBeforeRelease) {
  // This independent face precedes the box in the portable participant order.
  // Its candidate can advance before the later box rejects its collapsed walls.
  quader_poly::Document other_document;
  const auto a = quader_poly::add_vertex(other_document, {5.0F, -1.0F, -1.0F});
  const auto b = quader_poly::add_vertex(other_document, {5.0F, 1.0F, -1.0F});
  const auto c = quader_poly::add_vertex(other_document, {5.0F, 0.0F, 1.0F});
  const auto other_face = quader_poly::add_face(other_document, std::array{a, b, c});
  ASSERT_NE(other_face, quader_poly::kInvalidElementId);
  const auto other = modeling_.create_mesh({
      .document = std::move(other_document), .name = "Earlier Face Participant"});
  ASSERT_TRUE(other.ok) << other.message;
  create_box_face();
  const std::array<ModelingComponentSelectionRequest, 2> selections{
      ModelingComponentSelectionRequest{
          .modeling_object_id = other.object.modeling_object_id,
          .mode = ModelingSelectionMode::Face,
          .faces = {static_cast<int>(other_face)}},
      ModelingComponentSelectionRequest{
          .modeling_object_id = object_.modeling_object_id,
          .mode = ModelingSelectionMode::Face,
          .faces = {static_cast<int>(face_)}}};
  ASSERT_TRUE(modeling_.set_component_selections(selections).ok);
  set_auto_merge(false);
  begin_box_face_drag();
  const auto scene_revision = scene_.revision();
  const auto asset_revision = assets_.revision();
  const auto moved = drag_box_face_to(0.0F);
  ASSERT_TRUE(moved.ok) << moved.message;
  const auto rejected = drag_box_face_to(-1.0F);
  EXPECT_FALSE(rejected.ok);
  ASSERT_TRUE(modeling_.transform_drag_active());
  EXPECT_EQ(scene_.revision(), scene_revision);
  EXPECT_EQ(assets_.revision(), asset_revision);

  const auto meshes = modeling_.tool_preview_meshes();
  ASSERT_EQ(meshes.size(), 2U);
  for (const auto &mesh : meshes) {
    const bool is_box = mesh.source_entity == object_.entity;
    ASSERT_TRUE(is_box || mesh.source_entity == other.object.entity);
    const auto &vertices = mesh.mesh.geometry().vertices;
    ASSERT_FALSE(vertices.empty());
    if (mesh.dynamic_position_payload) {
      ASSERT_EQ(mesh.dynamic_position_payload->size(), vertices.size());
    }
    for (std::size_t index = 0; index < vertices.size(); ++index) {
      const auto &vertex = vertices[index];
      const auto position = mesh.dynamic_position_payload
          ? (*mesh.dynamic_position_payload)[index]
          : vertex.position;
      const bool moved_box_vertex = std::ranges::find(
          box_face_vertices_, vertex.source_vertex_id) != box_face_vertices_.end();
      EXPECT_FLOAT_EQ(position.x, is_box ? (moved_box_vertex ? 0.0F : -1.0F) : 4.0F);
    }
  }

  const auto expect_committed_positions = [&](float box_x, float other_x) {
    const auto selected = modeling_.export_selection();
    ASSERT_EQ(selected.objects.size(), 2U);
    EXPECT_EQ(modeling_.selection_summary().selected_face_count, 2U);
    for (const auto &object : selected.objects) {
      const auto mesh = modeling_.compile_object_mesh(object.modeling_object_id);
      ASSERT_TRUE(mesh.has_value());
      ASSERT_NE(mesh->authored_polygon_document, nullptr);
      const bool is_box = object.modeling_object_id == object_.modeling_object_id;
      for (const auto &vertex : mesh->authored_polygon_document->vertices) {
        const bool moved_box_vertex = std::ranges::find(
            box_face_vertices_, vertex.id) != box_face_vertices_.end();
        EXPECT_FLOAT_EQ(vertex.position.x,
                        is_box ? (moved_box_vertex ? box_x : -1.0F) : other_x);
      }
    }
  };
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_EQ(history_.undo_count(), 1U);
  expect_committed_positions(0.0F, 4.0F);
  ASSERT_TRUE(history_.undo().ok);
  expect_committed_positions(1.0F, 5.0F);
  ASSERT_TRUE(history_.redo().ok);
  expect_committed_positions(0.0F, 4.0F);
}

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

TEST_F(MoveAutoMergeTests, EdgeModeMoveKeepsCoincidentVerticesSeparate) {
  create(1.0F, ModelingSelectionMode::Edge);
  begin();
  move_one_unit();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_TRUE(committed.changed);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_edge_count, 1U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Edge);
  const auto moved = document();
  ASSERT_NE(quader_poly::find_vertex(moved, source_), nullptr);
  EXPECT_FLOAT_EQ(quader_poly::find_vertex(moved, source_)->position.x, 1.0F);
  EXPECT_EQ(history_.undo_count(), 1U);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_edge_count, 1U);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
}

TEST_F(MoveAutoMergeTests, FaceModeMoveKeepsCoincidentVerticesSeparate) {
  create(1.0F, ModelingSelectionMode::Face);
  begin();
  move_one_unit();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  EXPECT_TRUE(committed.changed);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Face);
  const auto moved = document();
  ASSERT_NE(quader_poly::find_vertex(moved, source_), nullptr);
  EXPECT_FLOAT_EQ(quader_poly::find_vertex(moved, source_)->position.x, 1.0F);
  EXPECT_EQ(history_.undo_count(), 1U);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(document().vertices.size(), 6U);
}

TEST_F(MoveAutoMergeTests, NonVertexZeroDistanceAcceptanceDoesNotWeldOrRecordHistory) {
  for (const auto mode : {ModelingSelectionMode::Edge, ModelingSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    create(0.0F, mode);
    begin();
    const auto committed = modeling_.commit_transform_drag(history_);
    ASSERT_TRUE(committed.ok) << committed.message;
    EXPECT_FALSE(committed.changed);
    EXPECT_EQ(document().vertices.size(), 6U);
    EXPECT_EQ(modeling_.selection_mode(), mode);
    EXPECT_EQ(history_.undo_count(), 0U);
  }
}

TEST_F(MoveAutoMergeTests, NonVertexExtrudeGestureAcceptedAtPressDoesNotWeld) {
  for (const auto mode : {ModelingSelectionMode::Edge, ModelingSelectionMode::Face}) {
    SCOPED_TRACE(static_cast<int>(mode));
    create(0.0F, mode);
    auto request = test::x_move_drag_begin({
        .operation = ModelingTransformOperation::Extrude,
    });
    request.grid_snap_enabled = false;
    const auto begun = modeling_.begin_transform_drag(request);
    ASSERT_TRUE(begun.ok) << begun.message;
    const auto committed = modeling_.commit_transform_drag(history_);
    ASSERT_TRUE(committed.ok) << committed.message;
    EXPECT_FALSE(committed.changed);
    EXPECT_EQ(document().vertices.size(), 6U);
    EXPECT_EQ(document().faces.size(), 2U);
    EXPECT_EQ(modeling_.selection_mode(), mode);
    EXPECT_EQ(history_.undo_count(), 0U);
  }
}

TEST_F(MoveAutoMergeTests, FaceMoveRepeatDoesNotEnableVertexMergingAfterModeChange) {
  create(1.0F, ModelingSelectionMode::Face);
  begin();
  move_one_unit();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_TRUE(history_.undo().ok);
  ASSERT_TRUE(modeling_.set_component_selection({
      .modeling_object_id = object_.modeling_object_id,
      .mode = ModelingSelectionMode::Vertex,
      .vertices = {static_cast<int>(source_)},
  }).ok);
  const auto repeated = modeling_.repeat_last_action();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_TRUE(repeated.changed);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Vertex);
}

TEST_F(MoveAutoMergeTests, VertexMoveRepeatDoesNotMergeFaceSelection) {
  create(1.0F);
  begin();
  move_one_unit();
  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_EQ(document().vertices.size(), 5U);
  ASSERT_TRUE(history_.undo().ok);
  ASSERT_TRUE(modeling_.set_component_selection({
      .modeling_object_id = object_.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {static_cast<int>(face_)},
  }).ok);
  const auto repeated = modeling_.repeat_last_action();
  ASSERT_TRUE(repeated.ok) << repeated.message;
  EXPECT_TRUE(repeated.changed);
  EXPECT_EQ(document().vertices.size(), 6U);
  EXPECT_EQ(modeling_.selection_mode(), ModelingSelectionMode::Face);
}

TEST_F(MoveAutoMergeTests, ExtrudedCapSelectionSurvivesMoveWithoutVertexMerging) {
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
  EXPECT_EQ(document().vertices.size(), 12U);
  ASSERT_TRUE(history_.undo().ok);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(document().vertices.size(), 8U);
  ASSERT_TRUE(history_.redo().ok);
  EXPECT_EQ(modeling_.selection_summary().selected_face_count, 1U);
  EXPECT_EQ(document().vertices.size(), 12U);
}

TEST_F(MoveAutoMergeTests, FaceExtrusionOntoIndependentFaceKeepsDestinationDisconnected) {
  quader_poly::Document source;
  constexpr std::array<quader::QVec3, 4> kCorners{{
      {0.0F, -1.0F, -1.0F}, {0.0F, 1.0F, -1.0F},
      {0.0F, 1.0F, 1.0F}, {0.0F, -1.0F, 1.0F},
  }};
  std::array<quader_poly::ElementId, 4> source_vertices;
  std::array<quader_poly::ElementId, 4> destination_vertices;
  for (std::size_t index = 0; index < kCorners.size(); ++index) {
    source_vertices[index] = quader_poly::add_vertex(source, kCorners[index]);
    destination_vertices[index] = quader_poly::add_vertex(
        source, kCorners[index] + quader::QVec3{1.0F, 0.0F, 0.0F});
  }
  const auto source_face = quader_poly::add_face(source, source_vertices);
  const auto destination_face = quader_poly::add_face(source, destination_vertices);
  ASSERT_NE(source_face, quader_poly::kInvalidElementId);
  ASSERT_NE(destination_face, quader_poly::kInvalidElementId);
  const auto created = modeling_.create_mesh({
      .document = std::move(source), .name = "Extrusion coincident with loose face"});
  ASSERT_TRUE(created.ok) << created.message;
  object_ = created.object;
  ASSERT_TRUE(modeling_.set_component_selection({
      .modeling_object_id = object_.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {static_cast<int>(source_face)},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = static_cast<int>(source_face),
  }).ok);
  ASSERT_TRUE(modeling_.interaction_settings().move_auto_merge_vertices);
  auto request = test::x_move_drag_begin({
      .operation = ModelingTransformOperation::Extrude,
  });
  request.grid_snap_enabled = false;
  ASSERT_TRUE(modeling_.begin_transform_drag(request).ok);
  move_one_unit();

  const auto expect_separate_destination = [&](const quader_poly::Document &candidate) {
    ASSERT_EQ(candidate.vertices.size(), 12U);
    ASSERT_EQ(candidate.faces.size(), 6U);
    const auto *cap = quader_poly::find_face(candidate, source_face);
    const auto *destination = quader_poly::find_face(candidate, destination_face);
    ASSERT_NE(cap, nullptr);
    ASSERT_NE(destination, nullptr);
    ASSERT_EQ(cap->vertices.size(), 4U);
    EXPECT_EQ(destination->vertices,
              (std::vector<quader_poly::ElementId>{destination_vertices.begin(),
                                                  destination_vertices.end()}));
    for (std::size_t index = 0; index < kCorners.size(); ++index) {
      EXPECT_TRUE(std::ranges::find(source_vertices, cap->vertices[index]) ==
                  source_vertices.end());
      EXPECT_TRUE(std::ranges::find(destination_vertices, cap->vertices[index]) ==
                  destination_vertices.end());
      const auto *cap_vertex = quader_poly::find_vertex(candidate, cap->vertices[index]);
      const auto *destination_vertex =
          quader_poly::find_vertex(candidate, destination_vertices[index]);
      ASSERT_NE(cap_vertex, nullptr);
      ASSERT_NE(destination_vertex, nullptr);
      EXPECT_FLOAT_EQ(cap_vertex->position.x, 1.0F);
      EXPECT_FLOAT_EQ(cap_vertex->position.x, destination_vertex->position.x);
      EXPECT_FLOAT_EQ(cap_vertex->position.y, destination_vertex->position.y);
      EXPECT_FLOAT_EQ(cap_vertex->position.z, destination_vertex->position.z);
    }
    const auto cap_part = quader_poly::connected_component_selection(
        candidate, {.hit = true, .kind = quader_poly::ElementKind::Face,
                    .face_id = source_face});
    EXPECT_EQ(cap_part.faces.size(), 5U);
    EXPECT_FALSE(quader_poly::selection_contains_face(cap_part, destination_face));
    const auto destination_part = quader_poly::connected_component_selection(
        candidate, {.hit = true, .kind = quader_poly::ElementKind::Face,
                    .face_id = destination_face});
    EXPECT_EQ(destination_part.faces,
              (std::vector<quader_poly::ElementId>{destination_face}));
  };
  // Shift extrusion publishes its candidate through the topology-changing
  // scene projection rather than the transient position-only preview stream.
  ASSERT_TRUE(modeling_.transform_drag_active());
  ASSERT_NO_FATAL_FAILURE(expect_separate_destination(document()));

  const auto committed = modeling_.commit_transform_drag(history_);
  ASSERT_TRUE(committed.ok) << committed.message;
  ASSERT_TRUE(committed.changed);
  ASSERT_NO_FATAL_FAILURE(expect_separate_destination(document()));
  EXPECT_EQ(history_.undo_count(), 1U);
}

TEST_F(MoveAutoMergeTests, FailedPublicationRetainsPreviewForRetryWithoutPublishing) {
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
    EXPECT_TRUE(modeling_.transform_drag_active());
    EXPECT_FALSE(modeling_.tool_preview_meshes().empty());
    EXPECT_EQ(document().vertices.size(), 6U);
    EXPECT_EQ(history_.undo_count(), 0U);
    EXPECT_EQ(scene_.revision(), scene_revision);
    EXPECT_EQ(assets_.revision(), asset_revision);
    EXPECT_EQ(modeling_.content_revision(), content_revision);
    EXPECT_EQ(modeling_.selection_summary().selection_revision, selection_revision);
    EXPECT_FLOAT_EQ(modeling_.export_selection()
                        .objects.front().selected_vertices.front().position.x,
                    0.0F);
    const auto retried = modeling_.commit_transform_drag(history_);
    ASSERT_TRUE(retried.ok) << retried.message;
    EXPECT_FALSE(modeling_.transform_drag_active());
    EXPECT_EQ(document().vertices.size(), 5U);
    EXPECT_EQ(history_.undo_count(), 1U);
    ASSERT_TRUE(history_.undo().ok);
    EXPECT_EQ(document().vertices.size(), 6U);
    ASSERT_EQ(modeling_.export_selection().objects.front().selected_vertices.front().id,
              source_);
    history_.clear();
  }
}

} // namespace
} // namespace quader
