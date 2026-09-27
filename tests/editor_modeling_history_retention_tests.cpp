////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling_command_execution_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "support/curve_modeling_test_support.hpp"

#include <quader/modeling/modeling.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <vector>

namespace quader {
namespace {

using test::curve::expect_same_history;
using test::curve::expect_same_mesh_geometry;
using test::curve::expect_same_revisions;

class EditorModelingHistoryRetentionTests : public testing::Test {
protected:
  EditorModelingHistoryRetentionTests()
      : api_session_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
        editor_(api_session_) {}

  [[nodiscard]] const modeling::ModelingSession &sdk() const {
    return *static_cast<const modeling::ModelingSession *>(
        session_.modeling().modeling_sdk_session_identity_for_tests());
  }

  [[nodiscard]] QuaderApiResult<QuaderModelingObjectRef> create_box() {
    return editor_.modeling().create_box(
        QuaderBounds{.min = {-1.0F, -1.0F, -1.0F},
                     .max = {1.0F, 1.0F, 1.0F}},
        "History Retention Box");
  }

  [[nodiscard]] ModelingCreateBoxResult create_unpublished_history_box() {
    // This owner-level setup deliberately leaves SDK history pending, as a
    // native command does before publishing its host-owned history entry.
    return session_.modeling().create_box_from_bounds({
        .bounds = {.min = {-1.0F, -1.0F, -1.0F},
                   .max = {1.0F, 1.0F, 1.0F}},
        .name = "Pending History Box",
    });
  }

  SceneSession session_;
  QuaderApiSession api_session_;
  QuaderEditorApi editor_;
};

TEST_F(EditorModelingHistoryRetentionTests,
       BoundedNativeHistoryDoesNotRetainASecondSdkUndoStack) {
  constexpr std::size_t kLimit = 4;
  constexpr std::size_t kCreated = 40;
  session_.history().set_max_steps(kLimit);
  std::vector<QuaderModelingObjectRef> objects;
  objects.reserve(kCreated);

  for (std::size_t index = 0; index < kCreated; ++index) {
    SCOPED_TRACE(index);
    const auto created = create_box();
    ASSERT_TRUE(created.ok()) << created.error_message();
    objects.push_back(created.value());
    EXPECT_EQ(sdk().undo_depth(), 0U);
    EXPECT_FALSE(sdk().can_redo());
    EXPECT_LE(session_.history().undo_count(), kLimit);
  }
  EXPECT_EQ(session_.history().undo_count(), kLimit);
  EXPECT_EQ(editor_.query().modeling_objects().size(), kCreated);

  for (std::size_t index = 0; index < kLimit; ++index) {
    ASSERT_TRUE(editor_.history().undo().ok());
    EXPECT_FALSE(editor_.query().entity(objects[kCreated - index - 1].entity)
                     .has_value());
    EXPECT_EQ(editor_.query().modeling_objects().size(), kCreated - index - 1);
    EXPECT_EQ(sdk().undo_depth(), 0U);
    EXPECT_FALSE(sdk().can_redo());
  }
  EXPECT_FALSE(editor_.query().history().can_undo);
  EXPECT_EQ(session_.history().redo_count(), kLimit);
  EXPECT_TRUE(editor_.query().entity(objects.front().entity).has_value());

  for (std::size_t index = kCreated - kLimit; index < kCreated; ++index) {
    ASSERT_TRUE(editor_.history().redo().ok());
    const auto restored =
        editor_.query().modeling_object_for_entity(objects[index].entity);
    ASSERT_TRUE(restored.has_value());
    EXPECT_EQ(restored->modeling_object_id, objects[index].modeling_object_id);
    EXPECT_EQ(editor_.query().modeling_objects().size(), index + 1);
    EXPECT_EQ(sdk().undo_depth(), 0U);
    EXPECT_FALSE(sdk().can_redo());
  }
  EXPECT_FALSE(editor_.query().history().can_redo);
}

TEST_F(EditorModelingHistoryRetentionTests,
       DisabledNativeUndoStillReleasesSdkHistoryAfterPublication) {
  session_.history().set_max_steps(0);
  for (std::size_t index = 0; index < 8; ++index) {
    const auto created = create_box();
    ASSERT_TRUE(created.ok()) << created.error_message();
    EXPECT_TRUE(editor_.query().entity(created.value().entity).has_value());
    EXPECT_EQ(sdk().undo_depth(), 0U);
    EXPECT_FALSE(sdk().can_redo());
    EXPECT_FALSE(editor_.query().history().can_undo);
    EXPECT_FALSE(editor_.query().history().can_redo);
  }
  EXPECT_EQ(editor_.query().modeling_objects().size(), 8U);
}

TEST_F(EditorModelingHistoryRetentionTests,
       IdleCleanupDropsOnlyHistoryAndPreservesAuthoredStateAndRevisions) {
  const auto created = create_unpublished_history_box();
  ASSERT_TRUE(created.ok) << created.message;
  ASSERT_GT(sdk().undo_depth(), 0U);
  const auto object =
      editor_.query().modeling_object_for_entity(created.object.entity);
  ASSERT_TRUE(object.has_value());
  const auto mesh_before = editor_.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_before.has_value());
  const auto revisions_before = editor_.revisions();
  const auto history_before = editor_.query().history();
  const auto entities_before = session_.scene().entities();
  const auto selected_before = session_.scene().selected_entities();
  const auto meshes_before = session_.assets().mesh_ids();
  const auto sdk_content_before = sdk().content_revision();
  const auto sdk_selection_before = sdk().selection_revision();
  const auto sdk_active_before = sdk().active_object();

