////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include "scene/scene_document.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

ModelingObjectSceneIdentity captureSceneIdentity(const SceneDocument& scene, EntityId entity) {
    ModelingObjectSceneIdentity identity;
    identity.entity = entity;
    identity.editorObjectId = scene.editorObjectId(entity);
    if (const NameComponent* name = scene.name(entity)) {
        identity.name = name->name;
    }
    identity.parent = scene.parent(entity);
    if (const TransformComponent* transform = scene.transform(entity)) {
        identity.transform = *transform;
    }
    if (const VisibilityComponent* visibility = scene.visibility(entity)) {
        identity.visibility = *visibility;
    }
    if (const LockComponent* lock = scene.lock(entity)) {
        identity.lock = *lock;
    }
    if (const MeshComponent* mesh = scene.mesh(entity)) {
        identity.mesh = *mesh;
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        identity.material = *material;
    }
    return identity;
}

} // namespace

std::size_t ModelingObjectRuntimeMap::size() const {
    return entitiesByObject_.size();
}

bool ModelingObjectRuntimeMap::empty() const {
    return entitiesByObject_.empty();
}

const std::unordered_map<int, EntityId>& ModelingObjectRuntimeMap::entitiesByObject() const {
    return entitiesByObject_;
}

const std::unordered_map<EntityId, int>& ModelingObjectRuntimeMap::objectsByEntity() const {
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

std::unordered_map<int, ModelingObjectSceneIdentity> ModelingObjectRuntimeMap::captureSceneIdentitiesForModelingObjects(
        const SceneDocument& scene,
        const std::unordered_set<int>& modelingObjectIds) const {
    std::unordered_map<int, ModelingObjectSceneIdentity> identities;
    identities.reserve(modelingObjectIds.size());
    for (const int modelingObjectId : modelingObjectIds) {
        const auto entity = entitiesByObject_.find(modelingObjectId);
        if (entity != entitiesByObject_.end() && scene.exists(entity->second)) {
            identities.emplace(modelingObjectId, captureSceneIdentity(scene, entity->second));
            continue;
        }
        const auto identity = sceneIdentityByObject_.find(modelingObjectId);
        if (identity != sceneIdentityByObject_.end()) {
            identities.emplace(modelingObjectId, identity->second);
        }
    }
    return identities;
}

std::vector<ModelingObjectSceneIdentity> ModelingObjectRuntimeMap::captureSceneIdentitiesForModelingObjects(
        const SceneDocument& scene,
        std::span<const int> modelingObjectIds) const {
    std::vector<ModelingObjectSceneIdentity> identities;
    identities.reserve(modelingObjectIds.size());
    for (const int modelingObjectId : modelingObjectIds) {
        const auto entity = entitiesByObject_.find(modelingObjectId);
        if (entity != entitiesByObject_.end() && scene.exists(entity->second)) {
            identities.push_back(captureSceneIdentity(scene, entity->second));
            continue;
        }
        const auto identity = sceneIdentityByObject_.find(modelingObjectId);
        if (identity != sceneIdentityByObject_.end()) {
            identities.push_back(identity->second);
        }
    }
    return identities;
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
    if (const auto existingEntity = entitiesByObject_.find(modelingObjectId);
            existingEntity != entitiesByObject_.end() && existingEntity->second != entity) {
        const auto reverse = objectsByEntity_.find(existingEntity->second);
        if (reverse != objectsByEntity_.end() && reverse->second == modelingObjectId) {
            objectsByEntity_.erase(reverse);
        }
    }
    if (const auto existingObject = objectsByEntity_.find(entity);
            existingObject != objectsByEntity_.end() && existingObject->second != modelingObjectId) {
        const auto forward = entitiesByObject_.find(existingObject->second);
        if (forward != entitiesByObject_.end() && forward->second == entity) {
            entitiesByObject_.erase(forward);
        }
    }
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

    sceneIdentityByObject_[modelingObjectId] = captureSceneIdentity(scene, entity);
}

void ModelingObjectRuntimeMap::restoreSceneIdentities(
        std::unordered_map<int, ModelingObjectSceneIdentity> identities) {
    sceneIdentityByObject_ = std::move(identities);
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
