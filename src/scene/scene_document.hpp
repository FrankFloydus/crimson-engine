////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "scene/components.hpp"

#include <entt/entt.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

class SceneDocument;
class SceneMeshOccurrenceProjectionSnapshot;

/**
 * Stores the Entity Record data contract used by the scene graph document layer.
 */
struct EntityRecord {
  EntityId id = kInvalidEntityId;
  std::string uuid;
  EditorObjectId editor_object_id = kInvalidEditorObjectId;
  std::string name;
  EntityKind kind = EntityKind::Generic;
  EntityId parent = kInvalidEntityId;
  int depth = 0;
  bool visible = true;
  bool effectively_visible = true;
  bool locked = false;
  bool effectively_locked = false;
  bool selected = false;
  bool active = false;
  bool has_children = false;
  bool has_mesh = false;
  bool has_curve = false;
  bool has_light = false;
  bool is_scene_root = false;
  bool has_scene_settings = false;
  bool has_modeling_object = false;
  bool is_instance_definition = false;
  bool has_instance_occurrence = false;
  SceneLayerId layer_id = kInvalidSceneLayerId;
};

/**
 * Stores a scene transform assignment used by batched preview updates.
 */
struct SceneTransformAssignment {
  EntityId entity = kInvalidEntityId;
  TransformComponent transform;
};

/**
 * Assigns an occurrence's exact optional texture mapping during an atomic update.
 * An absent mapping removes the component and restores identity sampling.
 */
struct SceneTextureMappingAssignment {
  EntityId entity = kInvalidEntityId;
  std::optional<TextureMappingComponent> mapping;
};

/**
 * Stores one exact parent link used by atomic scene hierarchy replacement.
 */
struct SceneParentAssignment {
  EntityId child = kInvalidEntityId;
  EntityId parent = kInvalidEntityId;
};

/**
 * Stores scene-derived hierarchy state for one entity.
 *
 * Records are emitted in exact scene tree order, so a parent always precedes
 * its children and sibling order matches the authored hierarchy.
 */
struct SceneHierarchyProjectionRecord {
  EntityId entity = kInvalidEntityId;
  EntityId parent = kInvalidEntityId;
  EntityId containing_instance_definition = kInvalidEntityId;
  std::size_t depth = 0u;
  QMat4 world_transform;
  bool effectively_visible = true;
  bool effectively_locked = false;
};

/**
 * Counts deterministic work performed by one hierarchy projection.
 */
struct SceneHierarchyProjectionWorkStats {
  std::uint64_t entity_visit_count = 0u;
  std::uint64_t parent_state_lookup_count = 0u;
  std::uint64_t world_transform_multiply_count = 0u;
};

/** Monotonic work counters for SceneDocument-owned spatial projections. */
struct SceneSpatialProjectionCacheStats final {
  std::uint64_t hierarchy_build_count = 0u;
  std::uint64_t hierarchy_cache_hit_count = 0u;
  std::uint64_t mesh_occurrence_build_count = 0u;
  std::uint64_t mesh_occurrence_cache_hit_count = 0u;
};

/** Scene-owned domains that one modeling host publication may mutate. */
enum class SceneModelingProjectionDomain : std::uint8_t {
  None = 0u,
  MeshBindings = 1u << 0u,
  CurveBindings = 1u << 1u,
  Selection = 1u << 2u,
  Structure = 1u << 3u,
};

[[nodiscard]] constexpr SceneModelingProjectionDomain
operator|(SceneModelingProjectionDomain left,
          SceneModelingProjectionDomain right) noexcept {
  return static_cast<SceneModelingProjectionDomain>(
      static_cast<std::uint8_t>(left) |
      static_cast<std::uint8_t>(right));
}

[[nodiscard]] constexpr SceneModelingProjectionDomain
operator&(SceneModelingProjectionDomain left,
          SceneModelingProjectionDomain right) noexcept {
  return static_cast<SceneModelingProjectionDomain>(
      static_cast<std::uint8_t>(left) &
      static_cast<std::uint8_t>(right));
}

