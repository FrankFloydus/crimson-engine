////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/history/editor_history_service.hpp"
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

} // namespace
} // namespace quader
