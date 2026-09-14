////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_session.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <utility>

namespace quader {
namespace {

struct FaceTextureCanvasHarness final {
  SceneSession session;
  QuaderEditorApi editor{QuaderApiSessionInternalAccess::from_scene_session(session)};

  QuaderModelingObjectRef create_box() {
    const auto created = editor.modeling().create_box(
        QuaderBounds{.min = {0.0f, 0.0f, 0.0f}, .max = {128.0f, 64.0f, 32.0f}},
        "Face texture canvas");
    EXPECT_TRUE(created.ok()) << created.error_message();
    if (!created.ok()) return {};
    EXPECT_TRUE(editor.selection().set_mode(QuaderSelectionMode::Face).ok());
    EXPECT_TRUE(editor.selection().select_face(created.value(), 1).ok());
    return created.value();
  }

  QuaderApiVoidResult begin_translation() {
    const auto canvas = editor.modeling().face_texture_canvas();
    return editor.modeling().begin_face_texture_gesture({
        .kind = QuaderFaceTextureGestureKind::Translate,
        .selection_token = canvas.selection_token, .start = {0.0f, 0.0f}});
  }
};

void expect_same_mapping(const QuaderFaceTextureCanvas &actual,
                         const QuaderFaceTextureCanvas &expected) {
  ASSERT_EQ(actual.outline.size(), expected.outline.size());
  for (std::size_t index = 0; index < actual.outline.size(); ++index) {
    EXPECT_FLOAT_EQ(actual.outline[index].x, expected.outline[index].x);
    EXPECT_FLOAT_EQ(actual.outline[index].y, expected.outline[index].y);
  }
  for (std::size_t index = 0; index < 4; ++index) {
    EXPECT_NEAR(actual.uv_from_plane[index], expected.uv_from_plane[index], 1.0e-6);
  }
  EXPECT_NEAR(actual.uv_offset.x, expected.uv_offset.x, 1.0e-6);
  EXPECT_NEAR(actual.uv_offset.y, expected.uv_offset.y, 1.0e-6);
}

void expect_same_history(const QuaderHistorySnapshot &actual,
                         const QuaderHistorySnapshot &expected) {
  EXPECT_EQ(actual.can_undo, expected.can_undo);
  EXPECT_EQ(actual.can_redo, expected.can_redo);
  EXPECT_EQ(actual.undo_label, expected.undo_label);
  EXPECT_EQ(actual.redo_label, expected.redo_label);
}

/// Drives semantic scale handles along the selected image axis in the face plane.
QVec2 texture_axis_point(const QuaderFaceTextureCanvas &canvas, double u, double v) {
  const auto &m = canvas.uv_from_plane;
  const double determinant = m[0] * m[3] - m[1] * m[2];
  return {static_cast<float>((m[3] * u - m[1] * v) / determinant),
          static_cast<float>((-m[2] * u + m[0] * v) / determinant)};
}

TEST(FaceTextureCanvasApiTests, OnlyOneFaceEnablesCanvasAndMissingImageKeepsOutline) {
  FaceTextureCanvasHarness harness;
  auto &editor = harness.editor;
  EXPECT_EQ(editor.modeling().face_texture_canvas().selected_face_count, 0u);
  EXPECT_TRUE(editor.modeling().face_texture_canvas().outline.empty());
  const auto object = harness.create_box();
  const auto single = editor.modeling().face_texture_canvas();
  EXPECT_EQ(single.selected_face_count, 1u);
  EXPECT_EQ(single.object_id, object.modeling_object_id);
  EXPECT_EQ(single.face_id, 1);
  EXPECT_TRUE(single.editable) << single.disabled_reason;
  EXPECT_EQ(single.outline.size(), 4u);
  EXPECT_GT(single.repeat_width, 0.0);
  EXPECT_GT(single.repeat_height, 0.0);
  ASSERT_TRUE(editor.selection().select_faces(object, {1, 2}).ok());
  const auto multiple = editor.modeling().face_texture_canvas();
  EXPECT_EQ(multiple.selected_face_count, 2u);
  EXPECT_FALSE(multiple.editable);
  EXPECT_TRUE(multiple.outline.empty());
  ASSERT_TRUE(editor.selection().clear().ok());
  EXPECT_TRUE(editor.modeling().face_texture_canvas().outline.empty());
}

TEST(FaceTextureCanvasApiTests, GesturePreviewsFromBaselineAndCreatesExactlyOneUndoEntry) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  const auto original = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().face_texture_canvas().gesture_active);
  for (const float position : {2.0f, 5.0f, 11.0f}) {
    const auto updated = editor.modeling().update_face_texture_gesture({.current = {position, 3.0f}});
    ASSERT_TRUE(updated.ok()) << updated.error_message();
  }
  const auto preview = editor.modeling().face_texture_canvas();
  EXPECT_NEAR(preview.uv_offset.x,
      original.uv_offset.x - 11.0 * original.uv_from_plane[0] - 3.0 * original.uv_from_plane[1], 1.0e-5);
  EXPECT_NEAR(preview.uv_offset.y,
      original.uv_offset.y - 11.0 * original.uv_from_plane[2] - 3.0 * original.uv_from_plane[3], 1.0e-5);
  const auto committed = editor.modeling().commit_face_texture_gesture();
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.changed());
  expect_same_mapping(editor.modeling().face_texture_canvas(), preview);
  EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), original);
  EXPECT_EQ(editor.history().snapshot().undo_label, history.undo_label);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), preview);
}