  session_.modeling().release_redundant_sdk_history();
  session_.modeling().release_redundant_sdk_history();

  EXPECT_EQ(sdk().undo_depth(), 0U);
  EXPECT_FALSE(sdk().can_redo());
  EXPECT_EQ(sdk().content_revision(), sdk_content_before);
  EXPECT_EQ(sdk().selection_revision(), sdk_selection_before);
  EXPECT_EQ(sdk().active_object(), sdk_active_before);
  EXPECT_EQ(sdk().objects().size(), 1U);
  expect_same_revisions(editor_.revisions(), revisions_before);
  expect_same_history(editor_.query().history(), history_before);
  EXPECT_EQ(session_.scene().entities(), entities_before);
  EXPECT_EQ(session_.scene().selected_entities(), selected_before);
  EXPECT_EQ(session_.assets().mesh_ids(), meshes_before);
  const auto mesh_after = editor_.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_after.has_value());
  expect_same_mesh_geometry(*mesh_after, *mesh_before);
}

TEST_F(EditorModelingHistoryRetentionTests,
       ActivePreviewKeepsPendingSdkHistoryAndCancelRestoresItsBaseline) {
  const auto created = create_unpublished_history_box();
  ASSERT_TRUE(created.ok) << created.message;
  auto &service = session_.modeling();
  ASSERT_TRUE(service.set_selection_mode(ModelingSelectionMode::Face).ok);
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.object.modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .edit = ModelingSelectionEdit::Replace,
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto object =
      editor_.query().modeling_object_for_entity(created.object.entity);
  ASSERT_TRUE(object.has_value());
  const auto mesh_before = editor_.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_before.has_value());
  const auto revisions_before = editor_.revisions();
  const auto sdk_content_before = sdk().content_revision();
  const auto sdk_selection_before = sdk().selection_revision();
  const auto depth_before = sdk().undo_depth();
  ASSERT_GT(depth_before, 0U);

  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(service.update_transform_translate_preview({0.0F, 2.0F, 0.0F}).ok);
  ASSERT_TRUE(sdk().preview_active());
  service.release_redundant_sdk_history();
  EXPECT_TRUE(sdk().preview_active());
  EXPECT_EQ(sdk().undo_depth(), depth_before);

  ASSERT_TRUE(service.cancel_transform_preview().ok);
  EXPECT_FALSE(sdk().preview_active());
  EXPECT_EQ(sdk().undo_depth(), depth_before);
  EXPECT_EQ(sdk().content_revision(), sdk_content_before);
  EXPECT_EQ(sdk().selection_revision(), sdk_selection_before);
  expect_same_revisions(editor_.revisions(), revisions_before);
  const auto mesh_after = editor_.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_after.has_value());
  expect_same_mesh_geometry(*mesh_after, *mesh_before);

  service.release_redundant_sdk_history();
  EXPECT_EQ(sdk().undo_depth(), 0U);
}