/** Exact bounded work performed while capturing a modeling projection. */
struct SceneModelingProjectionCheckpointWork final {
  std::size_t participant_reference_count = 0u;
  std::size_t participant_record_count = 0u;
  std::size_t live_entity_lookup_count = 0u;
  std::size_t live_entity_record_count = 0u;
  std::size_t mesh_binding_record_count = 0u;
  std::size_t curve_binding_record_count = 0u;
  std::size_t selection_record_count = 0u;
  std::size_t structure_record_count = 0u;
};

/** Exact bounded work performed while restoring a modeling projection. */
struct SceneModelingProjectionRestoreWork final {
  std::size_t participant_record_count = 0u;
  std::size_t entity_lookup_count = 0u;
  std::size_t created_entity_removal_count = 0u;
  std::size_t recreated_entity_count = 0u;
  std::size_t mesh_binding_write_count = 0u;
  std::size_t curve_binding_write_count = 0u;
  std::size_t selection_write_count = 0u;
  std::size_t structure_write_count = 0u;
  std::size_t boundary_hierarchy_write_count = 0u;
};

/** Truthful work for one owner-indexed binding reference-count query. */
struct SceneBindingReferenceCountWork final {
  std::size_t owner_index_lookup_count = 0u;
};

/**
 * Opaque scene-owner checkpoint for one bounded modeling publication.
 *
 * Allocation and scalar revision state are always captured. Selection and
 * structural state are included only when requested. A structural checkpoint
 * is an exclusive allocation lease: every successful entity allocation until
 * restore must be reported as a created participant, otherwise restore is
 * rejected before any scene mutation.
 */
class SceneModelingProjectionCheckpoint final {
public:
  SceneModelingProjectionCheckpoint() = default;
  [[nodiscard]] bool valid() const noexcept { return state_ != nullptr; }
  [[nodiscard]] const SceneModelingProjectionCheckpointWork &
  work() const noexcept {
    return work_;
  }

private:
  friend class SceneDocument;
  struct State;

  std::shared_ptr<const State> state_;
  SceneModelingProjectionCheckpointWork work_;
};

/** Bounded work performed by a counter-only modeling mutation fence. */
struct SceneModelingProjectionMutationFenceWork final {
  std::size_t input_reference_count = 0u;
  std::size_t participant_record_count = 0u;
  std::size_t mutation_counter_lookup_count = 0u;
};

/**
 * Opaque acknowledged fence for bounded participant/domain mutation counters.
 * It intentionally retains no component or payload state.
 */
class SceneModelingProjectionMutationFence final {
public:
  SceneModelingProjectionMutationFence();
  ~SceneModelingProjectionMutationFence();
  SceneModelingProjectionMutationFence(
      SceneModelingProjectionMutationFence &&) noexcept;
  SceneModelingProjectionMutationFence &
  operator=(SceneModelingProjectionMutationFence &&) noexcept;
  SceneModelingProjectionMutationFence(
      const SceneModelingProjectionMutationFence &) = delete;
  SceneModelingProjectionMutationFence &
  operator=(const SceneModelingProjectionMutationFence &) = delete;

  [[nodiscard]] bool valid() const noexcept;
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const SceneModelingProjectionMutationFenceWork &
  work() const noexcept {
    return work_;
  }
  [[nodiscard]] bool matches_current(
      const SceneDocument &owner,
      SceneModelingProjectionMutationFenceWork *work = nullptr) const;
  [[nodiscard]] bool acknowledge_current(
      const SceneDocument &owner,
      std::span<const EntityId> newly_created_participants = {},
      SceneModelingProjectionMutationFenceWork *work = nullptr);

private:
  friend class SceneDocument;
  struct State;

  std::shared_ptr<State> state_;
  SceneModelingProjectionMutationFenceWork work_;
};

/**
 * Move-only capability for a LIFO Scene scalar/allocation compensation scope.
 * The checkpoint retains no entity, component, selection, or layer payload.
 */
class SceneScalarCompensationCheckpoint final {
public:
  SceneScalarCompensationCheckpoint();
  ~SceneScalarCompensationCheckpoint();
  SceneScalarCompensationCheckpoint(
      SceneScalarCompensationCheckpoint &&) noexcept;
  SceneScalarCompensationCheckpoint &
  operator=(SceneScalarCompensationCheckpoint &&) noexcept;
  SceneScalarCompensationCheckpoint(
      const SceneScalarCompensationCheckpoint &) = delete;
  SceneScalarCompensationCheckpoint &
  operator=(const SceneScalarCompensationCheckpoint &) = delete;

