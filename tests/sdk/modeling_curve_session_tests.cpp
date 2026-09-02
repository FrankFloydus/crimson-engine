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
    auto added_mesh = session_.add_document(std::move(polygon).value(), "Box");
    ASSERT_TRUE(added_mesh.ok()) << added_mesh.error_message();
    mesh_ = added_mesh.value();

    const std::array points = {
        CurvePointSpec{.position = {-2.0F, 0.0F, 0.0F},
                       .outgoing_handle = {1.0F, 2.0F, 0.0F}},
        CurvePointSpec{.position = {2.0F, 0.0F, 0.0F},
                       .incoming_handle = {-1.0F, 2.0F, 0.0F}},
    };
    auto document = CurveDocument::make_bezier(points);
    ASSERT_TRUE(document.ok()) << document.error_message();
    auto added_curve = session_.add_curve_document(std::move(document).value(), "Path");
    ASSERT_TRUE(added_curve.ok()) << added_curve.error_message();
    curve_ = added_curve.value();
    ASSERT_TRUE(session_.select_object(mesh_).ok());
    session_.clear_history();
  }

  [[nodiscard]] CurveDocument current_curve() const {
    return session_.curve_document_copy(curve_).or_throw();
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

  [[nodiscard]] std::pair<PolygonDocument, OperationResult>
  translated_mesh(Vec3 delta) const {
    PolygonDocument document = session_.document_copy(mesh_).or_throw();
    static_cast<void>(document.select_all_faces().or_throw());
    OperationResult moved = document.translate_selection(delta).or_throw();
    static_cast<void>(document.clear_selection().or_throw());
    moved.dirty.selection = false;
    moved.dirty.overlays = true;
    return {std::move(document), std::move(moved)};
  }

  void expect_mesh_only_selected() const {
    const auto objects = session_.objects();
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
    ASSERT_EQ(actual.objects.size(), expected.objects.size());
    for (std::size_t index = 0; index < actual.objects.size(); ++index) {
      const auto &actual_object = actual.objects[index];
      const auto &expected_object = expected.objects[index];
      EXPECT_EQ(actual_object.object_id, expected_object.object_id);
      EXPECT_EQ(actual_object.action, expected_object.action);
      EXPECT_EQ(actual_object.kind, expected_object.kind);
      EXPECT_EQ(actual_object.base_content_revision,
                expected_object.base_content_revision);
      EXPECT_EQ(actual_object.content_revision,
                expected_object.content_revision);
      EXPECT_EQ(actual_object.changed, expected_object.changed);
      EXPECT_EQ(actual_object.material_slot, expected_object.material_slot);
      EXPECT_EQ(actual_object.selected, expected_object.selected);
      expect_same_dirty(actual_object.dirty, expected_object.dirty);
    }
  }

  static void expect_same_curve_snapshot(const CurveDocument &actual,
                                         const CurveDocument &expected) {
    EXPECT_TRUE(actual.has_same_geometry(expected));
    EXPECT_EQ(actual.content_identity(), expected.content_identity());
    EXPECT_EQ(actual.content_revision(), expected.content_revision());
    EXPECT_EQ(actual.identity_high_water(), expected.identity_high_water());
  }

  static void expect_same_change_snapshot(const SessionChangeSnapshot &actual,
                                          const SessionChangeSnapshot &expected) {
    EXPECT_EQ(actual.content_revision, expected.content_revision);
    EXPECT_EQ(actual.selection_revision, expected.selection_revision);
    ASSERT_EQ(actual.objects.size(), expected.objects.size());
    for (std::size_t index = 0; index < actual.objects.size(); ++index) {
      SCOPED_TRACE(index);
      EXPECT_EQ(actual.objects[index].id, expected.objects[index].id);
      EXPECT_EQ(actual.objects[index].name, expected.objects[index].name);
      EXPECT_EQ(actual.objects[index].deleted, expected.objects[index].deleted);
      EXPECT_EQ(actual.objects[index].kind, expected.objects[index].kind);
      expect_same_dirty(actual.objects[index].dirty,
                        expected.objects[index].dirty);
    }
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

  ModelingSession session_;
  ObjectId mesh_{};
  ObjectId curve_{};
};

TEST_F(ModelingCurveSessionTests, MixedKindsHaveTypedCopiesAndPinnedSnapshots) {
  const auto objects = session_.objects();
  ASSERT_EQ(objects.size(), 2U);
  EXPECT_EQ(objects[0].kind, SessionObjectKind::Mesh);
  EXPECT_EQ(objects[1].kind, SessionObjectKind::Curve);
  EXPECT_EQ(objects[1].name, "Path");
  EXPECT_EQ(objects[1].selection_revision, 0U);
  EXPECT_EQ(session_.document_copy(curve_).error().code, ErrorCode::UnsupportedOperation);
  EXPECT_EQ(session_.curve_document_copy(mesh_).error().code, ErrorCode::UnsupportedOperation);
  EXPECT_EQ(session_.curve_document_copy({}).error().code, ErrorCode::InvalidId);
  EXPECT_FALSE(session_.document_snapshot(curve_).ok());
  EXPECT_FALSE(session_.curve_document_snapshot(mesh_).ok());

  auto pinned = session_.curve_document_snapshot(curve_);
  ASSERT_TRUE(pinned.ok()) << pinned.error_message();
  const CurveDocument original = *pinned.value();
  const CurveDocument edited = translated_curve({3.0F, 0.0F, 0.0F});
  ASSERT_TRUE(session_.commit_curve_document(curve_, edited, changed_receipt()).ok());
  EXPECT_TRUE(pinned.value()->has_same_geometry(original));
  EXPECT_FALSE(pinned.value()->has_same_geometry(current_curve()));

  const std::array selected = {mesh_, curve_};
  ASSERT_TRUE(session_.apply_object_selection(selected).ok());
  const auto selected_objects = session_.objects();
  EXPECT_TRUE(selected_objects[0].selected);
  EXPECT_TRUE(selected_objects[1].selected);
  const auto changes = session_.changes_since(0);
  ASSERT_EQ(changes.objects.size(), 2U);
  EXPECT_EQ(changes.objects[0].kind, SessionObjectKind::Mesh);
  EXPECT_EQ(changes.objects[1].kind, SessionObjectKind::Curve);
}

TEST_F(ModelingCurveSessionTests, CurveCommitPreservesSelectionAndUsesExistingHistory) {
  const CurveDocument original = current_curve();
  const auto polygon = session_.document_copy(mesh_).or_throw();
  const auto revision = session_.content_revision();
  const CurveDocument edited = translated_curve({0.0F, 0.0F, 2.0F});
  auto committed = session_.commit_curve_document(curve_, edited, changed_receipt());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_EQ(session_.content_revision(), revision + 1U);
  expect_mesh_only_selected();
  EXPECT_EQ(session_.document_copy(mesh_).or_throw().content_identity(),
            polygon.content_identity());
  const auto changes = session_.changes_since(revision);
  ASSERT_EQ(changes.objects.size(), 1U);
  EXPECT_EQ(changes.objects.front().kind, SessionObjectKind::Curve);
  EXPECT_TRUE(changes.objects.front().dirty.geometry);
  EXPECT_FALSE(changes.objects.front().dirty.selection);

  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(edited));
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, FailedAndNoOpCurveCommitsKeepRedoAndRevisions) {
  const CurveDocument original = current_curve();
  const CurveDocument edited = translated_curve({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(session_.commit_curve_document(curve_, edited, changed_receipt()).ok());
  ASSERT_TRUE(session_.undo().ok());
  const auto revision = session_.content_revision();
  const auto identity = current_curve().content_identity();
  const CurveDocument identical =
      CurveDocument::from_authored_curve(current_curve().authored_curve()).or_throw();
  auto unchanged = session_.commit_curve_document(curve_, identical, changed_receipt());
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);

  OperationResult failed;
  failed.success = false;
  failed.error_code = ErrorCode::ValidationFailed;
  EXPECT_FALSE(session_.commit_curve_document(curve_, edited, failed).or_throw().success);
  EXPECT_FALSE(session_.commit_curve_document(curve_, edited, {}).or_throw().changed);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_EQ(current_curve().content_identity(), identity);
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.undo_depth(), 0U);
  EXPECT_TRUE(session_.can_redo());
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, CurvePreviewSharesOneRegistryAndCommitsOnce) {
  const CurveDocument original = current_curve();
  const CurveDocument first = translated_curve({0.0F, 2.0F, 0.0F});
  const auto revision = session_.content_revision();
  auto started = session_.begin_curve_document_preview(curve_, "modeling.curve.edit",
                                                       first, changed_receipt());
  ASSERT_TRUE(started.ok()) << started.error_message();
  const PreviewId preview = started.value();
  EXPECT_TRUE(session_.preview_active(preview));
  EXPECT_EQ(session_.preview_summary().kind, SessionObjectKind::Curve);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_TRUE(session_.preview_curve_document_copy(preview).or_throw().has_same_geometry(first));
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.undo_depth(), 0U);
  EXPECT_FALSE(session_.begin_translate_preview(mesh_, {}).ok());
  EXPECT_FALSE(session_.select_object(curve_).ok());
  EXPECT_FALSE(session_.commit_curve_document(curve_, first, changed_receipt()).ok());
  EXPECT_FALSE(session_.undo().ok());
  EXPECT_FALSE(session_.preview_payload(preview).ok());
  EXPECT_FALSE(session_.preview_document_copy(preview).ok());
  auto polygon = session_.document_copy(mesh_).or_throw();
  EXPECT_FALSE(session_.update_document_preview(preview, polygon, changed_receipt()).ok());
  EXPECT_TRUE(session_.preview_active(preview));

  const CurveDocument second = translated_curve({0.0F, 3.0F, 0.0F});
  ASSERT_TRUE(session_.update_curve_document_preview(preview, second, changed_receipt()).ok());
  const auto payload_revision = session_.preview_summary().content_revision;
  auto repeated = session_.update_curve_document_preview(preview, second, changed_receipt());
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  EXPECT_EQ(session_.preview_summary().content_revision, payload_revision);

  auto committed = session_.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_FALSE(session_.preview_active());
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_TRUE(current_curve().has_same_geometry(second));
  expect_mesh_only_selected();
  EXPECT_EQ(session_.commit_preview(preview).error().code, ErrorCode::StaleId);
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests,
       AtomicMixedPreviewUpdatesInBeginOrderAndFailurePreservesCandidate) {
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();
  const std::uint64_t content_revision = session_.content_revision();
  const std::size_t undo_depth = session_.undo_depth();

  auto [first_mesh, first_mesh_receipt] = translated_mesh({1.0F, 0.0F, 0.0F});
  const CurveDocument first_curve = translated_curve({0.0F, 1.0F, 0.0F});
  std::vector<SessionPreviewDocumentUpdate> begun_updates;
  begun_updates.push_back(
      {.object = mesh_, .document = first_mesh, .receipt = first_mesh_receipt});
  begun_updates.push_back(
      {.object = curve_, .document = first_curve, .receipt = changed_receipt()});
  const PreviewId preview =
      session_
          .begin_preview_transaction("modeling.mixed.transform",
                                     std::move(begun_updates))
          .or_throw();

  const PreviewTransactionSummary first_summary = session_.preview_summary();
  ASSERT_EQ(first_summary.objects.size(), 2U);
  EXPECT_EQ(first_summary.object_id, mesh_);
  EXPECT_EQ(first_summary.kind, SessionObjectKind::Mesh);
  EXPECT_EQ(first_summary.objects[0].object_id, mesh_);
  EXPECT_EQ(first_summary.objects[1].object_id, curve_);
  EXPECT_EQ(first_summary.objects[0].kind, SessionObjectKind::Mesh);
  EXPECT_EQ(first_summary.objects[1].kind, SessionObjectKind::Curve);
  EXPECT_TRUE(first_summary.objects[0].changed);
  EXPECT_TRUE(first_summary.objects[1].changed);

  const SessionPreviewPayload first_payload =
      session_.preview_transaction_payload(preview).or_throw();
  ASSERT_EQ(first_payload.objects.size(), 2U);
  EXPECT_EQ(first_payload.objects[0].object_id, mesh_);
  EXPECT_EQ(first_payload.objects[1].object_id, curve_);
  const auto *mesh_payload =
      std::get_if<ToolPreviewPayload>(&first_payload.objects[0].payload);
  ASSERT_NE(mesh_payload, nullptr);
  EXPECT_TRUE(mesh_payload->valid);
  EXPECT_FALSE(mesh_payload->mesh.vertices.empty());
  const auto *curve_payload =
      std::get_if<CurveDocument>(&first_payload.objects[1].payload);
  ASSERT_NE(curve_payload, nullptr);
  EXPECT_TRUE(curve_payload->has_same_geometry(first_curve));
  EXPECT_TRUE(session_.preview_payload(preview).ok());
  EXPECT_TRUE(session_.preview_document_copy(preview, mesh_)
                  .or_throw()
                  .has_same_geometry(first_mesh));
  EXPECT_TRUE(session_.preview_curve_document_copy(preview, curve_)
                  .or_throw()
                  .has_same_geometry(first_curve));
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(original_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));

  auto [second_mesh, second_mesh_receipt] = translated_mesh({2.0F, 0.0F, 0.0F});
  const CurveDocument second_curve = translated_curve({0.0F, 2.0F, 0.0F});
  std::vector<SessionPreviewDocumentUpdate> reversed_updates;
  reversed_updates.push_back({.object = curve_,
                              .document = second_curve,
                              .receipt = changed_receipt()});
  reversed_updates.push_back({.object = mesh_,
                              .document = second_mesh,
                              .receipt = second_mesh_receipt});
  ASSERT_TRUE(
      session_.update_preview_transaction(preview, std::move(reversed_updates))
          .ok());
  const PreviewTransactionSummary second_summary = session_.preview_summary();
  ASSERT_EQ(second_summary.objects.size(), 2U);
  EXPECT_EQ(second_summary.objects[0].object_id, mesh_);
  EXPECT_EQ(second_summary.objects[1].object_id, curve_);
  EXPECT_GT(second_summary.objects[0].content_revision,
            first_summary.objects[0].content_revision);
  EXPECT_GT(second_summary.objects[1].content_revision,
            first_summary.objects[1].content_revision);

  OperationResult repeated_mesh_receipt;
  repeated_mesh_receipt.changed = true;
  repeated_mesh_receipt.dirty.geometry = true;
  repeated_mesh_receipt.dirty.overlays = true;
  std::vector<SessionPreviewDocumentUpdate> repeated_updates;
  repeated_updates.push_back({
      .object = curve_,
      .document =
          session_.preview_curve_document_copy(preview, curve_).or_throw(),
      .receipt = changed_receipt(),
  });
  repeated_updates.push_back({
      .object = mesh_,
      .document = session_.preview_document_copy(preview, mesh_).or_throw(),
      .receipt = repeated_mesh_receipt,
  });
  const auto repeated =
      session_.update_preview_transaction(preview, std::move(repeated_updates));
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  expect_same_preview_summary(session_.preview_summary(), second_summary);

  std::vector<SessionPreviewDocumentUpdate> duplicate_updates;
  duplicate_updates.push_back({.object = mesh_,
                               .document = second_mesh,
                               .receipt = second_mesh_receipt});
  duplicate_updates.push_back({.object = mesh_,
                               .document = second_mesh,
                               .receipt = second_mesh_receipt});
  EXPECT_EQ(
      session_.update_preview_transaction(preview, std::move(duplicate_updates))
          .error()
          .code,
      ErrorCode::InvalidArgument);
  expect_same_preview_summary(session_.preview_summary(), second_summary);

  std::vector<SessionPreviewDocumentUpdate> mismatched_updates;
  mismatched_updates.push_back({.object = mesh_,
                                .document = second_curve,
                                .receipt = changed_receipt()});
  mismatched_updates.push_back({.object = curve_,
                                .document = second_mesh,
                                .receipt = second_mesh_receipt});
  EXPECT_EQ(
      session_.update_preview_transaction(preview,
                                         std::move(mismatched_updates))
          .error()
          .code,
      ErrorCode::UnsupportedOperation);
  expect_same_preview_summary(session_.preview_summary(), second_summary);

  auto [third_mesh, third_mesh_receipt] = translated_mesh({3.0F, 0.0F, 0.0F});
  OperationResult failed_curve_receipt;
  failed_curve_receipt.success = false;
  failed_curve_receipt.error_code = ErrorCode::ValidationFailed;
  failed_curve_receipt.message = "Rejected second object.";
  std::vector<SessionPreviewDocumentUpdate> failed_updates;
  failed_updates.push_back({.object = mesh_,
                            .document = std::move(third_mesh),
                            .receipt = std::move(third_mesh_receipt)});
  failed_updates.push_back({.object = curve_,
                            .document = translated_curve({0.0F, 3.0F, 0.0F}),
                            .receipt = std::move(failed_curve_receipt)});
  EXPECT_EQ(
      session_.update_preview_transaction(preview, std::move(failed_updates))
          .error()
          .code,
      ErrorCode::ValidationFailed);
  expect_same_preview_summary(session_.preview_summary(), second_summary);
  EXPECT_TRUE(session_.preview_document_copy(preview, mesh_)
                  .or_throw()
                  .has_same_geometry(second_mesh));
  EXPECT_TRUE(session_.preview_curve_document_copy(preview, curve_)
                  .or_throw()
                  .has_same_geometry(second_curve));

  ASSERT_TRUE(session_.cancel_preview(preview).ok());
  EXPECT_EQ(session_.content_revision(), content_revision);
  EXPECT_EQ(session_.undo_depth(), undo_depth);
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(original_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));

  std::vector<SessionPreviewDocumentUpdate> duplicate_begin;
  duplicate_begin.push_back({.object = mesh_,
                             .document = second_mesh,
                             .receipt = second_mesh_receipt});
  duplicate_begin.push_back({.object = mesh_,
                             .document = second_mesh,
                             .receipt = second_mesh_receipt});
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.duplicate",
                                           std::move(duplicate_begin))
                .error()
                .code,
            ErrorCode::InvalidArgument);
  EXPECT_FALSE(session_.preview_active());

  std::vector<SessionPreviewDocumentUpdate> mismatched_begin;
  mismatched_begin.push_back({.object = mesh_,
                              .document = second_curve,
                              .receipt = changed_receipt()});
  mismatched_begin.push_back({.object = curve_,
                              .document = second_mesh,
                              .receipt = second_mesh_receipt});
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.kind",
                                           std::move(mismatched_begin))
                .error()
                .code,
            ErrorCode::UnsupportedOperation);
  EXPECT_FALSE(session_.preview_active());
  EXPECT_EQ(session_.content_revision(), content_revision);
  EXPECT_EQ(session_.undo_depth(), undo_depth);

  std::vector<SessionPreviewDocumentUpdate> valid_after_failures;
  valid_after_failures.push_back({.object = mesh_,
                                  .document = second_mesh,
                                  .receipt = second_mesh_receipt});
  const PreviewId next_preview =
      session_
          .begin_preview_transaction("modeling.valid.after_failures",
                                     std::move(valid_after_failures))
          .or_throw();
  EXPECT_EQ(next_preview.index, preview.index + 1U);
  ASSERT_TRUE(session_.cancel_preview(next_preview).ok());
}