TEST(FaceTextureCanvasApiTests, CancelAndReturnToOriginPreserveRedoAndCleanContent) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {9.0f, 3.0f}}).ok());
  ASSERT_TRUE(editor.modeling().commit_face_texture_gesture().ok());
  const auto future = editor.modeling().face_texture_canvas();
  ASSERT_TRUE(editor.history().undo().ok());
  harness.session.mark_clean();
  const auto original = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(history.can_redo);
  for (const bool cancel : {true, false}) {
    ASSERT_TRUE(harness.begin_translation().ok());
    ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {20.0f, 2.0f}}).ok());
    if (cancel) {
      ASSERT_TRUE(editor.modeling().cancel_face_texture_gesture().ok());
    } else {
      ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {0.0f, 0.0f}}).ok());
      const auto accepted = editor.modeling().commit_face_texture_gesture();
      ASSERT_TRUE(accepted.ok()) << accepted.error_message();
      EXPECT_FALSE(accepted.changed());
    }
    expect_same_mapping(editor.modeling().face_texture_canvas(), original);
    expect_same_history(editor.history().snapshot(), history);
    EXPECT_FALSE(editor.project().dirty());
  }
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), future);
}

TEST(FaceTextureCanvasApiTests, StaleSelectionRejectsDelayedBeginAndInvalidatesGesture) {
  FaceTextureCanvasHarness harness;
  const auto object = harness.create_box();
  auto &editor = harness.editor;
  const auto old = editor.modeling().face_texture_canvas();
  ASSERT_TRUE(editor.selection().select_face(object, 2).ok());
  EXPECT_FALSE(editor.modeling().begin_face_texture_gesture({.selection_token = old.selection_token}).ok());
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {10.0f, 4.0f}}).ok());
  ASSERT_TRUE(editor.selection().select_face(object, 3).ok());
  const auto selected = editor.modeling().face_texture_canvas();
  EXPECT_FALSE(editor.modeling().update_face_texture_gesture({.current = {20.0f, 4.0f}}).ok());
  EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
  EXPECT_EQ(editor.modeling().face_texture_canvas().face_id, selected.face_id);
  ASSERT_TRUE(editor.modeling().cancel_face_texture_gesture().ok());
}

TEST(FaceTextureCanvasApiTests, PublicationFailureRestoresMappingAndHistory) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  const auto original = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  for (const auto point : {ModelingSdkProjectionFailurePoint::AfterSdkMutation,
                           ModelingSdkProjectionFailurePoint::PreviewAssetReplace,
                           ModelingSdkProjectionFailurePoint::PreviewSceneNotify}) {
    SCOPED_TRACE(static_cast<int>(point));
    ASSERT_TRUE(harness.begin_translation().ok());
    ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {2.0f, 1.0f}}).ok());
    harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed = editor.modeling().update_face_texture_gesture({.current = {7.0f, 2.0f}});
    EXPECT_FALSE(failed.ok());
    harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(ModelingSdkProjectionFailurePoint::None);
    EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
    expect_same_mapping(editor.modeling().face_texture_canvas(), original);
    expect_same_history(editor.history().snapshot(), history);
    ASSERT_TRUE(editor.modeling().cancel_face_texture_gesture().ok());
  }
}

