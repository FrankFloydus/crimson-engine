////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_document.hpp"

#include "scene/scene_transform.hpp"

#include <stduuid/uuid.h>

#include <algorithm>
#include <array>
#include <functional>
#include <random>
#include <unordered_set>
#include <string_view>
#include <utility>

namespace quader {
namespace {

std::uint32_t entity_key(entt::entity entity) {
  return static_cast<std::uint32_t>(entt::to_integral(entity));
}

void remove_child(std::vector<EntityId> &children, EntityId child) {
  children.erase(std::remove(children.begin(), children.end(), child),
                 children.end());
}

std::mt19937 &entity_uuid_random_engine() {
  thread_local std::mt19937 engine = [] {
    std::random_device random_device;
    std::array<std::uint32_t, 8> seed_data{};
    for (std::uint32_t &value : seed_data) {
      value = random_device();
    }
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    return std::mt19937(seed);
  }();
  return engine;
}

std::string generate_unique_entity_uuid(
    const std::unordered_map<std::string, EntityId> &existing) {
  uuids::uuid_random_generator generator{entity_uuid_random_engine()};
  for (;;) {
    const std::string uuid = uuids::to_string(generator());
    if (!uuid.empty() && !existing.contains(uuid)) {
      return uuid;
    }
  }
}

bool valid_entity_uuid(std::string_view uuid) {
  return uuids::uuid::from_string(uuid).has_value();
}

bool same_transform(const TransformComponent &left,
                    const TransformComponent &right) {
  return left.position.x == right.position.x &&
         left.position.y == right.position.y &&
         left.position.z == right.position.z &&
         left.rotation_euler.x == right.rotation_euler.x &&
         left.rotation_euler.y == right.rotation_euler.y &&
         left.rotation_euler.z == right.rotation_euler.z &&
         left.scale.x == right.scale.x && left.scale.y == right.scale.y &&
         left.scale.z == right.scale.z &&
         left.custom_pivot == right.custom_pivot;
}

bool is_supported_instance_member_subtree(const SceneDocument &scene,
                                          EntityId root) {
  for (const EntityId entity : scene.descendants(root, true)) {
    if (scene.is_instance_definition_entity(entity) ||
        scene.instance_occurrence(entity) != nullptr ||
        scene.light(entity) != nullptr) {
      return false;
    }
    switch (scene.kind(entity)) {
    case EntityKind::Mesh:
      if (scene.mesh(entity) == nullptr) {
        return false;
      }
      break;
    case EntityKind::Empty:
      if (scene.mesh(entity) != nullptr ||
          scene.modeling_object(entity) != nullptr) {
        return false;
      }
      break;
    case EntityKind::Light:
    case EntityKind::Generic:
    case EntityKind::Instance:
      return false;
    }
  }
  return true;
}

bool destruction_preserves_instance_graph(
    const SceneDocument &scene,
    const std::unordered_set<EntityId> &destroyed_entities) {
  for (const EntityId entity : scene.entities()) {
    if (!scene.is_instance_definition_entity(entity)) {
      continue;
    }
    const bool definition_destroyed = destroyed_entities.contains(entity);
    if (definition_destroyed) {
      for (const EntityId occurrence :
           scene.instance_occurrences_for_definition(entity)) {
        if (!destroyed_entities.contains(occurrence)) {
          return false;
        }
      }
      continue;
    }

    const std::vector<EntityId> members = scene.children(entity);
    if (!scene.instance_occurrences_for_definition(entity).empty() &&
        std::none_of(members.begin(), members.end(),
                     [&](EntityId member) {
                       return !destroyed_entities.contains(member);
                     })) {
      return false;
    }
  }
  return true;
}

} // namespace

void SceneDocument::set_editor_object_id_allocator(
    EditorObjectIdAllocator allocator) {
  editor_object_id_allocator_ = std::move(allocator);
}

EntityId SceneDocument::create_entity(std::string name, EntityKind kind,
                                      EditorObjectId editor_object_id) {
  const EntityId id = next_id_++;
  const EditorObjectId object_id = allocate_editor_object_id(editor_object_id);
  if (object_id == kInvalidEditorObjectId) {
    return kInvalidEntityId;
  }
  const entt::entity entity = registry_.create();
  registry_.emplace<EntityIdentityComponent>(entity,
                                             EntityIdentityComponent{.id = id});
  const std::string uuid = generate_unique_entity_uuid(entity_by_uuid_);
  registry_.emplace<EntityUuidComponent>(entity,
                                         EntityUuidComponent{.uuid = uuid});
  registry_.emplace<EditorObjectIdentityComponent>(
      entity, EditorObjectIdentityComponent{.id = object_id});
  registry_.emplace<NameComponent>(entity,
                                   NameComponent{.name = std::move(name)});
  registry_.emplace<TransformComponent>(entity, TransformComponent{});
  registry_.emplace<EntityKindComponent>(entity,
                                         EntityKindComponent{.kind = kind});
  registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
  registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
  registry_.emplace<LockComponent>(entity, LockComponent{});
  registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
  entities_.emplace(id, entity);
  ids_by_entity_.emplace(entity_key(entity), id);
  entity_by_uuid_.emplace(uuid, id);
  entity_by_editor_object_id_.emplace(object_id, id);
  ++visibility_revision_;
  ++transform_revision_;
  ++render_component_revision_;
  mark_dirty();
  return id;
}

EntityId SceneDocument::create_entity_with_id(EntityId id, std::string name,
                                              EntityKind kind,
                                              EditorObjectId editor_object_id) {
  if (id == kInvalidEntityId) {
    return create_entity(std::move(name), kind, editor_object_id);
  }
  if (exists(id)) {
    return kInvalidEntityId;
  }
  next_id_ = std::max(next_id_, id + 1u);
  const EditorObjectId object_id = allocate_editor_object_id(editor_object_id);
  if (object_id == kInvalidEditorObjectId) {
    return kInvalidEntityId;
  }
  const entt::entity entity = registry_.create();
  registry_.emplace<EntityIdentityComponent>(entity,
                                             EntityIdentityComponent{.id = id});
  const std::string uuid = generate_unique_entity_uuid(entity_by_uuid_);
  registry_.emplace<EntityUuidComponent>(entity,
                                         EntityUuidComponent{.uuid = uuid});
  registry_.emplace<EditorObjectIdentityComponent>(
      entity, EditorObjectIdentityComponent{.id = object_id});
  registry_.emplace<NameComponent>(entity,
                                   NameComponent{.name = std::move(name)});
  registry_.emplace<TransformComponent>(entity, TransformComponent{});
  registry_.emplace<EntityKindComponent>(entity,
                                         EntityKindComponent{.kind = kind});
  registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
  registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
  registry_.emplace<LockComponent>(entity, LockComponent{});
  registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
  entities_.emplace(id, entity);
  ids_by_entity_.emplace(entity_key(entity), id);
  entity_by_uuid_.emplace(uuid, id);
  entity_by_editor_object_id_.emplace(object_id, id);
  ++transform_revision_;
  ++visibility_revision_;
  ++render_component_revision_;
  mark_dirty();
  return id;
}

EntityId SceneDocument::create_empty(std::string name,
                                     EditorObjectId editor_object_id) {
  return create_entity(std::move(name), EntityKind::Empty, editor_object_id);
}

bool SceneDocument::destroy_entity(EntityId id) {
  if (is_scene_root(id)) {
    return false;
  }
  const std::vector<EntityId> to_destroy = descendants(id, true);
  if (to_destroy.empty()) {
    return false;
  }

  const std::unordered_set<EntityId> destroy_set(to_destroy.begin(),
                                                  to_destroy.end());
  if (!destruction_preserves_instance_graph(*this, destroy_set)) {
    return false;
  }

  const bool active_destroyed =
      std::find(to_destroy.begin(), to_destroy.end(), active_selection_) !=
      to_destroy.end();
  for (auto it = to_destroy.rbegin(); it != to_destroy.rend(); ++it) {
    const EntityId entity_id = *it;
    const entt::entity entity = require_entity(entity_id);
    if (entity == entt::null) {
      continue;
    }
    if (HierarchyComponent *component = mutable_hierarchy(entity_id)) {
      if (HierarchyComponent *parent_hierarchy =
              mutable_hierarchy(component->parent)) {
        remove_child(parent_hierarchy->children, entity_id);
      }
    }
    if (const EditorObjectIdentityComponent *identity =
            editor_object_identity(entity_id)) {
      entity_by_editor_object_id_.erase(identity->id);
    }
    if (const EntityUuidComponent *identity = entity_uuid(entity_id)) {
      entity_by_uuid_.erase(identity->uuid);
    }
    registry_.destroy(entity);
    entities_.erase(entity_id);
    ids_by_entity_.erase(entity_key(entity));
  }

  if (active_destroyed || !is_selected(active_selection_)) {
    active_selection_ = first_selected_entity();
  }
  ++selection_revision_;
  ++visibility_revision_;
  ++transform_revision_;
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty();
  return true;
}

bool SceneDocument::destroy_entities(std::span<const EntityId> ids) {
  std::unordered_set<EntityId> destroy_set;
  for (const EntityId id : ids) {
    if (is_scene_root(id)) {
      continue;
    }
    const std::vector<EntityId> descendants_to_destroy = descendants(id, true);
    destroy_set.insert(descendants_to_destroy.begin(),
                       descendants_to_destroy.end());
  }
  if (destroy_set.empty()) {
    return false;
  }

  std::vector<EntityId> to_destroy;
  to_destroy.reserve(destroy_set.size());
  for (const EntityId entity_id : entity_tree_order()) {
    if (destroy_set.contains(entity_id)) {
      to_destroy.push_back(entity_id);
    }
  }
  if (to_destroy.empty()) {
    return false;
  }

  if (!destruction_preserves_instance_graph(*this, destroy_set)) {
    return false;
  }

  const bool active_destroyed = destroy_set.contains(active_selection_);
  for (auto it = to_destroy.rbegin(); it != to_destroy.rend(); ++it) {
    const EntityId entity_id = *it;
    const entt::entity entity = require_entity(entity_id);
    if (entity == entt::null) {
      continue;
    }
    if (HierarchyComponent *component = mutable_hierarchy(entity_id)) {
      if (HierarchyComponent *parent_hierarchy =
              mutable_hierarchy(component->parent)) {
        remove_child(parent_hierarchy->children, entity_id);
      }
    }
    if (const EditorObjectIdentityComponent *identity =
            editor_object_identity(entity_id)) {
      entity_by_editor_object_id_.erase(identity->id);
    }
    if (const EntityUuidComponent *identity = entity_uuid(entity_id)) {
      entity_by_uuid_.erase(identity->uuid);
    }
    registry_.destroy(entity);
    entities_.erase(entity_id);
    ids_by_entity_.erase(entity_key(entity));
  }

  if (active_destroyed || !is_selected(active_selection_)) {
    active_selection_ = first_selected_entity();
  }
  ++selection_revision_;
  ++visibility_revision_;
  ++transform_revision_;
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty();
  return true;
}

bool SceneDocument::exists(EntityId id) const {
  return require_entity(id) != entt::null;
}

bool SceneDocument::rename_entity(EntityId id, std::string name) {
  if (NameComponent *component = mutable_name(id)) {
    component->name = std::move(name);
    mark_dirty();
    return true;
  }
  return false;
}

bool SceneDocument::set_entity_uuid(EntityId id, std::string uuid) {
  if (uuid.empty() || !valid_entity_uuid(uuid)) {
    return false;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  if (const auto existing = entity_by_uuid_.find(uuid);
      existing != entity_by_uuid_.end() && existing->second != id) {
    return false;
  }

  EntityUuidComponent *identity =
      registry_.try_get<EntityUuidComponent>(entity);
  if (identity != nullptr && identity->uuid == uuid) {
    return true;
  }
  if (identity != nullptr && !identity->uuid.empty()) {
    entity_by_uuid_.erase(identity->uuid);
    identity->uuid = std::move(uuid);
  } else {
    registry_.emplace_or_replace<EntityUuidComponent>(
        entity, EntityUuidComponent{.uuid = std::move(uuid)});
    identity = registry_.try_get<EntityUuidComponent>(entity);
  }
  if (identity != nullptr) {
    entity_by_uuid_[identity->uuid] = id;
  }
  mark_dirty();
  return true;
}

bool SceneDocument::set_editor_object_id(EntityId id,
                                         EditorObjectId editor_object_id) {
  if (editor_object_id == kInvalidEditorObjectId) {
    return false;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  if (const auto existing = entity_by_editor_object_id_.find(editor_object_id);
      existing != entity_by_editor_object_id_.end() && existing->second != id) {
    return false;
  }

  EditorObjectIdentityComponent *identity =
      registry_.try_get<EditorObjectIdentityComponent>(entity);
  if (identity != nullptr && identity->id == editor_object_id) {
    return true;
  }
  if (editor_object_id_allocator_) {
    const EditorObjectId reserved =
        editor_object_id_allocator_(editor_object_id);
    if (reserved != editor_object_id) {
      return false;
    }
  }
  if (identity != nullptr && identity->id != kInvalidEditorObjectId) {
    entity_by_editor_object_id_.erase(identity->id);
    identity->id = editor_object_id;
  } else {
    registry_.emplace_or_replace<EditorObjectIdentityComponent>(
        entity, EditorObjectIdentityComponent{.id = editor_object_id});
  }
  entity_by_editor_object_id_[editor_object_id] = id;
  next_editor_object_id_ =
      std::max(next_editor_object_id_, editor_object_id + 1u);
  mark_dirty();
  return true;
}

bool SceneDocument::set_entity_kind(EntityId id, EntityKind kind) {
  if (instance_occurrence(id) != nullptr && kind != EntityKind::Instance) {
    return false;
  }
  if (is_instance_definition_entity(id) && kind != EntityKind::Empty &&
      kind != EntityKind::Generic) {
    return false;
  }
  if (is_instance_definition_member(id) && kind != EntityKind::Mesh &&
      kind != EntityKind::Empty) {
    return false;
  }
  if (kind == EntityKind::Instance && instance_occurrence(id) == nullptr) {
    return false;
  }
  if (EntityKindComponent *component = mutable_entity_kind(id)) {
    component->kind = kind;
    mark_dirty();
    return true;
  }
  return false;
}

bool SceneDocument::set_visible(EntityId id, bool visible) {
  if (VisibilityComponent *component = mutable_visibility(id)) {
    if (component->visible == visible) {
      return true;
    }
    component->visible = visible;
    ++visibility_revision_;
    mark_dirty();
    return true;
  }
  return false;
}

bool SceneDocument::set_selectable(EntityId id, bool selectable) {
  if (VisibilityComponent *component = mutable_visibility(id)) {
    if (component->selectable == selectable) {
      return true;
    }
    component->selectable = selectable;
    ++visibility_revision_;
    mark_dirty();
    return true;
  }
  return false;
}

bool SceneDocument::set_locked(EntityId id, bool locked) {
  if (LockComponent *component = mutable_lock(id)) {
    component->locked = locked;
    mark_dirty();
    return true;
  }
  return false;
}

bool SceneDocument::set_selected(EntityId id, bool selected) {
  if (SelectionComponent *component = mutable_selection(id)) {
    const bool was_selected = component->selected;
    const EntityId previous_active_selection = active_selection_;
    component->selected = selected;
    if (selected) {
      active_selection_ = id;
    } else if (active_selection_ == id) {
      active_selection_ = first_selected_entity();
    }
    if (was_selected != selected ||
        previous_active_selection != active_selection_) {
      ++selection_revision_;
      mark_transient_dirty();
    }
    return true;
  }
  return false;
}

bool SceneDocument::set_active_selection(EntityId id) {
  if (!is_selected(id)) {
    return false;
  }
  if (active_selection_ != id) {
    active_selection_ = id;
    ++selection_revision_;
    mark_transient_dirty();
  }
  return true;
}

void SceneDocument::clear_selection() {
  bool changed = false;
  for (auto [entity, selection] : registry_.view<SelectionComponent>().each()) {
    if (selection.selected) {
      selection.selected = false;
      changed = true;
    }
  }
  if (active_selection_ != kInvalidEntityId) {
    active_selection_ = kInvalidEntityId;
    changed = true;
  }
  if (changed) {
    ++selection_revision_;
    mark_transient_dirty();
  }
}

bool SceneDocument::set_parent(EntityId child, EntityId new_parent,
                               bool preserve_world_transform) {
  if (child == kInvalidEntityId || require_entity(child) == entt::null) {
    return false;
  }
  if (is_scene_root(child) && new_parent != kInvalidEntityId) {
    return false;
  }
  if (new_parent != kInvalidEntityId &&
      require_entity(new_parent) == entt::null) {
    return false;
  }
  if (child == new_parent || is_descendant_of(new_parent, child)) {
    return false;
  }
  if (instance_occurrence(new_parent) != nullptr) {
    return false;
  }
  if (instance_occurrence(child) != nullptr &&
      containing_instance_definition(new_parent) != kInvalidEntityId) {
    return false;
  }
  if (is_instance_definition_entity(child) &&
      containing_instance_definition(new_parent) != kInvalidEntityId) {
    return false;
  }

  const EntityId old_definition = containing_instance_definition(child);
  const EntityId new_definition =
      containing_instance_definition(new_parent);
  if (old_definition != kInvalidEntityId && old_definition != child &&
      new_definition != old_definition &&
      !instance_occurrences_for_definition(old_definition).empty()) {
    const bool extracts_redundant_top_level_member =
        new_definition == kInvalidEntityId && parent(child) == old_definition &&
        children(old_definition).size() > 1u;
    if (!extracts_redundant_top_level_member) {
      return false;
    }
  }
  if (new_definition != kInvalidEntityId && new_definition != child &&
      old_definition != new_definition &&
      !is_supported_instance_member_subtree(*this, child)) {
    return false;
  }

  HierarchyComponent *child_hierarchy = mutable_hierarchy(child);
  if (!child_hierarchy) {
    return false;
  }

  const EntityId old_parent = child_hierarchy->parent;
  if (old_parent == new_parent) {
    return true;
  }

  const QMat4 child_world = preserve_world_transform
                                ? world_transform_matrix(child)
                                : local_transform_matrix(child);
  std::optional<TransformComponent> preserved_local_transform;
  if (preserve_world_transform) {
    const TransformComponent *current_transform = transform(child);
    const QMat4 parent_world = new_parent == kInvalidEntityId
                                   ? scene_transform_identity_matrix()
                                   : world_transform_matrix(new_parent);
    if (current_transform == nullptr) {
      return false;
    }
    preserved_local_transform = try_scene_local_transform_preserving_world(
        parent_world, child_world, *current_transform);
    if (!preserved_local_transform.has_value()) {
      return false;
    }
  }

  if (HierarchyComponent *old_parent_hierarchy =
          mutable_hierarchy(old_parent)) {
    remove_child(old_parent_hierarchy->children, child);
  }

  child_hierarchy->parent = new_parent;
  if (HierarchyComponent *new_parent_hierarchy =
          mutable_hierarchy(new_parent)) {
    if (std::find(new_parent_hierarchy->children.begin(),
                  new_parent_hierarchy->children.end(),
                  child) == new_parent_hierarchy->children.end()) {
      new_parent_hierarchy->children.push_back(child);
    }
  }

  if (preserve_world_transform) {
    if (TransformComponent *local_transform = mutable_transform(child)) {
      *local_transform = *preserved_local_transform;
    }
  }

  ++visibility_revision_;
  ++transform_revision_;
  mark_dirty();
  return true;
}

void SceneDocument::set_transform(EntityId id, TransformComponent transform) {
  if (!scene_transform_is_finite(transform)) {
    return;
  }
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return;
  }
  if (const TransformComponent *current =
          registry_.try_get<TransformComponent>(entity);
      current != nullptr && same_transform(*current, transform)) {
    return;
  }
  registry_.emplace_or_replace<TransformComponent>(entity, transform);
  ++transform_revision_;
  mark_dirty();
}

void SceneDocument::set_transforms(
    std::span<const SceneTransformAssignment> transforms) {
  bool changed = false;
  for (const SceneTransformAssignment &assignment : transforms) {
    if (!scene_transform_is_finite(assignment.transform)) {
      continue;
    }
    const entt::entity entity = require_entity(assignment.entity);
    if (entity == entt::null) {
      continue;
    }
    if (const TransformComponent *current =
            registry_.try_get<TransformComponent>(entity);
        current != nullptr && same_transform(*current, assignment.transform)) {
      continue;
    }
    registry_.emplace_or_replace<TransformComponent>(entity,
                                                     assignment.transform);
    changed = true;
  }
  if (changed) {
    ++transform_revision_;
    mark_dirty();
  }
}

void SceneDocument::set_modeling_object(EntityId id,
                                        ModelingObjectComponent object) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id)) {
    return;
  }
  registry_.emplace_or_replace<ModelingObjectComponent>(entity, object);
  mark_dirty();
}