  [[nodiscard]] bool valid() const noexcept;

private:
  friend class SceneDocument;
  struct State;

  void reset() noexcept;
  std::shared_ptr<State> state_;
};

/**
 * Owns one immutable, indexed projection of scene-derived hierarchy state.
 */
class SceneHierarchyProjectionSnapshot final {
public:
  [[nodiscard]] std::span<const SceneHierarchyProjectionRecord>
  records() const noexcept {
    return records_;
  }
  [[nodiscard]] const SceneHierarchyProjectionRecord *
  find(EntityId entity) const noexcept;

private:
  friend class SceneDocument;

  std::vector<SceneHierarchyProjectionRecord> records_;
  std::unordered_map<EntityId, std::size_t> index_by_entity_;
};

/**
 * Owns Scene Document behavior within the scene graph document layer.
 */
class SceneDocument {
public:
    using EditorObjectIdAllocator = std::function<EditorObjectId(EditorObjectId requested)>;

    void set_editor_object_id_allocator(EditorObjectIdAllocator allocator);

    EntityId
    create_entity(std::string name, EntityKind kind = EntityKind::Generic,
                  EditorObjectId editor_object_id = kInvalidEditorObjectId);
    EntityId create_entity_with_id(
        EntityId id, std::string name, EntityKind kind = EntityKind::Generic,
        EditorObjectId editor_object_id = kInvalidEditorObjectId);
    EntityId
    create_empty(std::string name,
                 EditorObjectId editor_object_id = kInvalidEditorObjectId);
    bool destroy_entity(EntityId id);
    bool destroy_entities(std::span<const EntityId> ids);
    bool exists(EntityId id) const;

    bool rename_entity(EntityId id, std::string name);
    bool set_entity_uuid(EntityId id, std::string uuid);
    bool set_editor_object_id(EntityId id, EditorObjectId editor_object_id);
    bool set_entity_kind(EntityId id, EntityKind kind);
    bool set_visible(EntityId id, bool visible);
    bool set_selectable(EntityId id, bool selectable);
    bool set_locked(EntityId id, bool locked);
    bool set_selected(EntityId id, bool selected);
    bool set_active_selection(EntityId id);
    /**
     * Replaces the complete selection in one publication. Missing entities
     * and duplicates are ignored; an invalid requested active entity falls
     * back to the last retained selection.
     */
    bool replace_selection(std::span<const EntityId> selected,
                           EntityId active = kInvalidEntityId);
    void clear_selection();
    bool set_parent(EntityId child, EntityId parent,
                    bool preserve_world_transform = true);
    /**
     * Moves an entity within its current parent's authored child order.
     * The ordinal must name a position in the current child list.
     */
    bool set_sibling_ordinal(EntityId entity, std::size_t ordinal);
    /**
     * Reparents a batch atomically while retaining each entity's current
     * local transform. This fast path is available before instance roles are
     * installed; callers that need world preservation precompute and publish
     * local transforms with set_transforms().
     */
    bool set_parents(std::span<const SceneParentAssignment> assignments);
    /**
     * Atomically replaces the complete hierarchy while retaining authored
     * local transforms. Every scene entity must appear exactly once.
     *
     * This storage-hydration path is accepted only before instance roles are
     * installed, validates the complete forest once, preserves assignment
     * order within each parent's child list, and advances hierarchy-dependent
     * revisions once.
     */
    bool replace_hierarchy(
        std::span<const SceneParentAssignment> assignments);