TEST_F(EditorModelingHistoryRetentionTests,
       FailedPublicationKeepsNativeRedoAndExactCommittedState) {
  const auto first = create_box();
  ASSERT_TRUE(first.ok()) << first.error_message();
  const auto second = create_box();
  ASSERT_TRUE(second.ok()) << second.error_message();
  ASSERT_TRUE(editor_.history().undo().ok());
  ASSERT_TRUE(editor_.query().history().can_redo);
  const auto revisions_before = editor_.revisions();
  const auto history_before = editor_.query().history();
  const auto entities_before = session_.scene().entities();
  const auto selected_before = session_.scene().selected_entities();
  const auto meshes_before = session_.assets().mesh_ids();
  const auto sdk_content_before = sdk().content_revision();
  const auto sdk_selection_before = sdk().selection_revision();
  const auto undo_before = session_.history().undo_count();
  const auto redo_before = session_.history().redo_count();
  constexpr std::array kFailures{
      ModelingSdkProjectionFailurePoint::AfterSdkMutation,
      ModelingSdkProjectionFailurePoint::AfterAssetInstall,
      ModelingSdkProjectionFailurePoint::AfterEntityCreate,
      ModelingSdkProjectionFailurePoint::AfterSceneBinding,
      ModelingSdkProjectionFailurePoint::AfterSelection,
      ModelingSdkProjectionFailurePoint::BeforeCommit,
  };

  for (const auto failure : kFailures) {
    SCOPED_TRACE(static_cast<int>(failure));
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(failure);
    const auto failed = create_box();
    session_.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(failed.ok());
    EXPECT_FALSE(failed.changed());
    EXPECT_EQ(sdk().undo_depth(), 0U);
    EXPECT_FALSE(sdk().can_redo());
    EXPECT_EQ(sdk().content_revision(), sdk_content_before);
    EXPECT_EQ(sdk().selection_revision(), sdk_selection_before);
    expect_same_revisions(editor_.revisions(), revisions_before);
    expect_same_history(editor_.query().history(), history_before);
    EXPECT_EQ(session_.history().undo_count(), undo_before);
    EXPECT_EQ(session_.history().redo_count(), redo_before);
    EXPECT_EQ(session_.scene().entities(), entities_before);
    EXPECT_EQ(session_.scene().selected_entities(), selected_before);
    EXPECT_EQ(session_.assets().mesh_ids(), meshes_before);
  }

  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_TRUE(editor_.query().entity(second.value().entity).has_value());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  EXPECT_EQ(sdk().undo_depth(), 0U);
  EXPECT_FALSE(sdk().can_redo());
}

TEST_F(EditorModelingHistoryRetentionTests,
       ImplicitTransformSettlementPreservesGeometryAndRecordsOneUndoStep) {
  const auto created = create_box();
  ASSERT_TRUE(created.ok());
  auto &service = session_.modeling();
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.value().modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
      .active_kind = ModelingActiveSelectionKind::Face,
      .active_face = 1,
  }).ok);
  const auto before = editor_.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(before.has_value());
  const auto history_before = session_.history().undo_count();
  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(service.update_transform_translate_preview({0.0F, 2.0F, 0.0F}).ok);
  // The committed mesh intentionally stays unchanged until acceptance; obtain
  // the displayed candidate from the retained preview's packed positions.
  const auto previews = service.tool_preview_meshes();
  ASSERT_EQ(previews.size(), 1U);
  const auto &preview = previews.front();
  QuaderMeshAsset candidate = *before;
  ASSERT_EQ(preview.mesh.geometry().vertices.size(), candidate.vertices.size());
  for (std::size_t index = 0; index < candidate.vertices.size(); ++index) {
    const QVec3 position = preview.dynamic_position_payload
        ? preview.dynamic_position_payload->at(index)
        : preview.mesh.geometry().vertices[index].position;
    candidate.vertices[index].position = {position.x, position.y, position.z};
  }

  // Raw owner transitions cannot silently discard an editor-owned draft.
  EXPECT_FALSE(service.activate_viewport_tool(ModelingViewportTool::Rotate).ok);
  EXPECT_FALSE(service.set_selection_mode(ModelingSelectionMode::Object).ok);
  EXPECT_TRUE(service.pending_operation_active());
  ModelingCommandExecutionService commands;
  const auto accepted = commands.settle_pending_operations(session_.history(), service);
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_FALSE(service.pending_operation_active());
  EXPECT_EQ(session_.history().undo_count(), history_before + 1U);
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()),
                            candidate);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()),
                            *before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()),
                            candidate);
}

