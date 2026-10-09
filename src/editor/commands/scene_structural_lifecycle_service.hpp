////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_structural_duplicate.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {

class DocumentCommandService;
class EditorHistoryService;
class EditorSelectionService;
class EditorTransaction;
class ModelingSessionService;
class SceneDocument;

/**
 * Opaque caller-owned structural mutation fence. Scalar compensation remains
 * implemented by SceneStructuralLifecycleService rather than its facades.
 */
class SceneStructuralCompensationScope final {
public:
  SceneStructuralCompensationScope() = default;
  ~SceneStructuralCompensationScope() { (void)release(); }
  SceneStructuralCompensationScope(
      SceneStructuralCompensationScope &&) noexcept = default;
  SceneStructuralCompensationScope &
  operator=(SceneStructuralCompensationScope &&) noexcept = default;
  SceneStructuralCompensationScope(
      const SceneStructuralCompensationScope &) = delete;
  SceneStructuralCompensationScope &
  operator=(const SceneStructuralCompensationScope &) = delete;

  [[nodiscard]] bool valid() const noexcept {
    return state_ != nullptr && state_->scene != nullptr &&
           state_->checkpoint.valid() && !state_->finished;
  }
  [[nodiscard]] bool seal_forward() noexcept {
    if (!valid()) {
      return false;
    }
    if (state_->sealed) {
      return true;
    }
    state_->sealed =
        state_->scene->acknowledge_scalar_compensation_checkpoint(
            state_->checkpoint);
    return state_->sealed;
  }
  [[nodiscard]] bool open_inverse() noexcept {
    if (!valid() || !state_->sealed) {
      return false;
    }
    if (state_->inverse_open) {
      return true;
    }
    state_->inverse_open =
        state_->scene->begin_scalar_compensation_restore(state_->checkpoint);
    return state_->inverse_open;
  }
  [[nodiscard]] bool restore() {
    if (!valid() || !state_->inverse_open) {
      return false;
    }
    const bool restored =
        state_->scene->restore_scalar_compensation_checkpoint(
            state_->checkpoint);
    state_->finished = restored;
    return restored;
  }
  [[nodiscard]] bool release() noexcept {
    if (state_ == nullptr || state_->finished) {
      return true;
    }
    if (state_->scene == nullptr || !state_->checkpoint.valid()) {
      state_->finished = true;
      return false;
    }
    const bool released =
        state_->scene->discard_scalar_compensation_checkpoint(
            state_->checkpoint);
    state_->finished = released;
    return released;
  }

private:
  friend class SceneStructuralLifecycleService;
  /** Owns scalar-checkpoint state across forward and compensating structural mutations. */
  struct State final {
    SceneDocument *scene = nullptr;
    SceneScalarCompensationCheckpoint checkpoint;
    bool sealed = false;
    bool inverse_open = false;
    bool finished = false;
  };
  explicit SceneStructuralCompensationScope(std::unique_ptr<State> state)
      : state_(std::move(state)) {}
  std::unique_ptr<State> state_;
};

/** Identifies one source root and the corresponding duplicated root. */
struct SceneStructuralClone final {
  EntityId source = kInvalidEntityId;
  EntityId duplicate = kInvalidEntityId;

  friend bool operator==(const SceneStructuralClone &,
                         const SceneStructuralClone &) = default;
};

/** Identifies one modeled descendant and its new native scene identity. */
struct SceneStructuralModelingClone final {
  EntityId source = kInvalidEntityId;
  EntityId duplicate = kInvalidEntityId;
  int source_modeling_object_id = kInvalidModelingObjectId;
  int duplicate_modeling_object_id = kInvalidModelingObjectId;

  friend bool operator==(const SceneStructuralModelingClone &,
                         const SceneStructuralModelingClone &) = default;
};

/** Reports one atomic, cross-domain hierarchy duplicate. */
struct SceneStructuralDuplicateResult final : CommandResult {
  SceneStructuralDuplicateResult() = default;
  explicit SceneStructuralDuplicateResult(CommandResult status)
      : CommandResult(std::move(status)) {}

