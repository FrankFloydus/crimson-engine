#pragma once

#include "assets/AssetRegistry.h"
#include "core/Types.h"
#include "scene/SceneDocument.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

using ProjectObjectId = std::uint64_t;
inline constexpr ProjectObjectId InvalidProjectObjectId = 0;

struct ProjectDocumentRecord {
    std::uint64_t id = 0;
    std::string name = "Untitled Project";
};

struct ProjectObjectRecord {
    ProjectObjectId id = InvalidProjectObjectId;
    EntityId sceneEntity = InvalidEntityId;
    AssetId mesh = InvalidAssetId;
    AssetId material = InvalidAssetId;
    ModelingObjectId modelingObject = InvalidModelingObjectId;
    std::string authoredName;
    bool visible = true;
    std::uint32_t materialSlot = 0;
};

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

    ProjectObjectId createObjectRecord(ProjectObjectRecord record);
    ProjectObjectId upsertObjectRecord(ProjectObjectRecord record);
    const ProjectObjectRecord* objectRecord(ProjectObjectId objectId) const;
    ProjectObjectRecord* objectRecord(ProjectObjectId objectId);
    ProjectObjectId objectIdForEntity(EntityId entity) const;
    EntityId entityForObjectId(ProjectObjectId objectId) const;
    void forgetObjectRecord(ProjectObjectId objectId);
    void forgetEntity(EntityId entity);
    std::vector<ProjectObjectRecord> objectRecords() const;

    std::uint64_t revision() const;

private:
    void markDirty();
    void indexRecord(ProjectObjectRecord& record);
    ProjectObjectId allocateObjectId(ProjectObjectId requested);

    ProjectDocumentRecord record_;
    ProjectObjectId nextObjectId_ = 1;
    std::uint64_t revision_ = 0;
    SceneDocument scene_;
    AssetRegistry assets_;
    std::unordered_map<ProjectObjectId, ProjectObjectRecord> objects_;
    std::unordered_map<EntityId, ProjectObjectId> objectByEntity_;
};

} // namespace quader