bool SceneDocument::set_instance_definition(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || is_scene_root(id) ||
      instance_occurrence(id) != nullptr ||
      containing_instance_definition(parent(id)) != kInvalidEntityId ||
      mesh(id) != nullptr || modeling_object(id) != nullptr ||
      material(id) != nullptr || light(id) != nullptr) {
    return false;
  }
  for (const EntityId child : children(id)) {
    if (!is_supported_instance_member_subtree(*this, child)) {
      return false;
    }
  }
  if (registry_.all_of<InstanceDefinitionComponent>(entity)) {
    return true;
  }
  registry_.emplace<InstanceDefinitionComponent>(entity);
  ++render_component_revision_;
  mark_dirty();
  return true;
}

bool SceneDocument::clear_instance_definition(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<InstanceDefinitionComponent>(entity) ||
      !instance_occurrences_for_definition(id).empty()) {
    return false;
  }
  registry_.remove<InstanceDefinitionComponent>(entity);
  ++render_component_revision_;
  mark_dirty();
  return true;
}

bool SceneDocument::set_instance_occurrence(
    EntityId id, InstanceOccurrenceComponent occurrence) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || occurrence.definition == kInvalidEntityId ||
      occurrence.definition == id ||
      !is_instance_definition_entity(occurrence.definition) ||
      is_instance_definition_entity(id) || has_children(id) ||
      containing_instance_definition(id) != kInvalidEntityId ||
      mesh(id) != nullptr || modeling_object(id) != nullptr ||
      material(id) != nullptr || light(id) != nullptr) {
    return false;
  }
  if (const InstanceOccurrenceComponent *current = instance_occurrence(id);
      current != nullptr && *current == occurrence &&
      kind(id) == EntityKind::Instance) {
    return true;
  }
  registry_.emplace_or_replace<InstanceOccurrenceComponent>(entity,
                                                             occurrence);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Instance});
  ++render_component_revision_;
  mark_dirty();
  return true;
}

