////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/project_document.hpp"

#include <algorithm>
#include <utility>

namespace quader {

ProjectDocument::ProjectDocument() {
    bindSceneObjectIdentityAllocator();
}

ProjectDocument::ProjectDocument(ProjectDocumentRecord record)
    : record_(std::move(record)) {
    bindSceneObjectIdentityAllocator();
}

void ProjectDocument::rename(std::string name) {
    if (record_.name == name) {
        return;
    }
    record_.name = std::move(name);
    markDirty();
}

bool ProjectDocument::synchronizeObjectRecordsWithScene() {
    if (!syncObjectRecordsWithScene()) {
        return false;
    }
    markDirty();
    return true;
}

EditorObjectId ProjectDocument::createObjectRecord(ProjectObjectRecord record) {
    const bool recordsSynced = syncObjectRecordsWithScene();
    const auto invalidResult = [&]() {
        if (recordsSynced) {
            markDirty();
        }
        return InvalidEditorObjectId;
    };
    if (record.sceneEntity != InvalidEntityId && scene_.exists(record.sceneEntity)) {
        const EditorObjectId requested = record.id;
        EditorObjectId objectId = requested != InvalidEditorObjectId
                ? requested
                : scene_.editorObjectId(record.sceneEntity);
        if (objectId == InvalidEditorObjectId) {
            objectId = allocateObjectId(InvalidEditorObjectId);
        }
        if (!canAssignObjectId(objectId, record.sceneEntity)) {
            return invalidResult();
        }
        (void)allocateObjectId(objectId);
        if (scene_.editorObjectId(record.sceneEntity) != objectId &&
                !scene_.setEditorObjectId(record.sceneEntity, objectId)) {
            return invalidResult();
        }
        record.id = objectId;
        if (const auto existing = objectByEntity_.find(record.sceneEntity); existing != objectByEntity_.end()) {
            objects_.erase(existing->second);
            objectByEntity_.erase(existing);
        }
    } else {
        if (record.id != InvalidEditorObjectId && objects_.contains(record.id)) {
            return invalidResult();
        }
        record.id = allocateObjectId(record.id);
        if (record.id == InvalidEditorObjectId) {
            return invalidResult();
        }
    }
    const EditorObjectId objectId = record.id;
    indexRecord(record);
    objects_[objectId] = std::move(record);
    markDirty();
    return objectId;
}

EditorObjectId ProjectDocument::upsertObjectRecord(ProjectObjectRecord record) {
    const bool recordsSynced = syncObjectRecordsWithScene();
    const auto invalidResult = [&]() {
        if (recordsSynced) {
            markDirty();
        }
        return InvalidEditorObjectId;
    };
    if (record.sceneEntity != InvalidEntityId && scene_.exists(record.sceneEntity)) {
        EditorObjectId objectId = record.id;
        if (objectId == InvalidEditorObjectId) {
            objectId = scene_.editorObjectId(record.sceneEntity);
        }
        if (objectId == InvalidEditorObjectId) {
            objectId = allocateObjectId(InvalidEditorObjectId);
        }
        if (!canAssignObjectId(objectId, record.sceneEntity)) {
            return invalidResult();
        }
        (void)allocateObjectId(objectId);
        if (scene_.editorObjectId(record.sceneEntity) != objectId &&
                !scene_.setEditorObjectId(record.sceneEntity, objectId)) {
            return invalidResult();
        }
        record.id = objectId;
        if (const auto existing = objectByEntity_.find(record.sceneEntity);
                existing != objectByEntity_.end() && existing->second != objectId) {
            objects_.erase(existing->second);
            objectByEntity_.erase(existing);
        }
    } else {
        if (record.id != InvalidEditorObjectId && scene_.entityForEditorObjectId(record.id) != InvalidEntityId) {
            return invalidResult();
        }
        record.id = allocateObjectId(record.id);
        if (record.id == InvalidEditorObjectId) {
            return invalidResult();
        }
    }
    const EditorObjectId objectId = record.id;
    if (auto existing = objects_.find(objectId); existing != objects_.end()) {
        if (existing->second.sceneEntity != InvalidEntityId) {
            objectByEntity_.erase(existing->second.sceneEntity);
        }
    }
    indexRecord(record);
    objects_[objectId] = std::move(record);
    markDirty();
    return objectId;
}

std::optional<ProjectObjectRecord> ProjectDocument::objectRecord(EditorObjectId objectId) const {
    if (objectId == InvalidEditorObjectId) {
        return std::nullopt;
    }
    if (const EntityId entity = scene_.entityForEditorObjectId(objectId); entity != InvalidEntityId) {
        return mergedSceneObjectRecord(recordForSceneEntity(entity, objectId));
    }
    return storedObjectRecord(objectId);
}

EditorObjectId ProjectDocument::objectIdForEntity(EntityId entity) const {
    if (const EditorObjectId objectId = scene_.editorObjectId(entity); objectId != InvalidEditorObjectId) {
        return objectId;
    }
    const auto existing = objectByEntity_.find(entity);
    if (existing != objectByEntity_.end() && objectRecord(existing->second).has_value()) {
        return existing->second;
    }
    for (const auto& [objectId, record] : objects_) {
        if (record.sceneEntity == entity && objectRecord(objectId).has_value()) {
            return objectId;
        }
    }
    return InvalidEditorObjectId;
}

EntityId ProjectDocument::entityForObjectId(EditorObjectId objectId) const {
    if (const EntityId entity = scene_.entityForEditorObjectId(objectId); entity != InvalidEntityId) {
        return entity;
    }
    const std::optional<ProjectObjectRecord> record = objectRecord(objectId);
    return record.has_value() ? record->sceneEntity : InvalidEntityId;
}

void ProjectDocument::forgetObjectRecord(EditorObjectId objectId) {
    auto existing = objects_.find(objectId);
    if (existing == objects_.end()) {
        return;
    }
    if (existing->second.sceneEntity != InvalidEntityId) {
        objectByEntity_.erase(existing->second.sceneEntity);
    }
    objects_.erase(existing);
    markDirty();
}

void ProjectDocument::forgetEntity(EntityId entity) {
    const auto existing = objectByEntity_.find(entity);
    if (existing == objectByEntity_.end()) {
        return;
    }
    objects_.erase(existing->second);
    objectByEntity_.erase(existing);
    markDirty();
}

std::vector<ProjectObjectRecord> ProjectDocument::objectRecords() const {
    std::unordered_map<EditorObjectId, ProjectObjectRecord> recordsById;
    recordsById.reserve(objects_.size() + scene_.entities().size());
    for (const auto& [objectId, record] : objects_) {
        if (const std::optional<ProjectObjectRecord> stored = storedObjectRecord(objectId)) {
            recordsById[objectId] = *stored;
        }
    }
    for (const EntityId entity : scene_.entities()) {
        const EditorObjectId objectId = scene_.editorObjectId(entity);
        if (objectId == InvalidEditorObjectId) {
            continue;
        }
        recordsById[objectId] = mergedSceneObjectRecord(recordForSceneEntity(entity, objectId));
    }

    std::vector<ProjectObjectRecord> records;
    records.reserve(recordsById.size());
    for (const auto& [id, record] : recordsById) {
        (void)id;
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const ProjectObjectRecord& left, const ProjectObjectRecord& right) {
        return left.id < right.id;
    });
    return records;
}