    bool set_transform(EntityId id, TransformComponent transform);
    void set_transforms(std::span<const SceneTransformAssignment> transforms);
    bool set_modeling_object(EntityId id, ModelingObjectComponent object);
    bool clear_modeling_object(EntityId id);
    bool set_instance_definition(EntityId id);
    bool clear_instance_definition(EntityId id);
    bool set_instance_occurrence(EntityId id,
                                 InstanceOccurrenceComponent occurrence);
    bool clear_instance_occurrence(EntityId id);
    bool set_mesh(EntityId id, MeshComponent mesh);
    bool clear_mesh(EntityId id);
    bool set_curve(EntityId id, CurveComponent curve);
    bool clear_curve(EntityId id);
    bool notify_curve_payload_changed(EntityId id);
    bool notify_mesh_payload_changed(EntityId id);
    /** Validates a batch without advancing any scene revision. */
    [[nodiscard]] bool can_notify_mesh_payloads_changed(
        std::span<const EntityId> ids) const;
    /**
     * Notifies one prevalidated mesh-payload publication and advances scene,
     * content, mesh-binding, and render-component revisions exactly once.
     */
    bool notify_mesh_payloads_changed(std::span<const EntityId> ids);
    bool set_material(EntityId id, MaterialComponent material);
    bool clear_material(EntityId id);
    bool set_texture_mapping(EntityId id, TextureMappingComponent mapping);
    bool clear_texture_mapping(EntityId id);
    /**
     * Validates and publishes placement and texture compensation together.
     * This is a data assignment, not a texture-lock policy hook.
     */
    bool set_transforms_and_texture_mappings(
        std::span<const SceneTransformAssignment> transforms,
        std::span<const SceneTextureMappingAssignment> mappings);
    bool set_light(EntityId id, LightComponent light);
    bool clear_light(EntityId id);
    bool set_scene_settings(EntityId id, SceneSettingsComponent settings);
    bool clear_scene_settings(EntityId id);

    SceneLayerId create_layer(
        std::string name = {},
        QVec3 wireframe_color = kDefaultSceneLayerWireframeColor);
    SceneLayerId create_layer_with_id(
        SceneLayerId id, std::string name, bool visible = true,
        QVec3 wireframe_color = kDefaultSceneLayerWireframeColor,
        bool locked = false);
    bool rename_layer(SceneLayerId id, std::string name);
    bool set_layer_visible(SceneLayerId id, bool visible);
    bool set_layer_wireframe_color(SceneLayerId id, QVec3 color);
    bool set_layer_locked(SceneLayerId id, bool locked);
    bool empty_layer(SceneLayerId id);
    bool delete_layer(SceneLayerId id);
    bool delete_layers(std::span<const SceneLayerId> ids);
    /** Sets one exact membership without recursively assigning descendants. */
    bool set_entity_layer(EntityId entity, SceneLayerId layer_id);
    /** Atomically replaces exact layer records and memberships. */
    bool replace_layer_state(SceneLayerState state);

    const NameComponent* name(EntityId id) const;
    const EntityUuidComponent *entity_uuid(EntityId id) const;
    const EditorObjectIdentityComponent *
    editor_object_identity(EntityId id) const;
    const TransformComponent* transform(EntityId id) const;
    const EntityKindComponent *entity_kind(EntityId id) const;
    const HierarchyComponent* hierarchy(EntityId id) const;
    const VisibilityComponent* visibility(EntityId id) const;
    const LockComponent* lock(EntityId id) const;
    const SelectionComponent* selection(EntityId id) const;
    const ModelingObjectComponent *modeling_object(EntityId id) const;
    const InstanceDefinitionComponent *instance_definition(EntityId id) const;
    const InstanceOccurrenceComponent *instance_occurrence(EntityId id) const;
    const MeshComponent* mesh(EntityId id) const;
    const CurveComponent *curve(EntityId id) const;
    [[nodiscard]] std::size_t mesh_binding_reference_count(
        AssetId mesh,
        SceneBindingReferenceCountWork *work = nullptr) const;
    [[nodiscard]] std::size_t curve_binding_reference_count(
        AssetId curve,
        SceneBindingReferenceCountWork *work = nullptr) const;
    const MaterialComponent* material(EntityId id) const;
    const TextureMappingComponent *texture_mapping(EntityId id) const;
    const LightComponent* light(EntityId id) const;
    const SceneSettingsComponent *scene_settings(EntityId id) const;
    const SceneLayerMembershipComponent *layer_membership(EntityId id) const;

