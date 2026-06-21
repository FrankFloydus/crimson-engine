#include "document/ProjectDocument.h"

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

ProjectObjectId ProjectDocument::createObjectRecord(ProjectObjectRecord record) {
    record.id = allocateObjectId(record.id);
    const ProjectObjectId objectId = record.id;
    indexRecord(record);
    objects_[objectId] = std::move(record);
    markDirty();
    return objectId;
}

ProjectObjectId ProjectDocument::upsertObjectRecord(ProjectObjectRecord record) {
    record.id = allocateObjectId(record.id);
    const ProjectObjectId objectId = record.id;
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

const ProjectObjectRecord* ProjectDocument::objectRecord(ProjectObjectId objectId) const {
    const auto existing = objects_.find(objectId);
    return existing == objects_.end() ? nullptr : &existing->second;
}

ProjectObjectRecord* ProjectDocument::objectRecord(ProjectObjectId objectId) {
    auto existing = objects_.find(objectId);
    return existing == objects_.end() ? nullptr : &existing->second;
}

ProjectObjectId ProjectDocument::objectIdForEntity(EntityId entity) const {
    const auto existing = objectByEntity_.find(entity);
    return existing == objectByEntity_.end() ? InvalidProjectObjectId : existing->second;
}

EntityId ProjectDocument::entityForObjectId(ProjectObjectId objectId) const {
    const ProjectObjectRecord* record = objectRecord(objectId);
    return record ? record->sceneEntity : InvalidEntityId;
}

void ProjectDocument::forgetObjectRecord(ProjectObjectId objectId) {
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

std::uint64_t ProjectDocument::revision() const {
    return revision_;
}

void ProjectDocument::markDirty() {
    ++revision_;
}

void ProjectDocument::indexRecord(ProjectObjectRecord& record) {
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

ProjectObjectId ProjectDocument::allocateObjectId(ProjectObjectId requested) {
    if (requested != InvalidProjectObjectId) {
        nextObjectId_ = std::max(nextObjectId_, requested + 1u);
        return requested;
    }
    return nextObjectId_++;
}

} // namespace quader
