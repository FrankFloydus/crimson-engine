////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/MeshProjectionService.h"

#include "assets/AssetRegistry.h"
#include "editor/modeling/services/SceneModelingSyncService.h"
#include "scene/SceneDocument.h"

#include <memory>
#include <string>
#include <utility>

#include <mesh/polygon/document.h>

namespace quader {
namespace {

Vec3 fromRuntime(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

MeshAsset toMeshAsset(const quader_poly::CompiledMesh& compiled) {
    MeshAsset mesh;
    mesh.name = "Mesh";
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.primitives.reserve(compiled.primitives.size());
    mesh.hasStableCpuTopology = true;

    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromRuntime(vertex.position),
                .normal = fromRuntime(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
        });
    }
    for (const quader_poly::CompiledPrimitive& primitive : compiled.primitives) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitive.index_offset,
                .indexCount = primitive.index_count,
                .materialSlot = primitive.material_slot,
        });
    }

    return mesh;
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    MeshAsset mesh = toMeshAsset(quader_poly::compile_document(object.document));
    mesh.hasAuthoredPolygonDocument = true;
    mesh.authoredPolygonDocument = std::make_shared<quader_poly::Document>(object.document);
    return mesh;
}

} // namespace

MeshAsset MeshProjectionService::compileObjectMesh(const quader_editor_core::PolygonObject& object) {
    return toMeshAsset(object);
}

void MeshProjectionService::projectModelingMeshesToScene(
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
        const auto existing = objectRegistry.entitiesByObject().find(object.id);
        if (existing == objectRegistry.entitiesByObject().end() || !scene.exists(existing->second)) {
            projectedAllObjects = false;
            continue;
        }

        const EntityId entity = existing->second;
        MeshAsset mesh = toMeshAsset(object);
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
            MeshAsset replacement = toMeshAsset(object);
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
            MeshComponent nextMesh = currentMesh != nullptr ? *currentMesh : MeshComponent{};
            nextMesh.mesh = meshId;
            scene.setMesh(entity, nextMesh);
        }
        objectRegistry.rememberSceneEntity(object.id, scene, entity);
    }
    if (projectedAllObjects) {
        sceneSync.markSceneObjectsSynced(session.state().content_revision);
    }
}

} // namespace quader
