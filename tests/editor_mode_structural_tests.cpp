////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/modes/editor_mode_service.hpp"
#include "editor/session/scene_session.hpp"
#include "support/command_observable_state.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <vector>

namespace quader::test {
namespace {

class EditorModeStructuralTests : public ::testing::Test {
protected:
  EditorModeStructuralTests()
      : editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)) {}

  QuaderModelingObjectRef create_box(float x) {
    const auto created = editor_.modeling().create_box(
        {.min = {x, -1, -1}, .max = {x + 2, 1, 1}}, "Mode participant");
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  QuaderModelingObjectRef create_quad(float x) {
    quader_poly::Document document;
    const std::array vertices{
        quader_poly::add_vertex(document, {x, 0, 0}),
        quader_poly::add_vertex(document, {x + 2, 0, 0}),
        quader_poly::add_vertex(document, {x + 2, 2, 0}),
        quader_poly::add_vertex(document, {x, 2, 0})};
    (void)quader_poly::add_face(document, vertices);
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Bridge participant"});
    EXPECT_TRUE(created.ok()) << created.error_message();
    return created.ok() ? created.value() : QuaderModelingObjectRef{};
  }

  void enter(std::initializer_list<EntityId> entities, EntityId active) {
    ASSERT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
    ASSERT_TRUE(editor_.selection().select_objects(entities, active).ok());
    ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
    expect_members(entities);
  }

  void expect_members(std::initializer_list<EntityId> expected) const {
    std::vector<EntityId> actual;
    for (const auto &participant : editor_.mode().state().participants) {
      actual.push_back(participant.id);
    }
    std::vector<EntityId> sorted_expected(expected);
    std::ranges::sort(actual);
    std::ranges::sort(sorted_expected);
    EXPECT_EQ(actual, sorted_expected);
    EXPECT_EQ(editor_.selection().summary().selected_object_count, expected.size());
  }

  void expect_unchanged(const CommandObservableState &before,
                        const EditorModeSnapshot &mode_before) const {
    const auto after = capture_command_observable_state(editor_);
    const auto difference = diff_command_observable_state(before, after);
    EXPECT_TRUE(difference.empty()) << difference.summary;
    EXPECT_EQ(session_.editor_mode().snapshot(), mode_before);
  }

  void select_bridge_pair(QuaderModelingObjectRef first,
                          QuaderModelingObjectRef second) {
    ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
    ASSERT_TRUE(editor_.selection().select_edge(first, 2, 3).ok());
    ASSERT_TRUE(editor_.selection().select_edge(
        second, 4, 1, QuaderSelectionEdit::Add).ok());
  }

  SceneSession session_;
  QuaderEditorApi editor_;
};

TEST_F(EditorModeStructuralTests,
       ComponentEditsSynchronizeParticipantsAndInvertIncludesEveryEligibleMesh) {
  const auto first = create_box(0);
  const auto second = create_box(4);
  const auto empty = create_box(8);
  enter({first.entity, second.entity, empty.entity}, first.entity);
  ASSERT_FALSE(HasFatalFailure());
  const auto generation = editor_.mode().generation();
  for (const auto kind : {QuaderComponentKind::Vertex, QuaderComponentKind::Edge,
                          QuaderComponentKind::Face}) {
    SCOPED_TRACE(static_cast<int>(kind));
    ASSERT_TRUE(editor_.selection().clear().ok());
    auto selection = QuaderComponentSelectionArgs{.object = first, .kind = kind};
    if (kind == QuaderComponentKind::Vertex) selection.vertices = {1};
    if (kind == QuaderComponentKind::Edge) selection.edges = {{1, 2}};
    if (kind == QuaderComponentKind::Face) selection.faces = {1};
    ASSERT_TRUE(editor_.selection().select_components(selection).ok());
    expect_members({first.entity});
    selection.object = second;
    selection.edit = QuaderSelectionEdit::Add;
    ASSERT_TRUE(editor_.selection().select_components(selection).ok());
    expect_members({first.entity, second.entity});
    selection.edit = QuaderSelectionEdit::Remove;
    ASSERT_TRUE(editor_.selection().select_components(selection).ok());
    expect_members({first.entity});
    ASSERT_TRUE(editor_.selection().clear().ok());
    expect_members({});
    const auto inverted = editor_.selection().select_inverse();
    ASSERT_TRUE(inverted.ok()) << inverted.error_message();
    expect_members({first.entity, second.entity, empty.entity});
    const auto all = editor_.selection().summary();
    EXPECT_EQ(kind == QuaderComponentKind::Vertex ? all.selected_vertex_count
              : kind == QuaderComponentKind::Edge ? all.selected_edge_count
                                                 : all.selected_face_count,
              kind == QuaderComponentKind::Vertex ? 24U
              : kind == QuaderComponentKind::Edge ? 36U : 18U);
    ASSERT_TRUE(editor_.selection().clear().ok());
    expect_members({});
    EXPECT_EQ(editor_.mode().generation(), generation);
  }
}

TEST_F(EditorModeStructuralTests,
       ComponentsOutsideEditOrOnHiddenPeersRejectWithoutMutation) {
  const auto member = create_box(0);
  const auto outside = create_box(4);
  ASSERT_TRUE(editor_.scene().set_visible(outside.entity, false).ok());
  const auto outside_mode = session_.editor_mode().snapshot();
  const auto before = capture_command_observable_state(editor_);
  EXPECT_FALSE(editor_.selection().select_vertex(member, 1).ok());
  expect_unchanged(before, outside_mode);

  enter({member.entity}, member.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_vertices(member, {1, 2}).ok());
  ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
  const auto preview = capture_command_observable_state(editor_);
  const auto preview_mode = session_.editor_mode().snapshot();
  for (const auto kind : {QuaderComponentKind::Vertex, QuaderComponentKind::Edge,
                          QuaderComponentKind::Face}) {
    auto selection = QuaderComponentSelectionArgs{
        .object = outside, .kind = kind, .edit = QuaderSelectionEdit::Add};
    if (kind == QuaderComponentKind::Vertex) selection.vertices = {1};
    if (kind == QuaderComponentKind::Edge) selection.edges = {{1, 2}};
    if (kind == QuaderComponentKind::Face) selection.faces = {1};
    const auto rejected = editor_.selection().select_components(selection);
    EXPECT_FALSE(rejected.ok());
    EXPECT_FALSE(rejected.changed());
    expect_unchanged(preview, preview_mode);
    EXPECT_TRUE(session_.modeling().operation_preview_active());
  }
  ASSERT_TRUE(editor_.modeling().cancel_preview().ok());
}

TEST_F(EditorModeStructuralTests,
       BridgePreservesUnselectedMeshAndSameGenerationUndoRestoresConsumedMember) {
  const auto first = create_quad(0);
  const auto second = create_quad(4);
  const auto empty = create_box(8);
  enter({first.entity, second.entity, empty.entity}, second.entity);
  ASSERT_FALSE(HasFatalFailure());
  select_bridge_pair(first, second);
  ASSERT_FALSE(HasFatalFailure());
  const auto generation = editor_.mode().generation();
  const auto empty_before = editor_.query().compiled_object_mesh(empty);
  ASSERT_TRUE(empty_before.has_value());
  const auto bridge = editor_.commands().execute(quader_command::kModelingBridgeEdges);
  ASSERT_TRUE(bridge.ok()) << bridge.error_message();
  ASSERT_TRUE(bridge.changed());
  expect_members({second.entity});
  EXPECT_FALSE(editor_.query().entity(first.entity).has_value());
  EXPECT_EQ(editor_.query().compiled_object_mesh(empty)->indices, empty_before->indices);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_members({first.entity, second.entity});
  EXPECT_EQ(editor_.mode().generation(), generation);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_members({second.entity});
  EXPECT_EQ(editor_.mode().generation(), generation);

  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  enter({empty.entity}, empty.entity);
  ASSERT_FALSE(HasFatalFailure());
  EXPECT_NE(editor_.mode().generation(), generation);
  ASSERT_EQ(editor_.query().history().undo_label, "Select Entities");
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(first.entity).has_value());
  expect_members({empty.entity});
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_TRUE(editor_.query().entity(first.entity).has_value());
  expect_members({empty.entity});
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_members({empty.entity});
}

