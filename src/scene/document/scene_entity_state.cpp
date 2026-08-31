////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/scene_entity_state.hpp"

#include <utility>

namespace quader {
namespace {

template <typename Component>
void capture_optional_component(const Component *component,
                                std::optional<Component> &destination) {
  if (component != nullptr) {
    destination = *component;
  }
}

} // namespace

std::optional<SceneEntityState>
capture_scene_entity_state(const SceneDocument &scene, EntityId entity) {
  if (entity == kInvalidEntityId || !scene.exists(entity)) {
    return std::nullopt;
  }

  SceneEntityState state;
  state.id = entity;
  state.uuid = scene.uuid(entity);
  state.editor_object_id = scene.editor_object_id(entity);
  state.name = scene.name(entity) != nullptr ? scene.name(entity)->name
                                             : std::string{};
  state.kind = scene.kind(entity);
  state.parent = scene.parent(entity);
  state.sibling_ordinal = scene.sibling_ordinal(entity);
  if (const TransformComponent *component = scene.transform(entity)) {
    state.transform = *component;
  }
  if (const VisibilityComponent *component = scene.visibility(entity)) {
    state.visibility = *component;
  }
  if (const LockComponent *component = scene.lock(entity)) {
    state.lock = *component;
  }
  if (const SelectionComponent *component = scene.selection(entity)) {
    state.selection = *component;
  }
  state.active_selection = scene.primary_selection() == entity;
  capture_optional_component(scene.modeling_object(entity),
                             state.modeling_object);
  capture_optional_component(scene.mesh(entity), state.mesh);
  capture_optional_component(scene.curve(entity), state.curve);
  capture_optional_component(scene.material(entity), state.material);
  capture_optional_component(scene.texture_mapping(entity),
                             state.texture_mapping);
  capture_optional_component(scene.light(entity), state.light);
  capture_optional_component(scene.instance_definition(entity),
                             state.instance_definition);
  capture_optional_component(scene.instance_occurrence(entity),
                             state.instance_occurrence);
  state.scene_root = scene.is_scene_root(entity);
  capture_optional_component(scene.scene_settings(entity),
                             state.scene_settings);
  state.layer_id = scene.entity_layer(entity);
  return state;
}

std::optional<SceneEntityStateTree>
capture_scene_entity_state_tree(const SceneDocument &scene, EntityId entity) {
  std::optional<SceneEntityState> captured =
      capture_scene_entity_state(scene, entity);
  if (!captured.has_value()) {
    return std::nullopt;
  }

  SceneEntityStateTree tree;
  static_cast<SceneEntityState &>(tree) = std::move(*captured);
  const std::vector<EntityId> children = scene.children(entity);
  tree.children.reserve(children.size());
  for (const EntityId child : children) {
    std::optional<SceneEntityStateTree> child_state =
        capture_scene_entity_state_tree(scene, child);
    if (!child_state.has_value()) {
      return std::nullopt;
    }
    tree.children.push_back(std::move(*child_state));
  }
  return tree;
}

bool apply_scene_entity_common_state(SceneDocument &scene, EntityId entity,
                                     const SceneEntityState &state,
                                     std::string name) {
  if (!scene.exists(entity) ||
      !scene.rename_entity(entity, std::move(name)) ||
      !scene.set_transform(entity, state.transform) ||
      !scene.set_visible(entity, state.visibility.visible) ||
      !scene.set_selectable(entity, state.visibility.selectable) ||
      !scene.set_locked(entity, state.lock.locked) ||
      !scene.set_entity_kind(entity, state.kind)) {
    return false;
  }
  const bool mapping_applied = state.texture_mapping.has_value()
                                   ? scene.set_texture_mapping(
                                         entity, *state.texture_mapping)
                                   : scene.clear_texture_mapping(entity) ||
                                         scene.texture_mapping(entity) == nullptr;
  return mapping_applied && scene.set_entity_layer(entity, state.layer_id);
}

bool apply_scene_entity_state(SceneDocument &scene, EntityId entity,
                              const SceneEntityState &state,
                              SceneEntityStateApplyOptions options) {
  if (!scene.exists(entity) ||
      !scene.set_transform(entity, state.transform) ||
      !scene.set_visible(entity, state.visibility.visible) ||
      !scene.set_selectable(entity, state.visibility.selectable) ||
      !scene.set_locked(entity, state.lock.locked)) {
    return false;
  }

  // Remove components that are absent from the captured state before adding
  // the desired inventory. This ordering also clears mutually exclusive scene
  // roles before their replacement is installed.
  if (options.include_instance_roles) {
    if (!state.instance_occurrence.has_value() &&
        scene.instance_occurrence(entity) != nullptr &&
        !scene.clear_instance_occurrence(entity)) {
      return false;
    }
    if (!state.instance_definition.has_value() &&
        scene.instance_definition(entity) != nullptr &&
        !scene.clear_instance_definition(entity)) {
      return false;
    }
  }

  if (!state.mesh.has_value() && scene.mesh(entity) != nullptr &&
      !scene.clear_mesh(entity)) {
    return false;
  }
  if (!state.curve.has_value() && scene.curve(entity) != nullptr &&
      !scene.clear_curve(entity)) {
    return false;
  }
  if (!state.material.has_value() && scene.material(entity) != nullptr &&
      !scene.clear_material(entity)) {
    return false;
  }
  if (!state.light.has_value() && scene.light(entity) != nullptr &&
      !scene.clear_light(entity)) {
    return false;
  }
  if (options.include_modeling_object && !state.modeling_object.has_value() &&
      scene.modeling_object(entity) != nullptr &&
      !scene.clear_modeling_object(entity)) {
    return false;
  }
  if (options.include_scene_settings && !state.scene_settings.has_value() &&
      scene.scene_settings(entity) != nullptr &&
      !scene.clear_scene_settings(entity)) {
    return false;
  }
  if (options.include_texture_mapping &&
      !state.texture_mapping.has_value() &&
      scene.texture_mapping(entity) != nullptr &&
      !scene.clear_texture_mapping(entity)) {
    return false;
  }

  if (state.mesh.has_value() && !scene.set_mesh(entity, *state.mesh)) {
    return false;
  }
  if (state.curve.has_value() && !scene.set_curve(entity, *state.curve)) {
    return false;
  }
  if (state.material.has_value() &&
      !scene.set_material(entity, *state.material)) {
    return false;
  }
  if (state.light.has_value() && !scene.set_light(entity, *state.light)) {
    return false;
  }
  if (options.include_modeling_object && state.modeling_object.has_value() &&
      !scene.set_modeling_object(entity, *state.modeling_object)) {
    return false;
  }
  if (options.include_scene_settings && state.scene_settings.has_value() &&
      !scene.set_scene_settings(entity, *state.scene_settings)) {
    return false;
  }
  if (options.include_instance_roles) {
    if (state.instance_definition.has_value() &&
        !scene.set_instance_definition(entity)) {
      return false;
    }
    if (state.instance_occurrence.has_value() &&
        !scene.set_instance_occurrence(entity, *state.instance_occurrence)) {
      return false;
    }
  }
  if (options.include_texture_mapping &&
      state.texture_mapping.has_value() &&
      !scene.set_texture_mapping(entity, *state.texture_mapping)) {
    return false;
  }
  if ((options.include_instance_roles || state.kind != EntityKind::Instance) &&
      scene.kind(entity) != state.kind &&
      !scene.set_entity_kind(entity, state.kind)) {
    return false;
  }
  if (options.include_layer_membership &&
      !scene.set_entity_layer(entity, state.layer_id)) {
    return false;
  }
  if (options.include_selection &&
      !scene.set_selected(entity, state.selection.selected)) {
    return false;
  }
  if (options.include_selection && state.active_selection &&
      !scene.set_active_selection(entity)) {
    return false;
  }
  return !options.include_hierarchy_placement ||
         apply_scene_entity_state_placement(scene, entity, state,
                                            state.parent);
}

bool apply_scene_entity_state_placement(SceneDocument &scene, EntityId entity,
                                        const SceneEntityState &state,
                                        EntityId parent) {
  if (!scene.exists(entity) ||
      (parent != kInvalidEntityId && !scene.exists(parent))) {
    return false;
  }
  if (scene.parent(entity) != parent &&
      !scene.set_parent(entity, parent, false)) {
    return false;
  }
  if (state.sibling_ordinal.has_value() && parent != kInvalidEntityId &&
      !scene.set_sibling_ordinal(entity, *state.sibling_ordinal)) {
    return false;
  }
  return true;
}

EntityId create_scene_entity_from_state(
    SceneDocument &scene, const SceneEntityState &state, EntityId parent,
    std::string name, SceneEntityStateIdentityMode identity_mode,
    SceneEntityStateApplyOptions options) {
  const bool preserve_identity =
      identity_mode == SceneEntityStateIdentityMode::Preserve;
  EntityId entity = preserve_identity
                        ? scene.create_entity_with_id(
                              state.id,
                              name.empty() ? state.name : std::move(name),
                              state.kind, state.editor_object_id)
                        : scene.create_entity(
                              name.empty() ? state.name : std::move(name),
                              state.kind);
  if (entity == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  const auto rollback = [&]() {
    if (scene.exists(entity)) {
      (void)scene.destroy_entity(entity);
    }
    return kInvalidEntityId;
  };
  if (preserve_identity && !state.uuid.empty() &&
      !scene.set_entity_uuid(entity, state.uuid)) {
    return rollback();
  }
  if (!apply_scene_entity_state(scene, entity, state, options)) {
    return rollback();
  }
  if (preserve_identity) {
    if (!apply_scene_entity_state_placement(scene, entity, state, parent)) {
      return rollback();
    }
  } else if (parent != kInvalidEntityId &&
             (!scene.exists(parent) ||
              !scene.set_parent(entity, parent, false))) {
    return rollback();
  }
  return entity;
}

} // namespace quader
