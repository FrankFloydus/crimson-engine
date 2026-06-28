////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"

#include "assets/asset_registry.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/scene_modeling_sync_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

std::unordered_map<std::uint32_t, AssetId> materialSlotsForLiveModelingNodes(
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectMap,
        std::optional<AssetId>& firstModelingMaterial) {
    std::unordered_map<std::uint32_t, AssetId> materialBySlot;
    const std::vector<std::pair<int, EntityId>> entitiesByObject =
            objectMap.objectEntityBindingsByModelingObject();

    for (const auto& [modelingObjectId, entity] : entitiesByObject) {
        (void)modelingObjectId;
        if (!scene.exists(entity)) {
            continue;
        }
        const ModelingObjectComponent* modelingObject = scene.modelingObject(entity);
        const MaterialComponent* material = scene.material(entity);
        if (modelingObject == nullptr || material == nullptr || material->material == InvalidAssetId) {
            continue;
        }
        if (!firstModelingMaterial) {
            firstModelingMaterial = material->material;
        }
        materialBySlot.try_emplace(modelingObject->materialSlot, material->material);
    }
    return materialBySlot;
}

std::optional<MaterialComponent> materialComponentForModelingSlot(
        std::uint32_t materialSlot,
        const std::unordered_map<std::uint32_t, AssetId>& materialBySlot,
        std::optional<AssetId> firstModelingMaterial,
        const MaterialComponent* existingMaterial) {
    AssetId primaryMaterial = InvalidAssetId;
    if (existingMaterial != nullptr && existingMaterial->material != InvalidAssetId) {
        primaryMaterial = existingMaterial->material;
    } else if (const auto material = materialBySlot.find(materialSlot); material != materialBySlot.end()) {
        primaryMaterial = material->second;
    } else if (firstModelingMaterial.has_value()) {
        primaryMaterial = *firstModelingMaterial;
    }
    if (primaryMaterial == InvalidAssetId) {
        return std::nullopt;
    }
    return MaterialComponent{
            .material = primaryMaterial,
            .materialSlots = materialBySlot,
    };
}

void syncMaterialSlotBindings(
        SceneDocument& scene,
        EntityId entity,
        std::uint32_t materialSlot,
        const std::unordered_map<std::uint32_t, AssetId>& materialBySlot,
        std::optional<AssetId> firstModelingMaterial) {
    const MaterialComponent* currentMaterial = scene.material(entity);
    const std::optional<MaterialComponent> nextMaterial =
            materialComponentForModelingSlot(materialSlot, materialBySlot, firstModelingMaterial, currentMaterial);
    if (!nextMaterial.has_value()) {
        return;
    }
    if (currentMaterial == nullptr || *currentMaterial != *nextMaterial) {
        scene.setMaterial(entity, *nextMaterial);
    }
}

void applyDurableSceneIdentityToEntity(
        SceneDocument& scene,
        EntityId entity,
        const ModelingObjectSceneIdentity& identity) {
    scene.setTransform(entity, identity.transform);
    if (identity.parent != InvalidEntityId && scene.exists(identity.parent)) {
        (void)scene.setParent(entity, identity.parent, false);
    }
    scene.setVisible(entity, identity.visibility.visible);
    scene.setLocked(entity, identity.lock.locked);
}

