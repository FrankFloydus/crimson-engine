////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "scene/document/scene_entity_state.hpp"

#include <quader/modeling/ids.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

class SceneDocument;
class ModelingObjectBindingRegistry;

/** Canonical scene-owned state retained as modeling-object tombstone history. */
struct ModelingObjectSceneIdentity : SceneEntityState {};

/** Exact bounded work performed while capturing binding participants. */
struct ModelingObjectBindingCheckpointWork final {
  std::size_t participant_reference_count = 0u;
  std::size_t record_count = 0u;
  std::size_t native_state_lookup_count = 0u;
  std::size_t entity_edge_record_count = 0u;
  std::size_t sdk_edge_record_count = 0u;
  std::size_t tombstone_record_count = 0u;
};

/** Exact registry state for a bounded set of native modeling identities. */
class ModelingObjectBindingProjectionCheckpoint final {
public:
  ModelingObjectBindingProjectionCheckpoint() = default;
  [[nodiscard]] bool valid() const noexcept { return valid_; }
  [[nodiscard]] std::size_t size() const noexcept { return records_.size(); }
  [[nodiscard]] const ModelingObjectBindingCheckpointWork &
  work() const noexcept {
    return work_;
  }

private:
  friend class ModelingObjectBindingRegistry;

  /** Captures every native, SDK, and scene identity enrolled for one modeling object. */
  struct Record final {
    int modeling_object_id = kInvalidModelingObjectId;
    std::optional<EntityId> entity;
    std::optional<modeling::ObjectId> sdk_object;
    std::optional<ModelingObjectSceneIdentity> scene_identity;
    std::uint64_t mutation_count = 0u;
  };

  bool valid_ = false;
  const ModelingObjectBindingRegistry *owner_ = nullptr;
  std::uint64_t revision_ = 0;
  std::vector<Record> records_;
  ModelingObjectBindingCheckpointWork work_;
};

/** Bounded work performed by a counter-only binding mutation fence. */
struct ModelingObjectBindingMutationFenceWork final {
  std::size_t input_reference_count = 0u;
  std::size_t participant_record_count = 0u;
  std::size_t mutation_counter_lookup_count = 0u;
};

/** Opaque acknowledged fence for native binding mutation counters. */
class ModelingObjectBindingMutationFence final {
public:
  ModelingObjectBindingMutationFence();
  ~ModelingObjectBindingMutationFence();
  ModelingObjectBindingMutationFence(
      ModelingObjectBindingMutationFence &&) noexcept;
  ModelingObjectBindingMutationFence &
  operator=(ModelingObjectBindingMutationFence &&) noexcept;
  ModelingObjectBindingMutationFence(
      const ModelingObjectBindingMutationFence &) = delete;
  ModelingObjectBindingMutationFence &
  operator=(const ModelingObjectBindingMutationFence &) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const ModelingObjectBindingMutationFenceWork &
  work() const noexcept {
    return work_;
  }
  [[nodiscard]] bool matches_current(
      const ModelingObjectBindingRegistry &owner,
      ModelingObjectBindingMutationFenceWork *work = nullptr) const;
  [[nodiscard]] bool acknowledge_current(
      const ModelingObjectBindingRegistry &owner,
      std::span<const int> newly_created_modeling_object_ids = {},
      ModelingObjectBindingMutationFenceWork *work = nullptr);

private:
  friend class ModelingObjectBindingRegistry;
  struct State;

  std::unique_ptr<State> state_;
  ModelingObjectBindingMutationFenceWork work_;
};

/**
 * Owns every live identity edge for one native modeling workspace.
 *
 * The legacy integer ID remains the durable editor/public identifier. SDK IDs
 * are generational runtime identities and scene entities are native document
 * identities. Keeping both bidirectional relationships here prevents the SDK
 * projection adapter and scene services from maintaining competing maps.
 * Durable scene identities survive live unbinding as tombstones for history.
 */
class ModelingObjectBindingRegistry {
public:
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const;
  [[nodiscard]] std::uint64_t revision() const;