TEST_F(ModelingCurveSessionTests,
       AtomicMixedPreviewCommitsAndUndoesAsOneSessionTransition) {
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();
  const std::uint64_t content_revision = session_.content_revision();

  auto [candidate_mesh, mesh_receipt] = translated_mesh({1.0F, 2.0F, 0.0F});
  const CurveDocument candidate_curve = translated_curve({0.0F, 0.0F, 2.0F});
  OperationResult curve_receipt = changed_receipt();
  curve_receipt.dirty.topology = true;
  std::vector<SessionPreviewDocumentUpdate> updates;
  updates.push_back(
      {.object = mesh_, .document = candidate_mesh, .receipt = mesh_receipt});
  updates.push_back(
      {.object = curve_, .document = candidate_curve, .receipt = curve_receipt});
  const PreviewId preview = session_
                                .begin_preview_transaction(
                                    "modeling.mixed.commit", std::move(updates))
                                .or_throw();

  const auto committed = session_.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  EXPECT_TRUE(committed.value().dirty.geometry);
  EXPECT_TRUE(committed.value().dirty.topology);
  EXPECT_TRUE(committed.value().dirty.overlays);
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_EQ(session_.content_revision(), content_revision + 1U);
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(candidate_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(candidate_curve));

  const SessionChangeSnapshot changes = session_.changes_since(content_revision);
  ASSERT_EQ(changes.objects.size(), 2U);
  const auto mesh_change = std::ranges::find_if(
      changes.objects,
      [&](const SessionObjectChange &change) { return change.id == mesh_; });
  const auto curve_change = std::ranges::find_if(
      changes.objects,
      [&](const SessionObjectChange &change) { return change.id == curve_; });
  ASSERT_NE(mesh_change, changes.objects.end());
  ASSERT_NE(curve_change, changes.objects.end());
  EXPECT_TRUE(mesh_change->dirty.geometry);
  EXPECT_TRUE(mesh_change->dirty.overlays);
  EXPECT_TRUE(curve_change->dirty.geometry);
  EXPECT_TRUE(curve_change->dirty.topology);
  EXPECT_TRUE(curve_change->dirty.overlays);

  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(original_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(candidate_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(candidate_curve));
}

TEST_F(ModelingCurveSessionTests,
       AtomicMixedNoOpCommitDoesNotPublishHistoryOrRevisions) {
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();
  const std::uint64_t content_revision = session_.content_revision();
  const std::size_t undo_depth = session_.undo_depth();

  OperationResult mesh_receipt = changed_receipt();
  mesh_receipt.dirty.geometry = true;
  std::vector<SessionPreviewDocumentUpdate> updates;
  updates.push_back({.object = mesh_,
                     .document = original_mesh,
                     .receipt = mesh_receipt});
  updates.push_back({.object = curve_,
                     .document = original_curve,
                     .receipt = changed_receipt()});
  const PreviewId preview =
      session_
          .begin_preview_transaction("modeling.mixed.noop", std::move(updates))
          .or_throw();
  const PreviewTransactionSummary summary = session_.preview_summary();
  ASSERT_EQ(summary.objects.size(), 2U);
  EXPECT_FALSE(summary.objects[0].changed);
  EXPECT_FALSE(summary.objects[1].changed);

  const auto committed = session_.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.value().changed);
  EXPECT_FALSE(session_.preview_active());
  EXPECT_EQ(session_.content_revision(), content_revision);
  EXPECT_EQ(session_.undo_depth(), undo_depth);
  EXPECT_TRUE(
      session_.document_copy(mesh_).or_throw().has_same_geometry(original_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));
}

TEST_F(ModelingCurveSessionTests,
       SingleObjectPreviewCompatibilityMirrorsPrimaryTransactionObject) {
  auto [candidate, receipt] = translated_mesh({0.0F, 1.0F, 0.0F});
  const PreviewId preview =
      session_
          .begin_document_preview(mesh_, "modeling.compatibility", candidate,
                                  receipt)
          .or_throw();
  const PreviewTransactionSummary summary = session_.preview_summary();
  ASSERT_EQ(summary.objects.size(), 1U);
  EXPECT_EQ(summary.object_id, summary.objects.front().object_id);
  EXPECT_EQ(summary.kind, summary.objects.front().kind);
  EXPECT_EQ(summary.content_revision, summary.objects.front().content_revision);
  expect_same_dirty(summary.dirty, summary.objects.front().dirty);

  const ToolPreviewPayload legacy_payload =
      session_.preview_payload(preview).or_throw();
  const SessionPreviewPayload transaction_payload =
      session_.preview_transaction_payload(preview).or_throw();
  ASSERT_EQ(transaction_payload.objects.size(), 1U);
  const auto *typed_payload = std::get_if<ToolPreviewPayload>(
      &transaction_payload.objects.front().payload);
  ASSERT_NE(typed_payload, nullptr);
  EXPECT_EQ(legacy_payload.mesh.vertices.size(),
            typed_payload->mesh.vertices.size());
  EXPECT_EQ(legacy_payload.mesh.indices.size(),
            typed_payload->mesh.indices.size());
  EXPECT_EQ(
      session_.preview_document_copy(preview).or_throw().content_identity(),
      session_.preview_document_copy(preview, mesh_)
          .or_throw()
          .content_identity());
  ASSERT_TRUE(session_.commit_preview(preview).ok());
  EXPECT_EQ(session_.undo_depth(), 1U);
  ASSERT_TRUE(session_.undo().ok());
}

