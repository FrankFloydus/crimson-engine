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
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

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
 * Stores the exact scene counters and selection visible before one staged
 * modeling projection.
 *
 * Component edits on pre-existing entities must be compensated by their
 * owning adapter before restore. The scene owner removes newly published
 * entities and restores allocation/revision/selection state exactly.
 */
class SceneProjectionCheckpoint final {
public:
  SceneProjectionCheckpoint() = default;
  [[nodiscard]] bool valid() const noexcept { return valid_; }

private:
  friend class SceneDocument;

  bool valid_ = false;
  EntityId next_id_ = 1;
  EditorObjectId next_editor_object_id_ = 1;
  SceneLayerId next_layer_id_ = 1;
  EntityId active_selection_ = kInvalidEntityId;
  std::vector<EntityId> selected_entities_;
  std::uint64_t revision_ = 0;
  std::uint64_t content_revision_ = 0;
  std::uint64_t selection_revision_ = 0;
  std::uint64_t hierarchy_presentation_revision_ = 0;
  std::uint64_t visibility_revision_ = 0;
  std::uint64_t transform_revision_ = 0;
  std::uint64_t mesh_binding_revision_ = 0;
  std::uint64_t render_component_revision_ = 0;
  std::uint64_t layer_revision_ = 0;
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
    bool set_instance_definition(EntityId id);
    bool clear_instance_definition(EntityId id);
    bool set_instance_occurrence(EntityId id,
                                 InstanceOccurrenceComponent occurrence);
    bool clear_instance_occurrence(EntityId id);
    bool set_mesh(EntityId id, MeshComponent mesh);
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
    bool set_light(EntityId id, LightComponent light);
    bool set_scene_settings(EntityId id, SceneSettingsComponent settings);

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
    const MaterialComponent* material(EntityId id) const;
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
    EntityId scene_root() const;
    bool is_scene_root(EntityId id) const;
    std::vector<EntityId> root_entities() const;
    std::vector<EntityId> children(EntityId id) const;
    std::vector<EntityId> descendants(EntityId id,
                                      bool include_self = true) const;
    std::vector<EntityId> entity_tree_order() const;
    /**
     * Computes world transforms, inherited visibility/locking, and instance
     * containment in one top-down scene traversal.
     */
    [[nodiscard]] SceneHierarchyProjectionSnapshot
    hierarchy_projection_snapshot(
        SceneHierarchyProjectionWorkStats *work_stats = nullptr) const;
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
    std::uint64_t mesh_binding_revision() const {
      return mesh_binding_revision_;
    }
    std::uint64_t render_component_revision() const {
      return render_component_revision_;
    }
    std::uint64_t layer_revision() const { return layer_revision_; }

    /** Captures scene state needed to roll back one staged host projection. */
    [[nodiscard]] SceneProjectionCheckpoint
    capture_projection_checkpoint() const;
    /**
     * Removes entities created by a failed projection and restores the exact
     * allocation, selection, and revision state captured before it.
     */
    [[nodiscard]] bool restore_projection_checkpoint(
        const SceneProjectionCheckpoint &checkpoint,
        std::span<const EntityId> created_entities = {});

private:
  NameComponent *mutable_name(EntityId id);
  TransformComponent *mutable_transform(EntityId id);
  EntityKindComponent *mutable_entity_kind(EntityId id);
  HierarchyComponent *mutable_hierarchy(EntityId id);
  VisibilityComponent *mutable_visibility(EntityId id);
  LockComponent *mutable_lock(EntityId id);
  SelectionComponent *mutable_selection(EntityId id);

  void mark_dirty();
  void mark_transient_dirty();
  EditorObjectId allocate_editor_object_id(EditorObjectId requested);
  entt::entity require_entity(EntityId id) const;
  bool is_selected(EntityId id) const;
  EntityId first_selected_entity() const;
  EntityRecord make_entity_record(EntityId id, int depth,
                                  EntityId active,
                                  const SceneHierarchyProjectionRecord *
                                      projection = nullptr) const;

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
  std::uint64_t mesh_binding_revision_ = 0;
  std::uint64_t render_component_revision_ = 0;
  std::uint64_t layer_revision_ = 0;
  EditorObjectIdAllocator editor_object_id_allocator_;
  entt::registry registry_;
  std::unordered_map<EntityId, entt::entity> entities_;
  std::unordered_map<std::uint32_t, EntityId> ids_by_entity_;
  std::unordered_map<std::string, EntityId> entity_by_uuid_;
  std::unordered_map<EditorObjectId, EntityId> entity_by_editor_object_id_;
  std::unordered_map<SceneLayerId, SceneLayerRecord> layers_;
};

} // namespace quader
