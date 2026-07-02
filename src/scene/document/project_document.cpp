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

ProjectDocument::ProjectDocument(ProjectDocumentRootPolicy root_policy) {
  bind_scene_object_identity_allocator();
  if (root_policy == ProjectDocumentRootPolicy::EnsureSceneRoot) {
    (void)ensure_scene_root();
  }
}

ProjectDocument::ProjectDocument(ProjectDocumentRecord record,
                                 ProjectDocumentRootPolicy root_policy)
    : record_(std::move(record)) {
  bind_scene_object_identity_allocator();
  if (root_policy == ProjectDocumentRootPolicy::EnsureSceneRoot) {
    (void)ensure_scene_root();
  }
}

void ProjectDocument::rename(std::string name) {
    if (record_.name == name) {
        return;
    }
    record_.name = std::move(name);
    mark_dirty();
}

bool ProjectDocument::synchronize_object_records_with_scene() {
  if (!sync_object_records_with_scene()) {
    return false;
  }
  mark_dirty();
  return true;
}

EditorObjectId
ProjectDocument::create_object_record(ProjectObjectRecord record) {
  const bool records_synced = sync_object_records_with_scene();
  const auto invalid_result = [&]() {
    if (records_synced) {
      mark_dirty();
    }
    return kInvalidEditorObjectId;
  };
  if (record.scene_entity != kInvalidEntityId &&
      scene_.exists(record.scene_entity)) {
    const EditorObjectId requested = record.id;
    EditorObjectId object_id =
        requested != kInvalidEditorObjectId
            ? requested
            : scene_.editor_object_id(record.scene_entity);
    if (object_id == kInvalidEditorObjectId) {
      object_id = allocate_object_id(kInvalidEditorObjectId);
    }
    if (!can_assign_object_id(object_id, record.scene_entity)) {
      return invalid_result();
    }
    (void)allocate_object_id(object_id);
    if (scene_.editor_object_id(record.scene_entity) != object_id &&
        !scene_.set_editor_object_id(record.scene_entity, object_id)) {
      return invalid_result();
    }
    record.id = object_id;
    if (const auto existing = object_by_entity_.find(record.scene_entity);
        existing != object_by_entity_.end()) {
      objects_.erase(existing->second);
      object_by_entity_.erase(existing);
    }
  } else {
    if (record.id != kInvalidEditorObjectId && objects_.contains(record.id)) {
      return invalid_result();
    }
    record.id = allocate_object_id(record.id);
    if (record.id == kInvalidEditorObjectId) {
      return invalid_result();
    }
  }
  const EditorObjectId object_id = record.id;
  index_record(record);
  objects_[object_id] = std::move(record);
  mark_dirty();
  return object_id;
}

EditorObjectId
ProjectDocument::upsert_object_record(ProjectObjectRecord record) {
  const bool records_synced = sync_object_records_with_scene();
  const EditorObjectId object_id =
      upsert_object_record_after_sync(std::move(record));
  if (object_id == kInvalidEditorObjectId) {
    if (records_synced) {
      mark_dirty();
    }
    return kInvalidEditorObjectId;
  }
  mark_dirty();
  return object_id;
}

std::vector<EditorObjectId> ProjectDocument::upsert_object_records(
    std::vector<ProjectObjectRecord> records) {
  const bool records_synced = sync_object_records_with_scene();
  std::vector<EditorObjectId> object_ids;
  object_ids.reserve(records.size());
  for (ProjectObjectRecord &record : records) {
    const EditorObjectId object_id =
        upsert_object_record_after_sync(std::move(record));
    if (object_id == kInvalidEditorObjectId) {
      if (records_synced || !object_ids.empty()) {
        mark_dirty();
      }
      return {};
    }
    object_ids.push_back(object_id);
  }
  if (records_synced || !object_ids.empty()) {
    mark_dirty();
  }
  return object_ids;
}