    [[nodiscard]] const SceneLayerRecord *layer(SceneLayerId id) const;
    [[nodiscard]] std::vector<SceneLayerRecord> layers() const;
    [[nodiscard]] std::vector<EntityId> layer_members(SceneLayerId id) const;
    [[nodiscard]] SceneLayerId entity_layer(EntityId entity) const;
    [[nodiscard]] SceneLayerState layer_state() const;
    [[nodiscard]] SceneLayerId next_layer_id() const { return next_layer_id_; }
    [[nodiscard]] std::string next_layer_name() const;
    [[nodiscard]] bool is_layer_eligible(EntityId entity) const;
    /** Expands selected hierarchy roots and retains public layer-eligible nodes. */
    [[nodiscard]] std::vector<EntityId>
    layer_eligible_entities(std::span<const EntityId> roots) const;

    std::vector<EntityId> entities() const;
    /** Returns authored-curve entities without visiting unrelated scene nodes. */
    [[nodiscard]] std::vector<EntityId> curve_entities() const;
    EntityId scene_root() const;
    bool is_scene_root(EntityId id) const;
    std::vector<EntityId> root_entities() const;
    std::vector<EntityId> children(EntityId id) const;
    [[nodiscard]] std::optional<std::size_t>
    sibling_ordinal(EntityId entity) const;
    std::vector<EntityId> descendants(EntityId id,
                                      bool include_self = true) const;
    std::vector<EntityId> entity_tree_order() const;
    /**
     * Computes world transforms, inherited visibility/locking, and instance
     * containment in one top-down scene traversal.
     */
    [[nodiscard]] std::shared_ptr<const SceneHierarchyProjectionSnapshot>
    hierarchy_projection() const;
    /**
     * Returns the scene-owned concrete mesh placement projection.
     *
     * The immutable pointer remains valid after later scene mutations; a new
     * accessor call observes current scene facts. Asset and viewport policy
     * remain consumer-owned.
     */
    [[nodiscard]]
    std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
    mesh_occurrence_projection() const;
    /** Compatibility value snapshot; hot consumers should borrow above. */
    [[nodiscard]] SceneHierarchyProjectionSnapshot
    hierarchy_projection_snapshot(
        SceneHierarchyProjectionWorkStats *work_stats = nullptr) const;
    [[nodiscard]] SceneSpatialProjectionCacheStats
    spatial_projection_cache_stats() const noexcept {
      return spatial_projection_cache_stats_;
    }
    std::vector<EntityRecord> entity_records() const;
    std::vector<EntityRecord> entity_records_tree_order() const;
    /** Projects only canonical members while an instance definition is edited. */
    std::vector<EntityRecord>
    entity_records_for_instance_definition(EntityId definition) const;
    std::vector<EntityId> selected_entities() const;
    EntityId primary_selection() const;
    EntityId parent(EntityId id) const;
    std::string uuid(EntityId id) const;
    EntityId entity_for_uuid(std::string_view uuid) const;
    EditorObjectId editor_object_id(EntityId id) const;
    EntityId entity_for_editor_object_id(EditorObjectId editor_object_id) const;
    bool has_children(EntityId id) const;
    bool is_descendant_of(EntityId entity, EntityId possible_ancestor) const;
    bool is_instance_definition_entity(EntityId entity) const;
    EntityId containing_instance_definition(EntityId entity) const;
    bool is_instance_definition_member(EntityId entity) const;
    std::vector<EntityId>
    instance_occurrences_for_definition(EntityId definition) const;
    EntityKind kind(EntityId id) const;
    bool is_effectively_visible(EntityId id) const;
    bool is_effectively_locked(EntityId id) const;
    QMat4 local_transform_matrix(EntityId id) const;
    QMat4 world_transform_matrix(EntityId id) const;
    void advance_revision_for_replacement(std::uint64_t previous_revision);

    entt::entity entt_entity(EntityId id) const;
    EntityId entity_id(entt::entity entity) const;

