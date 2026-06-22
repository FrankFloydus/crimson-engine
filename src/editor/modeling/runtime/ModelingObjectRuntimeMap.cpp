////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"

#include "scene/SceneDocument.h"

#include <algorithm>
#include <utility>

namespace quader {

std::size_t ModelingObjectRuntimeMap::size() const {
    return entitiesByObject_.size();
}

bool ModelingObjectRuntimeMap::empty() const {
    return entitiesByObject_.empty();
}

const std::unordered_map<int, EntityId>& ModelingObjectRuntimeMap::entitiesByObject() const {
    return entitiesByObject_;
}

std::unordered_map<int, EntityId>& ModelingObjectRuntimeMap::entitiesByObject() {
    return entitiesByObject_;
}

const std::unordered_map<EntityId, int>& ModelingObjectRuntimeMap::objectsByEntity() const {
    return objectsByEntity_;
}

std::unordered_map<EntityId, int>& ModelingObjectRuntimeMap::objectsByEntity() {
    return objectsByEntity_;
}

std::optional<EntityId> ModelingObjectRuntimeMap::entityForModelingObject(int modelingObjectId) const {
    const auto it = entitiesByObject_.find(modelingObjectId);
    if (it == entitiesByObject_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<int> ModelingObjectRuntimeMap::modelingObjectForEntity(EntityId entity) const {
    const auto it = objectsByEntity_.find(entity);
    if (it == objectsByEntity_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<ModelingObjectSceneIdentity> ModelingObjectRuntimeMap::sceneIdentityForModelingObject(
        int modelingObjectId) const {
    const auto it = sceneIdentityByObject_.find(modelingObjectId);
    if (it == sceneIdentityByObject_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::vector<ModelingObjectRecord> ModelingObjectRuntimeMap::records(const SceneDocument& scene) const {
    std::vector<ModelingObjectRecord> records;
    records.reserve(entitiesByObject_.size());
    for (const auto& [modelingObjectId, entity] : entitiesByObject_) {
        if (!scene.exists(entity)) {
            continue;
        }
        ModelingObjectRecord record;
        record.modelingObjectId = modelingObjectId;
        record.entity = entity;
        if (const MeshComponent* mesh = scene.mesh(entity)) {
            record.mesh = mesh->mesh;
        }
        if (const NameComponent* name = scene.name(entity)) {
            record.name = name->name;
        }
        records.push_back(std::move(record));
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.entity < rhs.entity;
    });
    return records;
}

void ModelingObjectRuntimeMap::bind(int modelingObjectId, EntityId entity) {
    entitiesByObject_[modelingObjectId] = entity;
    objectsByEntity_[entity] = modelingObjectId;
}

void ModelingObjectRuntimeMap::rememberSceneEntity(
        int modelingObjectId,
        const SceneDocument& scene,
        EntityId entity) {
    if (modelingObjectId == InvalidModelingObjectId || entity == InvalidEntityId || !scene.exists(entity)) {
        return;
    }

    ModelingObjectSceneIdentity identity;
    identity.entity = entity;
    identity.editorObjectId = scene.editorObjectId(entity);
    if (const NameComponent* name = scene.name(entity)) {
        identity.name = name->name;
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        identity.material = material->material;
    }
    sceneIdentityByObject_[modelingObjectId] = std::move(identity);
}

void ModelingObjectRuntimeMap::unbindObject(int modelingObjectId) {
    const auto it = entitiesByObject_.find(modelingObjectId);
    if (it == entitiesByObject_.end()) {
        return;
    }
    objectsByEntity_.erase(it->second);
    entitiesByObject_.erase(it);
}

void ModelingObjectRuntimeMap::clear() {
    entitiesByObject_.clear();
    objectsByEntity_.clear();
    sceneIdentityByObject_.clear();
}

} // namespace quader