EditorObjectId
ProjectDocument::upsert_object_record_after_sync(ProjectObjectRecord record) {
  if (record.scene_entity != kInvalidEntityId &&
      scene_.exists(record.scene_entity)) {
    EditorObjectId object_id = record.id;
    if (object_id == kInvalidEditorObjectId) {
      object_id = scene_.editor_object_id(record.scene_entity);
    }
    if (object_id == kInvalidEditorObjectId) {
      object_id = allocate_object_id(kInvalidEditorObjectId);
    }
    if (!can_assign_object_id(object_id, record.scene_entity)) {
      return kInvalidEditorObjectId;
    }
    (void)allocate_object_id(object_id);
    if (scene_.editor_object_id(record.scene_entity) != object_id &&
        !scene_.set_editor_object_id(record.scene_entity, object_id)) {
      return kInvalidEditorObjectId;
    }
    record.id = object_id;
    if (const auto existing = object_by_entity_.find(record.scene_entity);
        existing != object_by_entity_.end() && existing->second != object_id) {
      objects_.erase(existing->second);
      object_by_entity_.erase(existing);
    }
  } else {
    if (record.id != kInvalidEditorObjectId &&
        scene_.entity_for_editor_object_id(record.id) != kInvalidEntityId) {
      return kInvalidEditorObjectId;
    }
    record.id = allocate_object_id(record.id);
    if (record.id == kInvalidEditorObjectId) {
      return kInvalidEditorObjectId;
    }
  }
  const EditorObjectId object_id = record.id;
  if (auto existing = objects_.find(object_id); existing != objects_.end()) {
    if (existing->second.scene_entity != kInvalidEntityId) {
      object_by_entity_.erase(existing->second.scene_entity);
    }
  }
  index_record(record);
  objects_[object_id] = std::move(record);
  return object_id;
}

std::optional<ProjectObjectRecord>
ProjectDocument::object_record(EditorObjectId object_id) const {
  if (object_id == kInvalidEditorObjectId) {
    return std::nullopt;
  }
  if (const EntityId entity = scene_.entity_for_editor_object_id(object_id);
      entity != kInvalidEntityId) {
    return merged_scene_object_record(
        record_for_scene_entity(entity, object_id));
  }
  return stored_object_record(object_id);
}

EditorObjectId ProjectDocument::object_id_for_entity(EntityId entity) const {
  if (const EditorObjectId object_id = scene_.editor_object_id(entity);
      object_id != kInvalidEditorObjectId) {
    return object_id;
  }
  const auto existing = object_by_entity_.find(entity);
  if (existing != object_by_entity_.end() &&
      object_record(existing->second).has_value()) {
    return existing->second;
  }
  for (const auto &[objectId, record] : objects_) {
    if (record.scene_entity == entity && object_record(objectId).has_value()) {
      return objectId;
    }
  }
  return kInvalidEditorObjectId;
}

EntityId ProjectDocument::entity_for_object_id(EditorObjectId object_id) const {
  if (const EntityId entity = scene_.entity_for_editor_object_id(object_id);
      entity != kInvalidEntityId) {
    return entity;
  }
  const std::optional<ProjectObjectRecord> record = object_record(object_id);
  return record.has_value() ? record->scene_entity : kInvalidEntityId;
}

void ProjectDocument::forget_object_record(EditorObjectId object_id) {
  auto existing = objects_.find(object_id);
  if (existing == objects_.end()) {
    return;
  }
  if (existing->second.scene_entity != kInvalidEntityId) {
    object_by_entity_.erase(existing->second.scene_entity);
  }
  objects_.erase(existing);
  mark_dirty();
}

void ProjectDocument::forget_entity(EntityId entity) {
  const auto existing = object_by_entity_.find(entity);
  if (existing == object_by_entity_.end()) {
    return;
  }
  objects_.erase(existing->second);
  object_by_entity_.erase(existing);
  mark_dirty();
}

std::vector<ProjectObjectRecord> ProjectDocument::object_records() const {
  std::unordered_map<EditorObjectId, ProjectObjectRecord> records_by_id;
  records_by_id.reserve(objects_.size() + scene_.entities().size());
  for (const auto &[objectId, record] : objects_) {
    if (const std::optional<ProjectObjectRecord> stored =
            stored_object_record(objectId)) {
      records_by_id[objectId] = *stored;
    }
  }
  for (const EntityId entity : scene_.entities()) {
    const EditorObjectId object_id = scene_.editor_object_id(entity);
    if (object_id == kInvalidEditorObjectId) {
      continue;
    }
    records_by_id[object_id] =
        merged_scene_object_record(record_for_scene_entity(entity, object_id));
  }

  std::vector<ProjectObjectRecord> records;
  records.reserve(records_by_id.size());
  for (const auto &[id, record] : records_by_id) {
    (void)id;
    records.push_back(record);
  }

  std::sort(
      records.begin(), records.end(),
      [](const ProjectObjectRecord &left, const ProjectObjectRecord &right) {
        return left.id < right.id;
      });
  return records;
}

