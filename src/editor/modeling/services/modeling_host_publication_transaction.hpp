////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "editor/modeling/runtime/modeling_object_binding_registry.hpp"
#include "scene/scene_document.hpp"

#include <quader/modeling/session/modeling_session.hpp>

#include <cstddef>
#include <concepts>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

namespace quader {

/** Selects how a failed host publication restores portable modeling state. */
enum class ModelingSdkRollbackPolicy {
  Snapshot,
  PreviewCheckpoint,
  MutationCheckpoint,
  CancelStartedPreview,
  None,
};

/** Selects the narrow native owner checkpoint required by the publication. */
enum class ModelingHostPublicationCheckpointScope {
  Unspecified,
  ModelingProjection,
  MeshProjection,
  CurveProjection,
};

/** Configuration for one atomic SDK-to-native host publication. */
struct ModelingHostPublicationTransactionOptions final {
  ModelingSdkRollbackPolicy sdk_rollback =
      ModelingSdkRollbackPolicy::Snapshot;
  ModelingHostPublicationCheckpointScope checkpoint_scope =
      ModelingHostPublicationCheckpointScope::Unspecified;
  /** Optional independent overrides for mixed bounded publications. */
  std::optional<ModelingHostPublicationCheckpointScope> asset_checkpoint_scope;
  std::optional<ModelingHostPublicationCheckpointScope> scene_checkpoint_scope;
  bool capture_assets = true;
  bool capture_scene = true;
  bool capture_scene_entities = true;
  bool capture_bindings = true;
  bool capture_hierarchy = true;
  /** Includes the exact selected/active scene projection in a bounded scene
   * checkpoint. Continuous payload samples leave this disabled. */
  bool capture_scene_selection = false;
  std::span<const EntityId> affected_scene_entities = {};
  std::span<const AssetId> affected_asset_ids = {};
  std::span<const int> affected_modeling_object_ids = {};
  std::span<const modeling::ObjectId> affected_sdk_object_ids = {};
};

/** Deterministic capture counters used to keep continuous previews bounded. */
struct ModelingHostPublicationWork final {
  std::size_t sdk_participant_records = 0;
  std::size_t asset_records = 0;
  std::size_t scene_entity_records = 0;
  std::size_t hierarchy_records = 0;
  std::size_t binding_records = 0;
  bool full_sdk_snapshot = false;
};

/** Inputs for one participant-scoped native modeling projection checkpoint. */
struct ModelingHostProjectionCheckpointOptions final {
  bool capture_assets = true;
  bool capture_scene = true;
  bool capture_bindings = true;
  /**
   * Retains only the participant mutation counters acknowledged by a
   * long-lived publication owner. The projection checkpoint remains the sole
   * rollback payload.
   */
  bool capture_mutation_fence = false;
  /**
   * Restores enrolled preview participants while retaining unrelated owner
   * mutations and allocator progress. Requires capture_mutation_fence.
   */
  bool preserve_concurrent_owner_changes = false;
  SceneModelingProjectionDomain scene_domains =
      SceneModelingProjectionDomain::MeshBindings |
      SceneModelingProjectionDomain::CurveBindings;
  std::span<const EntityId> scene_entities = {};
  std::span<const AssetId> asset_ids = {};
  std::span<const int> modeling_object_ids = {};
};

/** Chooses exact compensation or monotonic participant restoration. */
enum class ModelingHostProjectionRestoreMode {
  PreferExact,
  PreserveConcurrent,
};

/**
 * Type-erased adapter-local rollback with a compile-time no-throw contract.
 *
 * Owner checkpoints are preflighted before mutation. The remaining adapter
 * cursors therefore restore last and must be an allocation-free move/swap.
 */
class ModelingHostLocalPublicationCheckpoint final {
public:
  ModelingHostLocalPublicationCheckpoint() = default;

  template <typename Restore>
    requires(!std::same_as<std::remove_cvref_t<Restore>,
                           ModelingHostLocalPublicationCheckpoint> &&
             std::is_nothrow_invocable_v<Restore &>)
  ModelingHostLocalPublicationCheckpoint(Restore &&restore)
      : restore_([callable = std::forward<Restore>(restore)]() mutable noexcept {
          callable();
        }) {}

  [[nodiscard]] explicit operator bool() const noexcept {
    return static_cast<bool>(restore_);
  }

  void restore() noexcept {
    if (restore_) {
      restore_();
      restore_ = nullptr;
    }
  }

private:
  std::function<void()> restore_;
};

/**
 * One opaque participant-scoped checkpoint shared by short publications and
 * long-lived SDK preview baselines. Owner restore order and exact allocation
 * leases live here rather than in adapter-specific compensation code.
 */
class ModelingHostProjectionCheckpoint final {
public:
  ModelingHostProjectionCheckpoint() = default;
  ModelingHostProjectionCheckpoint(
      SceneDocument &scene, AssetRegistry &assets,
      ModelingObjectBindingRegistry *bindings,
      ModelingHostProjectionCheckpointOptions options);