bool SceneDocument::clear_instance_occurrence(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null ||
      !registry_.all_of<InstanceOccurrenceComponent>(entity)) {
    return false;
  }
  registry_.remove<InstanceOccurrenceComponent>(entity);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Generic});
  ++render_component_revision_;
  mark_dirty();
  return true;
}

void SceneDocument::set_mesh(EntityId id, MeshComponent mesh) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id)) {
    return;
  }
  registry_.emplace_or_replace<MeshComponent>(entity, mesh);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Mesh});
  ++mesh_binding_revision_;
  ++render_component_revision_;
  mark_dirty();
}

void SceneDocument::set_material(EntityId id, MaterialComponent material) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id)) {
    return;
  }
  registry_.emplace_or_replace<MaterialComponent>(entity, material);
  ++render_component_revision_;
  mark_dirty();
}

bool SceneDocument::clear_material(EntityId id) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || !registry_.all_of<MaterialComponent>(entity)) {
    return false;
  }
  registry_.remove<MaterialComponent>(entity);
  ++render_component_revision_;
  mark_dirty();
  return true;
}

void SceneDocument::set_light(EntityId id, LightComponent light) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null || instance_occurrence(id) != nullptr ||
      is_instance_definition_entity(id) ||
      is_instance_definition_member(id)) {
    return;
  }
  registry_.emplace_or_replace<LightComponent>(entity, light);
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Light});
  ++render_component_revision_;
  mark_dirty();
}

