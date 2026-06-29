////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_query_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "scene/document/project_session.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_bounds.hpp"

#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


QuaderBounds apiBounds(const SceneBounds& bounds) {
    return {
            .min = bounds.min,
            .max = bounds.max,
    };
}

QuaderModelingObjectRef objectRefForEntity(const ProjectSession& session, EntityId entity) {
    QuaderModelingObjectRef ref;
    ref.entity = entity;
    ref.entityUuid = session.scene().uuid(entity);
    ref.editorObjectId = session.scene().editorObjectId(entity);
    if (const ModelingObjectComponent* object = session.scene().modelingObject(entity)) {
        ref.modelingObjectId = object->objectId;
    }
    if (const MeshComponent* mesh = session.scene().mesh(entity)) {
        ref.mesh = mesh->mesh;
    }
    if (const MaterialComponent* material = session.scene().material(entity)) {
        ref.material = material->material;
    }
    return ref;
}

QuaderEntitySnapshot entitySnapshotFor(const ProjectSession& session, EntityId entity) {
    const SceneDocument& scene = session.scene();
    QuaderEntitySnapshot snapshot;
    snapshot.id = entity;
    snapshot.uuid = scene.uuid(entity);
    snapshot.editorObjectId = scene.editorObjectId(entity);
    snapshot.kind = toApiEntityKind(scene.kind(entity));
    snapshot.parent = scene.parent(entity);
    snapshot.active = scene.primarySelection() == entity;

    if (const NameComponent* name = scene.name(entity)) {
        snapshot.name = name->name;
    }
    if (const TransformComponent* transform = scene.transform(entity)) {
        snapshot.transform = toApiTransform(*transform);
    }
    if (const VisibilityComponent* visibility = scene.visibility(entity)) {
        snapshot.visible = visibility->visible;
        snapshot.selectable = visibility->selectable;
    }
    if (const LockComponent* lock = scene.lock(entity)) {
        snapshot.locked = lock->locked;
    }
    if (const SelectionComponent* selection = scene.selection(entity)) {
        snapshot.selected = selection->selected;
    }
    if (const MeshComponent* mesh = scene.mesh(entity)) {
        snapshot.hasMesh = true;
        snapshot.mesh = mesh->mesh;
    }
    if (const MaterialComponent* material = scene.material(entity)) {
        snapshot.hasMaterial = true;
        snapshot.material = material->material;
    }
    if (const ModelingObjectComponent* object = scene.modelingObject(entity)) {
        snapshot.hasModelingObject = true;
        snapshot.modelingObjectId = object->objectId;
    }
    return snapshot;
}

QuaderMeshSnapshot meshSnapshotFor(AssetId id, const MeshAsset& mesh) {
    QuaderMeshSnapshot snapshot;
    snapshot.mesh = id;
    snapshot.revision = mesh.revision.value;
    snapshot.vertexCount = mesh.vertices.size();
    snapshot.indexCount = mesh.indices.size();
    snapshot.primitiveCount = mesh.primitives.size();
    snapshot.stableCpuTopology = mesh.hasStableCpuTopology;
    if (const std::optional<SceneBounds> bounds = localMeshBounds(mesh)) {
        snapshot.hasBounds = true;
        snapshot.bounds = apiBounds(*bounds);
    }
    return snapshot;
}

} // namespace

QuaderQueryApi::QuaderQueryApi(QuaderApiSession session)
    : session_(session) {}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entities() const {
    std::vector<QuaderEntitySnapshot> result;
    const std::vector<EntityId> ids = projectSession(session_).scene().entities();
    result.reserve(ids.size());
    for (const EntityId id : ids) {
        result.push_back(entitySnapshotFor(projectSession(session_), id));
    }
    return result;
}

std::vector<QuaderEntitySnapshot> QuaderQueryApi::entityTree() const {
    std::vector<QuaderEntitySnapshot> result;
    const std::vector<EntityId> ids = projectSession(session_).scene().entityTreeOrder();
    result.reserve(ids.size());
    for (const EntityId id : ids) {
        result.push_back(entitySnapshotFor(projectSession(session_), id));
    }
    return result;
}

std::optional<QuaderEntitySnapshot> QuaderQueryApi::entity(EntityId id) const {
    if (!projectSession(session_).scene().exists(id)) {
        return std::nullopt;
    }
    return entitySnapshotFor(projectSession(session_), id);
}