TEST(FaceTextureCanvasApiTests, BeginFailureRollsBackAndCommitFailureKeepsCandidateForRetry) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  harness.session.mark_clean();
  const auto original = editor.modeling().face_texture_canvas();
  const auto initial_history = editor.history().snapshot();
  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::AfterSdkMutation);
  const auto failed_begin = harness.begin_translation();
  EXPECT_FALSE(failed_begin.ok());
  EXPECT_FALSE(failed_begin.changed());
  harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
      ModelingSdkProjectionFailurePoint::None);
  EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
  expect_same_mapping(editor.modeling().face_texture_canvas(), original);
  expect_same_history(editor.history().snapshot(), initial_history);
  EXPECT_FALSE(editor.project().dirty());

  for (const auto point : {ModelingSdkProjectionFailurePoint::BeforeCommit,
                           ModelingSdkProjectionFailurePoint::CommittedAssetReplace,
                           ModelingSdkProjectionFailurePoint::CommittedSceneNotify}) {
    SCOPED_TRACE(static_cast<int>(point));
    ASSERT_TRUE(harness.begin_translation().ok());
    ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {7.0f, 2.0f}}).ok());
    const auto candidate = editor.modeling().face_texture_canvas();
    const auto history = editor.history().snapshot();
    harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(point);
    const auto failed_commit = editor.modeling().commit_face_texture_gesture();
    EXPECT_FALSE(failed_commit.ok());
    EXPECT_FALSE(failed_commit.changed());
    harness.session.modeling().set_modeling_sdk_projection_failure_for_tests(
        ModelingSdkProjectionFailurePoint::None);
    EXPECT_TRUE(editor.modeling().face_texture_canvas().gesture_active);
    expect_same_mapping(editor.modeling().face_texture_canvas(), candidate);
    expect_same_history(editor.history().snapshot(), history);
    const auto retried = editor.modeling().commit_face_texture_gesture();
    ASSERT_TRUE(retried.ok()) << retried.error_message();
    EXPECT_TRUE(retried.changed());
    expect_same_mapping(editor.modeling().face_texture_canvas(), candidate);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_same_mapping(editor.modeling().face_texture_canvas(), original);
  }
}

TEST(FaceTextureCanvasApiTests, ModifierChangesUseOneBaselineAndReturningWithSnapPreservesRedo) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(editor.modeling().set_face_texture_setting({
      .field = QuaderFaceTextureField::ScaleX, .value = 1.031,
      .selection_token = editor.modeling().face_texture_settings().selection_token}).ok());
  ASSERT_TRUE(editor.viewports().set_grid_preset(2).ok());
  const double grid_step = viewport_grid_world_size_from_contract(editor.viewports().grid_preset());
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {3.0f, 5.0f}}).ok());
  ASSERT_TRUE(editor.modeling().commit_face_texture_gesture().ok());
  const auto future = editor.modeling().face_texture_canvas();
  ASSERT_TRUE(editor.history().undo().ok());
  harness.session.mark_clean();
  const auto original = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(history.can_redo);
  const QVec2 start = texture_axis_point(original, 1.0, 0.0);
  ASSERT_TRUE(editor.modeling().begin_face_texture_gesture({
      .kind = QuaderFaceTextureGestureKind::ScaleWidth,
      .selection_token = original.selection_token, .start = start}).ok());
  for (const auto modifiers : {0, 1, 3, 2, 0}) {
    SCOPED_TRACE(modifiers);
    const bool precision = (modifiers & 1) != 0;
    const bool snap = (modifiers & 2) != 0;
    ASSERT_TRUE(editor.modeling().update_face_texture_gesture({
        .current = texture_axis_point(original, 1.37, 0.0),
        .precision = precision, .snap = snap}).ok());
    const auto candidate = editor.modeling().face_texture_canvas();
    const double continuous = original.repeat_width * 1.37;
    const double expected = snap ? std::round(continuous / grid_step) * grid_step
        : precision ? std::round(continuous) : continuous;
    EXPECT_NEAR(candidate.repeat_width, expected, 1.0e-4);
    EXPECT_NEAR(candidate.repeat_height, original.repeat_height, 1.0e-4);
  }
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({
      .current = start, .precision = true, .snap = true}).ok());
  const auto returned = editor.modeling().commit_face_texture_gesture();
  ASSERT_TRUE(returned.ok()) << returned.error_message();
  EXPECT_FALSE(returned.changed());
  expect_same_mapping(editor.modeling().face_texture_canvas(), original);
  expect_same_history(editor.history().snapshot(), history);
  EXPECT_FALSE(editor.project().dirty());
  ASSERT_TRUE(editor.history().redo().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), future);
}