    std::uint64_t revision() const { return revision_; }
    /** Revision of scene data that is persisted in a scene document. */
    std::uint64_t content_revision() const { return content_revision_; }
    std::uint64_t selection_revision() const { return selection_revision_; }
    /**
     * Revision of fields projected into the hierarchy presentation.
     *
     * Transform, material, mesh-payload, and other scene-content changes do
     * not advance this counter.
     */
    std::uint64_t hierarchy_presentation_revision() const {
      return hierarchy_presentation_revision_;
    }
    std::uint64_t visibility_revision() const { return visibility_revision_; }
    std::uint64_t transform_revision() const { return transform_revision_; }
    std::uint64_t texture_mapping_revision() const {
      return texture_mapping_revision_;
    }
    std::uint64_t mesh_binding_revision() const {
      return mesh_binding_revision_;
    }
    /// Revision of curve bindings and published authored curve contents.
    std::uint64_t curve_binding_revision() const {
      return curve_binding_revision_;
    }
    /// Revision of entity material bindings and material-surface eligibility.
    std::uint64_t material_binding_revision() const {
      return material_binding_revision_;
    }
    std::uint64_t render_component_revision() const {
      return render_component_revision_;
    }
    std::uint64_t layer_revision() const { return layer_revision_; }

    /**
     * Captures only the requested participants and modeling projection
     * domains. Scalar revisions and SceneDocument allocation high-water are
     * always retained. The participant span must contain the complete closure
     * of entities whose requested domains may change.
     */
    [[nodiscard]] SceneModelingProjectionCheckpoint
    capture_modeling_projection_checkpoint(
        std::span<const EntityId> participants,
        SceneModelingProjectionDomain domains) const;
    [[nodiscard]] SceneModelingProjectionMutationFence
    capture_modeling_projection_mutation_fence(
        std::span<const EntityId> participants,
        SceneModelingProjectionDomain domains) const;
    /** Performs the complete bounded restore preflight without mutation. */
    [[nodiscard]] bool can_restore_modeling_projection_checkpoint(
        const SceneModelingProjectionCheckpoint &checkpoint,
        std::span<const EntityId> created_participants = {}) const;
    /**
     * Preflights a bounded restore against an acknowledged participant fence
     * while permitting unrelated SceneDocument publications to survive.
     */
    [[nodiscard]] bool
    can_restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const SceneModelingProjectionCheckpoint &checkpoint,
        const SceneModelingProjectionMutationFence &acknowledged_fence,
        std::span<const EntityId> created_participants = {}) const;
    /**
     * Restores one bounded modeling projection without enumerating the scene.
     * `created_participants` must list every successful entity allocation made
     * after capture exactly once (duplicates are accepted and normalized).
     */
    [[nodiscard]] bool restore_modeling_projection_checkpoint(
        const SceneModelingProjectionCheckpoint &checkpoint,
        std::span<const EntityId> created_participants = {},
        SceneModelingProjectionRestoreWork *work = nullptr);
    /**
     * Restores only enrolled domains and participants, retains unrelated
     * allocator/revision progress, and publishes fresh owner revisions.
     */
    [[nodiscard]] bool
    restore_modeling_projection_checkpoint_preserving_concurrent_changes(
        const SceneModelingProjectionCheckpoint &checkpoint,
        const SceneModelingProjectionMutationFence &acknowledged_fence,
        std::span<const EntityId> created_participants = {},
        SceneModelingProjectionRestoreWork *work = nullptr);

    /** Begins a nestable strict-LIFO scalar/allocation compensation scope. */
    [[nodiscard]] SceneScalarCompensationCheckpoint
    capture_scalar_compensation_checkpoint();
    /** Pins the exact post-forward boundary before external publication. */
    [[nodiscard]] bool acknowledge_scalar_compensation_checkpoint(
        SceneScalarCompensationCheckpoint &checkpoint) noexcept;
    /** Validates the acknowledged boundary and enters semantic-inverse phase. */
    [[nodiscard]] bool begin_scalar_compensation_restore(
        SceneScalarCompensationCheckpoint &checkpoint) noexcept;
    /** Restores and consumes the current innermost compensation scope. */
    [[nodiscard]] bool restore_scalar_compensation_checkpoint(
        SceneScalarCompensationCheckpoint &checkpoint);
    /** Consumes the current innermost scope without restoring its baseline. */
    [[nodiscard]] bool discard_scalar_compensation_checkpoint(
        SceneScalarCompensationCheckpoint &checkpoint) noexcept;