ProjectDocumentIdentityReport
ProjectDocument::validate_object_identity_invariants() const {
  ProjectDocumentIdentityReport report;
  std::unordered_map<EditorObjectId, EntityId> entity_by_object_id;
  std::unordered_map<EditorObjectId, ProjectObjectRecord> records_by_id;
  for (const ProjectObjectRecord &record : object_records()) {
    records_by_id[record.id] = record;
  }
  for (const EntityId entity : scene_.entities()) {
    const EditorObjectId object_id = scene_.editor_object_id(entity);
    if (object_id == kInvalidEditorObjectId) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Scene entity is missing an editor object id.",
      });
      continue;
    }
    if (const auto duplicate = entity_by_object_id.find(object_id);
        duplicate != entity_by_object_id.end()) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Multiple scene entities share an editor object id.",
      });
    }
    entity_by_object_id[object_id] = entity;

    const auto record = records_by_id.find(object_id);
    if (record == records_by_id.end()) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message = "Scene entity is missing a project object record.",
      });
      continue;
    }
    if (record->second.scene_entity != entity) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = entity,
          .object_id = object_id,
          .message =
              "Project object record points at a different scene entity.",
      });
    }
  }

  for (const auto &[objectId, record] : records_by_id) {
    if (record.scene_entity == kInvalidEntityId) {
      continue;
    }
    if (!scene_.exists(record.scene_entity)) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = record.scene_entity,
          .object_id = objectId,
          .message = "Project object record points at a missing scene entity.",
      });
      continue;
    }
    if (const EditorObjectId scene_object_id =
            scene_.editor_object_id(record.scene_entity);
        scene_object_id != objectId) {
      report.issues.push_back(ProjectDocumentIdentityIssue{
          .entity = record.scene_entity,
          .object_id = objectId,
          .message = "Project object record id does not match the scene editor "
                     "object id.",
      });
    }
  }

  return report;
}

ProjectDocumentAssetReferenceReport
ProjectDocument::validate_asset_references() const {
  ProjectDocumentAssetReferenceReport report;
  for (const EntityId entity : scene_.entities()) {
    if (const MeshComponent *mesh = scene_.mesh(entity);
        mesh != nullptr && (mesh->mesh == kInvalidAssetId ||
                            assets_.mesh(mesh->mesh) == nullptr)) {
      report.issues.push_back(ProjectDocumentAssetReferenceIssue{
          .entity = entity,
          .asset = mesh->mesh,
          .message = "entity references a missing mesh asset.",
      });
    }
    if (const MaterialComponent *material = scene_.material(entity);
        material != nullptr &&
        assets_.material(material->material) == nullptr) {
      report.issues.push_back(ProjectDocumentAssetReferenceIssue{
          .entity = entity,
          .asset = material->material,
          .message = "entity references a missing material asset.",
      });
    }
    if (const MaterialComponent *material = scene_.material(entity)) {
      for (const auto &[slot, materialId] : material->material_slots) {
        (void)slot;
        if (materialId == kInvalidAssetId ||
            assets_.material(materialId) == nullptr) {
          report.issues.push_back(ProjectDocumentAssetReferenceIssue{
              .entity = entity,
              .asset = materialId,
              .message =
                  "entity material slot references a missing material asset.",
          });
        }
      }
    }
  }

  for (const ProjectObjectRecord &record : object_records()) {
    ProjectDocumentAssetReferenceReport record_report =
        validate_asset_references(record);
    report.issues.insert(report.issues.end(),
                         std::make_move_iterator(record_report.issues.begin()),
                         std::make_move_iterator(record_report.issues.end()));
  }
  return report;
}

