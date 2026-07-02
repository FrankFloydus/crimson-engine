////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/types.hpp"
#include "scene/scene_document.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

/**
 * Stores the Project Document Record data contract used by the editor document ownership layer.
 */
struct ProjectDocumentRecord {
    std::uint64_t id = 0;
    std::string name = "Untitled Project";
};

/**
 * Selects whether a ProjectDocument constructor should immediately synthesize the real scene root.
 */
enum class ProjectDocumentRootPolicy {
    EnsureSceneRoot,
    DeferSceneRoot,
};

/**
 * Stores the Project Object Record data contract used by the editor document ownership layer.
 */
struct ProjectObjectRecord {
  EditorObjectId id = kInvalidEditorObjectId;
  EntityId scene_entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  AssetId material = kInvalidAssetId;
  ModelingObjectId modeling_object = kInvalidModelingObjectId;
  std::string authored_name;
  bool visible = true;
  std::uint32_t material_slot = 0;

  [[nodiscard]] bool operator==(const ProjectObjectRecord &) const = default;
};

/**
 * Stores the Project Document Identity Issue data contract used by the editor document ownership layer.
 */
struct ProjectDocumentIdentityIssue {
  EntityId entity = kInvalidEntityId;
  EditorObjectId object_id = kInvalidEditorObjectId;
  std::string message;
};

/**
 * Stores the Project Document Identity Report data contract used by the editor document ownership layer.
 */
struct ProjectDocumentIdentityReport {
    std::vector<ProjectDocumentIdentityIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

/**
 * Stores one Project Document asset/reference issue.
 */
struct ProjectDocumentAssetReferenceIssue {
  EntityId entity = kInvalidEntityId;
  EditorObjectId object_id = kInvalidEditorObjectId;
  AssetId asset = kInvalidAssetId;
  std::string message;
};

/**
 * Stores Project Document asset/reference validation results.
 */
struct ProjectDocumentAssetReferenceReport {
    std::vector<ProjectDocumentAssetReferenceIssue> issues;

    [[nodiscard]] bool ok() const { return issues.empty(); }
};

/**
 * Owns Project Document behavior within the editor document ownership layer.
 */
class ProjectDocument {
public:
  explicit ProjectDocument(ProjectDocumentRootPolicy root_policy =
                               ProjectDocumentRootPolicy::EnsureSceneRoot);
  explicit ProjectDocument(ProjectDocumentRecord record,
                           ProjectDocumentRootPolicy root_policy =
                               ProjectDocumentRootPolicy::EnsureSceneRoot);
  ProjectDocument(const ProjectDocument &) = delete;
  ProjectDocument &operator=(const ProjectDocument &) = delete;
  ProjectDocument(ProjectDocument &&) noexcept = default;
  ProjectDocument &operator=(ProjectDocument &&) noexcept = default;

  std::uint64_t id() const { return record_.id; }
  const std::string &name() const { return record_.name; }
  void rename(std::string name);

  SceneDocument &scene() { return scene_; }
  const SceneDocument &scene() const { return scene_; }

  AssetRegistry &assets() { return assets_; }
  const AssetRegistry &assets() const { return assets_; }

  bool synchronize_object_records_with_scene();
  EditorObjectId create_object_record(ProjectObjectRecord record);
  EditorObjectId upsert_object_record(ProjectObjectRecord record);
  std::vector<EditorObjectId>
  upsert_object_records(std::vector<ProjectObjectRecord> records);
  std::optional<ProjectObjectRecord>
  object_record(EditorObjectId object_id) const;
  EditorObjectId object_id_for_entity(EntityId entity) const;
  EntityId entity_for_object_id(EditorObjectId object_id) const;
  void forget_object_record(EditorObjectId object_id);
  void forget_entity(EntityId entity);
  std::vector<ProjectObjectRecord> object_records() const;
  ProjectDocumentIdentityReport validate_object_identity_invariants() const;
  ProjectDocumentAssetReferenceReport validate_asset_references() const;
  ProjectDocumentAssetReferenceReport
  validate_asset_references(ProjectObjectRecord record) const;
  EntityId ensure_scene_root();

  std::uint64_t revision() const;

private:
  void mark_dirty();
  void bind_scene_object_identity_allocator();
  bool sync_object_records_with_scene();
  EditorObjectId upsert_object_record_after_sync(ProjectObjectRecord record);
  bool can_assign_object_id(EditorObjectId object_id, EntityId entity) const;
  std::optional<ProjectObjectRecord>
  stored_object_record(EditorObjectId object_id) const;
  ProjectObjectRecord record_for_scene_entity(EntityId entity,
                                              EditorObjectId object_id) const;
  ProjectObjectRecord
  merged_scene_object_record(ProjectObjectRecord record) const;
  void hydrate_record_from_scene(ProjectObjectRecord &record) const;
  void index_record(ProjectObjectRecord &record);
  EditorObjectId allocate_object_id(EditorObjectId requested);

  ProjectDocumentRecord record_;
  std::shared_ptr<EditorObjectId> next_object_id_ =
      std::make_shared<EditorObjectId>(1);
  std::uint64_t revision_ = 0;
  SceneDocument scene_;
  AssetRegistry assets_;
  std::unordered_map<EditorObjectId, ProjectObjectRecord> objects_;
  std::unordered_map<EntityId, EditorObjectId> object_by_entity_;
};

} // namespace quader
