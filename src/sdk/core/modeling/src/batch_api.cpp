////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"
#include "session_transaction_access.hpp"

#include <algorithm>
#include <exception>
#include <map>
#include <set>
#include <utility>

namespace quader::modeling {
namespace {

/**
 * Stores one scheduled public batch step.
 */
struct BatchStep {
  /**
   * Identifies which public batch action this step will execute.
   */
  enum class Kind {
    CreateBox,
    WithMesh,
  };

  Kind kind = Kind::CreateBox;
  BoxOptions box;
  std::string alias;
  std::function<OperationReceipt(MeshHandle)> mesh_operation;
  bool legacy_void_callback = false;
};

void append_delta(ElementDelta &target, const ElementDelta &source) {
  target.vertices.insert(target.vertices.end(), source.vertices.begin(),
                         source.vertices.end());
  target.edges.insert(target.edges.end(), source.edges.begin(),
                      source.edges.end());
  target.faces.insert(target.faces.end(), source.faces.begin(),
                      source.faces.end());
}

[[nodiscard]] OperationReceipt merged_batch_receipt(
    const std::vector<OperationReceipt> &receipts, RevisionStamp revisions) {
  OperationReceipt merged;
  merged.revisions = revisions;
  for (const OperationReceipt &receipt : receipts) {
    merged.success = merged.success && receipt.success;
    merged.changed = merged.changed || receipt.changed;
    merged.dirty.topology = merged.dirty.topology || receipt.dirty.topology;
    merged.dirty.geometry = merged.dirty.geometry || receipt.dirty.geometry;
    merged.dirty.selection = merged.dirty.selection || receipt.dirty.selection;
    merged.dirty.materials = merged.dirty.materials || receipt.dirty.materials;
    merged.dirty.overlays = merged.dirty.overlays || receipt.dirty.overlays;
    append_delta(merged.created, receipt.created);
    append_delta(merged.deleted, receipt.deleted);
    append_delta(merged.affected, receipt.affected);
    append_delta(merged.modified, receipt.modified);
    append_delta(merged.selection_remap.source,
                 receipt.selection_remap.source);
    append_delta(merged.selection_remap.destination,
                 receipt.selection_remap.destination);
    merged.diagnostics.insert(merged.diagnostics.end(),
                              receipt.diagnostics.begin(),
                              receipt.diagnostics.end());
    if (!receipt.success && merged.error.code == ErrorCode::Ok) {
      merged.error = receipt.error;
    }
  }
  return merged;
}

[[nodiscard]] OperationReceipt observed_step_receipt(
    ModelingApiContext &context, std::uint64_t previous_revision) {
  const SessionChangeSnapshot changes =
      context.session.changes_since(previous_revision);
  OperationReceipt receipt;
  receipt.changed = changes.content_revision > previous_revision;
  receipt.revisions = detail::current_revisions(context);
  for (const SessionObjectChange &change : changes.objects) {
    receipt.dirty.topology =
        receipt.dirty.topology || change.dirty.topology;
    receipt.dirty.geometry =
        receipt.dirty.geometry || change.dirty.geometry;
    receipt.dirty.selection =
        receipt.dirty.selection || change.dirty.selection;
    receipt.dirty.materials =
        receipt.dirty.materials || change.dirty.materials;
    receipt.dirty.overlays =
        receipt.dirty.overlays || change.dirty.overlays;
  }
  return receipt;
}

[[nodiscard]] bool same_dirty(const DirtyFlags &left,
                              const DirtyFlags &right) {
  return left.topology == right.topology &&
         left.geometry == right.geometry &&
         left.selection == right.selection &&
         left.materials == right.materials &&
         left.overlays == right.overlays;
}

[[nodiscard]] std::optional<ObjectId>
find_alias(const std::vector<std::pair<std::string, ObjectId>> &aliases,
           std::string_view alias) {
  const auto found = std::ranges::find_if(aliases, [&](const auto &entry) {
    return entry.first == alias;
  });
  if (found == aliases.end()) {
    return std::nullopt;
  }
  return found->second;
}

[[nodiscard]] std::string step_alias(const BatchStep &step) {
  return step.alias.empty() ? step.box.name : step.alias;
}

[[nodiscard]] OperationReceipt batch_failure(ModelingApiContext &context,
                                             Error error,
                                             std::size_t undo_mark) {
  const std::size_t current_depth = context.session.undo_depth();
  const bool needs_rollback = current_depth > undo_mark;
  const bool history_rewound = current_depth < undo_mark;
  const bool rolled_back =
      !needs_rollback || context.session.rollback_undo_since(undo_mark);
  if (history_rewound || !rolled_back) {
    Error rollback_error = make_error(
        ErrorCode::InternalError,
        "Atomic batch rollback could not restore its history boundary.");
    rollback_error.diagnostics.push_back({
        .code = "modeling.batch.rollback",
        .severity = DiagnosticSeverity::Error,
        .message = error.message,
    });
    error = std::move(rollback_error);
  }
  detail::record_error(context, error);
  return detail::failed_receipt(error, detail::current_revisions(context));
}

void finish_batch_guard(ModelingApiContext &context) {
  ModelingSessionTransactionAccess::clear_target(context.session);
  static_cast<void>(ModelingSessionTransactionAccess::finish(context.session));
}

void record_batch_step_failure(BatchResult &result,
                               ModelingApiContext &context, Error error,
                               std::size_t undo_mark) {
  finish_batch_guard(context);
  result.receipt = batch_failure(context, error, undo_mark);
  result.meshes.clear();
  result.step_receipts.push_back(
      detail::failed_receipt(error, result.receipt.revisions));
}

[[nodiscard]] Error unexpected_batch_exception(
    std::string_view operation, const std::exception *exception = nullptr) {
  std::string message = "Batch ";
  message += operation;
  message += " threw ";
  if (exception != nullptr) {
    message += "an unexpected exception: ";
    message += exception->what();
  } else {
    message += "an unknown exception.";
  }
  return make_error(ErrorCode::InternalError, std::move(message));
}

[[nodiscard]] std::optional<Error>
validate_batch_dependencies(const std::vector<BatchStep> &steps) {
  std::set<std::string> aliases;
  for (const BatchStep &step : steps) {
    if (step.kind == BatchStep::Kind::CreateBox) {
      const std::string alias = step_alias(step);
      if (alias.empty()) {
        return make_error(ErrorCode::InvalidArgument,
                          "Batch create_box requires a non-empty alias or name.");
      }
      if (!aliases.insert(alias).second) {
        return make_error(ErrorCode::InvalidArgument,
                          "Batch mesh alias is duplicated: " + alias);
      }
      continue;
    }

    if (step.alias.empty()) {
      return make_error(ErrorCode::InvalidArgument,
                        "Batch with_mesh requires a mesh alias.");
    }
    if (!aliases.contains(step.alias)) {
      return make_error(ErrorCode::InvalidArgument,
                        "Batch mesh alias is not available: " + step.alias);
    }
    if (!step.mesh_operation) {
      return make_error(ErrorCode::InvalidArgument,
                        "Batch with_mesh requires an operation.");
    }
  }
  return std::nullopt;
}

} // namespace

/**
 * Stores shared mutable state for a public modeling batch.
 */
struct ModelingBatchState {
  std::shared_ptr<ModelingApiContext> context;
  std::string label;
  std::vector<BatchStep> steps;
  bool committed = false;
};

MeshHandle BatchResult::mesh(std::string_view alias) const {
  if (!context_) {
    return {};
  }
  const std::optional<ObjectId> object = find_alias(meshes, alias);
  return object.has_value() ? MeshHandle(context_, *object) : MeshHandle{};
}

BatchCreatedMeshStep::BatchCreatedMeshStep(
    std::shared_ptr<ModelingBatchState> state, std::size_t step_index)
    : state_(std::move(state)), step_index_(step_index) {}

ModelingBatch BatchCreatedMeshStep::as(std::string alias) {
  if (state_ && step_index_ < state_->steps.size()) {
    state_->steps[step_index_].alias = std::move(alias);
  }
  return ModelingBatch(state_);
}

ModelingBatch::ModelingBatch(std::shared_ptr<ModelingApiContext> context,
                             std::string label)
    : state_(std::make_shared<ModelingBatchState>()) {
  state_->context = std::move(context);
  state_->label = std::move(label);
}

ModelingBatch::ModelingBatch(std::shared_ptr<ModelingBatchState> state)
    : state_(std::move(state)) {}

BatchCreatedMeshStep ModelingBatch::create_box(BoxOptions options) {
  if (!state_) {
    return {};
  }
  BatchStep step;
  step.kind = BatchStep::Kind::CreateBox;
  step.box = std::move(options);
  state_->steps.push_back(std::move(step));
  return BatchCreatedMeshStep(state_, state_->steps.size() - 1U);
}

ModelingBatch &
ModelingBatch::with_mesh_checked(
    std::string alias,
    std::function<OperationReceipt(MeshHandle)> operation) {
  if (state_) {
    BatchStep step;
    step.kind = BatchStep::Kind::WithMesh;
    step.alias = std::move(alias);
    step.mesh_operation = std::move(operation);
    state_->steps.push_back(std::move(step));
  }
  return *this;
}

ModelingBatch &
ModelingBatch::with_mesh(std::string alias,
                         std::function<void(MeshHandle)> operation) {
  if (state_) {
    BatchStep step;
    step.kind = BatchStep::Kind::WithMesh;
    step.alias = std::move(alias);
    step.legacy_void_callback = true;
    step.mesh_operation =
        [operation = std::move(operation)](MeshHandle mesh) mutable {
          operation(std::move(mesh));
          return OperationReceipt{};
        };
    state_->steps.push_back(std::move(step));
  }
  return *this;
}

BatchResult ModelingBatch::commit() {
  BatchResult result;
  if (!state_ || !state_->context) {
    result.receipt.success = false;
    result.receipt.error =
        make_error(ErrorCode::InvalidArgument, "Batch is not initialized.");
    return result;
  }
  if (state_->committed) {
    result.context_ = state_->context;
    result.receipt = detail::handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument, "Batch is already committed."));
    return result;
  }

  ModelingApi api(state_->context);
  result.context_ = state_->context;
  const std::size_t undo_mark = state_->context->session.undo_depth();
  if (std::optional<Error> validation =
          validate_batch_dependencies(state_->steps)) {
    result.receipt =
        batch_failure(*state_->context, std::move(*validation), undo_mark);
    return result;
  }
  if (!ModelingSessionTransactionAccess::begin(state_->context->session)) {
    result.receipt = detail::handle_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument,
                   "Another batch or preview transaction is already active."));
    return result;
  }

  for (BatchStep &step : state_->steps) {
    if (step.kind == BatchStep::Kind::CreateBox) {
      MeshHandle mesh;
      const std::uint64_t error_epoch = state_->context->error_epoch;
      const std::uint64_t previous_revision =
          state_->context->session.content_revision();
      ModelingSessionTransactionAccess::authorize_creation(
          state_->context->session);
      try {
        mesh = api.create_box(step.box);
      } catch (const ModelingException &exception) {
        record_batch_step_failure(result, *state_->context,
                                  exception.error(), undo_mark);
        return result;
      } catch (const std::exception &exception) {
        record_batch_step_failure(
            result, *state_->context,
            unexpected_batch_exception("create_box", &exception), undo_mark);
        return result;
      } catch (...) {
        record_batch_step_failure(
            result, *state_->context,
            unexpected_batch_exception("create_box"), undo_mark);
        return result;
      }
      if (state_->context->error_epoch != error_epoch) {
        const Error error = state_->context->last_error.value_or(make_error(
            ErrorCode::InternalError,
            "Batch create_box recorded an error without diagnostics."));
        record_batch_step_failure(result, *state_->context, error, undo_mark);
        return result;
      }
      if (!mesh.valid()) {
        record_batch_step_failure(
            result, *state_->context,
            make_error(ErrorCode::InternalError,
                       "Batch create_box did not create a mesh."),
            undo_mark);
        return result;
      }
      result.meshes.push_back({step_alias(step), mesh.id()});
      result.step_receipts.push_back(
          observed_step_receipt(*state_->context, previous_revision));
      continue;
    }

    const std::optional<ObjectId> object = find_alias(result.meshes, step.alias);
    if (!object.has_value()) {
      record_batch_step_failure(
          result, *state_->context,
          make_error(ErrorCode::InvalidArgument,
                     "Batch mesh alias is not available: " + step.alias),
          undo_mark);
      return result;
    }

    const std::uint64_t error_epoch = state_->context->error_epoch;
    const std::uint64_t previous_revision =
        state_->context->session.content_revision();
    ModelingSessionTransactionAccess::target(state_->context->session,
                                             *object);
    try {
      OperationReceipt receipt =
          step.mesh_operation(MeshHandle(state_->context, *object));
      ModelingSessionTransactionAccess::clear_target(
          state_->context->session);
      if (state_->context->error_epoch != error_epoch) {
        const Error error = state_->context->last_error.value_or(make_error(
            ErrorCode::InternalError,
            "Batch mesh operation recorded an error without diagnostics."));
        record_batch_step_failure(result, *state_->context, error, undo_mark);
        return result;
      }
      const OperationReceipt observed =
          observed_step_receipt(*state_->context, previous_revision);
      if (step.legacy_void_callback) {
        receipt = observed;
      } else if (!receipt.success) {
        const Error error =
            receipt.error.code == ErrorCode::Ok
                ? make_error(ErrorCode::InternalError,
                             "Batch callback returned failure without an error.")
                : receipt.error;
        record_batch_step_failure(result, *state_->context, error, undo_mark);
        return result;
      } else if (receipt.changed != observed.changed ||
                 !same_dirty(receipt.dirty, observed.dirty)) {
        record_batch_step_failure(
            result, *state_->context,
            make_error(ErrorCode::InternalError,
                       "Batch callback receipt does not match committed state."),
            undo_mark);
        return result;
      } else {
        receipt.revisions = observed.revisions;
      }
      result.step_receipts.push_back(std::move(receipt));
    } catch (const ModelingException &exception) {
      record_batch_step_failure(result, *state_->context,
                                exception.error(), undo_mark);
      return result;
    } catch (const std::exception &exception) {
      record_batch_step_failure(
          result, *state_->context,
          unexpected_batch_exception("mesh callback", &exception), undo_mark);
      return result;
    } catch (...) {
      record_batch_step_failure(
          result, *state_->context,
          unexpected_batch_exception("mesh callback"), undo_mark);
      return result;
    }
  }

  ModelingSessionTransactionAccess::clear_target(state_->context->session);
  if (!ModelingSessionTransactionAccess::finish(state_->context->session)) {
    result.receipt = batch_failure(
        *state_->context,
        make_error(ErrorCode::InvalidArgument,
                   "Batch callback violated the atomic transaction boundary."),
        undo_mark);
    result.meshes.clear();
    return result;
  }
  if (state_->context->session.undo_depth() < undo_mark) {
    result.receipt = batch_failure(
        *state_->context,
        make_error(ErrorCode::InternalError,
                   "Batch history crossed below its transaction boundary."),
        undo_mark);
    result.meshes.clear();
    return result;
  }
  state_->context->session.squash_undo_since(undo_mark);
  result.receipt =
      merged_batch_receipt(result.step_receipts, api.revisions());
  state_->committed = true;
  return result;
}

OperationReceipt ModelingBatch::cancel() {
  OperationReceipt receipt;
  if (state_ && state_->context) {
    receipt.revisions = detail::current_revisions(*state_->context);
  }
  return receipt;
}

} // namespace quader::modeling
