////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_mesh_sync_service.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/modeling/services/scene_modeling_sync_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kProjectedMeshSignatureSeed = 1469598103934665603ull;
constexpr std::uint64_t kProjectedMeshSignaturePrime = 1099511628211ull;

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * kProjectedMeshSignaturePrime;
}

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, int value) {
    return mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, std::uint32_t value) {
    return mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixProjectedMeshSignature(hash, bits);
}

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, QVec2 value) {
    hash = mixProjectedMeshSignature(hash, value.x);
    hash = mixProjectedMeshSignature(hash, value.y);
    return hash;
}

std::uint64_t mixProjectedMeshSignature(std::uint64_t hash, QVec3 value) {
    hash = mixProjectedMeshSignature(hash, value.x);
    hash = mixProjectedMeshSignature(hash, value.y);
    hash = mixProjectedMeshSignature(hash, value.z);
    return hash;
}

std::uint64_t projectedMeshSignature(const quader_editor_core::PolygonObject& object) {
    std::uint64_t hash = mixProjectedMeshSignature(kProjectedMeshSignatureSeed, object.id);
    hash = mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(object.document.vertices.size()));
    hash = mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(object.document.faces.size()));
    hash = mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(object.document.hard_normal_edges.size()));
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        hash = mixProjectedMeshSignature(hash, vertex.id);
        hash = mixProjectedMeshSignature(hash, vertex.position);
    }
    for (const quader_poly::Face& face : object.document.faces) {
        hash = mixProjectedMeshSignature(hash, face.id);
        hash = mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(face.vertices.size()));
        hash = mixProjectedMeshSignature(hash, static_cast<std::uint64_t>(face.uvs.size()));
        hash = mixProjectedMeshSignature(hash, face.material_slot);
        hash = mixProjectedMeshSignature(hash, static_cast<int>(face.normal_shading));
        for (const quader_poly::ElementId vertexId : face.vertices) {
            hash = mixProjectedMeshSignature(hash, vertexId);
        }
        for (const QVec2 uv : face.uvs) {
            hash = mixProjectedMeshSignature(hash, uv);
        }
    }
    for (const quader_poly::Edge edge : object.document.hard_normal_edges) {
        hash = mixProjectedMeshSignature(hash, edge.a);
        hash = mixProjectedMeshSignature(hash, edge.b);
    }
    return hash;
}

std::unordered_map<std::uint32_t, AssetId> materialSlotsForLiveModelingNodes(
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectMap,
        std::optional<AssetId>& firstModelingMaterial) {
    QDR_PROFILE_FUNCTION();
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

EntityId modelingDefaultParent(const SceneDocument& scene) {
    return scene.sceneRoot();
}

bool vec2Equals(QVec2 left, QVec2 right) {
    return left.x == right.x && left.y == right.y;
}

bool vec3Equals(QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool meshVerticesEqual(const std::vector<MeshVertex>& left, const std::vector<MeshVertex>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!vec3Equals(left[index].position, right[index].position) ||
                !vec3Equals(left[index].normal, right[index].normal) ||
                !vec2Equals(left[index].uv, right[index].uv) ||
                left[index].rgba != right[index].rgba) {
            return false;
        }
    }
    return true;
}

bool meshPrimitiveSpansEqual(const MeshAsset& left, const MeshAsset& right) {
    const std::vector<MeshPrimitiveSpan> leftSpans = normalizedMeshPrimitiveSpans(left);
    const std::vector<MeshPrimitiveSpan> rightSpans = normalizedMeshPrimitiveSpans(right);
    if (leftSpans.size() != rightSpans.size()) {
        return false;
    }
    for (std::size_t index = 0; index < leftSpans.size(); ++index) {
        if (leftSpans[index].indexOffset != rightSpans[index].indexOffset ||
                leftSpans[index].indexCount != rightSpans[index].indexCount ||
                leftSpans[index].materialSlot != rightSpans[index].materialSlot) {
            return false;
        }
    }
    return true;
}

bool polygonVerticesEqual(
        const std::vector<quader_poly::Vertex>& left,
        const std::vector<quader_poly::Vertex>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
                !vec3Equals(left[index].position, right[index].position)) {
            return false;
        }
    }
    return true;
}

