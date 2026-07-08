////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <quader/modeling/api/modeling_api.hpp>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace quader::modeling {

/**
 * Identifies which pending public preview operation a preview handle owns.
 */
enum class ModelingPreviewKind {
  None,
  BridgeEdges,
  BridgeFaces,
  EdgeBevel,
  MergeByDistance,
  SliceQuad,
  Thicken,
  Transform,
};

/**
 * Stores one object-level modeling clipboard entry.
 */
struct ModelingClipboardObject {
  std::string name;
  PolygonDocument document;
};

/**
 * Stores one repeatable public operation command.
 */
struct ModelingRepeatOperation {
  std::string id;
  OperationSettings settings;
};

/**
 * Stores shared mutable state for public API facade handles.
 */
struct ModelingApiContext {
  ModelingSession session;
  ModelingApiOptions options;
  std::optional<Error> last_error;
  SelectionEdit default_edit = SelectionEdit::Replace;
  std::string active_tool = "select";
  std::vector<ModelingClipboardObject> clipboard;
  std::optional<ModelingRepeatOperation> repeat_operation;
  bool repeating_operation = false;
};

/**
 * Stores one pending public preview operation without mutating session truth.
 */
struct ModelingPreviewState {
  std::shared_ptr<ModelingApiContext> context;
  PreviewId id{};
  ModelingPreviewKind kind = ModelingPreviewKind::None;
  SelectionUnion selection;
  BridgeOptions bridge;
  EdgeBevelOptions edge_bevel;
  MergeByDistanceOptions merge_by_distance;
  SliceQuadOptions slice_quad;
  ThickenOptions thicken;
  TransformOptions transform;
  bool active = true;
};

