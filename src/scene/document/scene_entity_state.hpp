////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/**
 * Owns the complete, renderer-neutral authored state of one scene entity.
 *
 * Persistence, history, clipboard, and structural commands share this value so
 * adding a scene component cannot silently create another partial component
 * inventory. Selection is captured for exact editor-state restoration but is
 * deliberately excluded by persistence codecs.
 */
struct SceneEntityState {
  EntityId id = kInvalidEntityId;
  std::string uuid;
  EditorObjectId editor_object_id = kInvalidEditorObjectId;
  std::string name;
  EntityKind kind = EntityKind::Generic;
  EntityId parent = kInvalidEntityId;
  std::optional<std::size_t> sibling_ordinal;
  TransformComponent transform;
  VisibilityComponent visibility;
  SelectableComponent selectable;
  SelectionComponent selection;
  bool active_selection = false;
  std::optional<ModelingObjectComponent> modeling_object;
  std::optional<MeshComponent> mesh;
  std::optional<CurveComponent> curve;
  std::optional<MaterialComponent> material;
  std::optional<TextureMappingComponent> texture_mapping;
  std::optional<LightComponent> light;
  std::optional<InstanceDefinitionComponent> instance_definition;
  std::optional<InstanceOccurrenceComponent> instance_occurrence;
  SceneLayerId layer_id = kInvalidSceneLayerId;
};

/** Owns one entity state and its authored child order. */
struct SceneEntityStateTree : SceneEntityState {
  std::vector<SceneEntityStateTree> children;
};

/** Controls which editor-only domains are restored from an entity state. */
struct SceneEntityStateApplyOptions {
  bool include_modeling_object = true;
  bool include_selection = false;
  bool include_instance_roles = true;
  bool include_texture_mapping = true;
  bool include_layer_membership = true;
  bool include_hierarchy_placement = false;
};

/** Selects whether applying captured state allocates or preserves entity identities. */
enum class SceneEntityStateIdentityMode {
  AllocateNew,
  Preserve,
};

[[nodiscard]] std::optional<SceneEntityState>
capture_scene_entity_state(const SceneDocument &scene, EntityId entity);

[[nodiscard]] std::optional<SceneEntityStateTree>
capture_scene_entity_state_tree(const SceneDocument &scene, EntityId entity);

/** Applies the common placement and presentation fields used by clone reuse. */
[[nodiscard]] bool apply_scene_entity_common_state(
    SceneDocument &scene, EntityId entity, const SceneEntityState &state,
    std::string name);

/** Applies the complete component inventory to an existing fresh entity. */
[[nodiscard]] bool apply_scene_entity_state(
    SceneDocument &scene, EntityId entity, const SceneEntityState &state,
    SceneEntityStateApplyOptions options = {});

/** Restores the captured parent and authored sibling position. */
[[nodiscard]] bool apply_scene_entity_state_placement(
    SceneDocument &scene, EntityId entity, const SceneEntityState &state,
    EntityId parent);

/** Creates one entity and restores the scene-owned state in one checked path. */
[[nodiscard]] EntityId create_scene_entity_from_state(
    SceneDocument &scene, const SceneEntityState &state, EntityId parent,
    std::string name, SceneEntityStateIdentityMode identity_mode,
    SceneEntityStateApplyOptions options = {});

} // namespace quader