bool polygonFacesEqual(
        const std::vector<quader_poly::Face>& left,
        const std::vector<quader_poly::Face>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].id != right[index].id ||
                left[index].vertices != right[index].vertices ||
                left[index].material_slot != right[index].material_slot ||
                left[index].normal_shading != right[index].normal_shading ||
                left[index].uvs.size() != right[index].uvs.size()) {
            return false;
        }
        for (std::size_t uvIndex = 0; uvIndex < left[index].uvs.size(); ++uvIndex) {
            if (!vec2Equals(left[index].uvs[uvIndex], right[index].uvs[uvIndex])) {
                return false;
            }
        }
    }
    return true;
}

bool polygonDocumentsEqual(const quader_poly::Document& left, const quader_poly::Document& right) {
    return polygonVerticesEqual(left.vertices, right.vertices) &&
            polygonFacesEqual(left.faces, right.faces) &&
            left.hard_normal_edges == right.hard_normal_edges &&
            left.next_vertex_id == right.next_vertex_id &&
            left.next_face_id == right.next_face_id;
}

bool authoredPolygonDocumentsEqual(const MeshAsset& left, const MeshAsset& right) {
    if (left.hasAuthoredPolygonDocument != right.hasAuthoredPolygonDocument) {
        return false;
    }
    if (!left.hasAuthoredPolygonDocument) {
        return true;
    }
    if (!left.authoredPolygonDocument || !right.authoredPolygonDocument) {
        return false;
    }
    return polygonDocumentsEqual(*left.authoredPolygonDocument, *right.authoredPolygonDocument);
}

bool modelingMeshAssetsEqual(const MeshAsset& left, const MeshAsset& right) {
    return left.name == right.name &&
            left.hasStableCpuTopology == right.hasStableCpuTopology &&
            meshVerticesEqual(left.vertices, right.vertices) &&
            left.indices == right.indices &&
            meshPrimitiveSpansEqual(left, right) &&
            authoredPolygonDocumentsEqual(left, right);
}

void syncMaterialSlotBindings(
        SceneDocument& scene,
        EntityId entity,
        std::uint32_t materialSlot,
        const std::unordered_map<std::uint32_t, AssetId>& materialBySlot,
        std::optional<AssetId> firstModelingMaterial) {
    QDR_PROFILE_FUNCTION();
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
    {
        QDR_PROFILE_SCOPE("Modeling mesh sync apply identity transform");
        scene.setTransform(entity, identity.transform);
    }
    const EntityId parent = identity.parent != InvalidEntityId && scene.exists(identity.parent)
            ? identity.parent
            : modelingDefaultParent(scene);
    if (parent != InvalidEntityId && scene.exists(parent)) {
        QDR_PROFILE_SCOPE("Modeling mesh sync apply identity parent");
        (void)scene.setParent(entity, parent, false);
    }
    {
        QDR_PROFILE_SCOPE("Modeling mesh sync apply identity visibility");
        scene.setVisible(entity, identity.visibility.visible);
    }
    {
        QDR_PROFILE_SCOPE("Modeling mesh sync apply identity lock");
        scene.setLocked(entity, identity.lock.locked);
    }
}