private:
  friend class SceneModelingProjectionMutationFence;
  friend class SceneScalarCompensationCheckpoint;
  struct ModelingProjectionRestorePreparation;
  struct ModelingProjectionMutationCounters;

  [[nodiscard]] bool preflight_modeling_projection_restore(
      const SceneModelingProjectionCheckpoint &checkpoint,
      const SceneModelingProjectionMutationFence *acknowledged_fence,
      std::span<const EntityId> created_participants,
      bool preserve_concurrent_changes,
      ModelingProjectionRestorePreparation *preparation,
      SceneModelingProjectionRestoreWork *work) const;
  [[nodiscard]] bool restore_modeling_projection_checkpoint_impl(
      const SceneModelingProjectionCheckpoint &checkpoint,
      const SceneModelingProjectionMutationFence *acknowledged_fence,
      std::span<const EntityId> created_participants,
      bool preserve_concurrent_changes,
      SceneModelingProjectionRestoreWork *work);

  NameComponent *mutable_name(EntityId id);
  TransformComponent *mutable_transform(EntityId id);
  EntityKindComponent *mutable_entity_kind(EntityId id);
  HierarchyComponent *mutable_hierarchy(EntityId id);
  VisibilityComponent *mutable_visibility(EntityId id);
  LockComponent *mutable_lock(EntityId id);
  SelectionComponent *mutable_selection(EntityId id);

  void mark_dirty();
  void mark_transient_dirty();
  void mark_dirty(EntityId entity, SceneModelingProjectionDomain domain);
  void mark_dirty(std::span<const EntityId> entities,
                  SceneModelingProjectionDomain domain);
  void mark_transient_dirty(EntityId entity,
                            SceneModelingProjectionDomain domain);
  void mark_transient_dirty(std::span<const EntityId> entities,
                            SceneModelingProjectionDomain domain);
  EditorObjectId allocate_editor_object_id(EditorObjectId requested);
  entt::entity require_entity(EntityId id) const;
  bool is_selected(EntityId id) const;
  EntityId first_selected_entity() const;
  EntityRecord make_entity_record(EntityId id, int depth,
                                  EntityId active,
                                  const SceneHierarchyProjectionRecord *
                                      projection = nullptr) const;
  void invalidate_spatial_projection_caches_after_revision_restore();
  void note_modeling_projection_mutation(
      EntityId entity, SceneModelingProjectionDomain domain);
  void note_modeling_projection_mutations(
      std::span<const EntityId> entities,
      SceneModelingProjectionDomain domain);
  void note_unscoped_modeling_projection_mutation();
  void note_scalar_compensation_entity_allocation(
      EntityId entity, EditorObjectId editor_object_id,
      std::uint64_t allocation_serial,
      std::uint64_t previous_allocation_serial);
  void note_scalar_compensation_layer_allocation(
      SceneLayerId layer, std::uint64_t allocation_serial,
      std::uint64_t previous_allocation_serial);
  void note_scalar_compensation_mutation(
      EntityId entity, const ModelingProjectionMutationCounters &counters,
      bool counter_record_existed, SceneModelingProjectionDomain domain);
  [[nodiscard]] bool close_scalar_compensation_checkpoint(
      const SceneScalarCompensationCheckpoint::State &state) noexcept;
  void abandon_scalar_compensation_checkpoint(
      const std::shared_ptr<SceneScalarCompensationCheckpoint::State> &state)
      noexcept;
  void collapse_abandoned_scalar_compensation_checkpoints() noexcept;

  /** Captures the scene revisions that invalidate the hierarchy projection cache. */
  struct HierarchyProjectionCacheKey final {
    std::uint64_t restore_epoch = 0u;
    std::uint64_t hierarchy_presentation_revision = 0u;
    std::uint64_t transform_revision = 0u;

    [[nodiscard]] bool
    operator==(const HierarchyProjectionCacheKey &) const = default;
  };

  /** Captures the scene revisions that invalidate expanded mesh occurrences. */
  struct MeshOccurrenceProjectionCacheKey final {
    std::uint64_t restore_epoch = 0u;
    std::uint64_t hierarchy_build_count = 0u;
    std::uint64_t visibility_revision = 0u;
    std::uint64_t mesh_binding_revision = 0u;

    [[nodiscard]] bool
    operator==(const MeshOccurrenceProjectionCacheKey &) const = default;
  };

  EntityId next_id_ = 1;
  EditorObjectId next_editor_object_id_ = 1;
  SceneLayerId next_layer_id_ = 1;
  EntityId active_selection_ = kInvalidEntityId;
  std::uint64_t revision_ = 0;
  std::uint64_t content_revision_ = 0;
  std::uint64_t selection_revision_ = 0;
  std::uint64_t hierarchy_presentation_revision_ = 0;
  std::uint64_t visibility_revision_ = 0;
  std::uint64_t transform_revision_ = 0;
  std::uint64_t texture_mapping_revision_ = 0;
  std::uint64_t mesh_binding_revision_ = 0;
  std::uint64_t curve_binding_revision_ = 0;
  std::uint64_t material_binding_revision_ = 0;
  std::uint64_t render_component_revision_ = 0;
  std::uint64_t layer_revision_ = 0;
  EditorObjectIdAllocator editor_object_id_allocator_;
  entt::registry registry_;
  std::unordered_map<EntityId, entt::entity> entities_;
  std::unordered_map<std::uint32_t, EntityId> ids_by_entity_;
  std::unordered_map<std::string, EntityId> entity_by_uuid_;
  std::unordered_map<EditorObjectId, EntityId> entity_by_editor_object_id_;
  /** Tracks per-entity mutations in each scene-owned modeling projection domain. */
  struct ModelingProjectionMutationCounters final {
    std::uint64_t mesh_bindings = 0u;
    std::uint64_t curve_bindings = 0u;
    std::uint64_t selection = 0u;
    std::uint64_t structure = 0u;
  };
  /** Mutation state immediately before the latest allocation of an entity ID. */
  struct EntityAllocationMutationBaseline final {
    std::uint64_t allocation_serial = 0u;
    ModelingProjectionMutationCounters counters;
    bool record_existed = false;
  };
  std::unordered_map<EntityId, std::uint64_t>
      last_entity_allocation_serial_;
  std::unordered_map<EntityId, EntityAllocationMutationBaseline>
      last_entity_allocation_mutation_baselines_;
  std::unordered_map<AssetId, std::size_t> mesh_binding_reference_counts_;
  std::unordered_map<AssetId, std::size_t> curve_binding_reference_counts_;
  std::unordered_map<SceneLayerId, SceneLayerRecord> layers_;
  std::unordered_map<EntityId, ModelingProjectionMutationCounters>
      modeling_projection_mutations_by_entity_;
  std::uint64_t modeling_projection_mutation_touch_serial_ = 0u;
  std::uint64_t unscoped_modeling_projection_mutation_serial_ = 0u;
  // Monotonic successful-allocation serial used only to validate exclusive
  // structural rollback leases. Unlike the public ID allocator high-water it
  // deliberately never rewinds.
  std::uint64_t entity_allocation_serial_ = 0u;
  std::uint64_t layer_allocation_serial_ = 0u;
  std::unordered_map<SceneLayerId, std::uint64_t>
      last_layer_allocation_serial_;
  std::uint64_t next_scalar_compensation_token_ = 0u;
  std::vector<std::shared_ptr<SceneScalarCompensationCheckpoint::State>>
      scalar_compensation_stack_;
  std::uint64_t spatial_projection_restore_epoch_ = 0u;
  mutable std::optional<HierarchyProjectionCacheKey>
      hierarchy_projection_cache_key_;
  mutable std::shared_ptr<const SceneHierarchyProjectionSnapshot>
      hierarchy_projection_cache_;
  mutable SceneHierarchyProjectionWorkStats hierarchy_projection_cached_work_;
  mutable std::optional<MeshOccurrenceProjectionCacheKey>
      mesh_occurrence_projection_cache_key_;
  mutable std::shared_ptr<const SceneMeshOccurrenceProjectionSnapshot>
      mesh_occurrence_projection_cache_;
  mutable SceneSpatialProjectionCacheStats spatial_projection_cache_stats_;
};

} // namespace quader