bool SceneDocument::set_scene_settings(EntityId id,
                                       SceneSettingsComponent settings) {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return false;
  }
  const EntityId root = scene_root();
  if (root != kInvalidEntityId && root != id) {
    return false;
  }
  if (const SceneSettingsComponent *current = scene_settings(id);
      current != nullptr && *current == settings) {
    return true;
  }
  registry_.emplace_or_replace<SceneSettingsComponent>(entity,
                                                       std::move(settings));
  registry_.emplace_or_replace<EntityKindComponent>(
      entity, EntityKindComponent{.kind = EntityKind::Empty});
  mark_dirty();
  return true;
}

NameComponent *SceneDocument::mutable_name(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<NameComponent>(entity);
}

TransformComponent *SceneDocument::mutable_transform(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<TransformComponent>(entity);
}

EntityKindComponent *SceneDocument::mutable_entity_kind(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityKindComponent>(entity);
}

HierarchyComponent *SceneDocument::mutable_hierarchy(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<HierarchyComponent>(entity);
}

VisibilityComponent *SceneDocument::mutable_visibility(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<VisibilityComponent>(entity);
}

LockComponent *SceneDocument::mutable_lock(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LockComponent>(entity);
}

SelectionComponent *SceneDocument::mutable_selection(EntityId id) {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<SelectionComponent>(entity);
}

