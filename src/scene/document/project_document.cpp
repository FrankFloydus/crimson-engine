////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/document/project_document.hpp"

#include <algorithm>
#include <iterator>
#include <utility>

namespace quader {

ProjectDocument::ProjectDocument(ProjectDocumentRootPolicy rootPolicy) {
    bindSceneObjectIdentityAllocator();
    if (rootPolicy == ProjectDocumentRootPolicy::EnsureSceneRoot) {
        (void)ensureSceneRoot();
    }
}

ProjectDocument::ProjectDocument(ProjectDocumentRecord record, ProjectDocumentRootPolicy rootPolicy)
    : record_(std::move(record)) {
    bindSceneObjectIdentityAllocator();
    if (rootPolicy == ProjectDocumentRootPolicy::EnsureSceneRoot) {
        (void)ensureSceneRoot();
    }
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
    const EditorObjectId objectId = upsertObjectRecordAfterSync(std::move(record));
    if (objectId == InvalidEditorObjectId) {
        if (recordsSynced) {
            markDirty();
        }
        return InvalidEditorObjectId;
    }
    markDirty();
    return objectId;
}

std::vector<EditorObjectId> ProjectDocument::upsertObjectRecords(std::vector<ProjectObjectRecord> records) {
    const bool recordsSynced = syncObjectRecordsWithScene();
    std::vector<EditorObjectId> objectIds;
    objectIds.reserve(records.size());
    for (ProjectObjectRecord& record : records) {
        const EditorObjectId objectId = upsertObjectRecordAfterSync(std::move(record));
        if (objectId == InvalidEditorObjectId) {
            if (recordsSynced || !objectIds.empty()) {
                markDirty();
            }
            return {};
        }
        objectIds.push_back(objectId);
    }
    if (recordsSynced || !objectIds.empty()) {
        markDirty();
    }
    return objectIds;
}

EditorObjectId ProjectDocument::upsertObjectRecordAfterSync(ProjectObjectRecord record) {
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

ProjectDocumentAssetReferenceReport ProjectDocument::validateAssetReferences() const {
    ProjectDocumentAssetReferenceReport report;
    for (const EntityId entity : scene_.entities()) {
        if (const MeshComponent* mesh = scene_.mesh(entity);
                mesh != nullptr && (mesh->mesh == InvalidAssetId || assets_.mesh(mesh->mesh) == nullptr)) {
            report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                    .entity = entity,
                    .asset = mesh->mesh,
                    .message = "entity references a missing mesh asset.",
            });
        }
        if (const MaterialComponent* material = scene_.material(entity);
                material != nullptr && assets_.material(material->material) == nullptr) {
            report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                    .entity = entity,
                    .asset = material->material,
                    .message = "entity references a missing material asset.",
            });
        }
        if (const MaterialComponent* material = scene_.material(entity)) {
            for (const auto& [slot, materialId] : material->materialSlots) {
                (void)slot;
                if (materialId == InvalidAssetId || assets_.material(materialId) == nullptr) {
                    report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                            .entity = entity,
                            .asset = materialId,
                            .message = "entity material slot references a missing material asset.",
                    });
                }
            }
        }
    }

    for (const ProjectObjectRecord& record : objectRecords()) {
        ProjectDocumentAssetReferenceReport recordReport = validateAssetReferences(record);
        report.issues.insert(
                report.issues.end(),
                std::make_move_iterator(recordReport.issues.begin()),
                std::make_move_iterator(recordReport.issues.end()));
    }
    return report;
}

ProjectDocumentAssetReferenceReport ProjectDocument::validateAssetReferences(ProjectObjectRecord record) const {
    ProjectDocumentAssetReferenceReport report;
    if (record.id == InvalidEditorObjectId) {
        report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                .entity = record.sceneEntity,
                .objectId = record.id,
                .message = "contains an object record without a stable object id.",
        });
    }
    if (record.sceneEntity == InvalidEntityId || !scene_.exists(record.sceneEntity)) {
        report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                .entity = record.sceneEntity,
                .objectId = record.id,
                .message = "object record points at a missing scene entity.",
        });
    }
    if (record.mesh != InvalidAssetId && assets_.mesh(record.mesh) == nullptr) {
        report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                .entity = record.sceneEntity,
                .objectId = record.id,
                .asset = record.mesh,
                .message = "object record references a missing mesh asset.",
        });
    }
    if (record.material != InvalidAssetId && assets_.material(record.material) == nullptr) {
        report.issues.push_back(ProjectDocumentAssetReferenceIssue{
                .entity = record.sceneEntity,
                .objectId = record.id,
                .asset = record.material,
                .message = "object record references a missing material asset.",
        });
    }
    return report;
}

EntityId ProjectDocument::ensureSceneRoot() {
    EntityId root = scene_.sceneRoot();
    if (root == InvalidEntityId) {
        root = scene_.createEntity("scene", EntityKind::Empty);
        if (root == InvalidEntityId) {
            return InvalidEntityId;
        }
        if (!scene_.setSceneSettings(root, SceneSettingsComponent{})) {
            return InvalidEntityId;
        }
    }

    if (const NameComponent* name = scene_.name(root); name != nullptr && name->name != "scene") {
        (void)scene_.renameEntity(root, "scene");
    }
    if (scene_.kind(root) != EntityKind::Empty) {
        (void)scene_.setEntityKind(root, EntityKind::Empty);
    }
    if (const VisibilityComponent* visibility = scene_.visibility(root)) {
        if (!visibility->visible) {
            (void)scene_.setVisible(root, true);
        }
        if (!visibility->selectable) {
            (void)scene_.setSelectable(root, true);
        }
    }
    if (const LockComponent* lock = scene_.lock(root); lock != nullptr && lock->locked) {
        (void)scene_.setLocked(root, false);
    }

    const std::vector<EntityId> entities = scene_.entities();
    for (const EntityId entity : entities) {
        if (entity != root && scene_.parent(entity) == InvalidEntityId) {
            (void)scene_.setParent(entity, root, true);
        }
    }
    return root;
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