TEST_F(ModelingCurveSessionTests,
       StructuralMixedPreviewPublishesStableIdsAndRestoresWholeCheckpoint) {
  const ModelingSession durable_before = session_;
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();
  auto [first_mesh, first_mesh_receipt] =
      translated_mesh({1.0F, 0.0F, 0.0F});
  CurveDocument first_added_curve =
      translated_curve({0.0F, 1.0F, 0.0F});

  SessionPreviewTransactionRequest request;
  request.replace_object_selection = true;
  request.active_participant_index = 0U;
  request.participants.push_back({
      .action = SessionPreviewAction::AddProvisional,
      .document = first_added_curve,
      .receipt = changed_receipt(),
      .name = "Path Split",
      .material_slot = 31U,
      .selected = true,
  });
  request.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = curve_,
      .material_slot = 21U,
  });
  request.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = mesh_,
      .document = first_mesh,
      .receipt = first_mesh_receipt,
      .material_slot = 11U,
      .selected = true,
  });

  const SessionPreviewBeginResult begun =
      session_
          .begin_preview_transaction("modeling.structural.mixed",
                                     std::move(request))
          .or_throw();
  ASSERT_EQ(begun.participants.size(), 3U);
  EXPECT_EQ(begun.participants[0].participant_index, 0U);
  EXPECT_EQ(begun.participants[0].action,
            SessionPreviewAction::AddProvisional);
  const ObjectId provisional = begun.participants[0].object_id;
  ASSERT_TRUE(provisional.valid());
  EXPECT_GT(provisional.index, curve_.index);
  EXPECT_EQ(begun.participants[1].object_id, curve_);
  EXPECT_EQ(begun.participants[2].object_id, mesh_);

  const PreviewTransactionSummary first_summary = session_.preview_summary();
  ASSERT_EQ(first_summary.objects.size(), 3U);
  EXPECT_EQ(first_summary.objects[0].object_id, mesh_);
  EXPECT_EQ(first_summary.objects[0].action,
            SessionPreviewAction::ReplaceExisting);
  EXPECT_EQ(first_summary.objects[0].material_slot, 11U);
  EXPECT_EQ(first_summary.objects[1].object_id, curve_);
  EXPECT_EQ(first_summary.objects[1].action,
            SessionPreviewAction::RemoveExisting);
  EXPECT_EQ(first_summary.objects[2].object_id, provisional);
  EXPECT_EQ(first_summary.objects[2].action,
            SessionPreviewAction::AddProvisional);
  EXPECT_EQ(first_summary.objects[2].kind, SessionObjectKind::Curve);
  EXPECT_EQ(first_summary.objects[2].material_slot, 31U);
  EXPECT_TRUE(first_summary.objects[2].selected);

  const SessionPreviewPayload first_payload =
      session_.preview_transaction_payload(begun.preview_id).or_throw();
  ASSERT_EQ(first_payload.objects.size(), 3U);
  EXPECT_TRUE(std::holds_alternative<ToolPreviewPayload>(
      first_payload.objects[0].payload));
  EXPECT_TRUE(std::holds_alternative<std::monostate>(
      first_payload.objects[1].payload));
  EXPECT_TRUE(std::holds_alternative<CurveDocument>(
      first_payload.objects[2].payload));
  EXPECT_EQ(first_payload.objects[2].object_id, provisional);

  EXPECT_TRUE(session_.preview_base_document_copy(begun.preview_id, mesh_)
                  .or_throw()
                  .has_same_geometry(original_mesh));
  EXPECT_TRUE(session_
                  .preview_base_curve_document_copy(begun.preview_id, curve_)
                  .or_throw()
                  .has_same_geometry(original_curve));
  EXPECT_EQ(session_
                .preview_base_curve_document_copy(begun.preview_id,
                                                  provisional)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  EXPECT_EQ(session_.preview_curve_document_copy(begun.preview_id, curve_)
                .error()
                .code,
            ErrorCode::InvalidId);
  EXPECT_TRUE(session_
                  .preview_curve_document_copy(begun.preview_id, provisional)
                  .or_throw()
                  .has_same_geometry(first_added_curve));

  const SessionPreviewCheckpoint checkpoint =
      session_.capture_preview_checkpoint(begun.preview_id).or_throw();
  auto [second_mesh, second_mesh_receipt] =
      translated_mesh({2.0F, 0.0F, 0.0F});
  CurveDocument second_added_curve = first_added_curve;
  ASSERT_TRUE(second_added_curve
                  .transform(Transform3{.origin = {0.0F, 2.0F, 0.0F}})
                  .ok());
  std::vector<SessionPreviewDocumentUpdate> second_updates;
  second_updates.push_back({.object = provisional,
                            .document = second_added_curve,
                            .receipt = changed_receipt()});
  second_updates.push_back({.object = mesh_,
                            .document = second_mesh,
                            .receipt = second_mesh_receipt});
  ASSERT_TRUE(session_
                  .update_preview_transaction(begun.preview_id,
                                              std::move(second_updates))
                  .ok());
  EXPECT_TRUE(session_
                  .preview_curve_document_copy(begun.preview_id, provisional)
                  .or_throw()
                  .has_same_geometry(second_added_curve));
  const PreviewTransactionSummary second_summary = session_.preview_summary();

  std::vector<SessionPreviewDocumentUpdate> stale_updates;
  stale_updates.push_back({.object = make_id<ObjectTag>(99'999U),
                           .document = second_mesh,
                           .receipt = second_mesh_receipt});
  stale_updates.push_back({.object = provisional,
                           .document = second_added_curve,
                           .receipt = changed_receipt()});
  EXPECT_EQ(session_
                .update_preview_transaction(begun.preview_id,
                                            std::move(stale_updates))
                .error()
                .code,
            ErrorCode::InvalidArgument);
  expect_same_preview_summary(session_.preview_summary(), second_summary);

  OperationResult failed_receipt;
  failed_receipt.success = false;
  failed_receipt.error_code = ErrorCode::ValidationFailed;
  std::vector<SessionPreviewDocumentUpdate> failed_updates;
  failed_updates.push_back({.object = mesh_,
                            .document = first_mesh,
                            .receipt = first_mesh_receipt});
  failed_updates.push_back({.object = provisional,
                            .document = first_added_curve,
                            .receipt = failed_receipt});
  EXPECT_EQ(session_
                .update_preview_transaction(begun.preview_id,
                                            std::move(failed_updates))
                .error()
                .code,
            ErrorCode::ValidationFailed);
  expect_same_preview_summary(session_.preview_summary(), second_summary);
  EXPECT_TRUE(session_
                  .preview_curve_document_copy(begun.preview_id, provisional)
                  .or_throw()
                  .has_same_geometry(second_added_curve));

  ASSERT_TRUE(session_.restore_preview_checkpoint(checkpoint).ok());
  EXPECT_TRUE(session_.preview_document_copy(begun.preview_id, mesh_)
                  .or_throw()
                  .has_same_geometry(first_mesh));
  EXPECT_TRUE(session_
                  .preview_curve_document_copy(begun.preview_id, provisional)
                  .or_throw()
                  .has_same_geometry(first_added_curve));
  ASSERT_TRUE(session_.cancel_preview(begun.preview_id).ok());
  expect_same_session_observables(session_, durable_before);

  const ObjectId after_cancel =
      session_.add_curve_document(first_added_curve, "After Cancel").or_throw();
  EXPECT_GT(after_cancel.index, provisional.index);
}

TEST_F(ModelingCurveSessionTests,
       StructuralCommitMaterializesRemapSelectionAndOneExactHistoryStep) {
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();
  const std::uint64_t content_revision = session_.content_revision();
  auto [candidate_mesh, mesh_receipt] =
      translated_mesh({1.0F, 2.0F, 0.0F});
  CurveDocument added_curve = translated_curve({0.0F, 0.0F, 3.0F});

  SessionPreviewTransactionRequest request;
  request.replace_object_selection = true;
  request.active_participant_index = 0U;
  request.participants.push_back({
      .action = SessionPreviewAction::AddProvisional,
      .document = added_curve,
      .receipt = changed_receipt(),
      .name = "Committed Split",
      .material_slot = 33U,
      .selected = true,
  });
  request.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = curve_,
      .material_slot = 22U,
  });
  request.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = mesh_,
      .document = candidate_mesh,
      .receipt = mesh_receipt,
      .material_slot = 11U,
      .selected = true,
  });
  const SessionPreviewBeginResult begun =
      session_
          .begin_preview_transaction("modeling.structural.commit",
                                     std::move(request))
          .or_throw();
  const ObjectId provisional = begun.participants[0].object_id;

  const SessionPreviewCommitResult committed =
      session_.commit_preview_transaction(begun.preview_id).or_throw();
  EXPECT_TRUE(committed.operation.changed);
  EXPECT_TRUE(committed.operation.dirty.topology);
  EXPECT_TRUE(committed.operation.dirty.geometry);
  EXPECT_TRUE(committed.operation.dirty.selection);
  EXPECT_TRUE(committed.operation.dirty.materials);
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_EQ(session_.content_revision(), content_revision + 1U);
  ASSERT_EQ(committed.objects.size(), 3U);
  EXPECT_EQ(committed.objects[0].preview_object_id, mesh_);
  EXPECT_EQ(committed.objects[0].committed_object_id, mesh_);
  EXPECT_EQ(committed.objects[0].action,
            SessionPreviewAction::ReplaceExisting);
  EXPECT_EQ(committed.objects[1].preview_object_id, curve_);
  EXPECT_FALSE(committed.objects[1].committed_object_id.valid());
  EXPECT_EQ(committed.objects[1].action,
            SessionPreviewAction::RemoveExisting);
  EXPECT_EQ(committed.objects[2].preview_object_id, provisional);
  EXPECT_EQ(committed.objects[2].committed_object_id, provisional);
  EXPECT_EQ(committed.objects[2].action,
            SessionPreviewAction::AddProvisional);
  EXPECT_EQ(committed.objects[2].material_slot, 33U);
  ASSERT_EQ(committed.selected_objects.size(), 2U);
  EXPECT_EQ(committed.selected_objects[0], mesh_);
  EXPECT_EQ(committed.selected_objects[1], provisional);
  EXPECT_EQ(committed.active_object, provisional);

  const auto after_objects = session_.objects();
  ASSERT_EQ(after_objects.size(), 2U);
  EXPECT_EQ(after_objects[0].id, mesh_);
  EXPECT_TRUE(after_objects[0].selected);
  EXPECT_EQ(after_objects[1].id, provisional);
  EXPECT_EQ(after_objects[1].name, "Committed Split");
  EXPECT_TRUE(after_objects[1].selected);
  EXPECT_TRUE(session_.document_copy(mesh_).or_throw().has_same_geometry(
      candidate_mesh));
  EXPECT_TRUE(session_.curve_document_copy(provisional)
                  .or_throw()
                  .has_same_geometry(added_curve));
  EXPECT_EQ(session_.curve_document_copy(curve_).error().code,
            ErrorCode::InvalidId);

  ASSERT_TRUE(session_.undo().ok());
  const auto undone_objects = session_.objects();
  ASSERT_EQ(undone_objects.size(), 2U);
  EXPECT_EQ(undone_objects[0].id, mesh_);
  EXPECT_TRUE(undone_objects[0].selected);
  EXPECT_EQ(undone_objects[1].id, curve_);
  EXPECT_FALSE(undone_objects[1].selected);
  EXPECT_TRUE(session_.document_copy(mesh_).or_throw().has_same_geometry(
      original_mesh));
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));
  EXPECT_TRUE(session_.can_redo());

  ASSERT_TRUE(session_.redo().ok());
  const auto redone_objects = session_.objects();
  ASSERT_EQ(redone_objects.size(), 2U);
  EXPECT_EQ(redone_objects[0].id, mesh_);
  EXPECT_EQ(redone_objects[1].id, provisional);
  EXPECT_TRUE(session_.document_copy(mesh_).or_throw().has_same_geometry(
      candidate_mesh));
  EXPECT_TRUE(session_.curve_document_copy(provisional)
                  .or_throw()
                  .has_same_geometry(added_curve));
  EXPECT_EQ(session_.commit_preview_transaction(begun.preview_id).error().code,
            ErrorCode::StaleId);
}

