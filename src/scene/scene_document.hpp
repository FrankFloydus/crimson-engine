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
};

/**
 * Stores a scene transform assignment used by batched preview updates.
 */
struct SceneTransformAssignment {
  EntityId entity = kInvalidEntityId;
  TransformComponent transform;
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
    void clear_selection();
    bool set_parent(EntityId child, EntityId parent,
                    bool preserve_world_transform = true);

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
    bool set_material(EntityId id, MaterialComponent material);
    bool clear_material(EntityId id);
    bool set_light(EntityId id, LightComponent light);
    bool set_scene_settings(EntityId id, SceneSettingsComponent settings);

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

    std::vector<EntityId> entities() const;
    EntityId scene_root() const;
    bool is_scene_root(EntityId id) const;
    std::vector<EntityId> root_entities() const;
    std::vector<EntityId> children(EntityId id) const;
    std::vector<EntityId> descendants(EntityId id,
                                      bool include_self = true) const;
    std::vector<EntityId> entity_tree_order() const;
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
    std::uint64_t visibility_revision() const { return visibility_revision_; }
    std::uint64_t transform_revision() const { return transform_revision_; }
    std::uint64_t mesh_binding_revision() const {
      return mesh_binding_revision_;
    }
    std::uint64_t render_component_revision() const {
      return render_component_revision_;
    }

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
                                  EntityId active) const;
  void append_tree_order(EntityId id, std::vector<EntityId> &ids) const;
  void append_entity_records(EntityId id, int depth, EntityId active,
                             std::vector<EntityRecord> &records) const;

  EntityId next_id_ = 1;
  EditorObjectId next_editor_object_id_ = 1;
  EntityId active_selection_ = kInvalidEntityId;
  std::uint64_t revision_ = 0;
  std::uint64_t content_revision_ = 0;
  std::uint64_t selection_revision_ = 0;
  std::uint64_t visibility_revision_ = 0;
  std::uint64_t transform_revision_ = 0;
  std::uint64_t mesh_binding_revision_ = 0;
  std::uint64_t render_component_revision_ = 0;
  EditorObjectIdAllocator editor_object_id_allocator_;
  entt::registry registry_;
  std::unordered_map<EntityId, entt::entity> entities_;
  std::unordered_map<std::uint32_t, EntityId> ids_by_entity_;
  std::unordered_map<std::string, EntityId> entity_by_uuid_;
  std::unordered_map<EditorObjectId, EntityId> entity_by_editor_object_id_;
};

} // namespace quader
