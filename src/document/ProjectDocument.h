////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "scene/SceneDocument.h"

#include <cstdint>
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
    ProjectDocument() = default;
    explicit ProjectDocument(ProjectDocumentRecord record);

    std::uint64_t id() const { return record_.id; }
    const std::string& name() const { return record_.name; }
    void rename(std::string name);

    SceneDocument& scene() { return scene_; }
    const SceneDocument& scene() const { return scene_; }

    AssetRegistry& assets() { return assets_; }
    const AssetRegistry& assets() const { return assets_; }

    EditorObjectId createObjectRecord(ProjectObjectRecord record);
    EditorObjectId upsertObjectRecord(ProjectObjectRecord record);
    const ProjectObjectRecord* objectRecord(EditorObjectId objectId) const;
    ProjectObjectRecord* objectRecord(EditorObjectId objectId);
    EditorObjectId objectIdForEntity(EntityId entity) const;
    EntityId entityForObjectId(EditorObjectId objectId) const;
    void forgetObjectRecord(EditorObjectId objectId);
    void forgetEntity(EntityId entity);
    std::vector<ProjectObjectRecord> objectRecords() const;
    ProjectDocumentIdentityReport validateObjectIdentityInvariants() const;

    std::uint64_t revision() const;

private:
    void markDirty();
    void syncObjectRecordsWithScene() const;
    bool canAssignObjectId(EditorObjectId objectId, EntityId entity) const;
    ProjectObjectRecord recordForSceneEntity(EntityId entity, EditorObjectId objectId) const;
    void indexRecord(ProjectObjectRecord& record) const;
    EditorObjectId allocateObjectId(EditorObjectId requested);

    ProjectDocumentRecord record_;
    EditorObjectId nextObjectId_ = 1;
    std::uint64_t revision_ = 0;
    SceneDocument scene_;
    AssetRegistry assets_;
    mutable std::unordered_map<EditorObjectId, ProjectObjectRecord> objects_;
    mutable std::unordered_map<EntityId, EditorObjectId> objectByEntity_;
};

} // namespace quader
