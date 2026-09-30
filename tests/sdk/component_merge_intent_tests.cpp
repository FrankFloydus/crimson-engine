////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

[[nodiscard]] PolygonDocument make_corner_selection(
    std::span<const Vec3> selected_positions,
    std::size_t active_index = 0U) {
  AuthoredTopology topology;
  PolygonSelectionSnapshot selection;
  for (std::size_t index = 0; index < selected_positions.size(); ++index) {
    const Vec3 position = selected_positions[index];
    const auto first_index = static_cast<std::uint32_t>(index * 3U + 1U);
    const std::array vertex_ids{make_id<VertexTag>(first_index),
                                make_id<VertexTag>(first_index + 1U),
                                make_id<VertexTag>(first_index + 2U)};
    topology.vertices.push_back({vertex_ids[0], position});
    topology.vertices.push_back(
        {vertex_ids[1], {position.x, position.y + 2.0F, position.z}});
    topology.vertices.push_back(
        {vertex_ids[2], {position.x, position.y, position.z + 3.0F}});
    topology.faces.push_back({
        .id = make_id<FaceTag>(static_cast<std::uint32_t>(index + 1U)),
        .vertices = {vertex_ids.begin(), vertex_ids.end()},
        .uvs = {{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}},
        .material_slot = static_cast<std::uint32_t>(index + 1U),
        .shading = AuthoredSurfaceShading::Flat,
    });
    selection.vertices.push_back(vertex_ids[0]);
  }
  topology.next_vertex_id =
      static_cast<std::uint32_t>(topology.vertices.size() + 1U);
  topology.next_face_id =
      static_cast<std::uint32_t>(topology.faces.size() + 1U);
  selection.has_active = true;
  selection.active_vertex = selection.vertices[active_index];
  PolygonDocument document =
      PolygonDocument::from_authored_topology(topology).or_throw();
  (void)document.apply_selection(selection).or_throw();
  return document;
}

[[nodiscard]] Vec3 vertex_position(const PolygonDocument &document,
                                    VertexId vertex_id) {
  const AuthoredTopology topology = document.authored_topology().or_throw();
  const auto found = std::ranges::find(topology.vertices, vertex_id,
                                      &AuthoredTopologyVertex::id);
  EXPECT_NE(found, topology.vertices.end());
  return found != topology.vertices.end() ? found->position : Vec3{};
}

void expect_position(Vec3 actual, Vec3 expected) {
  EXPECT_NEAR(actual.x, expected.x, 0.0001F);
  EXPECT_NEAR(actual.y, expected.y, 0.0001F);
  EXPECT_NEAR(actual.z, expected.z, 0.0001F);
}

void expect_document_restored(const PolygonDocument &actual,
                              const PolygonDocument &expected,
                              bool preserve_high_water = false) {
  const AuthoredTopology actual_topology = actual.authored_topology().or_throw();
  AuthoredTopology expected_topology = expected.authored_topology().or_throw();
  if (preserve_high_water) {
    EXPECT_GE(actual_topology.next_vertex_id, expected_topology.next_vertex_id);
    EXPECT_GE(actual_topology.next_face_id, expected_topology.next_face_id);
    expected_topology.next_vertex_id = actual_topology.next_vertex_id;
    expected_topology.next_face_id = actual_topology.next_face_id;
  }
  const PolygonDocument expected_document =
      PolygonDocument::from_authored_topology(expected_topology).or_throw();
  EXPECT_EQ(serialize_qdr_authored_topology(actual).or_throw(),
            serialize_qdr_authored_topology(expected_document).or_throw());
  EXPECT_EQ(actual.selection().kind, expected.selection().kind);
  EXPECT_EQ(actual.selection().vertices, expected.selection().vertices);
  EXPECT_EQ(actual.selection().has_active, expected.selection().has_active);
  EXPECT_EQ(actual.selection().active_vertex, expected.selection().active_vertex);
}

[[nodiscard]] Transform3 reflected_world_transform() {
  return {
      .x_axis = {-2.0F, 0.0F, 0.0F},
      .y_axis = {0.0F, 3.0F, 0.0F},
      .z_axis = {0.0F, 0.0F, 0.5F},
      .origin = {100.0F, -200.0F, 300.0F},
  };
}