TEST(FaceTextureCanvasApiTests, GenericUndoAndCancelFinishTheTextureCheckpoint) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  harness.session.mark_clean();
  const auto original = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  for (const bool use_undo : {true, false}) {
    ASSERT_TRUE(harness.begin_translation().ok());
    ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {9.0f, 2.0f}}).ok());
    const auto canceled = use_undo ? editor.history().undo() : editor.modeling().cancel_preview();
    ASSERT_TRUE(canceled.ok()) << canceled.error_message();
    EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
    expect_same_mapping(editor.modeling().face_texture_canvas(), original);
    expect_same_history(editor.history().snapshot(), history);
    EXPECT_FALSE(editor.project().dirty());
  }
}

TEST(FaceTextureCanvasApiTests, SceneReplacementEndsGestureAndRejectsOldUpdates) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {9.0f, 2.0f}}).ok());
  ASSERT_TRUE(editor.scene_documents().new_scene().ok());
  EXPECT_FALSE(editor.modeling().face_texture_canvas().gesture_active);
  EXPECT_TRUE(editor.modeling().face_texture_canvas().outline.empty());
  EXPECT_FALSE(editor.modeling().update_face_texture_gesture({.current = {19.0f, 2.0f}}).ok());
  EXPECT_FALSE(editor.history().can_undo());
  ASSERT_TRUE(editor.modeling().cancel_face_texture_gesture().ok());
}

TEST(FaceTextureCanvasApiTests, UnchangedQueriesDoNotExtractGeometryOrRepublish) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(editor.modeling().face_texture_canvas().editable);
  const auto revision = editor.modeling().face_texture_settings_revision();
  const auto before = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int frame = 0; frame != 32; ++frame) {
    EXPECT_EQ(editor.modeling().face_texture_settings_revision(), revision);
    EXPECT_TRUE(editor.modeling().face_texture_canvas().editable);
    EXPECT_TRUE(editor.modeling().face_texture_settings().editable);
  }
  const auto after = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.face_texture_canvas_build_count, before.face_texture_canvas_build_count);
  EXPECT_EQ(after.face_texture_summary_build_count, before.face_texture_summary_build_count);
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
  EXPECT_EQ(after.asset_publication_count, before.asset_publication_count);
}

TEST(FaceTextureCanvasApiTests, IdleCancellationPreservesRevisionsAndRetainedQueries) {
  FaceTextureCanvasHarness harness;
  harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(editor.modeling().face_texture_canvas().editable);
  const auto revision = editor.modeling().face_texture_settings_revision();
  const auto settings = editor.modeling().face_texture_settings();
  const auto history = editor.history().snapshot();
  const auto before = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  for (int attempt = 0; attempt != 3; ++attempt) {
    const auto canceled = editor.modeling().cancel_face_texture_gesture();
    ASSERT_TRUE(canceled.ok()) << canceled.error_message();
    EXPECT_FALSE(canceled.changed());
    EXPECT_EQ(editor.modeling().face_texture_settings_revision(), revision);
    EXPECT_EQ(editor.modeling().face_texture_settings().selection_token, settings.selection_token);
    EXPECT_TRUE(editor.modeling().face_texture_canvas().editable);
    expect_same_history(editor.history().snapshot(), history);
  }
  const auto after = harness.session.modeling().modeling_sdk_projection_stats_for_tests();
  EXPECT_EQ(after.face_texture_canvas_build_count, before.face_texture_canvas_build_count);
  EXPECT_EQ(after.face_texture_summary_build_count, before.face_texture_summary_build_count);
  EXPECT_EQ(after.mesh_compile_count, before.mesh_compile_count);
  EXPECT_EQ(after.asset_publication_count, before.asset_publication_count);
}