void reconcileModelingObjectSceneNodes(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        SceneModelingSyncService& sceneSync) {
    QDR_PROFILE_FUNCTION();
    ModelingObjectRuntimeMap& objectMap = sceneSync.objectMap();

    std::optional<AssetId> firstModelingMaterial;
    const std::unordered_map<std::uint32_t, AssetId> materialBySlot =
            materialSlotsForLiveModelingNodes(scene, objectMap, firstModelingMaterial);
    SceneEntityNameAllocator sceneNames(scene);

    std::unordered_set<int> liveObjects;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        QDR_PROFILE_SCOPE("Modeling mesh sync reconcile object node");
        liveObjects.insert(object.id);
        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        const std::optional<EntityId> existing = objectMap.entityForModelingObject(object.id);
        if (existing.has_value() && scene.exists(*existing)) {
            syncMaterialSlotBindings(scene, *existing, materialSlot, materialBySlot, firstModelingMaterial);
            continue;
        }

        const std::optional<ModelingObjectSceneIdentity> identity = [&]() {
            QDR_PROFILE_SCOPE("Modeling mesh sync lookup scene identity");
            return objectMap.sceneIdentityForModelingObject(object.id);
        }();
        const std::string name = [&]() {
            QDR_PROFILE_SCOPE("Modeling mesh sync resolve scene name");
            return identity.has_value() && !identity->name.empty()
                    ? identity->name
                    : sceneNames.nextNameForKind(SceneEntityKind::Mesh);
        }();
        const EntityId entity = [&]() {
            QDR_PROFILE_SCOPE("Modeling mesh sync create scene entity");
            return identity.has_value() && identity->entity != InvalidEntityId
                    ? scene.createEntityWithId(identity->entity, name, EntityKind::Mesh, identity->editorObjectId)
                    : scene.createEntity(name, EntityKind::Mesh);
        }();
        if (entity == InvalidEntityId) {
            continue;
        }
        sceneNames.reserveName(name);
        if (!identity.has_value()) {
            const EntityId parent = modelingDefaultParent(scene);
            if (parent != InvalidEntityId && scene.exists(parent)) {
                QDR_PROFILE_SCOPE("Modeling mesh sync set default parent");
                (void)scene.setParent(entity, parent, false);
            }
        }

        {
            QDR_PROFILE_SCOPE("Modeling mesh sync set modeling object component");
            scene.setModelingObject(entity, ModelingObjectComponent{
                    .objectId = object.id,
                    .materialSlot = materialSlot,
            });
        }
        if (identity.has_value()) {
            QDR_PROFILE_SCOPE("Modeling mesh sync apply durable scene identity");
            applyDurableSceneIdentityToEntity(scene, entity, *identity);
        }
        const MaterialComponent* identityMaterial =
                identity.has_value() && identity->material.has_value() ? &*identity->material : nullptr;
        {
            QDR_PROFILE_SCOPE("Modeling mesh sync set material component");
            if (const std::optional<MaterialComponent> material =
                        materialComponentForModelingSlot(materialSlot, materialBySlot, firstModelingMaterial, identityMaterial)) {
                scene.setMaterial(entity, *material);
            }
        }

        {
            QDR_PROFILE_SCOPE("Modeling mesh sync bind object entity");
            objectMap.bind(object.id, entity);
        }
    }

    std::vector<int> staleObjects;
    for (const auto& [modelingObjectId, entity] : objectMap.objectEntityBindingsByModelingObject()) {
        QDR_PROFILE_SCOPE("Modeling mesh sync detect stale object node");
        if (!liveObjects.contains(modelingObjectId)) {
            staleObjects.push_back(modelingObjectId);
        }
        (void)entity;
    }

    for (const int modelingObjectId : staleObjects) {
        QDR_PROFILE_SCOPE("Modeling mesh sync remove stale object node");
        const std::optional<EntityId> entity = objectMap.entityForModelingObject(modelingObjectId);
        if (entity.has_value() && scene.exists(*entity)) {
            objectMap.rememberSceneEntity(modelingObjectId, scene, *entity);
            scene.destroyEntity(*entity);
        }
        objectMap.unbindObject(modelingObjectId);
        sceneSync.forgetProjectedMeshSignature(modelingObjectId);
    }
}