  [[nodiscard]] std::optional<EntityId>
  entity_for_modeling_object(int modeling_object_id) const;
  [[nodiscard]] std::optional<int>
  modeling_object_for_entity(EntityId entity) const;
  [[nodiscard]] std::vector<std::pair<int, EntityId>>
  object_entity_bindings_by_modeling_object() const;

  [[nodiscard]] std::optional<modeling::ObjectId>
  sdk_object_for_modeling_object(int modeling_object_id) const;
  [[nodiscard]] std::optional<int>
  modeling_object_for_sdk_object(modeling::ObjectId object) const;
  [[nodiscard]] const std::map<int, modeling::ObjectId> &
  sdk_object_bindings_by_modeling_object() const;

  [[nodiscard]] std::optional<ModelingObjectSceneIdentity>
  scene_identity_for_modeling_object(int modeling_object_id) const;
  [[nodiscard]] std::unordered_map<int, ModelingObjectSceneIdentity>
  capture_scene_identities_for_modeling_objects(
      const SceneDocument &scene,
      const std::unordered_set<int> &modeling_object_ids) const;
  [[nodiscard]] std::vector<ModelingObjectSceneIdentity>
  capture_scene_identities_for_modeling_objects(
      const SceneDocument &scene,
      std::span<const int> modeling_object_ids) const;
  [[nodiscard]] std::vector<ModelingObjectRecord>
  records(const SceneDocument &scene) const;

  [[nodiscard]] ModelingObjectBindingProjectionCheckpoint
  capture_projection_checkpoint(
      std::span<const int> modeling_object_ids) const;
  [[nodiscard]] ModelingObjectBindingMutationFence capture_mutation_fence(
      std::span<const int> modeling_object_ids) const;
  /** Performs the complete bounded restore preflight without mutation. */
  [[nodiscard]] bool can_restore_projection_checkpoint(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint) const;
  [[nodiscard]] bool
  can_restore_projection_checkpoint_preserving_concurrent_changes(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint,
      const ModelingObjectBindingMutationFence &acknowledged_fence) const;
  [[nodiscard]] bool restore_projection_checkpoint(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint);
  [[nodiscard]] bool
  restore_projection_checkpoint_preserving_concurrent_changes(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint,
      const ModelingObjectBindingMutationFence &acknowledged_fence);

  /**
   * Binds a free entity endpoint or rebinds this native identity.
   * A foreign endpoint owner is a collision and leaves the registry unchanged.
   */
  [[nodiscard]] bool bind(int modeling_object_id, EntityId entity);
  /** SDK equivalent of bind(); ObjectId generation participates in identity. */
  [[nodiscard]] bool bind_sdk_object(int modeling_object_id,
                                     modeling::ObjectId object);
  void remember_scene_entity(int modeling_object_id,
                             const SceneDocument &scene, EntityId entity);
  void restore_scene_identities(
      std::unordered_map<int, ModelingObjectSceneIdentity> identities);
  [[nodiscard]] bool restore_sdk_object_bindings(
      std::map<int, modeling::ObjectId> bindings);
  void unbind_object(int modeling_object_id);
  void unbind_sdk_object(int modeling_object_id);
  void clear_sdk_object_bindings();
  void clear();

private:
  friend class ModelingObjectBindingMutationFence;
  [[nodiscard]] bool can_restore_projection_checkpoint_impl(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint,
      const ModelingObjectBindingMutationFence *acknowledged_fence,
      bool preserve_concurrent_changes) const;
  [[nodiscard]] bool restore_projection_checkpoint_impl(
      const ModelingObjectBindingProjectionCheckpoint &checkpoint,
      const ModelingObjectBindingMutationFence *acknowledged_fence,
      bool preserve_concurrent_changes);
  void mark_object_mutated(int modeling_object_id);

  std::unordered_map<int, EntityId> entities_by_object_;
  std::unordered_map<EntityId, int> objects_by_entity_;
  std::map<int, modeling::ObjectId> sdk_objects_by_object_;
  std::unordered_map<std::uint64_t, int> objects_by_sdk_object_;
  std::unordered_map<int, ModelingObjectSceneIdentity>
      scene_identity_by_object_;
  std::unordered_map<int, std::uint64_t> mutation_count_by_object_;
  std::uint64_t revision_ = 0;
};

} // namespace quader