TEST(FaceTextureCanvasApiTests, OccurrenceSamplingSurvivesPreviewCommitAndUndo) {
  FaceTextureCanvasHarness harness;
  const auto member = harness.create_box();
  auto &editor = harness.editor;
  ASSERT_TRUE(editor.selection().select_object(member).ok());
  const auto instance = editor.modeling().create_instance_from_selection();
  ASSERT_TRUE(instance.ok()) << instance.error_message();
  const auto occurrence = instance.value().occurrence.root.id;
  ASSERT_TRUE(editor.scene().set_transform(occurrence, {
      .position = {105.0f, -27.0f, 32.0f}, .scale = {-2.0f, 3.0f, 1.0f}}).ok());
  ASSERT_TRUE(editor.modeling().enter_instance_edit(occurrence).ok());
  ASSERT_TRUE(editor.selection().select_face(member, 1).ok());
  const auto original = editor.modeling().face_texture_canvas();
  ASSERT_TRUE(original.editable) << original.disabled_reason;
  const auto instance_before = editor.query().entity(occurrence);
  ASSERT_TRUE(instance_before.has_value());
  ASSERT_TRUE(instance_before->texture_sampling_from_local.has_value());
  const auto sampling = instance_before->texture_sampling_from_local->values;
  ASSERT_TRUE(harness.begin_translation().ok());
  ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = {16.0f, -8.0f}}).ok());
  const auto preview = editor.modeling().face_texture_canvas();
  ASSERT_TRUE(editor.modeling().commit_face_texture_gesture().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), preview);
  const auto instance_after = editor.query().entity(occurrence);
  ASSERT_TRUE(instance_after.has_value());
  ASSERT_TRUE(instance_after->texture_sampling_from_local.has_value());
  EXPECT_EQ(instance_after->texture_sampling_from_local->values, sampling);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_same_mapping(editor.modeling().face_texture_canvas(), original);
}

TEST(FaceTextureCanvasApiTests, StoredNumbersFollowEveryPreviewAndPersistAfterReselection) {
  FaceTextureCanvasHarness harness;
  const auto object = harness.create_box();
  auto &editor = harness.editor;
  const auto expect_numbers = [&] {
    const auto canvas = editor.modeling().face_texture_canvas();
    const auto values = editor.modeling().face_texture_settings();
    const auto mesh = editor.query().compiled_object_mesh(object);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_FALSE(mesh->face_textures.empty());
    const auto &stored = mesh->face_textures.front();
    EXPECT_DOUBLE_EQ(*values.scale_x, stored.scale.x);
    EXPECT_DOUBLE_EQ(*values.scale_y, stored.scale.y);
    EXPECT_NEAR(values.shift_x, stored.shift_uv.x * canvas.texture_extent.x, 1.0e-6);
    EXPECT_NEAR(values.shift_y, stored.shift_uv.y * canvas.texture_extent.y, 1.0e-6);
    EXPECT_NEAR(values.rotation_degrees, canvas.rotation_degrees, 1.0e-6);
  };
  for (const auto kind : {QuaderFaceTextureGestureKind::Translate,
                          QuaderFaceTextureGestureKind::ScaleWidth,
                          QuaderFaceTextureGestureKind::ScaleHeight,
                          QuaderFaceTextureGestureKind::Rotate}) {
    SCOPED_TRACE(static_cast<int>(kind));
    const auto original = editor.modeling().face_texture_canvas();
    const auto settings = editor.modeling().face_texture_settings();
    const bool width = kind == QuaderFaceTextureGestureKind::ScaleWidth;
    const bool height = kind == QuaderFaceTextureGestureKind::ScaleHeight;
    const QVec2 start = width || height
        ? texture_axis_point(original, width ? 1.0 : 0.0, height ? 1.0 : 0.0)
        : QVec2{8.0F, 8.0F};
    ASSERT_TRUE(editor.modeling().begin_face_texture_gesture({
        .kind = kind, .selection_token = original.selection_token,
        .start = start, .pivot = {0, 0}}).ok());
    auto revision = editor.modeling().face_texture_settings_revision();
    const std::array points = width || height
        ? std::array{texture_axis_point(original, width ? 1.5 : 0.0, height ? 1.5 : 0.0),
                     texture_axis_point(original, width ? 2.5 : 0.0, height ? 2.5 : 0.0)}
        : std::array{QVec2{12, 16}, QVec2{20, 24}};
    for (const QVec2 point : points) {
      ASSERT_TRUE(editor.modeling().update_face_texture_gesture({.current = point}).ok());
      EXPECT_NE(editor.modeling().face_texture_settings_revision(), revision);
      revision = editor.modeling().face_texture_settings_revision();
      EXPECT_FALSE(editor.modeling().face_texture_settings().editable);
      expect_numbers();
    }
    const auto preview = editor.modeling().face_texture_settings();
    if (kind == QuaderFaceTextureGestureKind::Translate) {
      EXPECT_TRUE(std::abs(preview.shift_x - settings.shift_x) > 0.01 ||
                  std::abs(preview.shift_y - settings.shift_y) > 0.01);
    } else if (kind == QuaderFaceTextureGestureKind::ScaleWidth) {
      EXPECT_GT(*preview.scale_x, *settings.scale_x);
    } else if (kind == QuaderFaceTextureGestureKind::ScaleHeight) {
      EXPECT_GT(*preview.scale_y, *settings.scale_y);
    } else {
      EXPECT_GT(std::abs(preview.rotation_degrees - settings.rotation_degrees), 0.1);
    }
    ASSERT_TRUE(editor.modeling().commit_face_texture_gesture().ok());
    expect_numbers();
    ASSERT_TRUE(editor.selection().select_face(object, 2).ok());
    ASSERT_TRUE(editor.selection().select_face(object, 1).ok());
    const auto retained = editor.modeling().face_texture_settings();
    EXPECT_NEAR(*retained.scale_x, *preview.scale_x, 1.0e-5);
    EXPECT_NEAR(*retained.scale_y, *preview.scale_y, 1.0e-5);
    EXPECT_NEAR(retained.rotation_degrees, preview.rotation_degrees, 1.0e-4);
    EXPECT_NEAR(retained.shift_x, preview.shift_x, 1.0e-4);
    EXPECT_NEAR(retained.shift_y, preview.shift_y, 1.0e-4);
    ASSERT_TRUE(editor.history().undo().ok());
    expect_same_mapping(editor.modeling().face_texture_canvas(), original);
    expect_numbers();
  }
}

