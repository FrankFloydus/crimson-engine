////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "public_api_detail.hpp"

#include <algorithm>
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
  std::function<void(MeshHandle)> mesh_operation;
};

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
    merged.diagnostics.insert(merged.diagnostics.end(),
                              receipt.diagnostics.begin(),
                              receipt.diagnostics.end());
    if (!receipt.success && merged.error.code == ErrorCode::Ok) {
      merged.error = receipt.error;
    }
  }
  return merged;
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
  static_cast<void>(context.session.rollback_undo_since(undo_mark));
  context.last_error = error;
  return detail::failed_receipt(error, detail::current_revisions(context));
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
ModelingBatch::with_mesh(std::string alias,
                         std::function<void(MeshHandle)> operation) {
  if (state_) {
    BatchStep step;
    step.kind = BatchStep::Kind::WithMesh;
    step.alias = std::move(alias);
    step.mesh_operation = std::move(operation);
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

  for (BatchStep &step : state_->steps) {
    if (step.kind == BatchStep::Kind::CreateBox) {
      MeshHandle mesh;
      try {
        mesh = api.create_box(step.box);
      } catch (const ModelingException &exception) {
        result.receipt =
            batch_failure(*state_->context, exception.error(), undo_mark);
        return result;
      }
      if (!mesh.valid()) {
        result.receipt = batch_failure(
            *state_->context,
            make_error(ErrorCode::InternalError,
                       "Batch create_box did not create a mesh."),
            undo_mark);
        return result;
      }
      result.meshes.push_back({step_alias(step), mesh.id()});

      OperationReceipt receipt;
      receipt.changed = true;
      receipt.revisions = api.revisions();
      receipt.dirty.topology = true;
      receipt.dirty.geometry = true;
      result.step_receipts.push_back(std::move(receipt));
      continue;
    }

    const std::optional<ObjectId> object = find_alias(result.meshes, step.alias);
    if (!object.has_value()) {
      result.receipt = batch_failure(
          *state_->context,
          make_error(ErrorCode::InvalidArgument,
                     "Batch mesh alias is not available: " + step.alias),
          undo_mark);
      return result;
    }

    try {
      step.mesh_operation(MeshHandle(state_->context, *object));
      OperationReceipt receipt;
      receipt.changed = true;
      receipt.revisions = api.revisions();
      receipt.dirty.topology = true;
      receipt.dirty.geometry = true;
      result.step_receipts.push_back(std::move(receipt));
    } catch (const ModelingException &exception) {
      result.receipt =
          batch_failure(*state_->context, exception.error(), undo_mark);
      return result;
    }
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