TEST_F(EditorModeStructuralTests,
       MergePreviewHistoryRestoresConsumedParticipantsAndPreservesUnselectedMesh) {
  const auto first = create_box(0);
  const auto survivor = create_box(0);
  const auto empty = create_box(8);
  enter({first.entity, survivor.entity, empty.entity}, survivor.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_vertex(first, 1).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(
      survivor, 1, QuaderSelectionEdit::Add).ok());
  const auto generation = editor_.mode().generation();
  const auto before = capture_command_observable_state(editor_);
  const auto mode_before = session_.editor_mode().snapshot();
  ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
  {
    SCOPED_TRACE("Active merge preview retains selected participants");
    expect_members({first.entity, survivor.entity});
    EXPECT_EQ(editor_.mode().generation(), generation);
  }
  ASSERT_TRUE(editor_.modeling().cancel_preview().ok());
  {
    SCOPED_TRACE("Canceled merge preview restores its context");
    expect_members({first.entity, survivor.entity});
    EXPECT_EQ(session_.editor_mode().snapshot(), mode_before);
  }
  ASSERT_TRUE(editor_.modeling().begin_preview("merge_by_distance").ok());
  ASSERT_TRUE(editor_.modeling().commit_preview("Merge Edit Participants").ok());
  {
    SCOPED_TRACE("Committed merge consumes only its source");
    expect_members({survivor.entity});
  }
  EXPECT_FALSE(editor_.query().entity(first.entity).has_value());
  ASSERT_TRUE(editor_.history().undo().ok());
  {
    SCOPED_TRACE("Undo restores the consumed source and selected participants");
    expect_members({first.entity, survivor.entity});
  }
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
  EXPECT_EQ(editor_.mode().generation(), generation);
  const auto restored = capture_command_observable_state(editor_);
  const auto difference = diff_command_observable_state(
      before, restored,
      command_observable_domain_mask(CommandObservableDomain::Geometry) |
          command_observable_domain_mask(CommandObservableDomain::Selection));
  EXPECT_TRUE(difference.empty()) << difference.summary;
  ASSERT_TRUE(editor_.history().redo().ok());
  {
    SCOPED_TRACE("Redo consumes only its source");
    expect_members({survivor.entity});
  }
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 1U);
  EXPECT_EQ(editor_.mode().generation(), generation);
}