  ModelingHostProjectionCheckpoint(
      const ModelingHostProjectionCheckpoint &) = delete;
  ModelingHostProjectionCheckpoint &operator=(
      const ModelingHostProjectionCheckpoint &) = delete;
  ModelingHostProjectionCheckpoint(
      ModelingHostProjectionCheckpoint &&other) noexcept;
  ModelingHostProjectionCheckpoint &operator=(
      ModelingHostProjectionCheckpoint &&other) noexcept;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] const ModelingHostPublicationWork &work() const noexcept;
  /** True when no enrolled owner changed since the last acknowledgement. */
  [[nodiscard]] bool mutations_match_acknowledged_fence() const;
  /**
   * Advances a retained counter-only fence after this publication's own
   * mutations and optionally enrolls its newly created participants.
   */
  [[nodiscard]] bool acknowledge_current_mutations(
      std::span<const EntityId> newly_created_entities = {},
      std::span<const AssetId> newly_created_assets = {},
      std::span<const int> newly_created_modeling_object_ids = {});
  [[nodiscard]] bool can_restore(
      std::span<const EntityId> created_entities = {},
      std::span<const AssetId> created_assets = {},
      ModelingHostProjectionRestoreMode mode =
          ModelingHostProjectionRestoreMode::PreferExact) const;
  [[nodiscard]] bool restore(
      std::span<const EntityId> created_entities = {},
      std::span<const AssetId> created_assets = {},
      ModelingHostProjectionRestoreMode mode =
          ModelingHostProjectionRestoreMode::PreferExact) noexcept;

private:
  SceneDocument *scene_ = nullptr;
  AssetRegistry *assets_ = nullptr;
  ModelingObjectBindingRegistry *bindings_ = nullptr;
  bool captures_scene_ = false;
  bool captures_assets_ = false;
  bool captures_bindings_ = false;
  bool captures_mutation_fence_ = false;
  bool preserves_concurrent_owner_changes_ = false;
  std::optional<SceneModelingProjectionCheckpoint> scene_checkpoint_;
  std::optional<ModelingAssetProjectionCheckpoint> asset_checkpoint_;
  std::optional<ModelingObjectBindingProjectionCheckpoint>
      binding_checkpoint_;
  std::optional<SceneModelingProjectionMutationFence> scene_mutation_fence_;
  std::optional<ModelingAssetProjectionMutationFence> asset_mutation_fence_;
  std::optional<ModelingObjectBindingMutationFence> binding_mutation_fence_;
  bool valid_ = false;
  ModelingHostPublicationWork work_;
};

/**
 * Owns the rollback boundary for one SDK mutation and its native publication.
 *
 * Destruction rolls back unless commit() was called. The registry checkpoint
 * includes SDK/native, native/entity and tombstone identities; callers provide
 * one narrow callback only for adapter-local cursors and revision counters.
 */
class ModelingHostPublicationTransaction final {
public:
  using RestoreHostState = ModelingHostLocalPublicationCheckpoint;

  ModelingHostPublicationTransaction(
      SceneDocument &scene, AssetRegistry &assets,
      ModelingObjectBindingRegistry &bindings,
      modeling::ModelingSession &session,
      RestoreHostState restore_host_state,
      ModelingHostPublicationTransactionOptions options);
  /** Creates the same publication boundary before an SDK session exists. */
  ModelingHostPublicationTransaction(
      SceneDocument &scene, AssetRegistry &assets,
      RestoreHostState restore_host_state,
      ModelingHostPublicationTransactionOptions options);
  ~ModelingHostPublicationTransaction();

  ModelingHostPublicationTransaction(
      const ModelingHostPublicationTransaction &) = delete;
  ModelingHostPublicationTransaction &operator=(
      const ModelingHostPublicationTransaction &) = delete;
  ModelingHostPublicationTransaction(
      ModelingHostPublicationTransaction &&) = delete;
  ModelingHostPublicationTransaction &operator=(
      ModelingHostPublicationTransaction &&) = delete;

  [[nodiscard]] std::size_t sdk_undo_mark() const noexcept;
  [[nodiscard]] bool active() const noexcept;
  [[nodiscard]] bool ready() const noexcept;
  [[nodiscard]] const ModelingHostPublicationWork &work() const noexcept;

  /// Reports the existing owner lease so idle maintenance cannot invalidate
  /// rollback marks during native publication.
  [[nodiscard]] static bool
  is_session_publishing(const modeling::ModelingSession &session) noexcept;

  void track_created_entity(EntityId entity);
  void track_created_entities(std::span<const EntityId> entities);
  void track_created_asset(AssetId asset);
  void track_created_assets(std::span<const AssetId> assets);
  void track_started_preview(modeling::PreviewId preview_id);
  [[nodiscard]] bool rollback() noexcept;
  [[nodiscard]] bool commit() noexcept;

private:
  ModelingHostPublicationTransaction(
      SceneDocument &scene, AssetRegistry &assets,
      ModelingObjectBindingRegistry *bindings,
      modeling::ModelingSession *session,
      RestoreHostState restore_host_state,
      ModelingHostPublicationTransactionOptions options);
  void release_nesting_guard() noexcept;

  SceneDocument &scene_;
  AssetRegistry &assets_;
  ModelingObjectBindingRegistry *bindings_ = nullptr;
  modeling::ModelingSession *session_ = nullptr;
  RestoreHostState restore_host_state_;
  ModelingHostPublicationTransactionOptions options_;
  std::vector<EntityId> affected_scene_entities_;
  std::vector<AssetId> affected_asset_ids_;
  std::vector<int> affected_modeling_object_ids_;
  std::vector<modeling::ObjectId> affected_sdk_object_ids_;
  std::size_t sdk_undo_mark_ = 0;
  std::optional<modeling::ModelingSession> sdk_snapshot_;
  std::optional<modeling::SessionPreviewCheckpoint> preview_checkpoint_;
  std::optional<modeling::SessionMutationCheckpoint> mutation_checkpoint_;
  std::optional<modeling::PreviewId> started_preview_;
  std::optional<ModelingHostProjectionCheckpoint> projection_checkpoint_;
  std::vector<EntityId> created_entities_;
  std::vector<AssetId> created_assets_;
  bool active_ = true;
  bool ready_ = true;
  bool rollback_succeeded_ = false;
  bool owns_nesting_guard_ = false;
  ModelingHostPublicationWork work_;
};

} // namespace quader
