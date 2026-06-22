////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/Types.h"
#include "editor/modeling/ModelingTypes.h"
#include "scene/Components.h"

#include <optional>
#include <string>
#include <unordered_map>
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
    AssetId material = InvalidAssetId;
};

/**
 * Tracks live runtime polygon object bindings and durable scene identity tombstones.
 */
class ModelingObjectRuntimeMap {
public:
    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] const std::unordered_map<int, EntityId>& entitiesByObject() const;
    [[nodiscard]] std::unordered_map<int, EntityId>& entitiesByObject();
    [[nodiscard]] const std::unordered_map<EntityId, int>& objectsByEntity() const;
    [[nodiscard]] std::unordered_map<EntityId, int>& objectsByEntity();
    [[nodiscard]] std::optional<EntityId> entityForModelingObject(int modelingObjectId) const;
    [[nodiscard]] std::optional<int> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<ModelingObjectSceneIdentity> sceneIdentityForModelingObject(int modelingObjectId) const;
    [[nodiscard]] std::vector<ModelingObjectRecord> records(const SceneDocument& scene) const;

    void bind(int modelingObjectId, EntityId entity);
    void rememberSceneEntity(int modelingObjectId, const SceneDocument& scene, EntityId entity);
    void unbindObject(int modelingObjectId);
    void clear();

private:
    std::unordered_map<int, EntityId> entitiesByObject_;
    std::unordered_map<EntityId, int> objectsByEntity_;
    std::unordered_map<int, ModelingObjectSceneIdentity> sceneIdentityByObject_;
};

} // namespace quader