TEST_F(EditorModeStructuralTests,
       ExtractKeepsOutputUnselectedUntilObjectModeAndPermitsSubsequentComponentSelection) {
  const auto source = create_box(0);
  const auto empty = create_box(4);
  enter({source.entity, empty.entity}, source.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_face(source, 1).ok());
  const auto extracted = editor_.commands().execute(quader_command::kModelingExtractFaces);
  ASSERT_TRUE(extracted.ok()) << extracted.error_message();
  ASSERT_TRUE(extracted.changed());
  const auto objects = editor_.query().modeling_objects();
  const auto output = std::ranges::find_if(objects, [&](const auto &object) {
    return object.entity != source.entity && object.entity != empty.entity;
  });
  ASSERT_NE(output, objects.end());
  const auto output_entity = output->entity;
  const auto output_ref = editor_.query().modeling_object_for_id(output->modeling_object_id);
  ASSERT_TRUE(output_ref.has_value());
  expect_members({source.entity});
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_members({source.entity});
  EXPECT_FALSE(editor_.query().entity(output_entity).has_value());
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_members({source.entity});
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  auto selected = session_.scene().selected_entities();
  std::ranges::sort(selected);
  auto expected = std::vector{source.entity, output_entity};
  std::ranges::sort(expected);
  EXPECT_EQ(selected, expected);
  ASSERT_TRUE(editor_.selection().select_object(source).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  expect_members({source.entity});
  ASSERT_TRUE(editor_.selection().select_face(*output_ref, 1).ok());
  expect_members({output_entity});
}

TEST_F(EditorModeStructuralTests,
       FailedStructuralPublicationRestoresMembershipGenerationAndAuthoredState) {
  const auto first = create_quad(0);
  const auto second = create_quad(4);
  const auto empty = create_box(8);
  enter({first.entity, second.entity, empty.entity}, second.entity);
  ASSERT_FALSE(HasFatalFailure());
  select_bridge_pair(first, second);
  ASSERT_FALSE(HasFatalFailure());
  const auto before = capture_command_observable_state(editor_);
  const auto mode_before = session_.editor_mode().snapshot();
  for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                           ModelingSdkProjectionFailurePoint::BeforeCommit}) {
    SCOPED_TRACE(static_cast<int>(point));
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = editor_.commands().execute(quader_command::kModelingBridgeEdges);
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    expect_unchanged(before, mode_before);
  }
}

