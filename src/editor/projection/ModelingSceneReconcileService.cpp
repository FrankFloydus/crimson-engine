////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/ModelingSceneReconcileService.h"

#include "editor/modeling/runtime/ModelingObjectRuntimeMap.h"
#include "editor/modeling/services/SceneModelingSyncService.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {
namespace {

std::unordered_map<std::uint32_t, AssetId> materialSlotsForLiveModelingNodes(
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectMap,
        std::optional<AssetId>& firstModelingMaterial) {
    std::unordered_map<std::uint32_t, AssetId> materialBySlot;
    for (const auto& [modelingObjectId, entity] : objectMap.entitiesByObject()) {
        (void)modelingObjectId;
        if (!scene.exists(entity)) {
            continue;
        }
        const ModelingObjectComponent* modelingObject = scene.modelingObject(entity);
        const MaterialComponent* material = scene.material(entity);
        if (modelingObject == nullptr || material == nullptr || material->material == InvalidAssetId) {
            continue;
        }
        materialBySlot.try_emplace(modelingObject->materialSlot, material->material);
        if (!firstModelingMaterial) {
            firstModelingMaterial = material->material;
        }
    }
    return materialBySlot;
}

} // namespace

void ModelingSceneReconcileService::reconcileModelingObjectSceneNodes(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        SceneModelingSyncService& sceneSync) {
    ModelingObjectRuntimeMap& objectMap = sceneSync.objectMap();

    std::optional<AssetId> firstModelingMaterial;
    const std::unordered_map<std::uint32_t, AssetId> materialBySlot =
            materialSlotsForLiveModelingNodes(scene, objectMap, firstModelingMaterial);

    std::unordered_set<int> liveObjects;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        liveObjects.insert(object.id);
        const auto existing = objectMap.entitiesByObject().find(object.id);
        if (existing != objectMap.entitiesByObject().end() && scene.exists(existing->second)) {
            continue;
        }

        const std::optional<ModelingObjectSceneIdentity> identity =
                objectMap.sceneIdentityForModelingObject(object.id);
        const std::string name = identity.has_value() && !identity->name.empty()
                ? identity->name
                : SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh);
        const EntityId entity = identity.has_value() && identity->entity != InvalidEntityId
                ? scene.createEntityWithId(identity->entity, name, EntityKind::Mesh, identity->editorObjectId)
                : scene.createEntity(name, EntityKind::Mesh);
        if (entity == InvalidEntityId) {
            continue;
        }

        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        scene.setModelingObject(entity, ModelingObjectComponent{
                .objectId = object.id,
                .materialSlot = materialSlot,
        });
        const auto material = materialBySlot.find(materialSlot);
        if (material != materialBySlot.end()) {
            scene.setMaterial(entity, MaterialComponent{.material = material->second});
        } else if (identity.has_value() && identity->material != InvalidAssetId) {
            scene.setMaterial(entity, MaterialComponent{.material = identity->material});
        } else if (firstModelingMaterial) {
            scene.setMaterial(entity, MaterialComponent{.material = *firstModelingMaterial});
        }

        objectMap.bind(object.id, entity);
        objectMap.rememberSceneEntity(object.id, scene, entity);
    }

    std::vector<int> staleObjects;
    for (const auto& [modelingObjectId, entity] : objectMap.entitiesByObject()) {
        if (!liveObjects.contains(modelingObjectId)) {
            staleObjects.push_back(modelingObjectId);
        }
        (void)entity;
    }

    for (const int modelingObjectId : staleObjects) {
        const std::optional<EntityId> entity = objectMap.entityForModelingObject(modelingObjectId);
        if (entity.has_value() && scene.exists(*entity)) {
            objectMap.rememberSceneEntity(modelingObjectId, scene, *entity);
            scene.destroyEntity(*entity);
        }
        objectMap.unbindObject(modelingObjectId);
    }
}

} // namespace quader