ProjectDocumentAssetReferenceReport
ProjectDocument::validate_asset_references(ProjectObjectRecord record) const {
  ProjectDocumentAssetReferenceReport report;
  if (record.id == kInvalidEditorObjectId) {
    report.issues.push_back(ProjectDocumentAssetReferenceIssue{
        .entity = record.scene_entity,
        .object_id = record.id,
        .message = "contains an object record without a stable object id.",
    });
  }
  if (record.scene_entity == kInvalidEntityId ||
      !scene_.exists(record.scene_entity)) {
    report.issues.push_back(ProjectDocumentAssetReferenceIssue{
        .entity = record.scene_entity,
        .object_id = record.id,
        .message = "object record points at a missing scene entity.",
    });
  }
  if (record.mesh != kInvalidAssetId && assets_.mesh(record.mesh) == nullptr) {
    report.issues.push_back(ProjectDocumentAssetReferenceIssue{
        .entity = record.scene_entity,
        .object_id = record.id,
        .asset = record.mesh,
        .message = "object record references a missing mesh asset.",
    });
  }
  if (record.material != kInvalidAssetId &&
      assets_.material(record.material) == nullptr) {
    report.issues.push_back(ProjectDocumentAssetReferenceIssue{
        .entity = record.scene_entity,
        .object_id = record.id,
        .asset = record.material,
        .message = "object record references a missing material asset.",
    });
  }
  return report;
}

EntityId ProjectDocument::ensure_scene_root() {
  EntityId root = scene_.scene_root();
  if (root == kInvalidEntityId) {
    root = scene_.create_entity("scene", EntityKind::Empty);
    if (root == kInvalidEntityId) {
      return kInvalidEntityId;
    }
    if (!scene_.set_scene_settings(root, SceneSettingsComponent{})) {
      return kInvalidEntityId;
    }
  }

  if (const NameComponent *name = scene_.name(root);
      name != nullptr && name->name != "scene") {
    (void)scene_.rename_entity(root, "scene");
  }
  if (scene_.kind(root) != EntityKind::Empty) {
    (void)scene_.set_entity_kind(root, EntityKind::Empty);
  }
  if (const VisibilityComponent *visibility = scene_.visibility(root)) {
    if (!visibility->visible) {
      (void)scene_.set_visible(root, true);
    }
    if (!visibility->selectable) {
      (void)scene_.set_selectable(root, true);
    }
  }
  if (const LockComponent *lock = scene_.lock(root);
      lock != nullptr && lock->locked) {
    (void)scene_.set_locked(root, false);
  }

  const std::vector<EntityId> entities = scene_.entities();
  for (const EntityId entity : entities) {
    if (entity != root && scene_.parent(entity) == kInvalidEntityId) {
      (void)scene_.set_parent(entity, root, true);
    }
  }
  return root;
}

std::uint64_t ProjectDocument::revision() const {
    return revision_;
}

void ProjectDocument::mark_dirty() { ++revision_; }

void ProjectDocument::bind_scene_object_identity_allocator() {
  scene_.set_editor_object_id_allocator(
      [next_object_id = next_object_id_](EditorObjectId requested) {
        if (!next_object_id) {
          return kInvalidEditorObjectId;
        }
        if (requested != kInvalidEditorObjectId) {
          *next_object_id = std::max(*next_object_id, requested + 1u);
          return requested;
        }
        return (*next_object_id)++;
      });
}

bool ProjectDocument::sync_object_records_with_scene() {
  std::unordered_map<EditorObjectId, ProjectObjectRecord> synced_objects;
  std::unordered_map<EntityId, EditorObjectId> synced_object_by_entity;
  for (ProjectObjectRecord record : object_records()) {
    if (record.id == kInvalidEditorObjectId) {
      continue;
    }
    if (record.scene_entity != kInvalidEntityId) {
      synced_object_by_entity[record.scene_entity] = record.id;
    }
    synced_objects[record.id] = std::move(record);
  }
  if (synced_objects == objects_ &&
      synced_object_by_entity == object_by_entity_) {
    return false;
  }
  objects_ = std::move(synced_objects);
  object_by_entity_ = std::move(synced_object_by_entity);
  return true;
}