TEST(ModelingSdkComponentMergeIntentTests,
     ActiveUsesSurvivorActiveIdentityAcrossTwoTransformedObjects) {
  ModelingSession session;
  const std::array survivor_positions{Vec3{1.0F, 2.0F, 3.0F},
                                      Vec3{4.0F, 2.0F, 3.0F}};
  const std::array source_positions{Vec3{-2.0F, 1.0F, 0.0F},
                                    Vec3{0.0F, -1.0F, 1.0F}};
  const PolygonDocument survivor_document =
      make_corner_selection(survivor_positions, 1U);
  const PolygonDocument source_document = make_corner_selection(source_positions);
  const ObjectId source = session.add_document(source_document, "Source").or_throw();
  const ObjectId survivor =
      session.add_document(survivor_document, "Active").or_throw();
  const std::array sources{SessionMergeSource{
      .object = source,
      .survivor_from_source = {.x_axis = {-0.5F, 0.0F, 0.0F},
                              .y_axis = {0.0F, 0.25F, 0.0F},
                              .z_axis = {0.0F, 0.0F, 2.0F},
                              .origin = {-3.0F, 4.0F, -2.0F}},
  }};
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();
  const std::size_t undo_depth = session.undo_depth();
  const auto prepared = session.prepare_merge_documents(
      survivor, sources, SelectionKind::Vertex, 0.0F,
      reflected_world_transform(), SessionMergeIntent::ToActive);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().receipt.changed);
  const PolygonDocument &candidate = prepared.value().survivor_document;
  ASSERT_TRUE(candidate.validate().ok());
  EXPECT_EQ(candidate.vertex_count(), 9U);
  EXPECT_EQ(candidate.face_count(), 4U);
  const VertexId active_vertex = make_id<VertexTag>(4U);
  EXPECT_EQ(candidate.selection().vertices, std::vector<VertexId>{active_vertex});
  EXPECT_TRUE(candidate.selection().has_active);
  EXPECT_EQ(candidate.selection().active_vertex, active_vertex);
  expect_position(vertex_position(candidate, active_vertex), survivor_positions[1]);
  EXPECT_EQ(session.objects().size(), 2U);
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  expect_document_restored(session.document_copy(survivor).or_throw(),
                           survivor_document);
  expect_document_restored(session.document_copy(source).or_throw(), source_document);
  const auto repeated = session.prepare_merge_documents(
      survivor, sources, SelectionKind::Vertex, 0.0F,
      reflected_world_transform(), SessionMergeIntent::ToActive);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  expect_document_restored(repeated.value().survivor_document, candidate);
}

TEST(ModelingSdkComponentMergeIntentTests,
     CenterUsesEveryDistinctSelectedVertexAcrossThreeTransformedObjects) {
  ModelingSession session;
  const std::array survivor_positions{Vec3{1.0F, 2.0F, 3.0F}};
  const std::array middle_positions{Vec3{0.0F, 1.0F, 1.0F},
                                    Vec3{2.0F, 0.0F, 2.0F}};
  const std::array last_positions{Vec3{-2.0F, 1.0F, 0.0F},
                                  Vec3{0.0F, -1.0F, 1.0F},
                                  Vec3{3.0F, 2.0F, -1.0F}};
  const PolygonDocument survivor_document = make_corner_selection(survivor_positions);
  const PolygonDocument middle_document = make_corner_selection(middle_positions);
  const PolygonDocument last_document = make_corner_selection(last_positions, 2U);
  const ObjectId survivor = session.add_document(survivor_document).or_throw();
  const ObjectId middle = session.add_document(middle_document).or_throw();
  const ObjectId last = session.add_document(last_document).or_throw();
  const std::array sources{
      SessionMergeSource{
          .object = middle,
          .survivor_from_source = {.x_axis = {0.0F, 2.0F, 0.0F},
                                  .y_axis = {-1.0F, 0.0F, 0.0F},
                                  .z_axis = {0.0F, 0.0F, 4.0F},
                                  .origin = {5.0F, -2.0F, 1.0F}},
      },
      SessionMergeSource{
          .object = last,
          .survivor_from_source = {.x_axis = {-0.5F, 0.0F, 0.0F},
                                  .y_axis = {0.0F, 0.25F, 0.0F},
                                  .z_axis = {0.0F, 0.0F, 2.0F},
                                  .origin = {-3.0F, 4.0F, -2.0F}},
      }};
  const std::uint64_t revision = session.content_revision();
  const auto prepared = session.prepare_merge_documents(
      survivor, sources, SelectionKind::Vertex, 0.0F,
      reflected_world_transform(), SessionMergeIntent::ToCenter);
  ASSERT_TRUE(prepared.ok()) << prepared.error_message();
  ASSERT_TRUE(prepared.value().receipt.changed);
  const PolygonDocument &candidate = prepared.value().survivor_document;
  ASSERT_TRUE(candidate.validate().ok());
  EXPECT_EQ(candidate.vertex_count(), 13U);
  EXPECT_EQ(candidate.face_count(), 6U);
  EXPECT_EQ(candidate.selection().vertices,
            std::vector<VertexId>{make_id<VertexTag>(1U)});
  expect_position(vertex_position(candidate, make_id<VertexTag>(1U)),
                  {1.0F / 12.0F, 29.0F / 12.0F, 11.0F / 6.0F});
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_TRUE(session.changes_since(revision).objects.empty());
  expect_document_restored(session.document_copy(survivor).or_throw(),
                           survivor_document);
  expect_document_restored(session.document_copy(middle).or_throw(), middle_document);
  expect_document_restored(session.document_copy(last).or_throw(), last_document);
}