std::optional<QuaderMeshSnapshot> QuaderQueryApi::mesh(AssetId id) const {
    const MeshAsset* mesh = projectSession(session_).assets().mesh(id);
    if (mesh == nullptr) {
        return std::nullopt;
    }
    return meshSnapshotFor(id, *mesh);
}

std::optional<QuaderMaterialAsset> QuaderQueryApi::material(AssetId id) const {
    const MaterialDocument* material = projectSession(session_).assets().material(id);
    if (material == nullptr) {
        return std::nullopt;
    }
    return toApiMaterialAsset(*material);
}

std::optional<QuaderTextureAsset> QuaderQueryApi::texture(AssetId id) const {
    const TextureAsset* texture = projectSession(session_).assets().texture(id);
    if (texture == nullptr) {
        return std::nullopt;
    }
    return toApiTextureAsset(*texture);
}

std::vector<QuaderModelingObjectRecord> QuaderQueryApi::modelingObjects() const {
    const std::vector<ModelingObjectRecord> records = projectSession(session_).modeling().objects();
    std::vector<QuaderModelingObjectRecord> result;
    result.reserve(records.size());
    for (const ModelingObjectRecord& record : records) {
        result.push_back(toApiModelingObjectRecord(record));
    }
    return result;
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modelingObjectForEntity(EntityId entity) const {
    if (!projectSession(session_).scene().exists(entity)) {
        return std::nullopt;
    }
    QuaderModelingObjectRef ref = objectRefForEntity(projectSession(session_), entity);
    if (ref.modelingObjectId == InvalidModelingObjectId) {
        return std::nullopt;
    }
    return ref;
}

std::optional<QuaderModelingObjectRef> QuaderQueryApi::modelingObjectForId(QuaderModelingObjectId modelingObjectId) const {
    const std::optional<EntityId> entity = projectSession(session_).modeling().entityForModelingObject(modelingObjectId);
    if (!entity.has_value()) {
        return std::nullopt;
    }
    return objectRefForEntity(projectSession(session_), *entity);
}

std::optional<QuaderMeshAsset> QuaderQueryApi::compiledObjectMesh(QuaderModelingObjectRef object) const {
    if (object.modelingObjectId == InvalidModelingObjectId) {
        return std::nullopt;
    }
    const std::optional<MeshAsset> mesh = projectSession(session_).modeling().compileObjectMesh(object.modelingObjectId);
    return mesh ? std::optional<QuaderMeshAsset>{toApiMeshAsset(*mesh)} : std::nullopt;
}

std::optional<QVec3> QuaderQueryApi::selectionCenter() const {
    return projectSession(session_).modeling().selectionCenter();
}

QuaderSelectionSummary QuaderQueryApi::selection() const {
    return toApiSelectionSummary(projectSession(session_).modeling().selectionSummary());
}

QuaderToolStatus QuaderQueryApi::toolStatus() const {
    return toApiToolStatus(projectSession(session_).modeling().toolStatus());
}

QuaderAssetSnapshot QuaderQueryApi::assets() const {
    return {
            .meshCount = projectSession(session_).assets().meshCount(),
            .materialCount = projectSession(session_).assets().materialCount(),
            .textureCount = projectSession(session_).assets().textureCount(),
            .revision = projectSession(session_).assets().revision(),
    };
}

QuaderHistorySnapshot QuaderQueryApi::history() const {
    QuaderHistorySnapshot result;
    result.canUndo = projectSession(session_).history().canUndo();
    result.canRedo = projectSession(session_).history().canRedo();
    if (const std::string* label = projectSession(session_).history().undoLabel()) {
        result.undoLabel = *label;
    }
    if (const std::string* label = projectSession(session_).history().redoLabel()) {
        result.redoLabel = *label;
    }
    return result;
}

QuaderOverlayFrame QuaderQueryApi::selectionOverlay() const {
    return toApiOverlayFrame(projectSession(session_).modeling().selectionOverlayFrame());
}

QuaderRevisionStamp QuaderQueryApi::revisions() const {
    return quaderApiCurrentRevisions(projectSession(session_));
}

QuaderApiSnapshot QuaderQueryApi::snapshot() const {
    return {
            .revisions = revisions(),
            .entities = entityTree(),
            .modelingObjects = modelingObjects(),
            .selection = selection(),
            .toolStatus = toolStatus(),
            .assets = assets(),
            .history = history(),
            .selectionOverlay = selectionOverlay(),
    };
}

} // namespace quader
