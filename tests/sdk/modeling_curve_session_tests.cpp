////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include <quader/modeling/modeling.hpp>

#include <array>
#include <limits>
#include <utility>
#include <vector>

namespace {

using namespace quader::modeling;

/// Exercises mixed authored document kinds through the portable session owner.
class ModelingCurveSessionTests : public ::testing::Test {
protected:
  void SetUp() override {
    auto polygon = PolygonDocument::make_box({});
    ASSERT_TRUE(polygon.ok()) << polygon.error_message();
    auto added_mesh = session.add_document(std::move(polygon).value(), "Box");
    ASSERT_TRUE(added_mesh.ok()) << added_mesh.error_message();
    mesh = added_mesh.value();

    const std::array points = {
        CurvePointSpec{.position = {-2.0F, 0.0F, 0.0F},
                       .outgoing_handle = {1.0F, 2.0F, 0.0F}},
        CurvePointSpec{.position = {2.0F, 0.0F, 0.0F},
                       .incoming_handle = {-1.0F, 2.0F, 0.0F}},
    };
    auto document = CurveDocument::make_bezier(points);
    ASSERT_TRUE(document.ok()) << document.error_message();
    auto added_curve = session.add_curve_document(std::move(document).value(), "Path");
    ASSERT_TRUE(added_curve.ok()) << added_curve.error_message();
    curve = added_curve.value();
    ASSERT_TRUE(session.select_object(mesh).ok());
    session.clear_history();
  }

  [[nodiscard]] CurveDocument current_curve() const {
    return session.curve_document_copy(curve).or_throw();
  }

  [[nodiscard]] static OperationResult changed_receipt() {
    OperationResult receipt;
    receipt.changed = true;
    receipt.dirty.geometry = true;
    receipt.dirty.overlays = true;
    return receipt;
  }

  [[nodiscard]] CurveDocument translated_curve(Vec3 delta) const {
    CurveDocument document = current_curve();
    static_cast<void>(document.transform(Transform3{.origin = delta}).or_throw());
    return document;
  }

  void expect_mesh_only_selected() const {
    const auto objects = session.objects();
    ASSERT_EQ(objects.size(), 2U);
    EXPECT_TRUE(objects[0].selected);
    EXPECT_FALSE(objects[1].selected);
  }

  static void expect_same_dirty(const DirtyFlags &actual,
                                const DirtyFlags &expected) {
    EXPECT_EQ(actual.topology, expected.topology);
    EXPECT_EQ(actual.geometry, expected.geometry);
    EXPECT_EQ(actual.selection, expected.selection);
    EXPECT_EQ(actual.materials, expected.materials);
    EXPECT_EQ(actual.overlays, expected.overlays);
  }

  static void expect_same_preview_summary(
      const PreviewTransactionSummary &actual,
      const PreviewTransactionSummary &expected) {
    EXPECT_EQ(actual.id, expected.id);
    EXPECT_EQ(actual.active, expected.active);
    EXPECT_EQ(actual.object_id, expected.object_id);
    EXPECT_EQ(actual.operation_id, expected.operation_id);
    EXPECT_FLOAT_EQ(actual.delta.x, expected.delta.x);
    EXPECT_FLOAT_EQ(actual.delta.y, expected.delta.y);
    EXPECT_FLOAT_EQ(actual.delta.z, expected.delta.z);
    EXPECT_EQ(actual.content_revision, expected.content_revision);
    EXPECT_EQ(actual.kind, expected.kind);
    expect_same_dirty(actual.dirty, expected.dirty);
  }

  static void expect_same_curve_snapshot(const CurveDocument &actual,
                                         const CurveDocument &expected) {
    EXPECT_TRUE(actual.has_same_geometry(expected));
    EXPECT_EQ(actual.content_identity(), expected.content_identity());
    EXPECT_EQ(actual.content_revision(), expected.content_revision());
    EXPECT_EQ(actual.identity_high_water(), expected.identity_high_water());
  }

  static void expect_same_session_observables(const ModelingSession &actual,
                                              const ModelingSession &expected) {
    EXPECT_EQ(actual.content_revision(), expected.content_revision());
    EXPECT_EQ(actual.selection_revision(), expected.selection_revision());
    EXPECT_EQ(actual.undo_depth(), expected.undo_depth());
    EXPECT_EQ(actual.can_undo(), expected.can_undo());
    EXPECT_EQ(actual.can_redo(), expected.can_redo());
    EXPECT_EQ(actual.clipboard_available(), expected.clipboard_available());
    EXPECT_EQ(actual.clipboard_object_count(), expected.clipboard_object_count());
    const auto actual_objects = actual.objects();
    const auto expected_objects = expected.objects();
    ASSERT_EQ(actual_objects.size(), expected_objects.size());
    for (std::size_t index = 0; index < actual_objects.size(); ++index) {
      SCOPED_TRACE(index);
      const auto &object = actual_objects[index];
      const auto &prior = expected_objects[index];
      EXPECT_EQ(object.id, prior.id);
      EXPECT_EQ(object.name, prior.name);
      EXPECT_EQ(object.selected, prior.selected);
      EXPECT_EQ(object.content_revision, prior.content_revision);
      EXPECT_EQ(object.selection_revision, prior.selection_revision);
      ASSERT_EQ(object.kind, prior.kind);
      if (object.kind == SessionObjectKind::Curve) {
        expect_same_curve_snapshot(
            actual.curve_document_copy(object.id).or_throw(),
            expected.curve_document_copy(prior.id).or_throw());
      } else {
        EXPECT_EQ(actual.document_copy(object.id).or_throw().content_identity(),
                  expected.document_copy(prior.id).or_throw().content_identity());
      }
    }
    const auto preview = actual.preview_summary();
    const auto prior_preview = expected.preview_summary();
    expect_same_preview_summary(preview, prior_preview);
    if (preview.active && prior_preview.active) {
      if (preview.kind == SessionObjectKind::Curve) {
        expect_same_curve_snapshot(
            actual.preview_curve_document_copy(preview.id).or_throw(),
            expected.preview_curve_document_copy(prior_preview.id).or_throw());
      } else {
        EXPECT_EQ(
            actual.preview_document_copy(preview.id).or_throw().content_identity(),
            expected.preview_document_copy(prior_preview.id).or_throw().content_identity());
      }
    }
  }