TEST(ModelingSdkComponentMergeIntentTests,
     RejectedIntentOperandsAndGenerationalIdsPreserveSessionState) {
  ModelingSession session;
  const std::array first_positions{Vec3{0.0F, 0.0F, 0.0F}};
  const std::array second_positions{Vec3{4.0F, 0.0F, 0.0F}};
  const PolygonDocument survivor_document =
      make_corner_selection(first_positions);
  const PolygonDocument source_document = make_corner_selection(second_positions);
  const ObjectId survivor = session.add_document(survivor_document).or_throw();
  const ObjectId source = session.add_document(source_document).or_throw();
  const std::array sources{SessionMergeSource{.object = source}};
  ASSERT_TRUE(session.rename_object(survivor, "Renamed").ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.can_redo());
  const std::uint64_t revision = session.content_revision();
  const std::size_t undo_depth = session.undo_depth();
  EXPECT_FALSE(session.prepare_merge_documents(
      survivor, sources, SelectionKind::Edge, 0.0F, {},
      SessionMergeIntent::ToActive).ok());
  EXPECT_FALSE(session.prepare_merge_documents(
      survivor, sources, SelectionKind::Vertex, 0.0F, {},
      static_cast<SessionMergeIntent>(99)).ok());
  EXPECT_FALSE(session.prepare_merge_documents(
      make_id<ObjectTag>(survivor.index, survivor.generation + 1U), sources,
      SelectionKind::Vertex, 0.0F, {}, SessionMergeIntent::ToActive).ok());
  const std::array stale_sources{SessionMergeSource{
      .object = make_id<ObjectTag>(source.index, source.generation + 1U),
  }};
  EXPECT_FALSE(session.prepare_merge_documents(
      survivor, stale_sources, SelectionKind::Vertex, 0.0F, {},
      SessionMergeIntent::ToCenter).ok());
  PolygonDocument unselected_source = source_document;
  ASSERT_TRUE(unselected_source.clear_selection().ok());
  const std::array unselected_sources{SessionMergeSource{
      .object = source,
      .prepared_document = unselected_source,
  }};
  EXPECT_FALSE(session.prepare_merge_documents(
      survivor, unselected_sources, SelectionKind::Vertex, 0.0F, {},
      SessionMergeIntent::ToActive).ok());
  PolygonDocument invalid_selection = survivor_document;
  PolygonSelectionSnapshot stale_selection = invalid_selection.selection();
  stale_selection.active_vertex.generation += 1U;
  EXPECT_FALSE(invalid_selection.apply_selection(stale_selection).ok());
  expect_document_restored(invalid_selection, survivor_document);
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), undo_depth);
  EXPECT_TRUE(session.can_redo());
  EXPECT_TRUE(session.changes_since(revision).objects.empty());
  expect_document_restored(session.document_copy(survivor).or_throw(),
                           survivor_document);
  expect_document_restored(session.document_copy(source).or_throw(), source_document);
}