bool ProjectDocument::can_assign_object_id(EditorObjectId object_id,
                                           EntityId entity) const {
  if (object_id == kInvalidEditorObjectId) {
    return false;
  }
  if (const EntityId owner = scene_.entity_for_editor_object_id(object_id);
      owner != kInvalidEntityId && owner != entity) {
    return false;
  }
  const auto existing = objects_.find(object_id);
  return existing == objects_.end() || existing->second.scene_entity == entity;
}

std::optional<ProjectObjectRecord>
ProjectDocument::stored_object_record(EditorObjectId object_id) const {
  const auto existing = objects_.find(object_id);
  if (existing == objects_.end()) {
    return std::nullopt;
  }
  ProjectObjectRecord record = existing->second;
  if (record.scene_entity != kInvalidEntityId) {
    if (!scene_.exists(record.scene_entity)) {
      return std::nullopt;
    }
    const EditorObjectId scene_object_id =
        scene_.editor_object_id(record.scene_entity);
    if (scene_object_id != kInvalidEditorObjectId &&
        scene_object_id != object_id) {
      return std::nullopt;
    }
  }
  hydrate_record_from_scene(record);
  return record;
}

ProjectObjectRecord
ProjectDocument::record_for_scene_entity(EntityId entity,
                                         EditorObjectId object_id) const {
  ProjectObjectRecord record{
      .id = object_id,
      .scene_entity = entity,
  };
  hydrate_record_from_scene(record);
  return record;
}

ProjectObjectRecord
ProjectDocument::merged_scene_object_record(ProjectObjectRecord record) const {
  if (const auto existing = objects_.find(record.id);
      existing != objects_.end()) {
    ProjectObjectRecord merged = existing->second;
    merged.scene_entity = record.scene_entity;
    merged.mesh = record.mesh;
    merged.material = record.material;
    merged.modeling_object = record.modeling_object;
    merged.visible = record.visible;
    merged.material_slot = record.material_slot;
    if (merged.authored_name.empty() && !record.authored_name.empty()) {
      merged.authored_name = record.authored_name;
    }
    return merged;
  }
  return record;
}

void ProjectDocument::hydrate_record_from_scene(
    ProjectObjectRecord &record) const {
  if (record.authored_name.empty() && record.scene_entity != kInvalidEntityId) {
    if (const NameComponent *name = scene_.name(record.scene_entity)) {
      record.authored_name = name->name;
    }
  }
  if (record.visible && record.scene_entity != kInvalidEntityId) {
    if (const VisibilityComponent *visibility =
            scene_.visibility(record.scene_entity)) {
      record.visible = visibility->visible;
    }
  }
  if (record.modeling_object == kInvalidModelingObjectId &&
      record.scene_entity != kInvalidEntityId) {
    if (const ModelingObjectComponent *object =
            scene_.modeling_object(record.scene_entity)) {
      record.modeling_object = object->object_id;
      record.material_slot = object->material_slot;
    }
  }
  if (record.mesh == kInvalidAssetId &&
      record.scene_entity != kInvalidEntityId) {
    if (const MeshComponent *mesh = scene_.mesh(record.scene_entity)) {
      record.mesh = mesh->mesh;
    }
  }
  if (record.material == kInvalidAssetId &&
      record.scene_entity != kInvalidEntityId) {
    if (const MaterialComponent *material =
            scene_.material(record.scene_entity)) {
      record.material = material->material;
    }
  }
}

void ProjectDocument::index_record(ProjectObjectRecord &record) {
  hydrate_record_from_scene(record);
  if (record.scene_entity != kInvalidEntityId) {
    object_by_entity_[record.scene_entity] = record.id;
  }
}

EditorObjectId ProjectDocument::allocate_object_id(EditorObjectId requested) {
  if (!next_object_id_) {
    next_object_id_ = std::make_shared<EditorObjectId>(1);
    bind_scene_object_identity_allocator();
  }
  if (requested != kInvalidEditorObjectId) {
    *next_object_id_ = std::max(*next_object_id_, requested + 1u);
    return requested;
  }
  while (objects_.contains(*next_object_id_) ||
         scene_.entity_for_editor_object_id(*next_object_id_) !=
             kInvalidEntityId) {
    ++(*next_object_id_);
  }
  return (*next_object_id_)++;
}

} // namespace quader
