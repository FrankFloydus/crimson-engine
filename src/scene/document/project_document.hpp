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
 * Stores the Project Object Record data contract used by the editor document ownership layer.
 */
struct ProjectObjectRecord {
    EditorObjectId id = InvalidEditorObjectId;
    EntityId sceneEntity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
    ModelingObjectId modelingObject = InvalidModelingObjectId;
    std::string authoredName;
    bool visible = true;
    std::uint32_t materialSlot = 0;

    [[nodiscard]] bool operator==(const ProjectObjectRecord&) const = default;
};

/**
 * Stores the Project Document Identity Issue data contract used by the editor document ownership layer.
 */
struct ProjectDocumentIdentityIssue {
    EntityId entity = InvalidEntityId;
    EditorObjectId objectId = InvalidEditorObjectId;
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
 * Owns Project Document behavior within the editor document ownership layer.
 */
class ProjectDocument {
public:
    ProjectDocument();
    explicit ProjectDocument(ProjectDocumentRecord record);
    ProjectDocument(const ProjectDocument&) = delete;
    ProjectDocument& operator=(const ProjectDocument&) = delete;
    ProjectDocument(ProjectDocument&&) noexcept = default;
    ProjectDocument& operator=(ProjectDocument&&) noexcept = default;

    std::uint64_t id() const { return record_.id; }
    const std::string& name() const { return record_.name; }
    void rename(std::string name);

    SceneDocument& scene() { return scene_; }
    const SceneDocument& scene() const { return scene_; }

    AssetRegistry& assets() { return assets_; }
    const AssetRegistry& assets() const { return assets_; }

    bool synchronizeObjectRecordsWithScene();
    EditorObjectId createObjectRecord(ProjectObjectRecord record);
    EditorObjectId upsertObjectRecord(ProjectObjectRecord record);
    std::optional<ProjectObjectRecord> objectRecord(EditorObjectId objectId) const;
    EditorObjectId objectIdForEntity(EntityId entity) const;
    EntityId entityForObjectId(EditorObjectId objectId) const;
    void forgetObjectRecord(EditorObjectId objectId);
    void forgetEntity(EntityId entity);
    std::vector<ProjectObjectRecord> objectRecords() const;
    ProjectDocumentIdentityReport validateObjectIdentityInvariants() const;

    std::uint64_t revision() const;

private:
    void markDirty();
    void bindSceneObjectIdentityAllocator();
    bool syncObjectRecordsWithScene();
    bool canAssignObjectId(EditorObjectId objectId, EntityId entity) const;
    std::optional<ProjectObjectRecord> storedObjectRecord(EditorObjectId objectId) const;
    ProjectObjectRecord recordForSceneEntity(EntityId entity, EditorObjectId objectId) const;
    ProjectObjectRecord mergedSceneObjectRecord(ProjectObjectRecord record) const;
    void hydrateRecordFromScene(ProjectObjectRecord& record) const;
    void indexRecord(ProjectObjectRecord& record);
    EditorObjectId allocateObjectId(EditorObjectId requested);

    ProjectDocumentRecord record_;
    std::shared_ptr<EditorObjectId> nextObjectId_ = std::make_shared<EditorObjectId>(1);
    std::uint64_t revision_ = 0;
    SceneDocument scene_;
    AssetRegistry assets_;
    std::unordered_map<EditorObjectId, ProjectObjectRecord> objects_;
    std::unordered_map<EntityId, EditorObjectId> objectByEntity_;
};

} // namespace quader