TEST(FaceTextureCanvasApiTests, AbsoluteAnglesAndStoredNumericFieldsRoundTripAndKeepNoOpHistory) {
  FaceTextureCanvasHarness harness;
  const auto object = harness.create_box();
  auto &editor = harness.editor;
  const auto edit = [&](QuaderFaceTextureField field, double value) {
    return editor.modeling().set_face_texture_setting({.field = field, .value = value,
        .selection_token = editor.modeling().face_texture_settings().selection_token});
  };
  for (const auto [field, value] : std::array{
           std::pair{QuaderFaceTextureField::Rotate, 37.0},
           std::pair{QuaderFaceTextureField::ScaleX, -2.0},
           std::pair{QuaderFaceTextureField::ScaleY, 0.75},
           std::pair{QuaderFaceTextureField::ShiftX, 13.25},
           std::pair{QuaderFaceTextureField::ShiftY, -7.5}}) {
    ASSERT_TRUE(edit(field, value).ok());
  }
  ASSERT_TRUE(editor.selection().select_face(object, 2).ok());
  ASSERT_TRUE(editor.selection().select_face(object, 1).ok());
  const auto values = editor.modeling().face_texture_settings();
  ASSERT_TRUE(values.scale_x.has_value());
  ASSERT_TRUE(values.scale_y.has_value());
  EXPECT_NEAR(*values.scale_x, -2.0, 1.0e-5);
  EXPECT_NEAR(*values.scale_y, 0.75, 1.0e-5);
  EXPECT_NEAR(values.rotation_degrees, 37.0, 1.0e-4);
  EXPECT_NEAR(values.shift_x, 13.25, 1.0e-4);
  EXPECT_NEAR(values.shift_y, -7.5, 1.0e-4);
  const auto mapping = editor.modeling().face_texture_canvas();
  const auto history = editor.history().snapshot();
  EXPECT_FALSE(edit(QuaderFaceTextureField::Rotate, values.rotation_degrees).changed());
  EXPECT_FALSE(edit(QuaderFaceTextureField::ScaleX, *values.scale_x).changed());
  EXPECT_FALSE(edit(QuaderFaceTextureField::ShiftY, values.shift_y).changed());
  expect_same_mapping(editor.modeling().face_texture_canvas(), mapping);
  expect_same_history(editor.history().snapshot(), history);
}