TEST_F(ModelingCurveSessionTests,
       AddOnlyPreviewSupportsStableRepeatedUpdatesAndShiftDuplicateSelection) {
  const PolygonDocument original = session_.document_copy(mesh_).or_throw();
  SessionPreviewTransactionRequest request;
  request.replace_object_selection = true;
  request.active_participant_index = 0U;
  request.participants.push_back({
      .action = SessionPreviewAction::AddProvisional,
      .document = original,
      .receipt = changed_receipt(),
      .name = "Box Copy",
      .material_slot = 19U,
      .selected = true,
  });
  const SessionPreviewBeginResult begun =
      session_
          .begin_preview_transaction("modeling.shift_duplicate",
                                     std::move(request))
          .or_throw();
  const ObjectId provisional = begun.participants.front().object_id;
  EXPECT_TRUE(session_.preview_document_copy(begun.preview_id, provisional)
                  .or_throw()
                  .has_same_geometry(original));

  PolygonDocument moved =
      session_.preview_document_copy(begun.preview_id, provisional).or_throw();
  ASSERT_TRUE(moved.select_all_faces().ok());
  OperationResult moved_receipt =
      moved.translate_selection({4.0F, 0.0F, 0.0F}).or_throw();
  ASSERT_TRUE(moved.clear_selection().ok());
  std::vector<SessionPreviewDocumentUpdate> updates;
  updates.push_back({.object = provisional,
                     .document = moved,
                     .receipt = moved_receipt});
  ASSERT_TRUE(session_
                  .update_preview_transaction(begun.preview_id,
                                              std::move(updates))
                  .ok());
  EXPECT_EQ(session_.preview_summary().objects.front().object_id, provisional);

  const SessionPreviewCommitResult committed =
      session_.commit_preview_transaction(begun.preview_id).or_throw();
  EXPECT_EQ(committed.active_object, provisional);
  ASSERT_EQ(committed.selected_objects.size(), 1U);
  EXPECT_EQ(committed.selected_objects.front(), provisional);
  ASSERT_EQ(session_.objects().size(), 3U);
  EXPECT_FALSE(session_.objects()[0].selected);
  EXPECT_FALSE(session_.objects()[1].selected);
  EXPECT_TRUE(session_.objects()[2].selected);
  EXPECT_TRUE(session_.document_copy(provisional)
                  .or_throw()
                  .has_same_geometry(moved));
  EXPECT_EQ(session_.undo_depth(), 1U);
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_EQ(session_.objects().size(), 2U);
  EXPECT_TRUE(session_.objects()[0].selected);
  ASSERT_TRUE(session_.redo().ok());
  ASSERT_EQ(session_.objects().size(), 3U);
  EXPECT_EQ(session_.objects()[2].id, provisional);
}

TEST_F(ModelingCurveSessionTests,
       StructuralCommitActivatesSelectedSurvivorWhenActiveObjectIsRemoved) {
  const CurveDocument survivor = current_curve();
  SessionPreviewTransactionRequest request;
  request.replace_object_selection = true;
  request.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = mesh_,
  });
  request.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = curve_,
      .document = survivor,
      .receipt = changed_receipt(),
      .selected = true,
  });

  const SessionPreviewBeginResult begun =
      session_
          .begin_preview_transaction("modeling.structural.active_fallback",
                                     std::move(request))
          .or_throw();
  const SessionPreviewCommitResult committed =
      session_.commit_preview_transaction(begun.preview_id).or_throw();
  EXPECT_EQ(committed.active_object, curve_);
  ASSERT_EQ(committed.selected_objects.size(), 1U);
  EXPECT_EQ(committed.selected_objects.front(), curve_);
  ASSERT_EQ(session_.objects().size(), 1U);
  EXPECT_EQ(session_.objects().front().id, curve_);
  EXPECT_TRUE(session_.objects().front().selected);
  EXPECT_EQ(session_.undo_depth(), 1U);

  ASSERT_TRUE(session_.undo().ok());
  ASSERT_EQ(session_.objects().size(), 2U);
  EXPECT_TRUE(session_.objects().front().selected);
  ASSERT_TRUE(session_.redo().ok());
  ASSERT_EQ(session_.objects().size(), 1U);
  EXPECT_EQ(session_.objects().front().id, curve_);
  EXPECT_TRUE(session_.objects().front().selected);
}