TEST_F(EditorModelingHistoryRetentionTests,
       FailedImplicitTransformSettlementRetainsCandidateForRetry) {
  const auto created = create_box();
  ASSERT_TRUE(created.ok());
  auto &service = session_.modeling();
  ASSERT_TRUE(service.set_component_selection({
      .modeling_object_id = created.value().modeling_object_id,
      .mode = ModelingSelectionMode::Face,
      .faces = {1},
  }).ok);
  ASSERT_TRUE(service.begin_transform_preview(false).ok);
  ASSERT_TRUE(service.update_transform_translate_preview({0.0F, 2.0F, 0.0F}).ok);
  const auto candidate = editor_.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(candidate.has_value());
  const auto history_before = session_.history().undo_count();
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::CommittedSceneNotify);
  ModelingCommandExecutionService commands;
  const auto failed = commands.settle_pending_operations(session_.history(), service);
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed.ok);
  EXPECT_FALSE(failed.changed);
  EXPECT_TRUE(service.pending_operation_active());
  EXPECT_EQ(session_.history().undo_count(), history_before);
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()),
                            *candidate);
  const auto retried = commands.settle_pending_operations(session_.history(), service);
  ASSERT_TRUE(retried.ok) << retried.message;
  EXPECT_FALSE(service.pending_operation_active());
  EXPECT_EQ(session_.history().undo_count(), history_before + 1U);
}

TEST_F(EditorModelingHistoryRetentionTests,
       KnifeSettlementRejectsIncompleteStrokeAndAcceptsCompletedCut) {
  const auto created = create_box();
  ASSERT_TRUE(created.ok());
  auto &service = session_.modeling();
  const auto before = editor_.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(before.has_value());
  const auto history_before = session_.history().undo_count();
  const auto event_at = [](float y) {
    ModelingToolRayEvent event;
    event.ray = {.origin = {0.0F, y, 6.0F}, .direction = {0.0F, 0.0F, -1.0F}};
    event.camera = {.orthographic = true, .origin = event.ray.origin,
                    .viewport_size = {800.0F, 600.0F}, .orthographic_size = 4.0};
    event.grid_size = 0.5;
    event.vertex_radius = 0.001;
    event.edge_radius = 0.12;
    event.snap_enabled = false;
    return event;
  };
  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::Knife).ok);
  ASSERT_TRUE(service.accept_knife_point(event_at(-1.0F)).ok);
  ASSERT_TRUE(service.tool_status().knife_has_active_stroke);
  ModelingCommandExecutionService commands;
  const auto incomplete = commands.settle_pending_operations(session_.history(), service);
  EXPECT_FALSE(incomplete.ok);
  EXPECT_TRUE(service.tool_status().knife_has_active_stroke);
  EXPECT_EQ(session_.history().undo_count(), history_before);
  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::Knife).ok);
  EXPECT_TRUE(service.tool_status().knife_has_active_stroke);
  ASSERT_TRUE(service.accept_knife_point(event_at(1.0F)).ok);
  ASSERT_TRUE(service.tool_status().knife_can_commit);
  ASSERT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Edge);
  const auto completed = editor_.selection().set_mode(QuaderSelectionMode::Face);
  ASSERT_TRUE(completed.ok()) << completed.error_message();
  EXPECT_EQ(editor_.selection().summary().mode, QuaderSelectionMode::Face);
  EXPECT_FALSE(service.pending_operation_active());
  EXPECT_EQ(session_.history().undo_count(), history_before + 1U);
  const auto after = editor_.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(after.has_value());
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()), *before);
  ASSERT_TRUE(editor_.history().redo().ok());
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()), *after);
}