ProjectDocumentIdentityReport ProjectDocument::validateObjectIdentityInvariants() const {
    ProjectDocumentIdentityReport report;
    std::unordered_map<EditorObjectId, EntityId> entityByObjectId;
    std::unordered_map<EditorObjectId, ProjectObjectRecord> recordsById;
    for (const ProjectObjectRecord& record : objectRecords()) {
        recordsById[record.id] = record;
    }
    for (const EntityId entity : scene_.entities()) {
        const EditorObjectId objectId = scene_.editorObjectId(entity);
        if (objectId == InvalidEditorObjectId) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = entity,
                    .objectId = objectId,
                    .message = "Scene entity is missing an editor object id.",
            });
            continue;
        }
        if (const auto duplicate = entityByObjectId.find(objectId); duplicate != entityByObjectId.end()) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = entity,
                    .objectId = objectId,
                    .message = "Multiple scene entities share an editor object id.",
            });
        }
        entityByObjectId[objectId] = entity;

        const auto record = recordsById.find(objectId);
        if (record == recordsById.end()) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = entity,
                    .objectId = objectId,
                    .message = "Scene entity is missing a project object record.",
            });
            continue;
        }
        if (record->second.sceneEntity != entity) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = entity,
                    .objectId = objectId,
                    .message = "Project object record points at a different scene entity.",
            });
        }
    }

    for (const auto& [objectId, record] : recordsById) {
        if (record.sceneEntity == InvalidEntityId) {
            continue;
        }
        if (!scene_.exists(record.sceneEntity)) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = record.sceneEntity,
                    .objectId = objectId,
                    .message = "Project object record points at a missing scene entity.",
            });
            continue;
        }
        if (const EditorObjectId sceneObjectId = scene_.editorObjectId(record.sceneEntity);
                sceneObjectId != objectId) {
            report.issues.push_back(ProjectDocumentIdentityIssue{
                    .entity = record.sceneEntity,
                    .objectId = objectId,
                    .message = "Project object record id does not match the scene editor object id.",
            });
        }
    }

    return report;
}

std::uint64_t ProjectDocument::revision() const {
    return revision_;
}

void ProjectDocument::markDirty() {
    ++revision_;
}

void ProjectDocument::bindSceneObjectIdentityAllocator() {
    scene_.setEditorObjectIdAllocator([nextObjectId = nextObjectId_](EditorObjectId requested) {
        if (!nextObjectId) {
            return InvalidEditorObjectId;
        }
        if (requested != InvalidEditorObjectId) {
            *nextObjectId = std::max(*nextObjectId, requested + 1u);
            return requested;
        }
        return (*nextObjectId)++;
    });
}