namespace detail {

[[nodiscard]] inline Error unsupported_error(std::string message) {
  return make_error(ErrorCode::UnsupportedOperation, std::move(message));
}

[[nodiscard]] inline OperationReceipt failed_receipt(
    const Error &error, RevisionStamp revisions = {}) {
  OperationReceipt receipt;
  receipt.success = false;
  receipt.changed = false;
  receipt.error = error;
  receipt.diagnostics = error.diagnostics;
  receipt.revisions = revisions;
  return receipt;
}

[[nodiscard]] inline OperationReceipt
receipt_from_result(const OperationResult &result, RevisionStamp revisions) {
  OperationReceipt receipt;
  receipt.success = result.success;
  receipt.changed = result.changed;
  receipt.revisions = revisions;
  receipt.dirty = result.dirty;
  receipt.created = result.created;
  receipt.deleted = result.deleted;
  receipt.affected = result.affected;
  receipt.modified = result.modified;
  receipt.selection_remap = result.selection_remap;
  receipt.diagnostics = result.diagnostics;
  receipt.error = {
      .code = result.error_code,
      .message = result.message,
      .diagnostics = result.diagnostics,
  };
  return receipt;
}

[[nodiscard]] inline MeshSummary
 to_mesh_summary(const SessionObjectSummary &summary) {
  return {
      .id = summary.id,
      .name = summary.name,
      .selected = summary.selected,
      .content_revision = summary.content_revision,
      .selection_revision = summary.selection_revision,
  };
}

[[nodiscard]] inline PolygonSelectionSnapshot
polygon_selection_from_union(const SelectionUnion &selection) {
  PolygonSelectionSnapshot snapshot;
  snapshot.kind = selection.kind;
  snapshot.vertices = selection.vertices;
  snapshot.edges = selection.edges;
  snapshot.faces = selection.faces;
  return snapshot;
}

[[nodiscard]] inline RevisionStamp current_revisions(
    const ModelingApiContext &context) {
  RevisionStamp stamp;
  for (const SessionObjectSummary &object : context.session.objects()) {
    stamp.content = std::max(stamp.content, object.content_revision);
    stamp.selection = std::max(stamp.selection, object.selection_revision);
  }
  return stamp;
}

[[nodiscard]] inline OperationReceipt handle_failure(ModelingApiContext &context,
                                                     Error error) {
  context.last_error = error;
  if (context.options.error_policy == ErrorPolicy::ThrowException) {
    throw ModelingException(std::move(error));
  }
  return failed_receipt(error, current_revisions(context));
}

inline void record_repeatable_operation(ModelingApiContext &context,
                                        std::string id,
                                        OperationSettings settings,
                                        const OperationReceipt &receipt) {
  if (context.repeating_operation || !receipt.success || !receipt.changed) {
    return;
  }
  context.repeat_operation = ModelingRepeatOperation{
      .id = std::move(id),
      .settings = std::move(settings),
  };
}

template <typename T>
[[nodiscard]] T handle_value_failure(ModelingApiContext &context, Error error,
                                     T fallback = {}) {
  context.last_error = error;
  if (context.options.error_policy == ErrorPolicy::ThrowException) {
    throw ModelingException(std::move(error));
  }
  return fallback;
}

[[nodiscard]] inline bool object_live(const ModelingApiContext &context,
                                      ObjectId object) {
  const std::vector<SessionObjectSummary> objects = context.session.objects();
  return std::ranges::any_of(objects, [&](const SessionObjectSummary &summary) {
    return summary.id == object;
  });
}

[[nodiscard]] inline std::optional<MeshSummary>
find_summary(const ModelingApiContext &context, ObjectId object) {
  for (const SessionObjectSummary &summary : context.session.objects()) {
    if (summary.id == object) {
      return to_mesh_summary(summary);
    }
  }
  return std::nullopt;
}

[[nodiscard]] inline Result<OperationReceipt>
commit_document_receipt(ModelingApiContext &context, ObjectId object,
                        PolygonDocument document, OperationResult receipt) {
  Result<OperationResult> committed = context.session.commit_document(
      object, std::move(document), std::move(receipt));
  if (!committed.ok()) {
    return Result<OperationReceipt>::failure(committed.error());
  }
  return Result<OperationReceipt>::success(
      receipt_from_result(committed.value(), current_revisions(context)));
}

[[nodiscard]] inline Result<OperationReceipt> mutate_selection(
    ModelingApiContext &context, const SelectionUnion &selection,
    const std::function<Result<OperationResult>(PolygonDocument &)> &operation) {
  Result<PolygonDocument> document =
      context.session.document_copy(selection.object);
  if (!document.ok()) {
    return Result<OperationReceipt>::failure(document.error());
  }

  PolygonDocument working = std::move(document).value();
  Result<OperationResult> selected = working.apply_selection(
      polygon_selection_from_union(selection), SelectionEdit::Replace);
  if (!selected.ok()) {
    return Result<OperationReceipt>::failure(selected.error());
  }

  Result<OperationResult> operation_result = operation(working);
  if (!operation_result.ok()) {
    return Result<OperationReceipt>::failure(operation_result.error());
  }
  return commit_document_receipt(context, selection.object, std::move(working),
                                 std::move(operation_result).value());
}

[[nodiscard]] inline OperationReceipt direct_mutate_selection(
    ModelingApiContext &context, const SelectionUnion &selection,
    const std::function<Result<OperationResult>(PolygonDocument &)> &operation) {
  Result<OperationReceipt> result =
      mutate_selection(context, selection, operation);
  if (!result.ok()) {
    return handle_failure(context, result.error());
  }
  context.last_error.reset();
  return std::move(result).value();
}

[[nodiscard]] inline Result<OperationReceipt> apply_selection_to_document(
    ModelingApiContext &context, const SelectionUnion &selection,
    SelectionEdit edit) {
  Result<PolygonDocument> document =
      context.session.document_copy(selection.object);
  if (!document.ok()) {
    return Result<OperationReceipt>::failure(document.error());
  }
  PolygonDocument working = std::move(document).value();
  Result<OperationResult> applied =
      working.apply_selection(polygon_selection_from_union(selection), edit);
  if (!applied.ok()) {
    return Result<OperationReceipt>::failure(applied.error());
  }
  return commit_document_receipt(context, selection.object, std::move(working),
                                 std::move(applied).value());
}

[[nodiscard]] inline SelectionUnion all_face_selection(const MeshHandle &mesh) {
  return const_cast<MeshHandle &>(mesh).faces().all().to_union();
}

[[nodiscard]] inline OperationReceipt unsupported(ModelingApiContext &context,
                                                  std::string message) {
  return handle_failure(context, unsupported_error(std::move(message)));
}

} // namespace detail
} // namespace quader::modeling
