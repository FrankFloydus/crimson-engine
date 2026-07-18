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
 * Stores one repeatable public operation command.
 */
struct ModelingRepeatOperation {
  std::string id;
  OperationSettings settings;
};

/**
 * Retains one owned compiled mesh across render snapshots while authored
 * topology identity remains unchanged.
 */
struct ModelingRenderMeshCacheEntry {
  ObjectId object{};
  PolygonContentIdentity content_identity{};
  std::uint64_t content_revision = 0;
  MeshPayload mesh;
};

/**
 * Stores shared mutable state for public API facade handles.
 */
struct ModelingApiContext {
  ModelingSession session;
  bool public_facade_bound = false;
  ModelingApiOptions options;
  std::optional<Error> last_error;
  std::uint64_t error_epoch = 0;
  SelectionEdit default_edit = SelectionEdit::Replace;
  std::string active_tool = "select";
  std::optional<ModelingRepeatOperation> repeat_operation;
  bool repeating_operation = false;
  std::vector<ModelingRenderMeshCacheEntry> render_mesh_cache;
};

/**
 * Stores one pending public preview operation without mutating session truth.
 */
struct ModelingPreviewState {
  ~ModelingPreviewState();

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
  std::uint64_t payload_revision = 0;
};

namespace detail {

inline void record_error(ModelingApiContext &context, const Error &error) {
  context.last_error = error;
  ++context.error_epoch;
}

template <typename T, typename Operation>
[[nodiscard]] Result<T>
checked_direct_value(ModelingApiContext &context, Operation &&operation) {
  const std::uint64_t error_epoch = context.error_epoch;
  try {
    T value = std::invoke(std::forward<Operation>(operation));
    if (context.error_epoch != error_epoch) {
      if (context.last_error.has_value()) {
        return Result<T>::failure(*context.last_error);
      }
      return Result<T>::failure(make_error(
          ErrorCode::InternalError,
          "A direct SDK call recorded an error without diagnostics."));
    }
    return Result<T>::success(std::move(value));
  } catch (const ModelingException &exception) {
    return Result<T>::failure(exception.error());
  }
}

template <typename T, typename Operation, typename Validator>
[[nodiscard]] Result<T> checked_direct_value(
    ModelingApiContext &context, Operation &&operation, Validator &&validator,
    Error invalid_value_error) {
  Result<T> result = checked_direct_value<T>(
      context, std::forward<Operation>(operation));
  if (!result.ok()) {
    return result;
  }
  if (!std::invoke(std::forward<Validator>(validator), result.value())) {
    return Result<T>::failure(std::move(invalid_value_error));
  }
  return result;
}

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
  if (!result.success) {
    receipt.error = {
        .code = result.error_code == ErrorCode::Ok ? ErrorCode::InternalError
                                                    : result.error_code,
        .message = result.message.empty() ? "Modeling operation failed."
                                          : result.message,
        .diagnostics = result.diagnostics,
    };
  }
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
  const std::uint64_t content = context.session.content_revision();
  return {
      .content = content,
      .selection = context.session.selection_revision(),
      .scene = content,
  };
}

[[nodiscard]] inline OperationReceipt handle_failure(ModelingApiContext &context,
                                                     Error error) {
  record_error(context, error);
  if (context.options.error_policy == ErrorPolicy::ThrowException) {
    throw ModelingException(std::move(error));
  }
  return failed_receipt(error, current_revisions(context));
}

[[nodiscard]] inline bool require_owned_snapshot_lifetime(
    ModelingApiContext &context, SnapshotLifetime lifetime,
    std::string_view operation) {
  if (lifetime == SnapshotLifetime::Owned) {
    return true;
  }
  std::string message(operation);
  message +=
      " supports only SnapshotLifetime::Owned; pinned and borrowed storage "
      "are not implemented.";
  static_cast<void>(
      handle_failure(context, unsupported_error(std::move(message))));
  return false;
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
  record_error(context, error);
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
