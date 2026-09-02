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
#include <unordered_map>
#include <unordered_set>
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

/** Distinguishes public object interaction from component interaction. */
enum class ModelingSelectionDomain {
  Object,
  Component,
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
  // Public-facade interaction state deliberately survives selection undo, as
  // an editor mode does. Documents own exact component sets; entries in this
  // map identify the current component-edit participants and repair each
  // participant's mode after undo. The map also supports heterogeneous
  // component kinds in one edit scope.
  ModelingSelectionDomain selection_domain = ModelingSelectionDomain::Object;
  std::unordered_map<std::uint64_t, SelectionMode> component_edit_modes;
  std::string active_tool = "select";
  std::optional<ModelingRepeatOperation> repeat_operation;
  bool repeating_operation = false;
  std::vector<ModelingRenderMeshCacheEntry> render_mesh_cache;
};

/** Captures facade-owned selection interaction state around atomic workflows. */
struct ModelingSelectionInteractionState {
  ModelingSelectionDomain domain = ModelingSelectionDomain::Object;
  std::unordered_map<std::uint64_t, SelectionMode> component_edit_modes;
};

[[nodiscard]] constexpr std::uint64_t
modeling_api_object_key(ObjectId object) noexcept {
  return (static_cast<std::uint64_t>(object.generation) << 32U) |
         static_cast<std::uint64_t>(object.index);
}

[[nodiscard]] inline std::optional<SelectionMode>
component_selection_mode(SelectionKind kind) {
  switch (kind) {
  case SelectionKind::Vertex:
    return SelectionMode::Vertex;
  case SelectionKind::Edge:
    return SelectionMode::Edge;
  case SelectionKind::Face:
    return SelectionMode::Face;
  case SelectionKind::Object:
    return std::nullopt;
  }
  return std::nullopt;
}

[[nodiscard]] inline SelectionKind
component_selection_kind(SelectionMode mode) noexcept {
  switch (mode) {
  case SelectionMode::Vertex:
    return SelectionKind::Vertex;
  case SelectionMode::Edge:
    return SelectionKind::Edge;
  case SelectionMode::Face:
    return SelectionKind::Face;
  }
  return SelectionKind::Vertex;
}

[[nodiscard]] inline ModelingSelectionInteractionState
capture_selection_interaction(const ModelingApiContext &context) {
  return {
      .domain = context.selection_domain,
      .component_edit_modes = context.component_edit_modes,
  };
}

inline void restore_selection_interaction(
    ModelingApiContext &context,
    const ModelingSelectionInteractionState &interaction) {
  context.selection_domain = interaction.domain;
  context.component_edit_modes = interaction.component_edit_modes;
}

inline void remember_selection_interaction(ModelingApiContext &context,
                                           const SelectionUnion &selection) {
  if (selection.kind == SelectionKind::Object) {
    context.selection_domain = ModelingSelectionDomain::Object;
    context.component_edit_modes.clear();
    return;
  }
  const std::optional<SelectionMode> mode =
      component_selection_mode(selection.kind);
  if (!mode.has_value() || !selection.object.valid()) {
    return;
  }
  context.selection_domain = ModelingSelectionDomain::Component;
  context.component_edit_modes.insert_or_assign(
      modeling_api_object_key(selection.object), *mode);
}

inline void forget_component_edit_participant(ModelingApiContext &context,
                                               ObjectId object) {
  if (object.valid()) {
    context.component_edit_modes.erase(modeling_api_object_key(object));
  }
  if (context.component_edit_modes.empty()) {
    context.selection_domain = ModelingSelectionDomain::Object;
  }
}

/**
 * Drops facade participants whose session objects no longer exist. Interaction
 * mode intentionally survives ordinary selection undo, but it must not leave
 * the next global selection command trapped in an empty component scope after
 * structural undo or another object-lifetime transition.
 */
inline void prune_stale_component_edit_participants(
    ModelingApiContext &context) {
  if (context.selection_domain == ModelingSelectionDomain::Object) {
    context.component_edit_modes.clear();
    return;
  }

  const std::vector<SessionObjectSummary> objects = context.session.objects();
  std::unordered_set<std::uint64_t> live_meshes;
  live_meshes.reserve(objects.size());
  for (const SessionObjectSummary &object : objects) {
    if (object.kind == SessionObjectKind::Mesh) {
      live_meshes.emplace(modeling_api_object_key(object.id));
    }
  }
  for (auto participant = context.component_edit_modes.begin();
       participant != context.component_edit_modes.end();) {
    if (!live_meshes.contains(participant->first)) {
      participant = context.component_edit_modes.erase(participant);
    } else {
      ++participant;
    }
  }
  if (context.component_edit_modes.empty()) {
    context.selection_domain = ModelingSelectionDomain::Object;
  }
}

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
  remember_selection_interaction(context, selection);
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