  std::vector<SceneStructuralClone> clones;
  std::vector<SceneStructuralModelingClone> modeling_clones;
};

/** One independently normalized root set in a structural duplicate batch. */
struct SceneStructuralDuplicateBatch final {
  std::vector<EntityId> roots;
};

/** Reports explicit source-to-duplicate mappings for every requested batch. */
struct SceneStructuralDuplicateBatchResult final : CommandResult {
  SceneStructuralDuplicateBatchResult() = default;
  explicit SceneStructuralDuplicateBatchResult(CommandResult status)
      : CommandResult(std::move(status)) {}

  std::vector<SceneStructuralDuplicateResult> batches;
};

/** Adapts the scene owner result to the dependency-neutral modeling seam. */
[[nodiscard]] ModelingStructuralDuplicateResult
modeling_structural_duplicate_result(SceneStructuralDuplicateResult result);

/**
 * Owns entity duplicate and destroy policy across SceneDocument and portable
 * modeling state. Every public API and editor command routes structural entity
 * mutations through this service so mixed generic/mesh/curve hierarchies are
 * captured, published, and replayed as one history entry.
 */
class SceneStructuralLifecycleService final {
public:
  SceneStructuralLifecycleService(SceneDocument &scene,
                                  EditorHistoryService &history,
                                  DocumentCommandService &document_commands,
                                  ModelingSessionService &modeling,
                                  EditorSelectionService *selection = nullptr);

  [[nodiscard]] bool can_duplicate_selected_hierarchy() const;
  [[nodiscard]] std::vector<EntityId> selected_roots() const;
  [[nodiscard]] bool hierarchy_contains_modeling_content(
      std::span<const EntityId> roots) const;
  [[nodiscard]] SceneStructuralCompensationScope
  begin_compensation_scope();

  DocumentGroupSelectionResult
  group_selection(std::string group_name = {},
                  TransformComponent group_transform = TransformComponent{});
  DocumentCommandResult ungroup_selection();

  SceneStructuralDuplicateResult duplicate_entity(EntityId entity);
  SceneStructuralDuplicateResult duplicate_selected_hierarchy(
      ModelingSelectionMode result_selection_mode);
  SceneStructuralDuplicateResult duplicate_hierarchy(
      std::span<const EntityId> roots,
      ModelingSelectionMode result_selection_mode,
      std::string label = "Duplicate Selection",
      bool independent_mesh_assets = false);
  SceneStructuralDuplicateResult duplicate_hierarchy_in_transaction(
      EditorTransaction &transaction, std::span<const EntityId> roots,
      ModelingSelectionMode result_selection_mode,
      std::string label = "Duplicate Selection",
      bool independent_mesh_assets = false);
  SceneStructuralDuplicateBatchResult duplicate_hierarchy_batches_in_transaction(
      EditorTransaction &transaction,
      std::span<const SceneStructuralDuplicateBatch> batches,
      ModelingSelectionMode result_selection_mode,
      std::string label = "Duplicate Selection",
      bool independent_mesh_assets = false,
      SceneEntityNamePolicy name_policy = SceneEntityNamePolicy::DuplicateFamily);

  CommandResult destroy_entity(EntityId entity);
  CommandResult destroy_selected_hierarchy();
  CommandResult destroy_hierarchy(std::span<const EntityId> roots,
                                  std::string label = "Destroy Entities");

private:
  /** Carries a validated ancestry-normalized root set or its failure status. */
  struct NormalizedRoots final {
    CommandResult status;
    std::vector<EntityId> roots;
  };

  [[nodiscard]] NormalizedRoots
  normalize_explicit_roots(std::span<const EntityId> roots) const;

  SceneDocument &scene_;
  EditorHistoryService &history_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
  EditorSelectionService *selection_ = nullptr;
};

inline SceneStructuralCompensationScope
SceneStructuralLifecycleService::begin_compensation_scope() {
  auto state = std::make_unique<SceneStructuralCompensationScope::State>();
  state->scene = &scene_;
  state->checkpoint = scene_.capture_scalar_compensation_checkpoint();
  return SceneStructuralCompensationScope(std::move(state));
}

} // namespace quader