TEST_F(EditorModelingHistoryRetentionTests,
       LoopHoverSettlementCreatesNoGeometryButDraggingAcceptsThroughHistory) {
  const auto created = create_box();
  ASSERT_TRUE(created.ok());
  auto &service = session_.modeling();
  const auto before = editor_.query().compiled_object_mesh(created.value());
  ASSERT_TRUE(before.has_value());
  const auto history_before = session_.history().undo_count();
  ModelingToolRayEvent event;
  event.ray = {.origin = {1.0F, 0.0F, 5.0F}, .direction = {0.0F, 0.0F, -1.0F}};
  event.camera = {.orthographic = true, .origin = {0.0F, 0.0F, 5.0F},
                  .viewport_size = {800.0F, 600.0F}, .orthographic_size = 4.0};
  event.snap_enabled = false;
  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop).ok);
  ASSERT_TRUE(service.update_insert_edge_loop_hover(event).ok);
  ASSERT_TRUE(service.tool_status().insert_edge_loop_has_preview);
  EXPECT_FALSE(service.pending_operation_active());
  ModelingCommandExecutionService commands;
  ASSERT_TRUE(commands.settle_pending_operations(session_.history(), service).ok);
  EXPECT_EQ(session_.history().undo_count(), history_before);
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()), *before);
  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::InsertEdgeLoop).ok);
  ASSERT_TRUE(service.begin_insert_edge_loop_drag(event).ok);
  ASSERT_TRUE(service.tool_status().insert_edge_loop_dragging);
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::BeforeCommit);
  const auto failed = commands.settle_pending_operations(session_.history(), service);
  service.set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(failed.ok);
  EXPECT_TRUE(service.tool_status().insert_edge_loop_dragging);
  EXPECT_EQ(session_.history().undo_count(), history_before);
  const auto accepted = commands.settle_pending_operations(session_.history(), service);
  ASSERT_TRUE(accepted.ok) << accepted.message;
  EXPECT_EQ(session_.history().undo_count(), history_before + 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  expect_same_mesh_geometry(*editor_.query().compiled_object_mesh(created.value()), *before);
}

TEST_F(EditorModelingHistoryRetentionTests,
       PolySelectionModeTransitionAcceptsValidFaceAndRetainsIncompleteDraft) {
  ASSERT_TRUE(create_box().ok());
  auto &service = session_.modeling();
  const auto history_before = session_.history().undo_count();
  ASSERT_TRUE(service.activate_viewport_tool(ModelingViewportTool::Poly).ok);
  const auto event_at = [](float x, float y) {
    ModelingToolRayEvent event;
    event.ray = {.origin = {x, y, 6.0F}, .direction = {0.0F, 0.0F, -1.0F}};
    event.camera = {.orthographic = true, .origin = event.ray.origin,
                    .viewport_size = {800.0F, 600.0F}, .orthographic_size = 4.0};
    event.snap_enabled = false;
    return event;
  };
  ASSERT_TRUE(service.accept_poly_point(event_at(-0.5F, -0.5F)).ok);
  EXPECT_FALSE(editor_.selection().set_mode(QuaderSelectionMode::Edge).ok());
  EXPECT_EQ(service.tool_status().poly_point_count, 1);
  EXPECT_EQ(session_.history().undo_count(), history_before);
  ASSERT_TRUE(service.accept_poly_point(event_at(0.5F, -0.5F)).ok);
  ASSERT_TRUE(service.accept_poly_point(event_at(0.5F, 0.5F)).ok);
  ASSERT_EQ(service.tool_status().poly_point_count, 3);
  const auto accepted = editor_.selection().set_mode(QuaderSelectionMode::Edge);
  ASSERT_TRUE(accepted.ok()) << accepted.error_message();
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
  EXPECT_EQ(session_.history().undo_count(), history_before + 1U);
  ASSERT_TRUE(editor_.history().undo().ok());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 1U);
  ASSERT_TRUE(editor_.history().redo().ok());
  EXPECT_EQ(editor_.query().modeling_objects().size(), 2U);
}

} // namespace
} // namespace quader