void reconcileModelingObjectSceneNodes(
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
        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        const std::optional<EntityId> existing = objectMap.entityForModelingObject(object.id);
        if (existing.has_value() && scene.exists(*existing)) {
            syncMaterialSlotBindings(scene, *existing, materialSlot, materialBySlot, firstModelingMaterial);
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

        scene.setModelingObject(entity, ModelingObjectComponent{
                .objectId = object.id,
                .materialSlot = materialSlot,
        });
        if (identity.has_value()) {
            applyDurableSceneIdentityToEntity(scene, entity, *identity);
        }
        const MaterialComponent* identityMaterial =
                identity.has_value() && identity->material.has_value() ? &*identity->material : nullptr;
        if (const std::optional<MaterialComponent> material =
                    materialComponentForModelingSlot(materialSlot, materialBySlot, firstModelingMaterial, identityMaterial)) {
            scene.setMaterial(entity, *material);
        }

        objectMap.bind(object.id, entity);
    }

    std::vector<int> staleObjects;
    for (const auto& [modelingObjectId, entity] : objectMap.objectEntityBindingsByModelingObject()) {
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

void projectModelingMeshesToScene(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        bool force) {
    if (!force && sceneSync.sceneObjectsSyncedForCurrentModelingContent(session, scene)) {
        return;
    }

    ModelingObjectRuntimeMap& objectRegistry = sceneSync.objectMap();
    bool projectedAllObjects = true;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        const std::optional<EntityId> existing = objectRegistry.entityForModelingObject(object.id);
        if (!existing.has_value() || !scene.exists(*existing)) {
            projectedAllObjects = false;
            continue;
        }

        const EntityId entity = *existing;
        MeshAsset mesh = MeshProjectionService::compileObjectMesh(object);
        if (const NameComponent* name = scene.name(entity)) {
            mesh.name = name->name;
        }
        AssetId meshId = InvalidAssetId;
        if (const MeshComponent* meshComponent = scene.mesh(entity)) {
            meshId = meshComponent->mesh;
        }
        bool meshContentsReplaced = false;
        if (meshId == InvalidAssetId) {
            meshId = assets.addMesh(std::move(mesh));
        } else if (assets.replaceMesh(meshId, std::move(mesh))) {
            meshContentsReplaced = true;
        } else {
            MeshAsset replacement = MeshProjectionService::compileObjectMesh(object);
            if (const NameComponent* name = scene.name(entity)) {
                replacement.name = name->name;
            }
            meshId = assets.addMesh(std::move(replacement));
        }
        const ModelingObjectComponent nextObject{
                .objectId = object.id,
                .materialSlot = materialSlot,
        };
        const ModelingObjectComponent* currentObject = scene.modelingObject(entity);
        if (currentObject == nullptr ||
                currentObject->objectId != nextObject.objectId ||
                currentObject->materialSlot != nextObject.materialSlot) {
            scene.setModelingObject(entity, nextObject);
        }
        const MeshComponent* currentMesh = scene.mesh(entity);
        if (currentMesh == nullptr || currentMesh->mesh != meshId || meshContentsReplaced) {
            const std::optional<ModelingObjectSceneIdentity> identity =
                    objectRegistry.sceneIdentityForModelingObject(object.id);
            MeshComponent nextMesh = currentMesh != nullptr
                    ? *currentMesh
                    : (identity.has_value() && identity->mesh.has_value() ? *identity->mesh : MeshComponent{});
            nextMesh.mesh = meshId;
            scene.setMesh(entity, nextMesh);
        }
        objectRegistry.rememberSceneEntity(object.id, scene, entity);
    }
    if (projectedAllObjects) {
        sceneSync.markSceneObjectsSynced(session.state().content_revision);
    }
}

} // namespace

bool ModelingMeshSyncService::shouldForceRefresh(bool previewActive, bool previewFinished) {
    return previewActive || previewFinished;
}

std::optional<MeshAsset> ModelingMeshSyncService::compileObjectMesh(
        const quader_editor_core::PolygonObject& object) const {
    return MeshProjectionService::compileObjectMesh(object);
}

void ModelingMeshSyncService::applySceneIdentityToEntity(
        SceneDocument& scene,
        EntityId entity,
        const ModelingObjectSceneIdentity& identity) const {
    applyDurableSceneIdentityToEntity(scene, entity, identity);
}

void ModelingMeshSyncService::applySceneIdentityToDuplicateEntity(
        SceneDocument& scene,
        EntityId entity,
        const ModelingObjectSceneIdentity& identity) const {
    applyDurableSceneIdentityToEntity(scene, entity, identity);
    if (identity.mesh.has_value()) {
        if (const MeshComponent* duplicateMesh = scene.mesh(entity)) {
            MeshComponent nextMesh = *identity.mesh;
            nextMesh.mesh = duplicateMesh->mesh;
            scene.setMesh(entity, nextMesh);
        }
    }
    if (identity.material.has_value()) {
        scene.setMaterial(entity, *identity.material);
    } else {
        scene.clearMaterial(entity);
    }
}

void ModelingMeshSyncService::syncSceneObjectsFromModeling(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        bool force) const {
    if (!force && sceneSync.sceneObjectsSyncedForCurrentModelingContent(session, scene)) {
        return;
    }
    reconcileModelingObjectSceneNodes(session, scene, sceneSync);
    projectModelingMeshesToScene(session, scene, assets, sceneSync, force);
}

ModelingCreateBoxResult ModelingMeshSyncService::createSceneObjectFromLatest(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        const std::string& name,
        const std::string& message) const {
    const quader_editor_core::PolygonObject& object = session.objects().back();
    MeshAsset mesh = MeshProjectionService::compileObjectMesh(object);
    mesh.name = name;
    const AssetId meshId = assets.addMesh(std::move(mesh));

    const EntityId entity = scene.createEntity(name);
    scene.setModelingObject(entity, ModelingObjectComponent{
            .objectId = object.id,
            .materialSlot = static_cast<std::uint32_t>(object.material_id),
    });
    scene.setMesh(entity, MeshComponent{.mesh = meshId});
    scene.clearSelection();
    scene.setSelected(entity, true);

    sceneSync.objectMap().bind(object.id, entity);
    sceneSync.markSceneObjectsSynced(session.state().content_revision);

    return {
            .ok = true,
            .changed = true,
            .message = message,
            .object = {
                    .modelingObjectId = object.id,
                    .entity = entity,
                    .mesh = meshId,
                    .name = name,
            },
    };
}

} // namespace quader