  ModelingSession session;
  ObjectId mesh{};
  ObjectId curve{};
};

TEST_F(ModelingCurveSessionTests, MixedKindsHaveTypedCopiesAndPinnedSnapshots) {
  const auto objects = session.objects();
  ASSERT_EQ(objects.size(), 2U);
  EXPECT_EQ(objects[0].kind, SessionObjectKind::Mesh);
  EXPECT_EQ(objects[1].kind, SessionObjectKind::Curve);
  EXPECT_EQ(objects[1].name, "Path");
  EXPECT_EQ(objects[1].selection_revision, 0U);
  EXPECT_EQ(session.document_copy(curve).error().code, ErrorCode::UnsupportedOperation);
  EXPECT_EQ(session.curve_document_copy(mesh).error().code, ErrorCode::UnsupportedOperation);
  EXPECT_EQ(session.curve_document_copy({}).error().code, ErrorCode::InvalidId);
  EXPECT_FALSE(session.document_snapshot(curve).ok());
  EXPECT_FALSE(session.curve_document_snapshot(mesh).ok());

  auto pinned = session.curve_document_snapshot(curve);
  ASSERT_TRUE(pinned.ok()) << pinned.error_message();
  const CurveDocument original = *pinned.value();
  const CurveDocument edited = translated_curve({3.0F, 0.0F, 0.0F});
  ASSERT_TRUE(session.commit_curve_document(curve, edited, changed_receipt()).ok());
  EXPECT_TRUE(pinned.value()->has_same_geometry(original));
  EXPECT_FALSE(pinned.value()->has_same_geometry(current_curve()));

  const std::array selected = {mesh, curve};
  ASSERT_TRUE(session.apply_object_selection(selected).ok());
  const auto selected_objects = session.objects();
  EXPECT_TRUE(selected_objects[0].selected);
  EXPECT_TRUE(selected_objects[1].selected);
  const auto changes = session.changes_since(0);
  ASSERT_EQ(changes.objects.size(), 2U);
  EXPECT_EQ(changes.objects[0].kind, SessionObjectKind::Mesh);
  EXPECT_EQ(changes.objects[1].kind, SessionObjectKind::Curve);
}

TEST_F(ModelingCurveSessionTests, CurveCommitPreservesSelectionAndUsesExistingHistory) {
  const CurveDocument original = current_curve();
  const auto polygon = session.document_copy(mesh).or_throw();
  const auto revision = session.content_revision();
  const CurveDocument edited = translated_curve({0.0F, 0.0F, 2.0F});
  auto committed = session.commit_curve_document(curve, edited, changed_receipt());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_EQ(session.undo_depth(), 1U);
  EXPECT_EQ(session.content_revision(), revision + 1U);
  expect_mesh_only_selected();
  EXPECT_EQ(session.document_copy(mesh).or_throw().content_identity(),
            polygon.content_identity());
  const auto changes = session.changes_since(revision);
  ASSERT_EQ(changes.objects.size(), 1U);
  EXPECT_EQ(changes.objects.front().kind, SessionObjectKind::Curve);
  EXPECT_TRUE(changes.objects.front().dirty.geometry);
  EXPECT_FALSE(changes.objects.front().dirty.selection);

  ASSERT_TRUE(session.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
  ASSERT_TRUE(session.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(edited));
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, FailedAndNoOpCurveCommitsKeepRedoAndRevisions) {
  const CurveDocument original = current_curve();
  const CurveDocument edited = translated_curve({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(session.commit_curve_document(curve, edited, changed_receipt()).ok());
  ASSERT_TRUE(session.undo().ok());
  const auto revision = session.content_revision();
  const auto identity = current_curve().content_identity();
  const CurveDocument identical =
      CurveDocument::from_authored_curve(current_curve().authored_curve()).or_throw();
  auto unchanged = session.commit_curve_document(curve, identical, changed_receipt());
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);

  OperationResult failed;
  failed.success = false;
  failed.error_code = ErrorCode::ValidationFailed;
  EXPECT_FALSE(session.commit_curve_document(curve, edited, failed).or_throw().success);
  EXPECT_FALSE(session.commit_curve_document(curve, edited, {}).or_throw().changed);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_EQ(current_curve().content_identity(), identity);
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), 0U);
  EXPECT_TRUE(session.can_redo());
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, CurvePreviewSharesOneRegistryAndCommitsOnce) {
  const CurveDocument original = current_curve();
  const CurveDocument first = translated_curve({0.0F, 2.0F, 0.0F});
  const auto revision = session.content_revision();
  auto started = session.begin_curve_document_preview(curve, "modeling.curve.edit",
                                                       first, changed_receipt());
  ASSERT_TRUE(started.ok()) << started.error_message();
  const PreviewId preview = started.value();
  EXPECT_TRUE(session.preview_active(preview));
  EXPECT_EQ(session.preview_summary().kind, SessionObjectKind::Curve);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_TRUE(session.preview_curve_document_copy(preview).or_throw().has_same_geometry(first));
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), 0U);
  EXPECT_FALSE(session.begin_translate_preview(mesh, {}).ok());
  EXPECT_FALSE(session.select_object(curve).ok());
  EXPECT_FALSE(session.commit_curve_document(curve, first, changed_receipt()).ok());
  EXPECT_FALSE(session.undo().ok());
  EXPECT_FALSE(session.preview_payload(preview).ok());
  EXPECT_FALSE(session.preview_document_copy(preview).ok());
  auto polygon = session.document_copy(mesh).or_throw();
  EXPECT_FALSE(session.update_document_preview(preview, polygon, changed_receipt()).ok());
  EXPECT_TRUE(session.preview_active(preview));

  const CurveDocument second = translated_curve({0.0F, 3.0F, 0.0F});
  ASSERT_TRUE(session.update_curve_document_preview(preview, second, changed_receipt()).ok());
  const auto payload_revision = session.preview_summary().content_revision;
  auto repeated = session.update_curve_document_preview(preview, second, changed_receipt());
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  EXPECT_EQ(session.preview_summary().content_revision, payload_revision);

  auto committed = session.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_FALSE(session.preview_active());
  EXPECT_EQ(session.undo_depth(), 1U);
  EXPECT_TRUE(current_curve().has_same_geometry(second));
  expect_mesh_only_selected();
  EXPECT_EQ(session.commit_preview(preview).error().code, ErrorCode::StaleId);
  ASSERT_TRUE(session.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests, CancelAndReturnToBasePreserveDurableStateAndRedo) {
  const CurveDocument original = current_curve();
  const CurveDocument changed = translated_curve({0.0F, 0.0F, 3.0F});
  ASSERT_TRUE(session.commit_curve_document(curve, changed, changed_receipt()).ok());
  ASSERT_TRUE(session.undo().ok());
  const auto revision = session.content_revision();
  auto first = session.begin_curve_document_preview(curve, "modeling.curve.edit",
                                                     changed, changed_receipt());
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(session.cancel_preview(first.value()).ok());
  EXPECT_FALSE(session.preview_curve_document_copy(first.value()).ok());
  EXPECT_EQ(current_curve().content_identity(), original.content_identity());

  auto second = session.begin_curve_document_preview(curve, "modeling.curve.edit",
                                                      changed, changed_receipt());
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_GT(second.value().index, first.value().index);
  auto restored = session.update_curve_document_preview(second.value(), current_curve(), {});
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_FALSE(restored.value().changed);
  auto committed = session.commit_preview(second.value());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.value().changed);
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), 0U);
  EXPECT_TRUE(session.can_redo());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRestoresCandidateWithoutChangingDurableStateOrRedo) {
  const CurveDocument original = current_curve();
  const std::array selected = {mesh, curve};
  ASSERT_TRUE(session.apply_object_selection(selected).ok());
  ASSERT_TRUE(session.copy_selection_to_clipboard().ok());
  ASSERT_TRUE(session.select_object(mesh).ok());
  session.clear_history();
  ASSERT_TRUE(session.rename_object(mesh, "Renamed Box").ok());
  const CurveDocument redo_candidate = translated_curve({0.0F, 0.0F, 4.0F});
  ASSERT_TRUE(session.commit_curve_document(curve, redo_candidate, changed_receipt()).ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_EQ(session.undo_depth(), 1U);
  ASSERT_TRUE(session.can_redo());

  const CurveDocument first = translated_curve({0.0F, 2.0F, 0.0F});
  const auto preview = session.begin_curve_document_preview(
      curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session.capture_preview_checkpoint(preview).or_throw();
  ASSERT_TRUE(checkpoint.valid());
  const ModelingSession captured = session;
  const auto unchanged = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);
  expect_same_session_observables(session, captured);

  const CurveDocument second = translated_curve({0.0F, 3.0F, 0.0F});
  auto second_receipt = changed_receipt();
  second_receipt.dirty.topology = true;
  ASSERT_TRUE(session.update_curve_document_preview(preview, second, second_receipt).ok());
  const auto discarded_revision = session.preview_summary().content_revision;
  const auto restored = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_dirty(restored.value().dirty, captured.preview_summary().dirty);
  expect_same_session_observables(session, captured);

  ASSERT_TRUE(session.update_curve_document_preview(preview, second, second_receipt).ok());
  EXPECT_GT(session.preview_summary().content_revision, discarded_revision);
  ASSERT_TRUE(session.restore_preview_checkpoint(checkpoint).ok());
  expect_same_session_observables(session, captured);

  ASSERT_TRUE(session.cancel_preview(preview).ok());
  ASSERT_TRUE(session.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(redo_candidate));
  EXPECT_EQ(session.objects()[0].name, "Renamed Box");
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(session.objects()[0].name, "Box");
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
  const auto pasted = session.paste_clipboard();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session.document_copy(pasted.value().objects[0]).ok());
  EXPECT_TRUE(session.curve_document_copy(pasted.value().objects[1]).or_throw()
                  .has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRestoredAppendedIdsCommitWithoutRewindingAllocators) {
  const CurveDocument original = current_curve();
  CurveDocument first = original;
  auto first_receipt = first.append_point({.position = {4.0F, 1.0F, 0.0F}}).or_throw();
  first_receipt.diagnostics.push_back({.code = "checkpoint.saved",
                                       .severity = DiagnosticSeverity::Info,
                                       .message = "Saved append candidate."});
  const CurvePointId retained_point = first.points().back().id;
  const auto preview = session.begin_curve_document_preview(
      curve, "modeling.curve.append", first, first_receipt).or_throw();
  const auto checkpoint = session.capture_preview_checkpoint(preview).or_throw();
  const auto summary = session.preview_summary();

  CurveDocument discarded = session.preview_curve_document_copy(preview).or_throw();
  const auto discarded_receipt =
      discarded.append_point({.position = {6.0F, 2.0F, 0.0F}}).or_throw();
  const CurvePointId discarded_point = discarded.points().back().id;
  const CurveSegmentId discarded_segment = discarded.segments().back();
  ASSERT_TRUE(session.update_curve_document_preview(preview, discarded, discarded_receipt).ok());
  const auto discarded_revision = session.preview_summary().content_revision;
  ASSERT_GT(discarded_revision, summary.content_revision);

  const auto restored = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_preview_summary(session.preview_summary(), summary);
  const CurveDocument restored_candidate = session.preview_curve_document_copy(preview).or_throw();
  EXPECT_TRUE(restored_candidate.has_same_geometry(first));
  EXPECT_EQ(restored_candidate.content_identity(), first.content_identity());
  EXPECT_EQ(restored_candidate.content_revision(), first.content_revision());
  EXPECT_EQ(restored_candidate.identity_high_water(), discarded.identity_high_water());
  EXPECT_EQ(current_curve().identity_high_water(), discarded.identity_high_water());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  const auto repeated = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  expect_same_curve_snapshot(session.preview_curve_document_copy(preview).or_throw(),
                             restored_candidate);

  const auto committed = session.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  expect_same_dirty(committed.value().dirty, summary.dirty);
  ASSERT_EQ(committed.value().diagnostics.size(), 1U);
  EXPECT_EQ(committed.value().diagnostics.front().code, "checkpoint.saved");
  EXPECT_TRUE(current_curve().has_same_geometry(first));
  EXPECT_EQ(current_curve().points().back().id, retained_point);
  EXPECT_EQ(session.undo_depth(), 1U);
  expect_mesh_only_selected();

  CurveDocument next = current_curve();
  const auto next_receipt = next.append_point({.position = {8.0F, 3.0F, 0.0F}}).or_throw();
  EXPECT_GT(next.points().back().id.index, discarded_point.index);
  EXPECT_GT(next.segments().back().index, discarded_segment.index);
  const auto next_preview = session.begin_curve_document_preview(
      curve, "modeling.curve.append", next, next_receipt).or_throw();
  EXPECT_GT(next_preview.index, preview.index);
  const auto next_revision = session.preview_summary().content_revision;
  EXPECT_GT(next_revision, discarded_revision);
  const auto translated = next.transform(Transform3{.origin = {0.0F, 1.0F, 0.0F}}).or_throw();
  ASSERT_TRUE(session.update_curve_document_preview(next_preview, next, translated).ok());
  EXPECT_GT(session.preview_summary().content_revision, next_revision);
  ASSERT_TRUE(session.cancel_preview(next_preview).ok());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRejectsEmptyWrongAndRetiredTokensWithoutMutation) {
  const SessionPreviewCheckpoint empty;
  EXPECT_FALSE(empty.valid());
  const ModelingSession inactive = session;
  EXPECT_EQ(session.restore_preview_checkpoint(empty).error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(session.capture_preview_checkpoint({}).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session, inactive);

  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const auto preview = session.begin_curve_document_preview(
      curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession active = session;
  const PreviewId wrong{preview.index + 1U, preview.generation};
  EXPECT_EQ(session.capture_preview_checkpoint(wrong).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session.restore_preview_checkpoint(empty).error().code, ErrorCode::InvalidArgument);
  expect_same_session_observables(session, active);

  ASSERT_TRUE(session.cancel_preview(preview).ok());
  const ModelingSession canceled = session;
  EXPECT_EQ(session.capture_preview_checkpoint(preview).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session, canceled);

  const auto replacement = session.begin_curve_document_preview(
      curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
  ASSERT_NE(replacement, preview);
  const ModelingSession replaced = session;
  EXPECT_EQ(session.capture_preview_checkpoint(preview).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session, replaced);
  ASSERT_TRUE(session.commit_preview(replacement).ok());
  const ModelingSession committed = session;
  EXPECT_EQ(session.capture_preview_checkpoint(replacement).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session, committed);
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRejectsCopiedAndForeignSessionsWithoutMutation) {
  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const CurveDocument second = translated_curve({0.0F, 2.0F, 0.0F});
  const auto preview = session.begin_curve_document_preview(
      curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession source = session;
  ModelingSession copied(session);
  ModelingSession assigned;
  assigned = session;
  const std::array destinations = {&copied, &assigned};
  for (ModelingSession *destination : destinations) {
    const auto foreign_checkpoint = destination->capture_preview_checkpoint(preview).or_throw();
    ASSERT_TRUE(destination->update_curve_document_preview(preview, second, changed_receipt()).ok());
    const ModelingSession before = *destination;
    EXPECT_EQ(destination->restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
    expect_same_session_observables(*destination, before);
    EXPECT_EQ(session.restore_preview_checkpoint(foreign_checkpoint).error().code, ErrorCode::StaleId);
    expect_same_session_observables(session, source);
  }

  const auto displaced = assigned.capture_preview_checkpoint(preview).or_throw();
  assigned = session;
  const ModelingSession overwritten = assigned;
  EXPECT_EQ(assigned.restore_preview_checkpoint(displaced).error().code, ErrorCode::StaleId);
  expect_same_session_observables(assigned, overwritten);
  ModelingSession unrelated;
  const ModelingSession empty = unrelated;
  EXPECT_EQ(unrelated.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(unrelated, empty);
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointSnapshotRestoreInvalidatesBothPreviousOrigins) {
  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const CurveDocument second = translated_curve({0.0F, 2.0F, 0.0F});
  for (const bool history_transition : {false, true}) {
    SCOPED_TRACE(history_transition);
    ModelingSession target = session;
    const auto preview = target.begin_curve_document_preview(
        curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
    const auto prior_checkpoint = target.capture_preview_checkpoint(preview).or_throw();
    const ModelingSession snapshot = target;
    const auto snapshot_checkpoint = snapshot.capture_preview_checkpoint(preview).or_throw();
    ASSERT_TRUE(target.update_curve_document_preview(preview, second, changed_receipt()).ok());
    if (history_transition) {
      target.restore_snapshot_as_history_transition(snapshot);
    } else {
      target.restore_snapshot_preserving_identity_high_water(snapshot);
    }
    ASSERT_TRUE(target.preview_active(preview));
    const ModelingSession restored = target;
    EXPECT_EQ(target.restore_preview_checkpoint(prior_checkpoint).error().code, ErrorCode::StaleId);
    EXPECT_EQ(target.restore_preview_checkpoint(snapshot_checkpoint).error().code, ErrorCode::StaleId);
    expect_same_session_observables(target, restored);

    const auto fresh_checkpoint = target.capture_preview_checkpoint(preview).or_throw();
    ASSERT_TRUE(target.update_curve_document_preview(preview, second, changed_receipt()).ok());
    const auto result = target.restore_preview_checkpoint(fresh_checkpoint);
    ASSERT_TRUE(result.ok()) << result.error_message();
    EXPECT_TRUE(result.value().changed);
    expect_same_session_observables(target, restored);
  }
}

TEST_F(ModelingCurveSessionTests, PreviewCheckpointFollowsMovedSessionState) {
  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const CurveDocument second = translated_curve({0.0F, 2.0F, 0.0F});
  const auto preview = session.begin_curve_document_preview(
      curve, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession captured = session;
  ASSERT_TRUE(session.update_curve_document_preview(preview, second, changed_receipt()).ok());
  ModelingSession moved(std::move(session));
  const auto restored = moved.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_session_observables(moved, captured);

  ASSERT_TRUE(moved.update_curve_document_preview(preview, second, changed_receipt()).ok());
  session = std::move(moved);
  const auto restored_again = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored_again.ok()) << restored_again.error_message();
  EXPECT_TRUE(restored_again.value().changed);
  expect_same_session_observables(session, captured);
  const auto repeated = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  expect_same_session_observables(session, captured);
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRestoresMaterializedMeshTranslatePayloadAndHistory) {
  const auto faces = session.document_copy(mesh).or_throw().face_ids();
  ASSERT_FALSE(faces.empty());
  ASSERT_TRUE(session.select_face(mesh, faces.front()).ok());
  session.clear_history();
  const auto original = session.document_copy(mesh).or_throw();
  const auto original_curve = current_curve();
  ASSERT_TRUE(session.begin_translate_preview(mesh, {0.0F, 0.25F, 0.0F}).ok());
  const auto summary = session.preview_summary();
  const auto candidate = session.preview_document_copy(summary.id).or_throw();
  const auto payload = session.preview_payload(summary.id).or_throw();
  ASSERT_TRUE(payload.valid);
  ASSERT_FALSE(payload.mesh.vertices.empty());
  const auto checkpoint = session.capture_preview_checkpoint(summary.id).or_throw();
  const ModelingSession captured = session;

  ASSERT_TRUE(session.update_translate_preview({0.0F, 0.5F, 0.0F}).ok());
  const auto updated_payload = session.preview_payload(summary.id).or_throw();
  EXPECT_GT(updated_payload.mesh.content_revision, payload.mesh.content_revision);
  const auto restored = session.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_session_observables(session, captured);
  const auto restored_payload = session.preview_payload(summary.id).or_throw();
  EXPECT_EQ(restored_payload.valid, payload.valid);
  EXPECT_EQ(restored_payload.mesh.content_revision, payload.mesh.content_revision);
  EXPECT_EQ(restored_payload.mesh.content_revision, summary.content_revision);
  EXPECT_EQ(restored_payload.mesh.indices, payload.mesh.indices);
  ASSERT_EQ(restored_payload.mesh.vertices.size(), payload.mesh.vertices.size());
  for (std::size_t index = 0; index < payload.mesh.vertices.size(); ++index) {
    const auto &actual = restored_payload.mesh.vertices[index];
    const auto &expected = payload.mesh.vertices[index];
    EXPECT_EQ(actual.source_vertex, expected.source_vertex);
    EXPECT_FLOAT_EQ(actual.position.x, expected.position.x);
    EXPECT_FLOAT_EQ(actual.position.y, expected.position.y);
    EXPECT_FLOAT_EQ(actual.position.z, expected.position.z);
    EXPECT_FLOAT_EQ(actual.normal.x, expected.normal.x);
    EXPECT_FLOAT_EQ(actual.normal.y, expected.normal.y);
    EXPECT_FLOAT_EQ(actual.normal.z, expected.normal.z);
  }

  const auto committed = session.commit_preview(summary.id);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  expect_same_dirty(committed.value().dirty, summary.dirty);
  EXPECT_EQ(session.undo_depth(), 1U);
  EXPECT_EQ(session.document_copy(mesh).or_throw().content_identity(), candidate.content_identity());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(session.document_copy(mesh).or_throw().content_identity(), original.content_identity());
  ASSERT_TRUE(session.redo().ok());
  EXPECT_EQ(session.document_copy(mesh).or_throw().content_identity(), candidate.content_identity());
  expect_same_curve_snapshot(current_curve(), original_curve);
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRepeatedMeshRestoreAfterHistoryRewindIsNoOp) {
  for (const bool commit_restored_preview : {false, true}) {
    SCOPED_TRACE(commit_restored_preview);
    ModelingSession target = session;
    const auto faces = target.document_copy(mesh).or_throw().face_ids();
    ASSERT_FALSE(faces.empty());
    ASSERT_TRUE(target.select_face(mesh, faces.front()).ok());
    target.clear_history();
    const ModelingSession snapshot = target;
    const auto original = target.document_copy(mesh).or_throw();
    const auto original_curve = target.curve_document_copy(curve).or_throw();

    auto expanded = target.document_copy(mesh).or_throw();
    const auto extrusion = expanded.extrude_selected_faces(0.25F).or_throw();
    ASSERT_TRUE(extrusion.changed);
    ASSERT_FALSE(extrusion.created.vertices.empty());
    ASSERT_FALSE(extrusion.created.faces.empty());
    const auto retired_topology = expanded.authored_topology().or_throw();
    ASSERT_TRUE(target.commit_document(mesh, expanded, extrusion).ok());
    target.restore_snapshot_preserving_identity_high_water(snapshot);
    const auto content_revision = target.content_revision();
    const auto selection_revision = target.selection_revision();

    ASSERT_TRUE(target.begin_translate_preview(mesh, {0.0F, 0.25F, 0.0F}).ok());
    const auto summary = target.preview_summary();
    const auto candidate = target.preview_document_copy(summary.id).or_throw();
    const auto checkpoint = target.capture_preview_checkpoint(summary.id).or_throw();
    ASSERT_TRUE(target.update_translate_preview({0.0F, 0.5F, 0.0F}).ok());
    const auto restored = target.restore_preview_checkpoint(checkpoint);
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    EXPECT_TRUE(restored.value().changed);
    const auto first_restore = target.preview_document_copy(summary.id).or_throw();
    EXPECT_TRUE(first_restore.has_same_geometry(candidate));
    const auto restored_topology = first_restore.authored_topology().or_throw();
    EXPECT_GE(restored_topology.next_vertex_id, retired_topology.next_vertex_id);
    EXPECT_GE(restored_topology.next_face_id, retired_topology.next_face_id);
    expect_same_preview_summary(target.preview_summary(), summary);

    const auto repeated = target.restore_preview_checkpoint(checkpoint);
    ASSERT_TRUE(repeated.ok()) << repeated.error_message();
    EXPECT_FALSE(repeated.value().changed);
    const auto second_restore = target.preview_document_copy(summary.id).or_throw();
    EXPECT_EQ(second_restore.content_identity(), first_restore.content_identity());
    EXPECT_EQ(second_restore.content_revision(), first_restore.content_revision());
    EXPECT_TRUE(second_restore.has_same_geometry(first_restore));
    expect_same_preview_summary(target.preview_summary(), summary);
    EXPECT_EQ(target.content_revision(), content_revision);
    EXPECT_EQ(target.selection_revision(), selection_revision);
    EXPECT_EQ(target.undo_depth(), 0U);
    EXPECT_FALSE(target.can_redo());
    EXPECT_TRUE(target.document_copy(mesh).or_throw().has_same_geometry(original));
    EXPECT_EQ(target.document_copy(mesh).or_throw().selection().faces,
              original.selection().faces);
    expect_same_curve_snapshot(target.curve_document_copy(curve).or_throw(), original_curve);

    if (commit_restored_preview) {
      const auto committed = target.commit_preview(summary.id);
      ASSERT_TRUE(committed.ok()) << committed.error_message();
      EXPECT_TRUE(committed.value().changed);
      EXPECT_EQ(target.undo_depth(), 1U);
      EXPECT_TRUE(target.document_copy(mesh).or_throw().has_same_geometry(first_restore));
      ASSERT_TRUE(target.undo().ok());
    } else {
      ASSERT_TRUE(target.cancel_preview(summary.id).ok());
      EXPECT_EQ(target.content_revision(), content_revision);
      EXPECT_EQ(target.selection_revision(), selection_revision);
    }
    EXPECT_FALSE(target.preview_active());
    EXPECT_TRUE(target.document_copy(mesh).or_throw().has_same_geometry(original));

    auto next = target.document_copy(mesh).or_throw();
    const auto next_extrusion = next.extrude_selected_faces(0.5F).or_throw();
    ASSERT_FALSE(next_extrusion.created.vertices.empty());
    ASSERT_FALSE(next_extrusion.created.faces.empty());
    for (const VertexId vertex : next_extrusion.created.vertices) {
      EXPECT_GE(vertex.index, retired_topology.next_vertex_id);
    }
    for (const FaceId face : next_extrusion.created.faces) {
      EXPECT_GE(face.index, retired_topology.next_face_id);
    }
    ASSERT_TRUE(target.commit_document(mesh, next, next_extrusion).ok());
    EXPECT_EQ(target.undo_depth(), 1U);
    ASSERT_TRUE(target.undo().ok());
    EXPECT_TRUE(target.document_copy(mesh).or_throw().has_same_geometry(original));
    ASSERT_TRUE(target.redo().ok());
    EXPECT_TRUE(target.document_copy(mesh).or_throw().has_same_geometry(next));
  }
}

TEST_F(ModelingCurveSessionTests, MixedDuplicatesAndClipboardRetainAuthoredCurveData) {
  const CurveDocument original = current_curve();
  const std::array targets = {mesh, curve};
  auto duplicates = session.duplicate_objects(targets, {4.0F, 0.0F, 0.0F});
  ASSERT_TRUE(duplicates.ok()) << duplicates.error_message();
  ASSERT_EQ(duplicates.value().size(), 2U);
  EXPECT_EQ(session.undo_depth(), 1U);
  EXPECT_TRUE(session.document_copy(duplicates.value()[0]).ok());
  CurveDocument duplicated = session.curve_document_copy(duplicates.value()[1]).or_throw();
  CurveDocument expected = original;
  ASSERT_TRUE(expected.transform(Transform3{.origin = {4.0F, 0.0F, 0.0F}}).ok());
  EXPECT_TRUE(duplicated.has_same_geometry(expected));
  ASSERT_TRUE(session.undo().ok());
  EXPECT_FALSE(session.curve_document_copy(duplicates.value()[1]).ok());
  ASSERT_TRUE(session.redo().ok());
  EXPECT_TRUE(session.curve_document_copy(duplicates.value()[1]).or_throw().has_same_geometry(expected));
  ASSERT_TRUE(session.undo().ok());

  ASSERT_TRUE(session.apply_object_selection(targets).ok());
  ASSERT_TRUE(session.copy_selection_to_clipboard().ok());
  EXPECT_EQ(session.clipboard_object_count(), 2U);
  const auto depth = session.undo_depth();
  auto pasted = session.paste_clipboard({4.0F, 0.0F, 0.0F});
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  EXPECT_EQ(session.undo_depth(), depth + 1U);
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session.curve_document_copy(pasted.value().objects[1]).or_throw().has_same_geometry(expected));
  ASSERT_TRUE(session.undo().ok());
  EXPECT_FALSE(session.curve_document_copy(pasted.value().objects[1]).ok());
  EXPECT_TRUE(session.clipboard_available());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests, RenameDeleteAndDuplicateUseOneObjectAllocator) {
  const CurveDocument original = current_curve();
  ASSERT_TRUE(session.rename_object(curve, "Rail").ok());
  EXPECT_EQ(session.objects()[1].name, "Rail");
  ASSERT_TRUE(session.undo().ok());
  EXPECT_EQ(session.objects()[1].name, "Path");
  ASSERT_TRUE(session.remove_object(curve).ok());
  EXPECT_FALSE(session.curve_document_copy(curve).ok());
  ASSERT_TRUE(session.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  auto duplicate = session.duplicate_object(curve);
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  EXPECT_TRUE(session.curve_document_copy(duplicate.value()).or_throw().has_same_geometry(original));
  ASSERT_TRUE(session.undo().ok());
  auto new_mesh = session.add_document(PolygonDocument::make_box({}).or_throw());
  ASSERT_TRUE(new_mesh.ok()) << new_mesh.error_message();
  EXPECT_GT(new_mesh.value().index, duplicate.value().index);
  EXPECT_FALSE(session.can_redo());
}

TEST_F(ModelingCurveSessionTests, BranchAndCanceledPreviewNeverRecycleComponentIdentities) {
  const CurveDocument stale_copy = current_curve();
  CurveDocument edited = stale_copy;
  auto appended = edited.append_point({.position = {4.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(appended.ok()) << appended.error_message();
  const CurvePointId retired_point = edited.points().back().id;
  const CurveSegmentId retired_segment = edited.segments().back();
  ASSERT_TRUE(session.commit_curve_document(curve, edited, appended.value()).ok());
  ASSERT_TRUE(session.undo().ok());

  CurveDocument stale_edit = stale_copy;
  auto stale_append = stale_edit.append_point({.position = {5.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(stale_append.ok()) << stale_append.error_message();
  const auto before_failure = session.content_revision();
  auto rejected = session.commit_curve_document(curve, stale_edit, stale_append.value());
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error().code, ErrorCode::StaleId);
  EXPECT_EQ(session.content_revision(), before_failure);
  EXPECT_TRUE(session.can_redo());

  edited = current_curve();
  appended = edited.append_point({.position = {5.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(appended.ok()) << appended.error_message();
  EXPECT_GT(edited.points().back().id.index, retired_point.index);
  EXPECT_GT(edited.segments().back().index, retired_segment.index);
  auto preview = session.begin_curve_document_preview(curve, "modeling.curve.append",
                                                       edited, appended.value());
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const auto canceled_point = edited.points().back().id;
  const auto canceled_segment = edited.segments().back();
  ASSERT_TRUE(session.cancel_preview(preview.value()).ok());
  edited = current_curve();
  ASSERT_TRUE(edited.append_point({.position = {6.0F, 0.0F, 0.0F}}).ok());
  EXPECT_GT(edited.points().back().id.index, canceled_point.index);
  EXPECT_GT(edited.segments().back().index, canceled_segment.index);
}

TEST_F(ModelingCurveSessionTests, CheckpointsAndHostHistoryPreserveCurveIdentityHighWater) {
  const CurveDocument original = current_curve();
  const ModelingSession checkpoint = session.current_workspace_checkpoint();
  CurveDocument expanded = original;
  auto append = expanded.append_point({.position = {5.0F, 1.0F, 0.0F}});
  ASSERT_TRUE(append.ok()) << append.error_message();
  const CurvePointId retired = expanded.points().back().id;
  ASSERT_TRUE(session.commit_curve_document(curve, expanded, append.value()).ok());
  const auto prior_revision = session.content_revision();
  session.restore_snapshot_as_history_transition(checkpoint);
  EXPECT_GT(session.content_revision(), prior_revision);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_EQ(session.undo_depth(), 0U);
  CurveDocument branched = current_curve();
  ASSERT_TRUE(branched.append_point({.position = {6.0F, 1.0F, 0.0F}}).ok());
  EXPECT_GT(branched.points().back().id.index, retired.index);

  std::vector<SessionDocumentHistoryRestore> history;
  history.push_back({.object = curve,
                     .dirty = {.topology = true, .geometry = true, .overlays = true},
                     .curve_document = expanded});
  auto restored = session.restore_documents_as_history_transition(std::move(history));
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(current_curve().has_same_geometry(expanded));
  expect_mesh_only_selected();
  EXPECT_EQ(session.undo_depth(), 0U);

  ModelingSession clean = session.current_workspace_checkpoint();
  EXPECT_FALSE(clean.can_undo());
  EXPECT_FALSE(clean.preview_active());
  EXPECT_TRUE(clean.curve_document_copy(curve).or_throw().has_same_geometry(expanded));
  session.restore_snapshot_preserving_identity_high_water(checkpoint);
  branched = current_curve();
  ASSERT_TRUE(branched.append_point({.position = {7.0F, 0.0F, 0.0F}}).ok());
  EXPECT_GT(branched.points().back().id.index, retired.index);
}

TEST_F(ModelingCurveSessionTests, HydrationIsAtomicAndRetainsMixedClipboard) {
  const std::array selected = {mesh, curve};
  ASSERT_TRUE(session.apply_object_selection(selected).ok());
  ASSERT_TRUE(session.copy_selection_to_clipboard().ok());
  const CurveDocument original = current_curve();
  std::vector<SessionWorkspaceDocument> documents;
  documents.push_back({.document = session.document_copy(mesh).or_throw(), .name = "Solid"});
  documents.push_back({.name = "Cable", .selected = true, .curve_document = original});
  auto loaded = session.replace_workspace_preserving_clipboard(std::move(documents));
  ASSERT_TRUE(loaded.ok()) << loaded.error_message();
  ASSERT_EQ(loaded.value().size(), 2U);
  EXPECT_EQ(session.undo_depth(), 0U);
  EXPECT_EQ(session.clipboard_object_count(), 2U);
  EXPECT_EQ(session.objects()[1].kind, SessionObjectKind::Curve);
  EXPECT_TRUE(session.objects()[1].selected);
  EXPECT_TRUE(session.curve_document_copy(loaded.value()[1]).or_throw().has_same_geometry(original));

  const auto revision = session.content_revision();
  std::vector<SessionWorkspaceDocument> invalid;
  invalid.push_back({.document = PolygonDocument::make_box({}).or_throw(),
                     .name = "Ambiguous", .curve_document = original});
  EXPECT_FALSE(session.replace_workspace_preserving_clipboard(std::move(invalid)).ok());
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.objects()[1].name, "Cable");
  EXPECT_EQ(session.clipboard_object_count(), 2U);

  session.reset_workspace_preserving_clipboard();
  EXPECT_TRUE(session.objects().empty());
  auto pasted = session.paste_clipboard();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session.curve_document_copy(pasted.value().objects[1]).or_throw().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests, PolygonOperationsRejectCurveTargetsAtomically) {
  const auto original_curve = current_curve();
  const auto original_mesh = session.document_copy(mesh).or_throw();
  PolygonDocument changed_mesh = original_mesh;
  ASSERT_TRUE(changed_mesh.select_all_faces().ok());
  auto moved = changed_mesh.translate_selection({2.0F, 0.0F, 0.0F});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  const auto revision = session.content_revision();
  std::vector<SessionDocumentUpdate> updates;
  updates.push_back({.object = mesh, .document = changed_mesh, .receipt = moved.value()});
  updates.push_back({.object = curve, .document = original_mesh, .receipt = moved.value()});
  EXPECT_FALSE(session.commit_documents_batch(std::move(updates)).ok());
  const std::array targets = {mesh, curve};
  EXPECT_FALSE(session.document_copies(targets).ok());
  EXPECT_FALSE(session.combine_objects(targets).ok());
  EXPECT_FALSE(session.bridge_objects(mesh, curve, {}, SelectionKind::Face).ok());
  EXPECT_FALSE(session.select_face(curve, original_mesh.face_ids().front()).ok());
  EXPECT_FALSE(session.commit_document(curve, changed_mesh, moved.value()).ok());
  EXPECT_FALSE(session.commit_curve_document(mesh, original_curve, changed_receipt()).ok());
  EXPECT_FALSE(session.begin_document_preview(curve, "mesh.edit", original_mesh, moved.value()).ok());
  EXPECT_FALSE(session.begin_curve_document_preview(mesh, "curve.edit", original_curve, changed_receipt()).ok());
  EXPECT_FALSE(session.semantic_overlay(curve).ok());
  EXPECT_EQ(session.document_copy(mesh).or_throw().content_identity(), original_mesh.content_identity());
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), 0U);
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, ComponentSelectionBatchIncludesCurveObjectSelectionSafely) {
  ASSERT_TRUE(session.select_object(curve).ok());
  session.clear_history();
  const auto original = current_curve();
  auto polygon = session.document_copy(mesh).or_throw();
  ASSERT_TRUE(polygon.select_face(polygon.face_ids().front()).ok());
  const PolygonSelectionSnapshot selected = polygon.selection();
  const std::array targets = {mesh};
  const std::array updates = {SessionComponentSelectionUpdate{mesh, selected}};
  const auto committed =
      session.commit_component_selection_batch(updates, targets, mesh);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  expect_mesh_only_selected();
  ASSERT_TRUE(session.undo().ok());
  EXPECT_FALSE(session.objects()[0].selected);
  EXPECT_TRUE(session.objects()[1].selected);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  const auto revision = session.content_revision();
  const std::array invalid = {
      SessionComponentSelectionUpdate{mesh, selected},
      SessionComponentSelectionUpdate{curve, selected},
  };
  EXPECT_FALSE(session.commit_component_selection_batch(invalid, targets, mesh).ok());
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_TRUE(session.can_redo());
  ASSERT_TRUE(session.redo().ok());
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, CurveTranslateAndHistoryRestoreRejectInvalidTargets) {
  ASSERT_TRUE(session.select_object(curve).ok());
  session.clear_history();
  const auto original = current_curve();
  ASSERT_TRUE(session.translate_selection({0.0F, 4.0F, 0.0F}).ok());
  EXPECT_FLOAT_EQ(current_curve().points().front().position.y, 4.0F);
  EXPECT_FLOAT_EQ(current_curve().points().front().outgoing_handle.y,
                  original.points().front().outgoing_handle.y);
  const auto revision = session.content_revision();
  EXPECT_FALSE(session.translate_selection({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}).ok());
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_EQ(session.undo_depth(), 1U);

  std::vector<SessionDocumentHistoryRestore> invalid;
  invalid.push_back({.object = curve, .dirty = {.geometry = true}, .curve_document = original});
  invalid.push_back({.object = mesh, .dirty = {.geometry = true}, .curve_document = original});
  EXPECT_FALSE(session.restore_documents_as_history_transition(std::move(invalid)).ok());
  EXPECT_EQ(session.content_revision(), revision);
  EXPECT_FLOAT_EQ(current_curve().points().front().position.y, 4.0F);
  EXPECT_EQ(session.undo_depth(), 1U);
  ASSERT_TRUE(session.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  ASSERT_TRUE(session.begin_translate_preview(curve, {1.0F, 0.0F, 0.0F}).ok());
  const auto preview = session.preview_summary().id;
  EXPECT_FLOAT_EQ(session.preview_curve_document_copy(preview).or_throw().points().front().position.x,
                  original.points().front().position.x + 1.0F);
  ASSERT_TRUE(session.cancel_preview(preview).ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

} // namespace