TEST_F(ModelingCurveSessionTests,
       StructuralBeginRejectsInvalidSetsWithoutConsumingIdsOrPreviewState) {
  const std::uint64_t content_revision = session_.content_revision();
  const std::size_t undo_depth = session_.undo_depth();
  const PolygonDocument original_mesh = session_.document_copy(mesh_).or_throw();
  const CurveDocument original_curve = current_curve();

  SessionPreviewTransactionRequest duplicate;
  duplicate.participants.push_back({
      .action = SessionPreviewAction::AddProvisional,
      .document = original_curve,
      .receipt = changed_receipt(),
  });
  duplicate.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = mesh_,
      .document = original_mesh,
      .receipt = changed_receipt(),
  });
  duplicate.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = mesh_,
  });
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.duplicate_action",
                                           std::move(duplicate))
                .error()
                .code,
            ErrorCode::InvalidArgument);

  SessionPreviewTransactionRequest wrong_kind;
  wrong_kind.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = mesh_,
      .document = original_curve,
      .receipt = changed_receipt(),
  });
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.kind",
                                           std::move(wrong_kind))
                .error()
                .code,
            ErrorCode::UnsupportedOperation);

  SessionPreviewTransactionRequest stale;
  stale.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = make_id<ObjectTag>(9999U),
  });
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.stale",
                                           std::move(stale))
                .error()
                .code,
            ErrorCode::InvalidId);
  EXPECT_FALSE(session_.preview_active());
  EXPECT_EQ(session_.content_revision(), content_revision);
  EXPECT_EQ(session_.undo_depth(), undo_depth);

  SessionPreviewTransactionRequest valid;
  valid.participants.push_back({
      .action = SessionPreviewAction::AddProvisional,
      .document = original_curve,
      .receipt = changed_receipt(),
  });
  const SessionPreviewBeginResult begun =
      session_
          .begin_preview_transaction("modeling.valid.after_failures",
                                     std::move(valid))
          .or_throw();
  EXPECT_EQ(begun.participants.front().object_id.index, curve_.index + 1U);
  const PreviewTransactionSummary active_summary = session_.preview_summary();

  SessionPreviewTransactionRequest blocked;
  blocked.participants.push_back({
      .action = SessionPreviewAction::RemoveExisting,
      .object = mesh_,
  });
  EXPECT_EQ(session_
                .begin_preview_transaction("modeling.invalid.already_active",
                                           std::move(blocked))
                .error()
                .code,
            ErrorCode::InvalidArgument);
  expect_same_preview_summary(session_.preview_summary(), active_summary);
  ASSERT_TRUE(session_.cancel_preview(begun.preview_id).ok());
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointRestoresMixedKindsRevisionsChangeLedgerAndRedo) {
  const CurveDocument redo_candidate =
      translated_curve({0.0F, 0.0F, 3.0F});
  ASSERT_TRUE(session_.rename_object(mesh_, "Renamed Box").ok());
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, redo_candidate, changed_receipt())
          .ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.can_redo());
  ASSERT_EQ(session_.undo_depth(), 1U);

  const ModelingSession expected = session_;
  const SessionChangeSnapshot expected_changes = session_.changes_since(0U);
  const std::array participants = {mesh_, curve_};
  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(participants).or_throw();
  ASSERT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());
  EXPECT_FALSE(session_.can_redo());

  const std::array curve_only = {curve_};
  ASSERT_TRUE(session_.apply_object_selection(curve_only).ok());
  auto [changed_mesh, mesh_receipt] =
      translated_mesh({2.0F, 0.0F, 0.0F});
  ASSERT_TRUE(session_
                  .commit_document(mesh_, std::move(changed_mesh),
                                   std::move(mesh_receipt))
                  .ok());
  const CurveDocument changed_curve =
      translated_curve({0.0F, 4.0F, 0.0F});
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, changed_curve, changed_receipt())
          .ok());
  EXPECT_GT(session_.content_revision(), expected.content_revision());
  EXPECT_GT(session_.selection_revision(), expected.selection_revision());
  EXPECT_GT(session_.undo_depth(), expected.undo_depth());
  const std::uint64_t before_preflight_revision = session_.content_revision();
  const std::size_t before_preflight_undo_depth = session_.undo_depth();
  ASSERT_TRUE(session_.preflight_mutation_checkpoint(checkpoint).ok());
  EXPECT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());
  EXPECT_EQ(session_.content_revision(), before_preflight_revision);
  EXPECT_EQ(session_.undo_depth(), before_preflight_undo_depth);

  const auto restored =
      session_.restore_mutation_checkpoint(std::move(checkpoint));
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  EXPECT_FALSE(checkpoint.valid());
  EXPECT_FALSE(session_.mutation_checkpoint_active());
  expect_same_session_observables(session_, expected);
  expect_same_change_snapshot(session_.changes_since(0U), expected_changes);

  ModelingSession expected_history = expected;
  ASSERT_TRUE(session_.redo().ok());
  ASSERT_TRUE(expected_history.redo().ok());
  expect_same_session_observables(session_, expected_history);
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(expected_history.undo().ok());
  expect_same_session_observables(session_, expected_history);
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(expected_history.undo().ok());
  expect_same_session_observables(session_, expected_history);
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointCheckedCommitConsumesOnlyOwnedValidToken) {
  const CurveDocument old_redo = translated_curve({1.0F, 0.0F, 0.0F});
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, old_redo, changed_receipt()).ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.can_redo());

  const std::array participants = {curve_};
  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(participants).or_throw();
  const CurveDocument committed_curve =
      translated_curve({0.0F, 2.0F, 0.0F});
  ASSERT_TRUE(session_
                  .commit_curve_document(curve_, committed_curve,
                                         changed_receipt())
                  .ok());
  EXPECT_FALSE(session_.abort_active_mutation_checkpoint().ok());
  EXPECT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());

  ModelingSession foreign;
  EXPECT_EQ(foreign.preflight_mutation_checkpoint(checkpoint).error().code,
            ErrorCode::StaleId);
  EXPECT_TRUE(checkpoint.valid());
  const auto wrong_owner =
      foreign.commit_mutation_checkpoint(std::move(checkpoint));
  ASSERT_FALSE(wrong_owner.ok());
  EXPECT_EQ(wrong_owner.error().code, ErrorCode::StaleId);
  EXPECT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());

  const auto committed =
      session_.commit_mutation_checkpoint(std::move(checkpoint));
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(checkpoint.valid());
  EXPECT_FALSE(session_.mutation_checkpoint_active());
  EXPECT_FALSE(session_.can_redo());
  EXPECT_TRUE(current_curve().has_same_geometry(committed_curve));
  EXPECT_EQ(session_.commit_mutation_checkpoint(std::move(checkpoint))
                .error()
                .code,
            ErrorCode::InvalidArgument);

  ASSERT_TRUE(session_.undo().ok());
  EXPECT_FALSE(current_curve().has_same_geometry(committed_curve));
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(committed_curve));
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointNoOpCommitReattachesExactRedoBranch) {
  const CurveDocument redo_candidate =
      translated_curve({0.0F, 0.0F, 6.0F});
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, redo_candidate, changed_receipt())
          .ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.can_redo());
  const ModelingSession expected = session_;
  const SessionChangeSnapshot expected_changes = session_.changes_since(0U);

  const std::array participants = {mesh_};
  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(participants).or_throw();
  EXPECT_FALSE(session_.can_redo());
  const PolygonSelectionSnapshot current_selection =
      session_.document_copy(mesh_).or_throw().selection();
  const std::array updates = {
      SessionComponentSelectionUpdate{mesh_, current_selection}};
  const std::array selected_objects = {mesh_};
  const auto unchanged = session_.commit_component_selection_batch(
      updates, selected_objects, mesh_);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);

  const auto committed =
      session_.commit_mutation_checkpoint(std::move(checkpoint));
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.value().changed);
  EXPECT_FALSE(checkpoint.valid());
  EXPECT_FALSE(session_.mutation_checkpoint_active());
  expect_same_session_observables(session_, expected);
  expect_same_change_snapshot(session_.changes_since(0U), expected_changes);

  ModelingSession expected_after_redo = expected;
  ASSERT_TRUE(session_.redo().ok());
  ASSERT_TRUE(expected_after_redo.redo().ok());
  expect_same_session_observables(session_, expected_after_redo);
  EXPECT_TRUE(current_curve().has_same_geometry(redo_candidate));
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointDiscardKeepsCurrentBranchAndReleasesGuard) {
  const CurveDocument detached_redo =
      translated_curve({0.0F, 0.0F, 4.0F});
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, detached_redo, changed_receipt())
          .ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.can_redo());

  const std::array participants = {curve_};
  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(participants).or_throw();
  const CurveDocument current_branch =
      translated_curve({0.0F, 3.0F, 0.0F});
  ASSERT_TRUE(session_
                  .commit_curve_document(curve_, current_branch,
                                         changed_receipt())
                  .ok());
  const std::uint64_t branch_content_revision = session_.content_revision();
  const std::uint64_t branch_selection_revision =
      session_.selection_revision();
  const std::size_t branch_undo_depth = session_.undo_depth();

  ModelingSession foreign;
  const auto wrong_owner =
      foreign.discard_mutation_checkpoint(std::move(checkpoint));
  ASSERT_FALSE(wrong_owner.ok());
  EXPECT_EQ(wrong_owner.error().code, ErrorCode::StaleId);
  EXPECT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());

  const auto discarded =
      session_.discard_mutation_checkpoint(std::move(checkpoint));
  ASSERT_TRUE(discarded.ok()) << discarded.error_message();
  EXPECT_TRUE(discarded.value().changed);
  EXPECT_FALSE(checkpoint.valid());
  EXPECT_FALSE(session_.mutation_checkpoint_active());
  EXPECT_FALSE(session_.can_redo());
  EXPECT_EQ(session_.content_revision(), branch_content_revision);
  EXPECT_EQ(session_.selection_revision(), branch_selection_revision);
  EXPECT_EQ(session_.undo_depth(), branch_undo_depth);
  EXPECT_TRUE(current_curve().has_same_geometry(current_branch));
  EXPECT_EQ(session_.discard_mutation_checkpoint(std::move(checkpoint))
                .error()
                .code,
            ErrorCode::InvalidArgument);

  ASSERT_TRUE(session_.undo().ok());
  EXPECT_FALSE(current_curve().has_same_geometry(current_branch));
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(current_branch));
  ASSERT_TRUE(session_.rename_object(curve_, "Usable After Discard").ok());
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointRejectsInvalidConflictingAndStructuralScopesAtomically) {
  const ModelingSession baseline = session_;
  const SessionMutationCheckpoint empty_checkpoint;
  EXPECT_EQ(session_.preflight_mutation_checkpoint(empty_checkpoint)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  const SessionPreviewCheckpoint empty_preview_checkpoint;
  EXPECT_EQ(session_.preflight_preview_checkpoint(empty_preview_checkpoint)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  const std::array duplicate_participants = {curve_, curve_};
  EXPECT_EQ(session_.capture_mutation_checkpoint(duplicate_participants)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  const std::array invalid_participants = {
      ObjectId{.index = curve_.index + 100U, .generation = 1U}};
  EXPECT_EQ(session_.capture_mutation_checkpoint(invalid_participants)
                .error()
                .code,
            ErrorCode::InvalidId);
  expect_same_session_observables(session_, baseline);

  const CurveDocument preview_candidate =
      translated_curve({0.0F, 1.0F, 0.0F});
  const PreviewId preview =
      session_
          .begin_curve_document_preview(curve_, "modeling.checkpoint.conflict",
                                        preview_candidate,
                                        changed_receipt())
          .or_throw();
  const PreviewTransactionSummary preview_summary = session_.preview_summary();
  const std::array curve_participant = {curve_};
  EXPECT_EQ(session_.capture_mutation_checkpoint(curve_participant)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  expect_same_preview_summary(session_.preview_summary(), preview_summary);
  ASSERT_TRUE(session_.cancel_preview(preview).ok());

  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(curve_participant).or_throw();
  EXPECT_EQ(session_.capture_mutation_checkpoint(curve_participant)
                .error()
                .code,
            ErrorCode::InvalidArgument);
  EXPECT_FALSE(session_
                   .add_document(PolygonDocument::make_box({}).or_throw(),
                                 "Blocked")
                   .ok());
  EXPECT_FALSE(session_.rename_object(mesh_, "Unenrolled").ok());
  EXPECT_FALSE(session_.select_object(curve_).ok());
  EXPECT_TRUE(checkpoint.valid());
  EXPECT_TRUE(session_.mutation_checkpoint_active());
  const auto restored =
      session_.restore_mutation_checkpoint(std::move(checkpoint));
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_FALSE(restored.value().changed);
  expect_same_session_observables(session_, baseline);
}

TEST_F(ModelingCurveSessionTests,
       DroppedMutationCheckpointBlocksMutationsUntilCheckedAbortRestoresRedo) {
  const CurveDocument redo_candidate =
      translated_curve({0.0F, 0.0F, 5.0F});
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, redo_candidate, changed_receipt())
          .ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.can_redo());
  const ModelingSession expected = session_;
  const SessionChangeSnapshot expected_changes = session_.changes_since(0U);

  {
    const std::array participants = {curve_};
    SessionMutationCheckpoint checkpoint =
        session_.capture_mutation_checkpoint(participants).or_throw();
    const CurveDocument scoped_edit =
        translated_curve({0.0F, 3.0F, 0.0F});
    ASSERT_TRUE(session_
                    .commit_curve_document(curve_, scoped_edit,
                                           changed_receipt())
                    .ok());
  }
  ASSERT_TRUE(session_.mutation_checkpoint_active());
  EXPECT_FALSE(session_.can_redo());
  const std::uint64_t orphaned_content_revision = session_.content_revision();
  const std::uint64_t orphaned_selection_revision =
      session_.selection_revision();
  const std::size_t orphaned_undo_depth = session_.undo_depth();

  const auto blocked = session_.rename_object(curve_, "Blocked");
  ASSERT_FALSE(blocked.ok());
  EXPECT_EQ(blocked.error().code, ErrorCode::InvalidArgument);
  EXPECT_NE(blocked.error_message().find("abort_active_mutation_checkpoint"),
            std::string::npos);
  EXPECT_EQ(session_.content_revision(), orphaned_content_revision);
  EXPECT_EQ(session_.selection_revision(), orphaned_selection_revision);
  EXPECT_EQ(session_.undo_depth(), orphaned_undo_depth);
  EXPECT_TRUE(session_.mutation_checkpoint_active());

  ModelingSession replacement;
  session_ = std::move(replacement);
  EXPECT_TRUE(session_.mutation_checkpoint_active());
  EXPECT_EQ(session_.content_revision(), orphaned_content_revision);

  const auto aborted = session_.abort_active_mutation_checkpoint();
  ASSERT_TRUE(aborted.ok()) << aborted.error_message();
  EXPECT_TRUE(aborted.value().changed);
  EXPECT_FALSE(session_.mutation_checkpoint_active());
  expect_same_session_observables(session_, expected);
  expect_same_change_snapshot(session_.changes_since(0U), expected_changes);

  ModelingSession expected_after_redo = expected;
  ASSERT_TRUE(session_.redo().ok());
  ASSERT_TRUE(expected_after_redo.redo().ok());
  expect_same_session_observables(session_, expected_after_redo);
  ASSERT_TRUE(session_.rename_object(curve_, "Recovered Path").ok());
  EXPECT_EQ(session_.objects()[1].name, "Recovered Path");
  EXPECT_FALSE(session_.abort_active_mutation_checkpoint().ok());
}

TEST_F(ModelingCurveSessionTests,
       MutationCheckpointRollbackNeverRecyclesCurveComponentIdentities) {
  const CurveDocument original = current_curve();
  const std::uint64_t original_content_revision = session_.content_revision();
  const std::uint64_t original_selection_revision =
      session_.selection_revision();
  const std::array participants = {curve_};
  SessionMutationCheckpoint checkpoint =
      session_.capture_mutation_checkpoint(participants).or_throw();

  CurveDocument discarded = current_curve();
  const OperationResult discarded_receipt =
      discarded.append_point({.position = {5.0F, 1.0F, 0.0F}}).or_throw();
  const CurvePointId discarded_point = discarded.points().back().id;
  const CurveSegmentId discarded_segment = discarded.segments().back();
  ASSERT_TRUE(session_
                  .commit_curve_document(curve_, discarded,
                                         discarded_receipt)
                  .ok());
  ASSERT_TRUE(
      session_.restore_mutation_checkpoint(std::move(checkpoint)).ok());

  const CurveDocument restored = current_curve();
  EXPECT_TRUE(restored.has_same_geometry(original));
  EXPECT_EQ(restored.content_identity(), original.content_identity());
  EXPECT_EQ(restored.content_revision(), original.content_revision());
  EXPECT_EQ(session_.content_revision(), original_content_revision);
  EXPECT_EQ(session_.selection_revision(), original_selection_revision);
  EXPECT_EQ(session_.undo_depth(), 0U);

  CurveDocument next = restored;
  const OperationResult next_receipt =
      next.append_point({.position = {6.0F, 2.0F, 0.0F}}).or_throw();
  EXPECT_GT(next.points().back().id.index, discarded_point.index);
  EXPECT_GT(next.segments().back().index, discarded_segment.index);
  ASSERT_TRUE(
      session_.commit_curve_document(curve_, next, next_receipt).ok());

  CurveDocument allocate_then_remove = current_curve();
  ASSERT_TRUE(allocate_then_remove
                  .append_point({.position = {8.0F, 3.0F, 0.0F}})
                  .ok());
  const CurvePointId no_op_discarded_point =
      allocate_then_remove.points().back().id;
  const CurveSegmentId no_op_discarded_segment =
      allocate_then_remove.segments().back();
  ASSERT_TRUE(
      allocate_then_remove.remove_point(no_op_discarded_point).ok());
  const auto unchanged = session_.commit_curve_document(
      curve_, allocate_then_remove, changed_receipt());
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);

  CurveDocument after_no_op = current_curve();
  ASSERT_TRUE(after_no_op
                  .append_point({.position = {9.0F, 4.0F, 0.0F}})
                  .ok());
  EXPECT_GT(after_no_op.points().back().id.index,
            no_op_discarded_point.index);
  EXPECT_GT(after_no_op.segments().back().index,
            no_op_discarded_segment.index);
}

TEST(ModelingSessionMutationCheckpointScalingTests,
     OneParticipantCaptureAndRestoreStayBoundedWithSixThousandUnrelatedObjects) {
  ModelingSession session;
  const PolygonDocument prototype = PolygonDocument::make_box({}).or_throw();
  constexpr std::size_t kObjectCount = 6'001U;
  std::vector<SessionWorkspaceDocument> workspace;
  workspace.reserve(kObjectCount);
  for (std::size_t index = 0; index < kObjectCount; ++index) {
    workspace.push_back({
        .document = prototype,
        .name = "Indexed Object",
        .selected = index + 1U == kObjectCount,
    });
  }
  const std::vector<ObjectId> objects =
      session.replace_workspace_preserving_clipboard(std::move(workspace))
          .or_throw();
  const ObjectId unrelated = objects.front();
  const ObjectId target = objects.back();
  session.clear_history();
  const SessionObjectSummary unrelated_before = session.objects().front();
  const PolygonContentIdentity unrelated_identity =
      session.document_copy(unrelated).or_throw().content_identity();
  const SessionObjectSummary target_before = session.objects().back();
  const PolygonDocument target_document =
      session.document_copy(target).or_throw();
  const std::uint64_t content_revision = session.content_revision();
  const std::uint64_t selection_revision = session.selection_revision();

  PolygonDocument changed = target_document;
  ASSERT_TRUE(changed.select_all_faces().ok());
  OperationResult receipt =
      changed.translate_selection({2.0F, 0.0F, 0.0F}).or_throw();
  ASSERT_TRUE(changed.clear_selection().ok());
  receipt.dirty.selection = false;
  receipt.dirty.overlays = true;

  const SessionMutationCheckpointWorkStats before =
      session.mutation_checkpoint_work_stats();
  const std::array participants = {target};
  SessionMutationCheckpoint checkpoint =
      session.capture_mutation_checkpoint(participants).or_throw();
  const SessionMutationCheckpointWorkStats after_capture =
      session.mutation_checkpoint_work_stats();
  EXPECT_EQ(after_capture.capture_count, before.capture_count + 1U);
  EXPECT_EQ(after_capture.participant_lookup_count,
            before.participant_lookup_count + 1U);
  EXPECT_EQ(after_capture.participant_snapshot_count,
            before.participant_snapshot_count + 1U);
  EXPECT_EQ(after_capture.transition_record_snapshot_count,
            before.transition_record_snapshot_count + 1U);
  EXPECT_EQ(after_capture.workspace_copy_count, before.workspace_copy_count);

  ASSERT_TRUE(session
                  .commit_document(target, std::move(changed),
                                   std::move(receipt))
                  .ok());
  ASSERT_TRUE(
      session.restore_mutation_checkpoint(std::move(checkpoint)).ok());
  const SessionMutationCheckpointWorkStats after_restore =
      session.mutation_checkpoint_work_stats();
  EXPECT_EQ(after_restore.restore_count, before.restore_count + 1U);
  EXPECT_EQ(after_restore.participant_restore_count,
            before.participant_restore_count + 1U);
  EXPECT_EQ(after_restore.history_suffix_record_count,
            before.history_suffix_record_count + 1U);
  EXPECT_EQ(after_restore.workspace_copy_count, before.workspace_copy_count);
  EXPECT_EQ(session.content_revision(), content_revision);
  EXPECT_EQ(session.selection_revision(), selection_revision);
  EXPECT_EQ(session.undo_depth(), 0U);

  const auto summaries = session.objects();
  ASSERT_EQ(summaries.size(), kObjectCount);
  EXPECT_EQ(summaries.front().id, unrelated_before.id);
  EXPECT_EQ(summaries.front().name, unrelated_before.name);
  EXPECT_EQ(summaries.front().selected, unrelated_before.selected);
  EXPECT_EQ(summaries.front().content_revision,
            unrelated_before.content_revision);
  EXPECT_EQ(summaries.front().selection_revision,
            unrelated_before.selection_revision);
  EXPECT_EQ(session.document_copy(unrelated).or_throw().content_identity(),
            unrelated_identity);
  EXPECT_EQ(summaries.back().id, target_before.id);
  EXPECT_EQ(summaries.back().selected, target_before.selected);
  EXPECT_EQ(summaries.back().content_revision,
            target_before.content_revision);
  EXPECT_EQ(session.document_copy(target).or_throw().content_identity(),
            target_document.content_identity());
}

TEST(ModelingSessionPreviewScalingTests,
     OneParticipantWorkStaysBoundedWithSixThousandUnrelatedObjects) {
  ModelingSession session;
  const PolygonDocument prototype = PolygonDocument::make_box({}).or_throw();
  constexpr std::size_t kObjectCount = 6'001U;
  std::vector<SessionWorkspaceDocument> workspace;
  workspace.reserve(kObjectCount);
  for (std::size_t index = 0; index < kObjectCount; ++index) {
    workspace.push_back({
        .document = prototype,
        .name = "Indexed Object",
        .selected = index + 1U == kObjectCount,
    });
  }
  const std::vector<ObjectId> objects =
      session.replace_workspace_preserving_clipboard(std::move(workspace))
          .or_throw();
  const ObjectId target = objects.back();
  session.clear_history();

  PolygonDocument first = session.document_copy(target).or_throw();
  ASSERT_TRUE(first.select_all_faces().ok());
  OperationResult first_receipt =
      first.translate_selection({1.0F, 0.0F, 0.0F}).or_throw();
  ASSERT_TRUE(first.clear_selection().ok());

  const SessionPreviewWorkStats before_begin = session.preview_work_stats();
  SessionPreviewTransactionRequest request;
  request.participants.push_back({
      .action = SessionPreviewAction::ReplaceExisting,
      .object = target,
      .document = first,
      .receipt = first_receipt,
      .selected = true,
  });
  const SessionPreviewBeginResult begun =
      session
          .begin_preview_transaction("modeling.scaling.lookup",
                                     std::move(request))
          .or_throw();
  const SessionPreviewWorkStats after_begin = session.preview_work_stats();
  EXPECT_LE(after_begin.live_object_lookup_count -
                before_begin.live_object_lookup_count,
            2U);
  EXPECT_EQ(after_begin.mesh_payload_build_count -
                before_begin.mesh_payload_build_count,
            1U);

  const SessionPreviewCheckpoint checkpoint =
      session.capture_preview_checkpoint(begun.preview_id).or_throw();
  ASSERT_TRUE(checkpoint.valid());
  const SessionPreviewWorkStats after_checkpoint =
      session.preview_work_stats();
  EXPECT_LE(after_checkpoint.live_object_lookup_count -
                after_begin.live_object_lookup_count,
            2U);
  EXPECT_EQ(after_checkpoint.checkpoint_participant_reference_count -
                after_begin.checkpoint_participant_reference_count,
            1U);
  EXPECT_EQ(after_checkpoint.checkpoint_authored_byte_copy_count,
            before_begin.checkpoint_authored_byte_copy_count);

  PolygonDocument second =
      session.preview_base_document_copy(begun.preview_id, target).or_throw();
  const SessionPreviewWorkStats after_base_read =
      session.preview_work_stats();
  EXPECT_EQ(after_base_read.preview_object_lookup_count -
                after_checkpoint.preview_object_lookup_count,
            1U);
  ASSERT_TRUE(second.select_all_faces().ok());
  OperationResult second_receipt =
      second.translate_selection({2.0F, 0.0F, 0.0F}).or_throw();
  ASSERT_TRUE(second.clear_selection().ok());
  std::vector<SessionPreviewDocumentUpdate> updates;
  updates.push_back({.object = target,
                     .document = second,
                     .receipt = second_receipt});
  ASSERT_TRUE(session
                  .update_preview_transaction(begun.preview_id,
                                              std::move(updates))
                  .ok());
  const SessionPreviewWorkStats after_update = session.preview_work_stats();
  EXPECT_LE(after_update.live_object_lookup_count -
                after_base_read.live_object_lookup_count,
            2U);
  EXPECT_LE(after_update.preview_object_lookup_count -
                after_base_read.preview_object_lookup_count,
            2U);
  EXPECT_EQ(after_update.mesh_payload_build_count -
                after_base_read.mesh_payload_build_count,
            1U);

  ASSERT_TRUE(session.commit_preview_transaction(begun.preview_id).ok());
  ASSERT_TRUE(session.undo().ok());
  ASSERT_TRUE(session.redo().ok());

  const auto exercise_index = [&](ModelingSession &candidate) {
    PolygonDocument current = candidate.document_copy(target).or_throw();
    const PreviewId preview =
        candidate
            .begin_document_preview(target, "modeling.scaling.index_integrity",
                                    current, OperationResult{})
            .or_throw();
    EXPECT_TRUE(candidate.preview_document_copy(preview, target).ok());
    EXPECT_TRUE(candidate.cancel_preview(preview).ok());
  };
  exercise_index(session);
  ModelingSession copied = session;
  exercise_index(copied);
  ModelingSession moved = std::move(copied);
  exercise_index(moved);
  ModelingSession copy_assigned;
  copy_assigned = session;
  exercise_index(copy_assigned);
  ModelingSession move_assigned;
  move_assigned = std::move(copy_assigned);
  exercise_index(move_assigned);
  const ModelingSession snapshot = session.current_workspace_checkpoint();
  session.restore_snapshot_preserving_identity_high_water(snapshot);
  exercise_index(session);
}

TEST(ModelingSessionPreviewScalingTests,
     LargeCurveCheckpointSharesAuthoredStorageAndOmitsCompiledPayloadCopies) {
  constexpr std::size_t kPointCount = 50'000U;
  std::vector<CurvePointSpec> points;
  points.reserve(kPointCount);
  for (std::size_t index = 0; index < kPointCount; ++index) {
    points.push_back({
        .position = {static_cast<float>(index),
                     static_cast<float>(index % 17U), 0.0F},
    });
  }
  CurveDocument curve = CurveDocument::make_bezier(points).or_throw();
  ModelingSession session;
  const ObjectId object =
      session.add_curve_document(curve, "Large Curve").or_throw();
  session.clear_history();

  CurveDocument edited = session.curve_document_copy(object).or_throw();
  const CurveControlPoint first = edited.points().front();
  ASSERT_TRUE(edited
                  .set_point(first.id,
                             {.position = {first.position.x,
                                           first.position.y + 1.0F,
                                           first.position.z},
                              .incoming_handle = first.incoming_handle,
                              .outgoing_handle = first.outgoing_handle})
                  .ok());
  OperationResult receipt;
  receipt.changed = true;
  receipt.dirty.geometry = true;
  receipt.dirty.overlays = true;
  const PreviewId preview =
      session
          .begin_curve_document_preview(object, "modeling.curve.large",
                                        edited, receipt)
          .or_throw();
  const CurveContentIdentity edited_identity =
      session.preview_curve_document_copy(preview, object)
          .or_throw()
          .content_identity();
  const SessionPreviewWorkStats before = session.preview_work_stats();
  const SessionPreviewCheckpoint checkpoint =
      session.capture_preview_checkpoint(preview).or_throw();
  const SessionPreviewWorkStats after = session.preview_work_stats();
  EXPECT_EQ(after.checkpoint_count, before.checkpoint_count + 1U);
  EXPECT_EQ(after.checkpoint_participant_reference_count,
            before.checkpoint_participant_reference_count + 1U);
  EXPECT_EQ(after.checkpoint_authored_byte_copy_count,
            before.checkpoint_authored_byte_copy_count);
  const OperationResult unchanged =
      session.restore_preview_checkpoint(checkpoint).or_throw();
  EXPECT_FALSE(unchanged.changed);
  EXPECT_EQ(session.preview_curve_document_copy(preview, object)
                .or_throw()
                .content_identity(),
            edited_identity);
  ASSERT_TRUE(session.cancel_preview(preview).ok());
}

TEST_F(ModelingCurveSessionTests, CancelAndReturnToBasePreserveDurableStateAndRedo) {
  const CurveDocument original = current_curve();
  const CurveDocument changed = translated_curve({0.0F, 0.0F, 3.0F});
  ASSERT_TRUE(session_.commit_curve_document(curve_, changed, changed_receipt()).ok());
  ASSERT_TRUE(session_.undo().ok());
  const auto revision = session_.content_revision();
  auto first = session_.begin_curve_document_preview(curve_, "modeling.curve.edit",
                                                     changed, changed_receipt());
  ASSERT_TRUE(first.ok()) << first.error_message();
  ASSERT_TRUE(session_.cancel_preview(first.value()).ok());
  EXPECT_FALSE(session_.preview_curve_document_copy(first.value()).ok());
  EXPECT_EQ(current_curve().content_identity(), original.content_identity());

  auto second = session_.begin_curve_document_preview(curve_, "modeling.curve.edit",
                                                      changed, changed_receipt());
  ASSERT_TRUE(second.ok()) << second.error_message();
  EXPECT_GT(second.value().index, first.value().index);
  auto restored = session_.update_curve_document_preview(second.value(), current_curve(), {});
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_FALSE(restored.value().changed);
  auto committed = session_.commit_preview(second.value());
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_FALSE(committed.value().changed);
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.undo_depth(), 0U);
  EXPECT_TRUE(session_.can_redo());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRestoresCandidateWithoutChangingDurableStateOrRedo) {
  const CurveDocument original = current_curve();
  const std::array selected = {mesh_, curve_};
  ASSERT_TRUE(session_.apply_object_selection(selected).ok());
  ASSERT_TRUE(session_.copy_selection_to_clipboard().ok());
  ASSERT_TRUE(session_.select_object(mesh_).ok());
  session_.clear_history();
  ASSERT_TRUE(session_.rename_object(mesh_, "Renamed Box").ok());
  const CurveDocument redo_candidate = translated_curve({0.0F, 0.0F, 4.0F});
  ASSERT_TRUE(session_.commit_curve_document(curve_, redo_candidate, changed_receipt()).ok());
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_EQ(session_.undo_depth(), 1U);
  ASSERT_TRUE(session_.can_redo());

  const CurveDocument first = translated_curve({0.0F, 2.0F, 0.0F});
  const auto preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session_.capture_preview_checkpoint(preview).or_throw();
  ASSERT_TRUE(checkpoint.valid());
  const ModelingSession captured = session_;
  ASSERT_TRUE(session_.preflight_preview_checkpoint(checkpoint).ok());
  expect_same_session_observables(session_, captured);
  const auto unchanged = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.value().changed);
  expect_same_session_observables(session_, captured);

  const CurveDocument second = translated_curve({0.0F, 3.0F, 0.0F});
  auto second_receipt = changed_receipt();
  second_receipt.dirty.topology = true;
  ASSERT_TRUE(session_.update_curve_document_preview(preview, second, second_receipt).ok());
  const auto discarded_revision = session_.preview_summary().content_revision;
  const auto restored = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_dirty(restored.value().dirty, captured.preview_summary().dirty);
  expect_same_session_observables(session_, captured);

  ASSERT_TRUE(session_.update_curve_document_preview(preview, second, second_receipt).ok());
  EXPECT_GT(session_.preview_summary().content_revision, discarded_revision);
  ASSERT_TRUE(session_.restore_preview_checkpoint(checkpoint).ok());
  expect_same_session_observables(session_, captured);

  ASSERT_TRUE(session_.cancel_preview(preview).ok());
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(redo_candidate));
  EXPECT_EQ(session_.objects()[0].name, "Renamed Box");
  ASSERT_TRUE(session_.undo().ok());
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_EQ(session_.objects()[0].name, "Box");
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  expect_mesh_only_selected();
  const auto pasted = session_.paste_clipboard();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session_.document_copy(pasted.value().objects[0]).ok());
  EXPECT_TRUE(session_.curve_document_copy(pasted.value().objects[1]).or_throw()
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
  const auto preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.append", first, first_receipt).or_throw();
  const auto checkpoint = session_.capture_preview_checkpoint(preview).or_throw();
  const auto summary = session_.preview_summary();

  CurveDocument discarded = session_.preview_curve_document_copy(preview).or_throw();
  const auto discarded_receipt =
      discarded.append_point({.position = {6.0F, 2.0F, 0.0F}}).or_throw();
  const CurvePointId discarded_point = discarded.points().back().id;
  const CurveSegmentId discarded_segment = discarded.segments().back();
  ASSERT_TRUE(session_.update_curve_document_preview(preview, discarded, discarded_receipt).ok());
  const auto discarded_revision = session_.preview_summary().content_revision;
  ASSERT_GT(discarded_revision, summary.content_revision);

  const auto restored = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_preview_summary(session_.preview_summary(), summary);
  const CurveDocument restored_candidate = session_.preview_curve_document_copy(preview).or_throw();
  EXPECT_TRUE(restored_candidate.has_same_geometry(first));
  EXPECT_EQ(restored_candidate.content_identity(), first.content_identity());
  EXPECT_EQ(restored_candidate.content_revision(), first.content_revision());
  EXPECT_EQ(restored_candidate.identity_high_water(), discarded.identity_high_water());
  EXPECT_EQ(current_curve().identity_high_water(), discarded.identity_high_water());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  const auto repeated = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  expect_same_curve_snapshot(session_.preview_curve_document_copy(preview).or_throw(),
                             restored_candidate);

  const auto committed = session_.commit_preview(preview);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  expect_same_dirty(committed.value().dirty, summary.dirty);
  ASSERT_EQ(committed.value().diagnostics.size(), 1U);
  EXPECT_EQ(committed.value().diagnostics.front().code, "checkpoint.saved");
  EXPECT_TRUE(current_curve().has_same_geometry(first));
  EXPECT_EQ(current_curve().points().back().id, retained_point);
  EXPECT_EQ(session_.undo_depth(), 1U);
  expect_mesh_only_selected();

  CurveDocument next = current_curve();
  const auto next_receipt = next.append_point({.position = {8.0F, 3.0F, 0.0F}}).or_throw();
  EXPECT_GT(next.points().back().id.index, discarded_point.index);
  EXPECT_GT(next.segments().back().index, discarded_segment.index);
  const auto next_preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.append", next, next_receipt).or_throw();
  EXPECT_GT(next_preview.index, preview.index);
  const auto next_revision = session_.preview_summary().content_revision;
  EXPECT_GT(next_revision, discarded_revision);
  const auto translated = next.transform(Transform3{.origin = {0.0F, 1.0F, 0.0F}}).or_throw();
  ASSERT_TRUE(session_.update_curve_document_preview(next_preview, next, translated).ok());
  EXPECT_GT(session_.preview_summary().content_revision, next_revision);
  ASSERT_TRUE(session_.cancel_preview(next_preview).ok());
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRejectsEmptyWrongAndRetiredTokensWithoutMutation) {
  const SessionPreviewCheckpoint empty;
  EXPECT_FALSE(empty.valid());
  const ModelingSession inactive = session_;
  EXPECT_EQ(session_.restore_preview_checkpoint(empty).error().code, ErrorCode::InvalidArgument);
  EXPECT_EQ(session_.capture_preview_checkpoint({}).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session_, inactive);

  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const auto preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session_.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession active = session_;
  const PreviewId wrong{preview.index + 1U, preview.generation};
  EXPECT_EQ(session_.capture_preview_checkpoint(wrong).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session_.restore_preview_checkpoint(empty).error().code, ErrorCode::InvalidArgument);
  expect_same_session_observables(session_, active);

  ASSERT_TRUE(session_.cancel_preview(preview).ok());
  const ModelingSession canceled = session_;
  EXPECT_EQ(session_.capture_preview_checkpoint(preview).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session_.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session_, canceled);

  const auto replacement = session_.begin_curve_document_preview(
      curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
  ASSERT_NE(replacement, preview);
  const ModelingSession replaced = session_;
  EXPECT_EQ(session_.capture_preview_checkpoint(preview).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session_.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session_, replaced);
  ASSERT_TRUE(session_.commit_preview(replacement).ok());
  const ModelingSession committed = session_;
  EXPECT_EQ(session_.capture_preview_checkpoint(replacement).error().code, ErrorCode::StaleId);
  EXPECT_EQ(session_.restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
  expect_same_session_observables(session_, committed);
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRejectsCopiedAndForeignSessionsWithoutMutation) {
  const CurveDocument first = translated_curve({0.0F, 1.0F, 0.0F});
  const CurveDocument second = translated_curve({0.0F, 2.0F, 0.0F});
  const auto preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session_.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession source = session_;
  ModelingSession copied(session_);
  ModelingSession assigned;
  assigned = session_;
  const std::array destinations = {&copied, &assigned};
  for (ModelingSession *destination : destinations) {
    EXPECT_EQ(destination->preflight_preview_checkpoint(checkpoint)
                  .error()
                  .code,
              ErrorCode::StaleId);
    const auto foreign_checkpoint = destination->capture_preview_checkpoint(preview).or_throw();
    ASSERT_TRUE(destination->update_curve_document_preview(preview, second, changed_receipt()).ok());
    const ModelingSession before = *destination;
    EXPECT_EQ(destination->restore_preview_checkpoint(checkpoint).error().code, ErrorCode::StaleId);
    expect_same_session_observables(*destination, before);
    EXPECT_EQ(session_.restore_preview_checkpoint(foreign_checkpoint).error().code, ErrorCode::StaleId);
    expect_same_session_observables(session_, source);
  }

  const auto displaced = assigned.capture_preview_checkpoint(preview).or_throw();
  assigned = session_;
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
    ModelingSession target = session_;
    const auto preview = target.begin_curve_document_preview(
        curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
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
  const auto preview = session_.begin_curve_document_preview(
      curve_, "modeling.curve.edit", first, changed_receipt()).or_throw();
  const auto checkpoint = session_.capture_preview_checkpoint(preview).or_throw();
  const ModelingSession captured = session_;
  ASSERT_TRUE(session_.update_curve_document_preview(preview, second, changed_receipt()).ok());
  ModelingSession moved(std::move(session_));
  const auto restored = moved.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_session_observables(moved, captured);

  ASSERT_TRUE(moved.update_curve_document_preview(preview, second, changed_receipt()).ok());
  session_ = std::move(moved);
  const auto restored_again = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored_again.ok()) << restored_again.error_message();
  EXPECT_TRUE(restored_again.value().changed);
  expect_same_session_observables(session_, captured);
  const auto repeated = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.value().changed);
  expect_same_session_observables(session_, captured);
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRestoresMaterializedMeshTranslatePayloadAndHistory) {
  const auto faces = session_.document_copy(mesh_).or_throw().face_ids();
  ASSERT_FALSE(faces.empty());
  ASSERT_TRUE(session_.select_face(mesh_, faces.front()).ok());
  session_.clear_history();
  const auto original = session_.document_copy(mesh_).or_throw();
  const auto original_curve = current_curve();
  ASSERT_TRUE(session_.begin_translate_preview(mesh_, {0.0F, 0.25F, 0.0F}).ok());
  const auto summary = session_.preview_summary();
  const auto candidate = session_.preview_document_copy(summary.id).or_throw();
  const auto payload = session_.preview_payload(summary.id).or_throw();
  ASSERT_TRUE(payload.valid);
  ASSERT_FALSE(payload.mesh.vertices.empty());
  const auto checkpoint = session_.capture_preview_checkpoint(summary.id).or_throw();
  const ModelingSession captured = session_;

  ASSERT_TRUE(session_.update_translate_preview({0.0F, 0.5F, 0.0F}).ok());
  const auto updated_payload = session_.preview_payload(summary.id).or_throw();
  EXPECT_GT(updated_payload.mesh.content_revision, payload.mesh.content_revision);
  const auto restored = session_.restore_preview_checkpoint(checkpoint);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(restored.value().changed);
  expect_same_session_observables(session_, captured);
  const auto restored_payload = session_.preview_payload(summary.id).or_throw();
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

  const auto committed = session_.commit_preview(summary.id);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  EXPECT_TRUE(committed.value().changed);
  expect_same_dirty(committed.value().dirty, summary.dirty);
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_EQ(session_.document_copy(mesh_).or_throw().content_identity(), candidate.content_identity());
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_EQ(session_.document_copy(mesh_).or_throw().content_identity(), original.content_identity());
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_EQ(session_.document_copy(mesh_).or_throw().content_identity(), candidate.content_identity());
  expect_same_curve_snapshot(current_curve(), original_curve);
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests,
       PreviewCheckpointRepeatedMeshRestoreAfterHistoryRewindIsNoOp) {
  for (const bool commit_restored_preview : {false, true}) {
    SCOPED_TRACE(commit_restored_preview);
    ModelingSession target = session_;
    const auto faces = target.document_copy(mesh_).or_throw().face_ids();
    ASSERT_FALSE(faces.empty());
    ASSERT_TRUE(target.select_face(mesh_, faces.front()).ok());
    target.clear_history();
    const ModelingSession snapshot = target;
    const auto original = target.document_copy(mesh_).or_throw();
    const auto original_curve = target.curve_document_copy(curve_).or_throw();

    auto expanded = target.document_copy(mesh_).or_throw();
    const auto extrusion = expanded.extrude_selected_faces(0.25F).or_throw();
    ASSERT_TRUE(extrusion.changed);
    ASSERT_FALSE(extrusion.created.vertices.empty());
    ASSERT_FALSE(extrusion.created.faces.empty());
    const auto retired_topology = expanded.authored_topology().or_throw();
    ASSERT_TRUE(target.commit_document(mesh_, expanded, extrusion).ok());
    target.restore_snapshot_preserving_identity_high_water(snapshot);
    const auto content_revision = target.content_revision();
    const auto selection_revision = target.selection_revision();

    ASSERT_TRUE(target.begin_translate_preview(mesh_, {0.0F, 0.25F, 0.0F}).ok());
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
    EXPECT_TRUE(target.document_copy(mesh_).or_throw().has_same_geometry(original));
    EXPECT_EQ(target.document_copy(mesh_).or_throw().selection().faces,
              original.selection().faces);
    expect_same_curve_snapshot(target.curve_document_copy(curve_).or_throw(), original_curve);

    if (commit_restored_preview) {
      const auto committed = target.commit_preview(summary.id);
      ASSERT_TRUE(committed.ok()) << committed.error_message();
      EXPECT_TRUE(committed.value().changed);
      EXPECT_EQ(target.undo_depth(), 1U);
      EXPECT_TRUE(target.document_copy(mesh_).or_throw().has_same_geometry(first_restore));
      ASSERT_TRUE(target.undo().ok());
    } else {
      ASSERT_TRUE(target.cancel_preview(summary.id).ok());
      EXPECT_EQ(target.content_revision(), content_revision);
      EXPECT_EQ(target.selection_revision(), selection_revision);
    }
    EXPECT_FALSE(target.preview_active());
    EXPECT_TRUE(target.document_copy(mesh_).or_throw().has_same_geometry(original));

    auto next = target.document_copy(mesh_).or_throw();
    const auto next_extrusion = next.extrude_selected_faces(0.5F).or_throw();
    ASSERT_FALSE(next_extrusion.created.vertices.empty());
    ASSERT_FALSE(next_extrusion.created.faces.empty());
    for (const VertexId vertex : next_extrusion.created.vertices) {
      EXPECT_GE(vertex.index, retired_topology.next_vertex_id);
    }
    for (const FaceId face : next_extrusion.created.faces) {
      EXPECT_GE(face.index, retired_topology.next_face_id);
    }
    ASSERT_TRUE(target.commit_document(mesh_, next, next_extrusion).ok());
    EXPECT_EQ(target.undo_depth(), 1U);
    ASSERT_TRUE(target.undo().ok());
    EXPECT_TRUE(target.document_copy(mesh_).or_throw().has_same_geometry(original));
    ASSERT_TRUE(target.redo().ok());
    EXPECT_TRUE(target.document_copy(mesh_).or_throw().has_same_geometry(next));
  }
}

TEST_F(ModelingCurveSessionTests, MixedDuplicatesAndClipboardRetainAuthoredCurveData) {
  const CurveDocument original = current_curve();
  const std::array targets = {mesh_, curve_};
  auto duplicates = session_.duplicate_objects(targets, {4.0F, 0.0F, 0.0F});
  ASSERT_TRUE(duplicates.ok()) << duplicates.error_message();
  ASSERT_EQ(duplicates.value().size(), 2U);
  EXPECT_EQ(session_.undo_depth(), 1U);
  EXPECT_TRUE(session_.document_copy(duplicates.value()[0]).ok());
  CurveDocument duplicated = session_.curve_document_copy(duplicates.value()[1]).or_throw();
  CurveDocument expected = original;
  ASSERT_TRUE(expected.transform(Transform3{.origin = {4.0F, 0.0F, 0.0F}}).ok());
  EXPECT_TRUE(duplicated.has_same_geometry(expected));
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_FALSE(session_.curve_document_copy(duplicates.value()[1]).ok());
  ASSERT_TRUE(session_.redo().ok());
  EXPECT_TRUE(session_.curve_document_copy(duplicates.value()[1]).or_throw().has_same_geometry(expected));
  ASSERT_TRUE(session_.undo().ok());

  ASSERT_TRUE(session_.apply_object_selection(targets).ok());
  ASSERT_TRUE(session_.copy_selection_to_clipboard().ok());
  EXPECT_EQ(session_.clipboard_object_count(), 2U);
  const auto depth = session_.undo_depth();
  auto pasted = session_.paste_clipboard({4.0F, 0.0F, 0.0F});
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  EXPECT_EQ(session_.undo_depth(), depth + 1U);
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session_.curve_document_copy(pasted.value().objects[1]).or_throw().has_same_geometry(expected));
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_FALSE(session_.curve_document_copy(pasted.value().objects[1]).ok());
  EXPECT_TRUE(session_.clipboard_available());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests, RenameDeleteAndDuplicateUseOneObjectAllocator) {
  const CurveDocument original = current_curve();
  ASSERT_TRUE(session_.rename_object(curve_, "Rail").ok());
  EXPECT_EQ(session_.objects()[1].name, "Rail");
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_EQ(session_.objects()[1].name, "Path");
  ASSERT_TRUE(session_.remove_object(curve_).ok());
  EXPECT_FALSE(session_.curve_document_copy(curve_).ok());
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  auto duplicate = session_.duplicate_object(curve_);
  ASSERT_TRUE(duplicate.ok()) << duplicate.error_message();
  EXPECT_TRUE(session_.curve_document_copy(duplicate.value()).or_throw().has_same_geometry(original));
  ASSERT_TRUE(session_.undo().ok());
  auto new_mesh = session_.add_document(PolygonDocument::make_box({}).or_throw());
  ASSERT_TRUE(new_mesh.ok()) << new_mesh.error_message();
  EXPECT_GT(new_mesh.value().index, duplicate.value().index);
  EXPECT_FALSE(session_.can_redo());
}

