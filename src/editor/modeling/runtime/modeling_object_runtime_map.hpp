////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "scene/components.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

class SceneDocument;

/**
 * Stores the editor-side scene identity last associated with a runtime modeling object.
 */
struct ModelingObjectSceneIdentity {
  EntityId entity = kInvalidEntityId;
  EditorObjectId editor_object_id = kInvalidEditorObjectId;
  std::string uuid;
  std::string name;
  EntityId parent = kInvalidEntityId;
  TransformComponent transform;
  VisibilityComponent visibility;
  LockComponent lock;
  std::optional<MeshComponent> mesh;
  std::optional<MaterialComponent> material;
};

/**
 * Tracks live runtime polygon object bindings and durable scene identity tombstones.
 */
class ModelingObjectRuntimeMap {
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
    [[nodiscard]] std::vector<ModelingObjectRecord> records(const SceneDocument& scene) const;

    void bind(int modeling_object_id, EntityId entity);
    void remember_scene_entity(int modeling_object_id,
                               const SceneDocument &scene, EntityId entity);
    void restore_scene_identities(
        std::unordered_map<int, ModelingObjectSceneIdentity> identities);
    void unbind_object(int modeling_object_id);
    void clear();

private:
  std::unordered_map<int, EntityId> entities_by_object_;
  std::unordered_map<EntityId, int> objects_by_entity_;
  std::unordered_map<int, ModelingObjectSceneIdentity>
      scene_identity_by_object_;
  std::uint64_t revision_ = 0;
};

} // namespace quader
