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
    EntityId entity = InvalidEntityId;
    EditorObjectId editorObjectId = InvalidEditorObjectId;
    std::string name;
    EntityId parent = InvalidEntityId;
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
    [[nodiscard]] std::optional<EntityId> entityForModelingObject(int modelingObjectId) const;
    [[nodiscard]] std::optional<int> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::vector<std::pair<int, EntityId>> objectEntityBindingsByModelingObject() const;
    [[nodiscard]] std::optional<ModelingObjectSceneIdentity> sceneIdentityForModelingObject(int modelingObjectId) const;
    [[nodiscard]] std::unordered_map<int, ModelingObjectSceneIdentity> captureSceneIdentitiesForModelingObjects(
            const SceneDocument& scene,
            const std::unordered_set<int>& modelingObjectIds) const;
    [[nodiscard]] std::vector<ModelingObjectSceneIdentity> captureSceneIdentitiesForModelingObjects(
            const SceneDocument& scene,
            std::span<const int> modelingObjectIds) const;
    [[nodiscard]] std::vector<ModelingObjectRecord> records(const SceneDocument& scene) const;

    void bind(int modelingObjectId, EntityId entity);
    void rememberSceneEntity(int modelingObjectId, const SceneDocument& scene, EntityId entity);
    void restoreSceneIdentities(std::unordered_map<int, ModelingObjectSceneIdentity> identities);
    void unbindObject(int modelingObjectId);
    void clear();

private:
    std::unordered_map<int, EntityId> entitiesByObject_;
    std::unordered_map<EntityId, int> objectsByEntity_;
    std::unordered_map<int, ModelingObjectSceneIdentity> sceneIdentityByObject_;
};

} // namespace quader