const NameComponent* SceneDocument::name(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<NameComponent>(entity);
}

const EntityUuidComponent *SceneDocument::entity_uuid(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityUuidComponent>(entity);
}

const EditorObjectIdentityComponent *
SceneDocument::editor_object_identity(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<EditorObjectIdentityComponent>(entity);
}

const TransformComponent* SceneDocument::transform(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<TransformComponent>(entity);
}

const EntityKindComponent *SceneDocument::entity_kind(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<EntityKindComponent>(entity);
}

const HierarchyComponent* SceneDocument::hierarchy(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<HierarchyComponent>(entity);
}

const VisibilityComponent* SceneDocument::visibility(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<VisibilityComponent>(entity);
}

const LockComponent* SceneDocument::lock(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LockComponent>(entity);
}

const SelectionComponent* SceneDocument::selection(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<SelectionComponent>(entity);
}

const ModelingObjectComponent *
SceneDocument::modeling_object(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<ModelingObjectComponent>(entity);
}

const InstanceDefinitionComponent *
SceneDocument::instance_definition(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<InstanceDefinitionComponent>(entity);
}

const InstanceOccurrenceComponent *
SceneDocument::instance_occurrence(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<InstanceOccurrenceComponent>(entity);
}

const MeshComponent* SceneDocument::mesh(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<MeshComponent>(entity);
}