TEST(ModelingSdkComponentMergeIntentTests,
     ActiveAndCenterPreviewCancelCommitUndoRedoPreserveIdentities) {
  for (const SessionMergeIntent intent :
       std::array{SessionMergeIntent::ToActive, SessionMergeIntent::ToCenter}) {
    SCOPED_TRACE(static_cast<int>(intent));
    ModelingSession session;
    const std::array first_positions{Vec3{0.0F, 0.0F, 0.0F}};
    const std::array second_positions{Vec3{4.0F, 0.0F, 0.0F}};
    const PolygonDocument survivor_document = make_corner_selection(first_positions);
    const PolygonDocument source_document = make_corner_selection(second_positions);
    const ObjectId survivor = session.add_document(survivor_document).or_throw();
    const ObjectId source = session.add_document(source_document).or_throw();
    const std::array sources{SessionMergeSource{.object = source}};
    const std::size_t undo_depth = session.undo_depth();
    const auto begin_merge_preview = [&](const SessionMergePreviewCandidate &candidate) {
      SessionPreviewTransactionRequest request;
      request.replace_object_selection = true;
      request.active_participant_index = 0U;
      request.participants.push_back({
          .action = SessionPreviewAction::ReplaceExisting,
          .object = survivor,
          .document = candidate.survivor_document,
          .receipt = candidate.receipt,
          .selected = true,
      });
      request.participants.push_back({
          .action = SessionPreviewAction::RemoveExisting,
          .object = source,
      });
      return session.begin_preview_transaction("modeling.merge.intent.preview",
                                                std::move(request)).or_throw();
    };
    const auto first = session.prepare_merge_documents(
        survivor, sources, SelectionKind::Vertex, 0.0F, {}, intent);
    ASSERT_TRUE(first.ok()) << first.error_message();
    ASSERT_TRUE(first.value().receipt.changed);
    const AuthoredTopology first_topology =
        first.value().survivor_document.authored_topology().or_throw();
    const auto canceled = begin_merge_preview(first.value());
    EXPECT_EQ(session.preview_document_copy(canceled.preview_id, survivor)
                  .or_throw().vertex_count(), 5U);
    ASSERT_TRUE(session.cancel_preview(canceled.preview_id).ok());
    EXPECT_EQ(session.objects().size(), 2U);
    EXPECT_EQ(session.undo_depth(), undo_depth);
    expect_document_restored(session.document_copy(survivor).or_throw(),
                             survivor_document, true);
    expect_document_restored(session.document_copy(source).or_throw(), source_document);

    const auto second = session.prepare_merge_documents(
        survivor, sources, SelectionKind::Vertex, 0.0F, {}, intent);
    ASSERT_TRUE(second.ok()) << second.error_message();
    ASSERT_TRUE(second.value().receipt.changed);
    const AuthoredTopology second_topology =
        second.value().survivor_document.authored_topology().or_throw();
    for (const AuthoredTopologyVertex &vertex : second_topology.vertices) {
      if (vertex.id.index > 3U) {
        EXPECT_GE(vertex.id.index, first_topology.next_vertex_id);
      }
    }
    const auto begun = begin_merge_preview(second.value());
    const auto committed = session.commit_preview_transaction(begun.preview_id);
    ASSERT_TRUE(committed.ok()) << committed.error_message();
    EXPECT_EQ(committed.value().active_object, survivor);
    EXPECT_EQ(committed.value().selected_objects, std::vector<ObjectId>{survivor});
    EXPECT_EQ(session.objects().size(), 1U);
    EXPECT_EQ(session.undo_depth(), undo_depth + 1U);
    const PolygonDocument merged = session.document_copy(survivor).or_throw();
    expect_document_restored(merged, second.value().survivor_document);
    ASSERT_TRUE(session.undo().ok());
    EXPECT_EQ(session.objects().size(), 2U);
    EXPECT_EQ(session.undo_depth(), undo_depth);
    const PolygonDocument undone = session.document_copy(survivor).or_throw();
    expect_document_restored(undone, survivor_document, true);
    expect_document_restored(session.document_copy(source).or_throw(), source_document);
    EXPECT_GE(undone.authored_topology().or_throw().next_vertex_id,
              second_topology.next_vertex_id);
    EXPECT_GE(undone.authored_topology().or_throw().next_face_id,
              second_topology.next_face_id);
    ASSERT_TRUE(session.redo().ok());
    EXPECT_EQ(session.objects().size(), 1U);
    expect_document_restored(session.document_copy(survivor).or_throw(), merged);
  }
}

} // namespace