void projectModelingMeshesToScene(
        const quader_editor_core::EditorSession& session,
        SceneDocument& scene,
        AssetRegistry& assets,
        SceneModelingSyncService& sceneSync,
        bool force) {
    QDR_PROFILE_FUNCTION();
    if (!force && sceneSync.sceneObjectsSyncedForCurrentModelingContent(session, scene)) {
        return;
    }

    ModelingObjectRuntimeMap& objectRegistry = sceneSync.objectMap();
    bool projectedAllObjects = true;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        QDR_PROFILE_SCOPE("Modeling mesh sync project object mesh");
        const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
        const std::uint64_t meshSignature = projectedMeshSignature(object);
        const std::optional<EntityId> existing = objectRegistry.entityForModelingObject(object.id);
        if (!existing.has_value() || !scene.exists(*existing)) {
            projectedAllObjects = false;
            continue;
        }

        const EntityId entity = *existing;
        const ModelingObjectComponent nextObject{
                .objectId = object.id,
                .materialSlot = materialSlot,
        };
        const ModelingObjectComponent* currentObject = scene.modelingObject(entity);
        const bool hadProjectedMeshSignature = sceneSync.projectedMeshSignatureKnown(object.id);
        if (!force) {
            const MeshComponent* currentMesh = scene.mesh(entity);
            if (currentMesh != nullptr &&
                    currentMesh->mesh != InvalidAssetId &&
                    assets.mesh(currentMesh->mesh) != nullptr &&
                    sceneSync.projectedMeshSignatureMatches(object.id, meshSignature)) {
                if (currentObject == nullptr ||
                        currentObject->objectId != nextObject.objectId ||
                        currentObject->materialSlot != nextObject.materialSlot) {
                    scene.setModelingObject(entity, nextObject);
                }
                objectRegistry.rememberSceneEntity(object.id, scene, entity);
                continue;
            }
        }

        MeshAsset mesh = [&]() {
            QDR_PROFILE_SCOPE("Modeling mesh sync compile object mesh");
            return MeshProjectionService::compileObjectMesh(object);
        }();
        if (const NameComponent* name = scene.name(entity)) {
            mesh.name = name->name;
        }
        AssetId meshId = InvalidAssetId;
        if (const MeshComponent* meshComponent = scene.mesh(entity)) {
            meshId = meshComponent->mesh;
        }
        bool meshContentsReplaced = false;
        if (meshId == InvalidAssetId) {
            QDR_PROFILE_SCOPE("Modeling mesh sync add mesh asset");
            meshId = assets.addMesh(std::move(mesh));
        } else if (!force && hadProjectedMeshSignature) {
            const bool replaced = [&]() {
                QDR_PROFILE_SCOPE("Modeling mesh sync replace changed mesh asset");
                return assets.replaceMesh(meshId, std::move(mesh));
            }();
            if (replaced) {
                meshContentsReplaced = true;
            } else {
                MeshAsset replacement = [&]() {
                    QDR_PROFILE_SCOPE("Modeling mesh sync compile changed replacement mesh");
                    return MeshProjectionService::compileObjectMesh(object);
                }();
                if (const NameComponent* name = scene.name(entity)) {
                    replacement.name = name->name;
                }
                QDR_PROFILE_SCOPE("Modeling mesh sync add changed replacement mesh asset");
                meshId = assets.addMesh(std::move(replacement));
            }
        } else if (const MeshAsset* currentMesh = assets.mesh(meshId);
                currentMesh != nullptr && modelingMeshAssetsEqual(*currentMesh, mesh)) {
            QDR_PROFILE_SCOPE("Modeling mesh sync keep unchanged mesh asset");
        } else if (assets.mesh(meshId) == nullptr) {
            QDR_PROFILE_SCOPE("Modeling mesh sync add replacement mesh asset");
            meshId = assets.addMesh(std::move(mesh));
            meshContentsReplaced = true;
        } else {
            const bool replaced = [&]() {
                QDR_PROFILE_SCOPE("Modeling mesh sync replace mesh asset");
                return assets.replaceMesh(meshId, std::move(mesh));
            }();
            if (replaced) {
                meshContentsReplaced = true;
            } else {
                MeshAsset replacement = [&]() {
                    QDR_PROFILE_SCOPE("Modeling mesh sync compile replacement mesh");
                    return MeshProjectionService::compileObjectMesh(object);
                }();
                if (const NameComponent* name = scene.name(entity)) {
                    replacement.name = name->name;
                }
                QDR_PROFILE_SCOPE("Modeling mesh sync add replacement mesh asset");
                meshId = assets.addMesh(std::move(replacement));
            }
        }
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
        sceneSync.rememberProjectedMeshSignature(object.id, meshSignature);
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
    QDR_PROFILE_FUNCTION();
    if (!force && sceneSync.sceneObjectsSyncedForCurrentModelingContent(session, scene)) {
        return;
    }
    {
        QDR_PROFILE_SCOPE("Modeling mesh sync reconcile scene nodes");
        reconcileModelingObjectSceneNodes(session, scene, sceneSync);
    }
    {
        QDR_PROFILE_SCOPE("Modeling mesh sync project meshes to scene");
        projectModelingMeshesToScene(session, scene, assets, sceneSync, force);
    }
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
    const EntityId parent = modelingDefaultParent(scene);
    if (parent != InvalidEntityId && scene.exists(parent)) {
        (void)scene.setParent(entity, parent, false);
    }
    scene.setModelingObject(entity, ModelingObjectComponent{
            .objectId = object.id,
            .materialSlot = static_cast<std::uint32_t>(object.material_id),
    });
    scene.setMesh(entity, MeshComponent{.mesh = meshId});
    scene.clearSelection();
    scene.setSelected(entity, true);

    sceneSync.objectMap().bind(object.id, entity);
    sceneSync.rememberProjectedMeshSignature(object.id, projectedMeshSignature(object));
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