const MaterialComponent* SceneDocument::material(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<MaterialComponent>(entity);
}

const LightComponent* SceneDocument::light(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null ? nullptr
                              : registry_.try_get<LightComponent>(entity);
}

const SceneSettingsComponent *SceneDocument::scene_settings(EntityId id) const {
  const entt::entity entity = require_entity(id);
  return entity == entt::null
             ? nullptr
             : registry_.try_get<SceneSettingsComponent>(entity);
}

std::vector<EntityId> SceneDocument::entities() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

EntityId SceneDocument::scene_root() const {
  EntityId root = kInvalidEntityId;
  for (auto [entity, settings, identity] :
       registry_.view<SceneSettingsComponent, EntityIdentityComponent>()
           .each()) {
    (void)settings;
    if (root == kInvalidEntityId || identity.id < root) {
      root = identity.id;
    }
  }
  return exists(root) ? root : kInvalidEntityId;
}

bool SceneDocument::is_scene_root(EntityId id) const {
  return id != kInvalidEntityId && scene_settings(id) != nullptr;
}

std::vector<EntityId> SceneDocument::root_entities() const {
  std::vector<EntityId> roots;
  const std::vector<EntityId> ids = entities();
  roots.reserve(ids.size());
  const EntityId root = scene_root();
  if (root != kInvalidEntityId && parent(root) == kInvalidEntityId) {
    roots.push_back(root);
  }
  for (const EntityId id : ids) {
    if (id != root && parent(id) == kInvalidEntityId) {
      roots.push_back(id);
    }
  }
  return roots;
}

std::vector<EntityId> SceneDocument::children(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->children;
    }
    return {};
}

std::vector<EntityId> SceneDocument::descendants(EntityId id,
                                                 bool include_self) const {
  if (require_entity(id) == entt::null) {
    return {};
  }

  std::vector<EntityId> result;
  if (include_self) {
    result.push_back(id);
  }
  for (const EntityId child : children(id)) {
    std::vector<EntityId> child_descendants = descendants(child, true);
    result.insert(result.end(), child_descendants.begin(),
                  child_descendants.end());
  }
  return result;
}

std::vector<EntityId> SceneDocument::entity_tree_order() const {
  std::vector<EntityId> ids;
  ids.reserve(entities_.size());
  for (const EntityId root : root_entities()) {
    append_tree_order(root, ids);
  }
  return ids;
}

std::vector<EntityRecord> SceneDocument::entity_records() const {
  std::vector<EntityRecord> records;
  const std::vector<EntityId> ids = entities();
  const EntityId active = primary_selection();
  records.reserve(ids.size());

  for (const EntityId id : ids) {
    records.push_back(make_entity_record(id, 0, active));
  }

  return records;
}

std::vector<EntityRecord> SceneDocument::entity_records_tree_order() const {
  std::vector<EntityRecord> records;
  records.reserve(entities_.size());
  const EntityId active = primary_selection();
  for (const EntityId root : root_entities()) {
    append_entity_records(root, 0, active, records);
  }
  return records;
}

std::vector<EntityRecord>
SceneDocument::entity_records_for_instance_definition(
    EntityId definition) const {
  std::vector<EntityRecord> records;
  if (!is_instance_definition_entity(definition)) {
    return records;
  }

  const EntityId active = primary_selection();
  const std::vector<EntityId> members = descendants(definition, false);
  records.reserve(members.size());
  std::function<void(EntityId, int, bool, bool)> append_member =
      [&](EntityId member, int depth, bool parent_visible,
          bool parent_locked) {
        EntityRecord record = make_entity_record(member, depth, active);
        if (depth == 0) {
          record.parent = kInvalidEntityId;
        }
        record.effectively_visible = parent_visible && record.visible;
        record.effectively_locked = parent_locked || record.locked;
        records.push_back(record);
        for (const EntityId child : children(member)) {
          append_member(child, depth + 1, record.effectively_visible,
                        record.effectively_locked);
        }
      };
  for (const EntityId member : children(definition)) {
    append_member(member, 0, true, false);
  }
  return records;
}