bool ProjectDocument::syncObjectRecordsWithScene() {
    std::unordered_map<EditorObjectId, ProjectObjectRecord> syncedObjects;
    std::unordered_map<EntityId, EditorObjectId> syncedObjectByEntity;
    for (ProjectObjectRecord record : objectRecords()) {
        if (record.id == InvalidEditorObjectId) {
            continue;
        }
        if (record.sceneEntity != InvalidEntityId) {
            syncedObjectByEntity[record.sceneEntity] = record.id;
        }
        syncedObjects[record.id] = std::move(record);
    }
    if (syncedObjects == objects_ && syncedObjectByEntity == objectByEntity_) {
        return false;
    }
    objects_ = std::move(syncedObjects);
    objectByEntity_ = std::move(syncedObjectByEntity);
    return true;
}

bool ProjectDocument::canAssignObjectId(EditorObjectId objectId, EntityId entity) const {
    if (objectId == InvalidEditorObjectId) {
        return false;
    }
    if (const EntityId owner = scene_.entityForEditorObjectId(objectId);
            owner != InvalidEntityId && owner != entity) {
        return false;
    }
    const auto existing = objects_.find(objectId);
    return existing == objects_.end() || existing->second.sceneEntity == entity;
}

std::optional<ProjectObjectRecord> ProjectDocument::storedObjectRecord(EditorObjectId objectId) const {
    const auto existing = objects_.find(objectId);
    if (existing == objects_.end()) {
        return std::nullopt;
    }
    ProjectObjectRecord record = existing->second;
    if (record.sceneEntity != InvalidEntityId) {
        if (!scene_.exists(record.sceneEntity)) {
            return std::nullopt;
        }
        const EditorObjectId sceneObjectId = scene_.editorObjectId(record.sceneEntity);
        if (sceneObjectId != InvalidEditorObjectId && sceneObjectId != objectId) {
            return std::nullopt;
        }
    }
    hydrateRecordFromScene(record);
    return record;
}

ProjectObjectRecord ProjectDocument::recordForSceneEntity(EntityId entity, EditorObjectId objectId) const {
    ProjectObjectRecord record{
            .id = objectId,
            .sceneEntity = entity,
    };
    hydrateRecordFromScene(record);
    return record;
}

ProjectObjectRecord ProjectDocument::mergedSceneObjectRecord(ProjectObjectRecord record) const {
    if (const auto existing = objects_.find(record.id); existing != objects_.end()) {
        ProjectObjectRecord merged = existing->second;
        merged.sceneEntity = record.sceneEntity;
        merged.mesh = record.mesh;
        merged.material = record.material;
        merged.modelingObject = record.modelingObject;
        merged.visible = record.visible;
        merged.materialSlot = record.materialSlot;
        if (merged.authoredName.empty() && !record.authoredName.empty()) {
            merged.authoredName = record.authoredName;
        }
        return merged;
    }
    return record;
}

void ProjectDocument::hydrateRecordFromScene(ProjectObjectRecord& record) const {
    if (record.authoredName.empty() && record.sceneEntity != InvalidEntityId) {
        if (const NameComponent* name = scene_.name(record.sceneEntity)) {
            record.authoredName = name->name;
        }
    }
    if (record.visible && record.sceneEntity != InvalidEntityId) {
        if (const VisibilityComponent* visibility = scene_.visibility(record.sceneEntity)) {
            record.visible = visibility->visible;
        }
    }
    if (record.modelingObject == InvalidModelingObjectId && record.sceneEntity != InvalidEntityId) {
        if (const ModelingObjectComponent* object = scene_.modelingObject(record.sceneEntity)) {
            record.modelingObject = object->objectId;
            record.materialSlot = object->materialSlot;
        }
    }
    if (record.mesh == InvalidAssetId && record.sceneEntity != InvalidEntityId) {
        if (const MeshComponent* mesh = scene_.mesh(record.sceneEntity)) {
            record.mesh = mesh->mesh;
        }
    }
    if (record.material == InvalidAssetId && record.sceneEntity != InvalidEntityId) {
        if (const MaterialComponent* material = scene_.material(record.sceneEntity)) {
            record.material = material->material;
        }
    }
}

void ProjectDocument::indexRecord(ProjectObjectRecord& record) {
    hydrateRecordFromScene(record);
    if (record.sceneEntity != InvalidEntityId) {
        objectByEntity_[record.sceneEntity] = record.id;
    }
}

EditorObjectId ProjectDocument::allocateObjectId(EditorObjectId requested) {
    if (!nextObjectId_) {
        nextObjectId_ = std::make_shared<EditorObjectId>(1);
        bindSceneObjectIdentityAllocator();
    }
    if (requested != InvalidEditorObjectId) {
        *nextObjectId_ = std::max(*nextObjectId_, requested + 1u);
        return requested;
    }
    while (objects_.contains(*nextObjectId_) ||
            scene_.entityForEditorObjectId(*nextObjectId_) != InvalidEntityId) {
        ++(*nextObjectId_);
    }
    return (*nextObjectId_)++;
}

} // namespace quader