TEST_F(EditorModeStructuralTests,
       ObjectTransformAndMaterialKeepDormantComponentIdsThroughUndoAndReentry) {
  const auto object = create_box(0);
  enter({object.entity}, object.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_vertices(object, {1, 3}).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto material = editor_.assets().add_material(
      QuaderMaterialArgs{.name = "Dormant selection material"});
  ASSERT_TRUE(material.ok());
  ASSERT_TRUE(editor_.modeling().move_selection({2, 0, 0}).ok());
  ASSERT_TRUE(editor_.object(object).assign_material(material.value().id).ok());
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 0U);
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.history().redo().ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
  std::vector<QuaderElementId> selected;
  for (const auto &point : editor_.query().selection_overlay().points) {
    if (point.layer == QuaderOverlayLayer::SelectedVertex &&
        point.element.object_id == object.modeling_object_id) {
      selected.push_back(point.element.vertex_id);
    }
  }
  std::ranges::sort(selected);
  selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
  EXPECT_EQ(selected, (std::vector<QuaderElementId>{1, 3}));
}

TEST_F(EditorModeStructuralTests,
       ObjectHalfCutPreservesDormantSurvivingVertexIdsAndUndoRestoresThem) {
  const auto object = create_box(0);
  enter({object.entity}, object.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_vertices(object, {1, 3}).ok());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  const auto before = editor_.query().compiled_object_mesh(object);
  ASSERT_TRUE(before.has_value());
  ASSERT_TRUE(editor_.commands().execute(quader_command::kModelingHalfCut).ok());
  ASSERT_TRUE(editor_.modeling().commit_preview("Half-Cut Meshes").ok());
  const auto cut = editor_.query().compiled_object_mesh(object);
  ASSERT_TRUE(cut.has_value());
  EXPECT_GT(cut->indices.size(), before->indices.size());
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_members({object.entity});
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
  EXPECT_EQ(editor_.query().compiled_object_mesh(object)->indices, before->indices);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_members({object.entity});
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
}

TEST_F(EditorModeStructuralTests,
       EmptyComponentSelectionDoesNotFallBackToTransformingEditMeshes) {
  const auto first = create_box(0);
  const auto second = create_box(4);
  enter({first.entity, second.entity}, first.entity);
  ASSERT_FALSE(HasFatalFailure());
  for (const auto kind : {QuaderSelectionMode::Vertex, QuaderSelectionMode::Edge,
                          QuaderSelectionMode::Face}) {
    ASSERT_TRUE(editor_.selection().set_mode(kind).ok());
    const auto before = capture_command_observable_state(editor_);
    const auto mode_before = session_.editor_mode().snapshot();
    const auto moved = editor_.modeling().move_selection({3, 0, 0});
    EXPECT_FALSE(moved.changed());
    expect_unchanged(before, mode_before);
    expect_members({first.entity, second.entity});
  }
}

TEST_F(EditorModeStructuralTests,
       ObjectDeleteUndoPreservesUnrelatedCurrentEditMembershipAndSelectionKind) {
  const auto deleted = create_box(0);
  const auto member = create_box(4);
  ASSERT_TRUE(editor_.selection().select_objects({deleted.entity}, deleted.entity).ok());
  ASSERT_TRUE(editor_.commands().execute(quader_command::kDeleteSelection).ok());
  ASSERT_FALSE(editor_.query().entity(deleted.entity).has_value());
  enter({member.entity}, member.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  const auto generation = editor_.mode().generation();
  ASSERT_EQ(editor_.query().history().undo_label, "Select Entities");
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(deleted.entity).has_value());
  expect_members({member.entity});
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_TRUE(editor_.query().entity(deleted.entity).has_value());
  expect_members({member.entity});
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{member.entity});
  EXPECT_EQ(session_.scene().primary_selection(), member.entity);
  EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor_.mode().generation(), generation);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_FALSE(editor_.query().entity(deleted.entity).has_value());
  expect_members({member.entity});
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{member.entity});
  EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
  EXPECT_EQ(editor_.mode().generation(), generation);
}