std::vector<EntityId> SceneDocument::selected_entities() const {
  std::vector<EntityId> ids;
  for (auto [entity, selection, identity] :
       registry_.view<SelectionComponent, EntityIdentityComponent>().each()) {
    if (selection.selected) {
      ids.push_back(identity.id);
    }
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

EntityId SceneDocument::primary_selection() const {
  if (is_selected(active_selection_)) {
    return active_selection_;
  }
  return first_selected_entity();
}

EntityId SceneDocument::parent(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->parent;
    }
    return kInvalidEntityId;
}

std::string SceneDocument::uuid(EntityId id) const {
  const EntityUuidComponent *identity = entity_uuid(id);
  return identity == nullptr ? std::string{} : identity->uuid;
}

EntityId SceneDocument::entity_for_uuid(std::string_view uuid) const {
  const auto existing = entity_by_uuid_.find(std::string(uuid));
  if (existing == entity_by_uuid_.end() || !exists(existing->second)) {
    return kInvalidEntityId;
  }
  return existing->second;
}

EditorObjectId SceneDocument::editor_object_id(EntityId id) const {
  const EditorObjectIdentityComponent *identity = editor_object_identity(id);
  return identity == nullptr ? kInvalidEditorObjectId : identity->id;
}

EntityId SceneDocument::entity_for_editor_object_id(
    EditorObjectId editor_object_id) const {
  const auto existing = entity_by_editor_object_id_.find(editor_object_id);
  if (existing == entity_by_editor_object_id_.end() ||
      !exists(existing->second)) {
    return kInvalidEntityId;
  }
  return existing->second;
}

bool SceneDocument::has_children(EntityId id) const {
  if (const HierarchyComponent *component = hierarchy(id)) {
    return !component->children.empty();
  }
  return false;
}

bool SceneDocument::is_descendant_of(EntityId entity,
                                     EntityId possible_ancestor) const {
  if (entity == kInvalidEntityId || possible_ancestor == kInvalidEntityId) {
    return false;
  }
  EntityId current = parent(entity);
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (current == possible_ancestor) {
      return true;
    }
    current = parent(current);
  }
  return false;
}

bool SceneDocument::is_instance_definition_entity(EntityId entity) const {
  return instance_definition(entity) != nullptr;
}

EntityId SceneDocument::containing_instance_definition(EntityId entity) const {
  if (entity == kInvalidEntityId || !exists(entity)) {
    return kInvalidEntityId;
  }
  EntityId current = entity;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (is_instance_definition_entity(current)) {
      return current;
    }
    current = parent(current);
  }
  return kInvalidEntityId;
}

bool SceneDocument::is_instance_definition_member(EntityId entity) const {
  const EntityId definition = containing_instance_definition(entity);
  return definition != kInvalidEntityId && definition != entity;
}

std::vector<EntityId> SceneDocument::instance_occurrences_for_definition(
    EntityId definition) const {
  std::vector<EntityId> result;
  if (!is_instance_definition_entity(definition)) {
    return result;
  }
  for (const EntityId entity : entities()) {
    const InstanceOccurrenceComponent *occurrence =
        instance_occurrence(entity);
    if (occurrence != nullptr && occurrence->definition == definition) {
      result.push_back(entity);
    }
  }
  return result;
}

EntityKind SceneDocument::kind(EntityId id) const {
  if (const EntityKindComponent *component = entity_kind(id)) {
    return component->kind;
  }
    return EntityKind::Generic;
}

bool SceneDocument::is_effectively_visible(EntityId id) const {
  if (require_entity(id) == entt::null) {
    return false;
  }
  EntityId current = id;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (const VisibilityComponent *component = visibility(current)) {
      if (!component->visible) {
        return false;
      }
    }
    current = parent(current);
  }
  return true;
}

bool SceneDocument::is_effectively_locked(EntityId id) const {
  if (require_entity(id) == entt::null) {
    return false;
  }
  EntityId current = id;
  std::size_t guard = entities_.size() + 1u;
  while (current != kInvalidEntityId && guard-- > 0u) {
    if (const LockComponent *component = lock(current)) {
      if (component->locked) {
        return true;
      }
    }
    current = parent(current);
  }
  return false;
}

QMat4 SceneDocument::local_transform_matrix(EntityId id) const {
  if (const TransformComponent *component = transform(id)) {
    return compose_transform(component->spatial_transform());
  }
  return scene_transform_identity_matrix();
}

QMat4 SceneDocument::world_transform_matrix(EntityId id) const {
  const entt::entity entity = require_entity(id);
  if (entity == entt::null) {
    return scene_transform_identity_matrix();
  }

  const TransformComponent *local_transform =
      registry_.try_get<TransformComponent>(entity);
  const QMat4 local_matrix =
      local_transform != nullptr
          ? compose_transform(local_transform->spatial_transform())
          : scene_transform_identity_matrix();
  const HierarchyComponent *hierarchy =
      registry_.try_get<HierarchyComponent>(entity);
  const EntityId parent_id =
      hierarchy != nullptr ? hierarchy->parent : kInvalidEntityId;
  if (parent_id == kInvalidEntityId) {
    return local_matrix;
  }
  return scene_transform_multiply(world_transform_matrix(parent_id),
                                  local_matrix);
}