TEST(FaceTextureCanvasApiTests, NumericScaleKeepsStoredUnitsAndDoesNotMoveGeometry) {
  FaceTextureCanvasHarness harness;
  const auto object = harness.create_box();
  auto &editor = harness.editor;
  const auto original = editor.query().compiled_object_mesh(object);
  ASSERT_TRUE(original.has_value());
  ASSERT_FALSE(original->face_textures.empty());
  const auto initial = editor.modeling().face_texture_settings();
  ASSERT_EQ(initial.scale_x, std::optional<double>(1.0));
  for (const int exponent : {-13, 0, 10}) {
    SCOPED_TRACE(exponent);
    const double value = std::ldexp(1.0, exponent);
    const auto result = editor.modeling().set_face_texture_setting({
        .field = QuaderFaceTextureField::ScaleX, .value = value,
        .selection_token = editor.modeling().face_texture_settings().selection_token});
    ASSERT_TRUE(result.ok()) << result.error_message();
    const auto settings = editor.modeling().face_texture_settings();
    ASSERT_TRUE(settings.scale_x.has_value());
    EXPECT_DOUBLE_EQ(*settings.scale_x, value);
    EXPECT_EQ(settings.scale_y, initial.scale_y);
    EXPECT_NEAR(settings.shift_x, initial.shift_x, 1.0e-5);
    const auto mesh = editor.query().compiled_object_mesh(object);
    ASSERT_TRUE(mesh.has_value());
    ASSERT_EQ(mesh->face_textures.size(), original->face_textures.size());
    // Numeric scale remains the stored face modifier, independent of face size
    // and decoded image dimensions.
    EXPECT_FLOAT_EQ(mesh->face_textures.front().scale.x,
        static_cast<float>(value));
    EXPECT_FLOAT_EQ(mesh->face_textures.front().scale.y, original->face_textures.front().scale.y);
    EXPECT_EQ(mesh->indices, original->indices);
    ASSERT_EQ(mesh->vertices.size(), original->vertices.size());
    for (std::size_t vertex = 0; vertex < mesh->vertices.size(); ++vertex) {
      EXPECT_FLOAT_EQ(mesh->vertices[vertex].position.x, original->vertices[vertex].position.x);
      EXPECT_FLOAT_EQ(mesh->vertices[vertex].position.y, original->vertices[vertex].position.y);
      EXPECT_FLOAT_EQ(mesh->vertices[vertex].position.z, original->vertices[vertex].position.z);
    }
  }
}

TEST(FaceTextureCanvasApiTests, AbsoluteMultiObjectRotationUsesEachFaceChartAndOneUndo) {
  FaceTextureCanvasHarness harness;
  const auto first = harness.create_box();
  const auto second = harness.create_box();
  auto &editor = harness.editor;
  const auto rotate = [&](double value) {
    return editor.modeling().set_face_texture_setting({
        .field = QuaderFaceTextureField::Rotate, .value = value,
        .selection_token = editor.modeling().face_texture_settings().selection_token});
  };
  ASSERT_TRUE(editor.selection().select_face(first, 1).ok());
  ASSERT_TRUE(rotate(31.0).ok());
  ASSERT_TRUE(editor.selection().select_face(second, 2).ok());
  ASSERT_TRUE(rotate(-42.0).ok());
  ASSERT_TRUE(editor.selection().select_face(first, 1, QuaderSelectionEdit::Add).ok());
  EXPECT_TRUE(editor.modeling().face_texture_settings().rotation_mixed);
  const auto history = editor.history().snapshot();
  ASSERT_TRUE(rotate(-15.0).ok());
  EXPECT_FALSE(editor.modeling().face_texture_settings().rotation_mixed);
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees, -15.0, 1.0e-4);
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_TRUE(editor.modeling().face_texture_settings().rotation_mixed);
  EXPECT_EQ(editor.history().snapshot().undo_label, history.undo_label);
  ASSERT_TRUE(editor.selection().select_face(first, 1).ok());
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees, 31.0, 1.0e-4);
  ASSERT_TRUE(editor.selection().select_face(second, 2).ok());
  EXPECT_NEAR(editor.modeling().face_texture_settings().rotation_degrees, -42.0, 1.0e-4);
}

} // namespace
} // namespace quader