TEST_F(EditorModeStructuralTests,
       GroupDeleteUndoPreservesUnrelatedCurrentEditMembershipAndSelectionKind) {
  const auto grouped = create_box(0);
  const auto member = create_box(4);
  ASSERT_TRUE(editor_.selection().select_objects({grouped.entity}, grouped.entity).ok());
  const auto group = editor_.scene().group_selection("Historical group");
  ASSERT_TRUE(group.ok()) << group.error_message();
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Group).ok());
  ASSERT_TRUE(editor_.selection().select_objects({group.value().id}, group.value().id).ok());
  ASSERT_TRUE(editor_.commands().execute(quader_command::kDeleteSelection).ok());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Object).ok());
  enter({member.entity}, member.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().set_mode(QuaderSelectionMode::Face).ok());
  const auto generation = editor_.mode().generation();
  ASSERT_EQ(editor_.query().history().undo_label, "Select Entities");
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_FALSE(editor_.query().entity(group.value().id).has_value());
  expect_members({member.entity});
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_TRUE(editor_.query().entity(group.value().id).has_value());
  EXPECT_TRUE(editor_.query().entity(grouped.entity).has_value());
  expect_members({member.entity});
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{member.entity});
  EXPECT_EQ(session_.scene().primary_selection(), member.entity);
  EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Face);
  EXPECT_EQ(editor_.mode().generation(), generation);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_FALSE(editor_.query().entity(group.value().id).has_value());
  EXPECT_FALSE(editor_.query().entity(grouped.entity).has_value());
  expect_members({member.entity});
  EXPECT_EQ(session_.scene().selected_entities(), std::vector<EntityId>{member.entity});
  EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Face);
  EXPECT_EQ(editor_.mode().generation(), generation);
}

TEST_F(EditorModeStructuralTests,
       ObjectCombineRemapsDormantSelectionsFromBothSourcesIntoTheSurvivor) {
  const auto first = create_box(0);
  const auto survivor = create_box(4);
  enter({first.entity, survivor.entity}, survivor.entity);
  ASSERT_FALSE(HasFatalFailure());
  ASSERT_TRUE(editor_.selection().select_vertex(first, 1).ok());
  ASSERT_TRUE(editor_.selection().select_vertex(
      survivor, 3, QuaderSelectionEdit::Add).ok());
  const auto selected_positions = [&] {
    std::vector<std::array<float, 3>> positions;
    for (const auto &point : editor_.query().selection_overlay().points) {
      if (point.layer == QuaderOverlayLayer::SelectedVertex) {
        positions.push_back({point.position.x, point.position.y, point.position.z});
      }
    }
    std::ranges::sort(positions);
    positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
    return positions;
  };
  const auto expected = selected_positions();
  ASSERT_EQ(expected.size(), 2U);
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Object).ok());
  ASSERT_TRUE(editor_.selection().select_objects(
      {first.entity, survivor.entity}, survivor.entity).ok());
  const auto combined = editor_.commands().execute(quader_command::kModelingCombineMeshes);
  ASSERT_TRUE(combined.ok()) << combined.error_message();
  EXPECT_FALSE(editor_.query().entity(first.entity).has_value());
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(editor_.mode().current(), QuaderEditorMode::Object);
  ASSERT_TRUE(editor_.mode().set(QuaderEditorMode::Edit).ok());
  expect_members({survivor.entity});
  EXPECT_EQ(editor_.selection().summary().selected_vertex_count, 2U);
  EXPECT_EQ(selected_positions(), expected);
}

} // namespace
} // namespace quader::test
