////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/ProjectDocument.h"

#include <algorithm>
#include <utility>

namespace quader {

ProjectDocument::ProjectDocument(ProjectDocumentRecord record)
    : record_(std::move(record)) {}

void ProjectDocument::rename(std::string name) {
    if (record_.name == name) {
        return;
    }
    record_.name = std::move(name);
    markDirty();
}

EditorObjectId ProjectDocument::createObjectRecord(ProjectObjectRecord record) {
    syncObjectRecordsWithScene();
    if (record.sceneEntity != InvalidEntityId && scene_.exists(record.sceneEntity)) {
        const EditorObjectId requested = record.id;
        EditorObjectId objectId = requested != InvalidEditorObjectId
                ? requested
                : scene_.editorObjectId(record.sceneEntity);
        if (objectId == InvalidEditorObjectId) {
            objectId = allocateObjectId(InvalidEditorObjectId);
        }
        if (!canAssignObjectId(objectId, record.sceneEntity)) {
            return InvalidEditorObjectId;
        }
        (void)allocateObjectId(objectId);
        if (scene_.editorObjectId(record.sceneEntity) != objectId &&
                !scene_.setEditorObjectId(record.sceneEntity, objectId)) {
            return InvalidEditorObjectId;
        }
        record.id = objectId;
        if (const auto existing = objectByEntity_.find(record.sceneEntity); existing != objectByEntity_.end()) {
            objects_.erase(existing->second);
            objectByEntity_.erase(existing);
        }
    } else {
        if (record.id != InvalidEditorObjectId && objects_.contains(record.id)) {
            return InvalidEditorObjectId;
        }
        record.id = allocateObjectId(record.id);
        if (record.id == InvalidEditorObjectId) {
            return InvalidEditorObjectId;
        }
    }
    const EditorObjectId objectId = record.id;
    indexRecord(record);
    objects_[objectId] = std::move(record);
    markDirty();
    return objectId;
}

EditorObjectId ProjectDocument::upsertObjectRecord(ProjectObjectRecord record) {
    syncObjectRecordsWithScene();
    if (record.sceneEntity != InvalidEntityId && scene_.exists(record.sceneEntity)) {
        EditorObjectId objectId = record.id;
        if (objectId == InvalidEditorObjectId) {
            objectId = scene_.editorObjectId(record.sceneEntity);
        }
        if (objectId == InvalidEditorObjectId) {
            objectId = allocateObjectId(InvalidEditorObjectId);
        }
        if (!canAssignObjectId(objectId, record.sceneEntity)) {
            return InvalidEditorObjectId;
        }
        (void)allocateObjectId(objectId);
        if (scene_.editorObjectId(record.sceneEntity) != objectId &&
                !scene_.setEditorObjectId(record.sceneEntity, objectId)) {
            return InvalidEditorObjectId;
        }
        record.id = objectId;
        if (const auto existing = objectByEntity_.find(record.sceneEntity);
                existing != objectByEntity_.end() && existing->second != objectId) {
            objects_.erase(existing->second);
            objectByEntity_.erase(existing);
        }
    } else {
        if (record.id != InvalidEditorObjectId && scene_.entityForEditorObjectId(record.id) != InvalidEntityId) {
            return InvalidEditorObjectId;
        }
        record.id = allocateObjectId(record.id);
        if (record.id == InvalidEditorObjectId) {
            return InvalidEditorObjectId;
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

const ProjectObjectRecord* ProjectDocument::objectRecord(EditorObjectId objectId) const {
    syncObjectRecordsWithScene();
    const auto existing = objects_.find(objectId);
    return existing == objects_.end() ? nullptr : &existing->second;
}

ProjectObjectRecord* ProjectDocument::objectRecord(EditorObjectId objectId) {
    syncObjectRecordsWithScene();
    auto existing = objects_.find(objectId);
    return existing == objects_.end() ? nullptr : &existing->second;
}

EditorObjectId ProjectDocument::objectIdForEntity(EntityId entity) const {
    if (const EditorObjectId objectId = scene_.editorObjectId(entity); objectId != InvalidEditorObjectId) {
        syncObjectRecordsWithScene();
        return objectId;
    }
    syncObjectRecordsWithScene();
    const auto existing = objectByEntity_.find(entity);
    return existing == objectByEntity_.end() ? InvalidEditorObjectId : existing->second;
}

EntityId ProjectDocument::entityForObjectId(EditorObjectId objectId) const {
    if (const EntityId entity = scene_.entityForEditorObjectId(objectId); entity != InvalidEntityId) {
        syncObjectRecordsWithScene();
        return entity;
    }
    const ProjectObjectRecord* record = objectRecord(objectId);
    return record ? record->sceneEntity : InvalidEntityId;
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
    syncObjectRecordsWithScene();
    std::vector<ProjectObjectRecord> records;
    records.reserve(objects_.size());
    for (const auto& [id, record] : objects_) {
        records.push_back(record);
    }

    std::sort(records.begin(), records.end(), [](const ProjectObjectRecord& left, const ProjectObjectRecord& right) {
        return left.id < right.id;
    });
    return records;
}

ProjectDocumentIdentityReport ProjectDocument::validateObjectIdentityInvariants() const {
    syncObjectRecordsWithScene();

    ProjectDocumentIdentityReport report;
    std::unordered_map<EditorObjectId, EntityId> entityByObjectId;
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

        const auto record = objects_.find(objectId);
        if (record == objects_.end()) {
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

    for (const auto& [objectId, record] : objects_) {
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

void ProjectDocument::syncObjectRecordsWithScene() const {
    for (auto it = objects_.begin(); it != objects_.end();) {
        const EntityId entity = it->second.sceneEntity;
        if (entity != InvalidEntityId && !scene_.exists(entity)) {
            if (const auto byEntity = objectByEntity_.find(entity);
                    byEntity != objectByEntity_.end() && byEntity->second == it->first) {
                objectByEntity_.erase(byEntity);
            }
            it = objects_.erase(it);
            continue;
        }
        ++it;
    }

    for (const EntityId entity : scene_.entities()) {
        const EditorObjectId objectId = scene_.editorObjectId(entity);
        if (objectId == InvalidEditorObjectId) {
            continue;
        }
        if (const auto previous = objectByEntity_.find(entity);
                previous != objectByEntity_.end() && previous->second != objectId) {
            objects_.erase(previous->second);
            objectByEntity_.erase(previous);
        }

        ProjectObjectRecord record = recordForSceneEntity(entity, objectId);
        if (const auto existing = objects_.find(objectId); existing != objects_.end()) {
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
            record = std::move(merged);
        }
        objects_[objectId] = std::move(record);
        objectByEntity_[entity] = objectId;
    }
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

ProjectObjectRecord ProjectDocument::recordForSceneEntity(EntityId entity, EditorObjectId objectId) const {
    ProjectObjectRecord record{
            .id = objectId,
            .sceneEntity = entity,
    };
    indexRecord(record);
    return record;
}

void ProjectDocument::indexRecord(ProjectObjectRecord& record) const {
    if (record.sceneEntity != InvalidEntityId) {
        objectByEntity_[record.sceneEntity] = record.id;
    }
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

EditorObjectId ProjectDocument::allocateObjectId(EditorObjectId requested) {
    if (requested != InvalidEditorObjectId) {
        nextObjectId_ = std::max(nextObjectId_, requested + 1u);
        return requested;
    }
    while (objects_.contains(nextObjectId_) ||
            scene_.entityForEditorObjectId(nextObjectId_) != InvalidEntityId) {
        ++nextObjectId_;
    }
    return nextObjectId_++;
}

} // namespace quader