void SceneDocument::advance_revision_for_replacement(
    std::uint64_t previous_revision) {
  revision_ = std::max(revision_ + 1u, previous_revision + 1u);
  content_revision_ =
      std::max(content_revision_ + 1u, previous_revision + 1u);
  selection_revision_ = std::max(selection_revision_ + 1u, revision_);
  visibility_revision_ = std::max(visibility_revision_ + 1u, revision_);
  transform_revision_ = std::max(transform_revision_ + 1u, revision_);
  mesh_binding_revision_ = std::max(mesh_binding_revision_ + 1u, revision_);
  render_component_revision_ =
      std::max(render_component_revision_ + 1u, revision_);
}

entt::entity SceneDocument::entt_entity(EntityId id) const {
  return require_entity(id);
}

EntityId SceneDocument::entity_id(entt::entity entity) const {
  if (entity == entt::null || !registry_.valid(entity)) {
    return kInvalidEntityId;
  }
  const auto it = ids_by_entity_.find(entity_key(entity));
  return it == ids_by_entity_.end() ? kInvalidEntityId : it->second;
}

void SceneDocument::mark_dirty() {
  ++revision_;
  ++content_revision_;
}

void SceneDocument::mark_transient_dirty() { ++revision_; }

EditorObjectId
SceneDocument::allocate_editor_object_id(EditorObjectId requested) {
  if (requested != kInvalidEditorObjectId) {
    if (entity_by_editor_object_id_.contains(requested)) {
      return kInvalidEditorObjectId;
    }
    if (editor_object_id_allocator_) {
      const EditorObjectId allocated = editor_object_id_allocator_(requested);
      if (allocated != requested) {
        return kInvalidEditorObjectId;
      }
    }
    next_editor_object_id_ = std::max(next_editor_object_id_, requested + 1u);
    return requested;
  }
  if (editor_object_id_allocator_) {
    const std::size_t guard = entity_by_editor_object_id_.size() + 2u;
    for (std::size_t attempt = 0; attempt < guard; ++attempt) {
      const EditorObjectId allocated =
          editor_object_id_allocator_(kInvalidEditorObjectId);
      if (allocated != kInvalidEditorObjectId &&
          !entity_by_editor_object_id_.contains(allocated)) {
        next_editor_object_id_ =
            std::max(next_editor_object_id_, allocated + 1u);
        return allocated;
      }
    }
    return kInvalidEditorObjectId;
  }
  while (entity_by_editor_object_id_.contains(next_editor_object_id_)) {
    ++next_editor_object_id_;
  }
  return next_editor_object_id_++;
}

entt::entity SceneDocument::require_entity(EntityId id) const {
  const auto it = entities_.find(id);
  if (it == entities_.end() || it->second == entt::null ||
      !registry_.valid(it->second)) {
    return entt::null;
  }
  return it->second;
}

bool SceneDocument::is_selected(EntityId id) const {
  if (id == kInvalidEntityId) {
    return false;
  }
  if (const SelectionComponent *component = selection(id)) {
    return component->selected;
  }
  return false;
}

EntityId SceneDocument::first_selected_entity() const {
  const std::vector<EntityId> selected = selected_entities();
  return selected.empty() ? kInvalidEntityId : selected.front();
}

EntityRecord SceneDocument::make_entity_record(EntityId id, int depth,
                                               EntityId active) const {
  EntityRecord record;
  record.id = id;
  record.uuid = uuid(id);
  record.editor_object_id = editor_object_id(id);
  record.kind = kind(id);
  record.parent = parent(id);
  record.depth = depth;
  if (const NameComponent *component = name(id)) {
    record.name = component->name;
  }
  if (const VisibilityComponent *component = visibility(id)) {
    record.visible = component->visible;
  }
  record.effectively_visible = is_effectively_visible(id);
  if (const LockComponent *component = lock(id)) {
    record.locked = component->locked;
  }
  record.effectively_locked = is_effectively_locked(id);
  if (const SelectionComponent *component = selection(id)) {
    record.selected = component->selected;
  }
  record.active = id == active;
  record.has_children = has_children(id);
  record.has_mesh = mesh(id) != nullptr;
  record.has_light = light(id) != nullptr;
  record.is_scene_root = is_scene_root(id);
  record.has_scene_settings = scene_settings(id) != nullptr;
  record.has_modeling_object = modeling_object(id) != nullptr;
  record.is_instance_definition = is_instance_definition_entity(id);
  record.has_instance_occurrence = instance_occurrence(id) != nullptr;
  return record;
}

void SceneDocument::append_tree_order(EntityId id,
                                      std::vector<EntityId> &ids) const {
  if (require_entity(id) == entt::null) {
    return;
  }
  ids.push_back(id);
  for (const EntityId child : children(id)) {
    append_tree_order(child, ids);
  }
}

void SceneDocument::append_entity_records(
    EntityId id, int depth, EntityId active,
    std::vector<EntityRecord> &records) const {
  if (require_entity(id) == entt::null) {
    return;
  }
  // Instance definitions and their canonical children are internal scene
  // content. They remain in entities()/entity_tree_order() for persistence and
  // extraction, but never appear in the ordinary outliner projection.
  if (is_instance_definition_entity(id)) {
    return;
  }
  records.push_back(make_entity_record(id, depth, active));
  for (const EntityId child : children(id)) {
    append_entity_records(child, depth + 1, active, records);
  }
}

} // namespace quader