TEST_F(ModelingCurveSessionTests, BranchAndCanceledPreviewNeverRecycleComponentIdentities) {
  const CurveDocument stale_copy = current_curve();
  CurveDocument edited = stale_copy;
  auto appended = edited.append_point({.position = {4.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(appended.ok()) << appended.error_message();
  const CurvePointId retired_point = edited.points().back().id;
  const CurveSegmentId retired_segment = edited.segments().back();
  ASSERT_TRUE(session_.commit_curve_document(curve_, edited, appended.value()).ok());
  ASSERT_TRUE(session_.undo().ok());

  CurveDocument stale_edit = stale_copy;
  auto stale_append = stale_edit.append_point({.position = {5.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(stale_append.ok()) << stale_append.error_message();
  const auto before_failure = session_.content_revision();
  auto rejected = session_.commit_curve_document(curve_, stale_edit, stale_append.value());
  ASSERT_FALSE(rejected.ok());
  EXPECT_EQ(rejected.error().code, ErrorCode::StaleId);
  EXPECT_EQ(session_.content_revision(), before_failure);
  EXPECT_TRUE(session_.can_redo());

  edited = current_curve();
  appended = edited.append_point({.position = {5.0F, 0.0F, 0.0F}});
  ASSERT_TRUE(appended.ok()) << appended.error_message();
  EXPECT_GT(edited.points().back().id.index, retired_point.index);
  EXPECT_GT(edited.segments().back().index, retired_segment.index);
  auto preview = session_.begin_curve_document_preview(curve_, "modeling.curve.append",
                                                       edited, appended.value());
  ASSERT_TRUE(preview.ok()) << preview.error_message();
  const auto canceled_point = edited.points().back().id;
  const auto canceled_segment = edited.segments().back();
  ASSERT_TRUE(session_.cancel_preview(preview.value()).ok());
  edited = current_curve();
  ASSERT_TRUE(edited.append_point({.position = {6.0F, 0.0F, 0.0F}}).ok());
  EXPECT_GT(edited.points().back().id.index, canceled_point.index);
  EXPECT_GT(edited.segments().back().index, canceled_segment.index);
}

TEST_F(ModelingCurveSessionTests, CheckpointsAndHostHistoryPreserveCurveIdentityHighWater) {
  const CurveDocument original = current_curve();
  const ModelingSession checkpoint = session_.current_workspace_checkpoint();
  CurveDocument expanded = original;
  auto append = expanded.append_point({.position = {5.0F, 1.0F, 0.0F}});
  ASSERT_TRUE(append.ok()) << append.error_message();
  const CurvePointId retired = expanded.points().back().id;
  ASSERT_TRUE(session_.commit_curve_document(curve_, expanded, append.value()).ok());
  const auto prior_revision = session_.content_revision();
  session_.restore_snapshot_as_history_transition(checkpoint);
  EXPECT_GT(session_.content_revision(), prior_revision);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  EXPECT_EQ(session_.undo_depth(), 0U);
  CurveDocument branched = current_curve();
  ASSERT_TRUE(branched.append_point({.position = {6.0F, 1.0F, 0.0F}}).ok());
  EXPECT_GT(branched.points().back().id.index, retired.index);

  std::vector<SessionDocumentHistoryRestore> history;
  history.push_back({.object = curve_,
                     .dirty = {.topology = true, .geometry = true, .overlays = true},
                     .curve_document = expanded});
  auto restored = session_.restore_documents_as_history_transition(std::move(history));
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  EXPECT_TRUE(current_curve().has_same_geometry(expanded));
  expect_mesh_only_selected();
  EXPECT_EQ(session_.undo_depth(), 0U);

  ModelingSession clean = session_.current_workspace_checkpoint();
  EXPECT_FALSE(clean.can_undo());
  EXPECT_FALSE(clean.preview_active());
  EXPECT_TRUE(clean.curve_document_copy(curve_).or_throw().has_same_geometry(expanded));
  session_.restore_snapshot_preserving_identity_high_water(checkpoint);
  branched = current_curve();
  ASSERT_TRUE(branched.append_point({.position = {7.0F, 0.0F, 0.0F}}).ok());
  EXPECT_GT(branched.points().back().id.index, retired.index);
}

TEST_F(ModelingCurveSessionTests, HydrationIsAtomicAndRetainsMixedClipboard) {
  const std::array selected = {mesh_, curve_};
  ASSERT_TRUE(session_.apply_object_selection(selected).ok());
  ASSERT_TRUE(session_.copy_selection_to_clipboard().ok());
  const CurveDocument original = current_curve();
  std::vector<SessionWorkspaceDocument> documents;
  documents.push_back({.document = session_.document_copy(mesh_).or_throw(), .name = "Solid"});
  documents.push_back({.name = "Cable", .selected = true, .curve_document = original});
  auto loaded = session_.replace_workspace_preserving_clipboard(std::move(documents));
  ASSERT_TRUE(loaded.ok()) << loaded.error_message();
  ASSERT_EQ(loaded.value().size(), 2U);
  EXPECT_EQ(session_.undo_depth(), 0U);
  EXPECT_EQ(session_.clipboard_object_count(), 2U);
  EXPECT_EQ(session_.objects()[1].kind, SessionObjectKind::Curve);
  EXPECT_TRUE(session_.objects()[1].selected);
  EXPECT_TRUE(session_.curve_document_copy(loaded.value()[1]).or_throw().has_same_geometry(original));

  const auto revision = session_.content_revision();
  std::vector<SessionWorkspaceDocument> invalid;
  invalid.push_back({.document = PolygonDocument::make_box({}).or_throw(),
                     .name = "Ambiguous", .curve_document = original});
  EXPECT_FALSE(session_.replace_workspace_preserving_clipboard(std::move(invalid)).ok());
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.objects()[1].name, "Cable");
  EXPECT_EQ(session_.clipboard_object_count(), 2U);

  session_.reset_workspace_preserving_clipboard();
  EXPECT_TRUE(session_.objects().empty());
  auto pasted = session_.paste_clipboard();
  ASSERT_TRUE(pasted.ok()) << pasted.error_message();
  ASSERT_EQ(pasted.value().objects.size(), 2U);
  EXPECT_TRUE(session_.curve_document_copy(pasted.value().objects[1]).or_throw().has_same_geometry(original));
}

TEST_F(ModelingCurveSessionTests, PolygonOperationsRejectCurveTargetsAtomically) {
  const auto original_curve = current_curve();
  const auto original_mesh = session_.document_copy(mesh_).or_throw();
  PolygonDocument changed_mesh = original_mesh;
  ASSERT_TRUE(changed_mesh.select_all_faces().ok());
  auto moved = changed_mesh.translate_selection({2.0F, 0.0F, 0.0F});
  ASSERT_TRUE(moved.ok()) << moved.error_message();
  const auto revision = session_.content_revision();
  std::vector<SessionDocumentUpdate> updates;
  updates.push_back({.object = mesh_, .document = changed_mesh, .receipt = moved.value()});
  updates.push_back({.object = curve_, .document = original_mesh, .receipt = moved.value()});
  EXPECT_FALSE(session_.commit_documents_batch(std::move(updates)).ok());
  const std::array targets = {mesh_, curve_};
  EXPECT_FALSE(session_.document_copies(targets).ok());
  EXPECT_FALSE(session_.combine_objects(targets).ok());
  EXPECT_FALSE(session_.bridge_objects(mesh_, curve_, {}, SelectionKind::Face).ok());
  EXPECT_FALSE(session_.select_face(curve_, original_mesh.face_ids().front()).ok());
  EXPECT_FALSE(session_.commit_document(curve_, changed_mesh, moved.value()).ok());
  EXPECT_FALSE(session_.commit_curve_document(mesh_, original_curve, changed_receipt()).ok());
  EXPECT_FALSE(session_.begin_document_preview(curve_, "mesh.edit", original_mesh, moved.value()).ok());
  EXPECT_FALSE(session_.begin_curve_document_preview(mesh_, "curve.edit", original_curve, changed_receipt()).ok());
  EXPECT_FALSE(session_.semantic_overlay(curve_).ok());
  EXPECT_EQ(session_.document_copy(mesh_).or_throw().content_identity(), original_mesh.content_identity());
  EXPECT_TRUE(current_curve().has_same_geometry(original_curve));
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.undo_depth(), 0U);
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, ComponentSelectionBatchIncludesCurveObjectSelectionSafely) {
  ASSERT_TRUE(session_.select_object(curve_).ok());
  session_.clear_history();
  const auto original = current_curve();
  auto polygon = session_.document_copy(mesh_).or_throw();
  ASSERT_TRUE(polygon.select_face(polygon.face_ids().front()).ok());
  const PolygonSelectionSnapshot selected = polygon.selection();
  const std::array targets = {mesh_};
  const std::array updates = {SessionComponentSelectionUpdate{mesh_, selected}};
  const auto committed =
      session_.commit_component_selection_batch(updates, targets, mesh_);
  ASSERT_TRUE(committed.ok()) << committed.error_message();
  expect_mesh_only_selected();
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_FALSE(session_.objects()[0].selected);
  EXPECT_TRUE(session_.objects()[1].selected);
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  const auto revision = session_.content_revision();
  const std::array invalid = {
      SessionComponentSelectionUpdate{mesh_, selected},
      SessionComponentSelectionUpdate{curve_, selected},
  };
  EXPECT_FALSE(session_.commit_component_selection_batch(invalid, targets, mesh_).ok());
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_TRUE(session_.can_redo());
  ASSERT_TRUE(session_.redo().ok());
  expect_mesh_only_selected();
}

TEST_F(ModelingCurveSessionTests, CurveTranslateAndHistoryRestoreRejectInvalidTargets) {
  ASSERT_TRUE(session_.select_object(curve_).ok());
  session_.clear_history();
  const auto original = current_curve();
  ASSERT_TRUE(session_.translate_selection({0.0F, 4.0F, 0.0F}).ok());
  EXPECT_FLOAT_EQ(current_curve().points().front().position.y, 4.0F);
  EXPECT_FLOAT_EQ(current_curve().points().front().outgoing_handle.y,
                  original.points().front().outgoing_handle.y);
  const auto revision = session_.content_revision();
  EXPECT_FALSE(session_.translate_selection({std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F}).ok());
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_EQ(session_.undo_depth(), 1U);

  std::vector<SessionDocumentHistoryRestore> invalid;
  invalid.push_back({.object = curve_, .dirty = {.geometry = true}, .curve_document = original});
  invalid.push_back({.object = mesh_, .dirty = {.geometry = true}, .curve_document = original});
  EXPECT_FALSE(session_.restore_documents_as_history_transition(std::move(invalid)).ok());
  EXPECT_EQ(session_.content_revision(), revision);
  EXPECT_FLOAT_EQ(current_curve().points().front().position.y, 4.0F);
  EXPECT_EQ(session_.undo_depth(), 1U);
  ASSERT_TRUE(session_.undo().ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
  ASSERT_TRUE(session_.begin_translate_preview(curve_, {1.0F, 0.0F, 0.0F}).ok());
  const auto preview = session_.preview_summary().id;
  EXPECT_FLOAT_EQ(session_.preview_curve_document_copy(preview).or_throw().points().front().position.x,
                  original.points().front().position.x + 1.0F);
  ASSERT_TRUE(session_.cancel_preview(preview).ok());
  EXPECT_TRUE(current_curve().has_same_geometry(original));
}

} // namespace
