#pragma once

#include "core/Types.h"
#include "scene/Components.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

struct EntityRecord {
    EntityId id = InvalidEntityId;
    std::string name;
    EntityKind kind = EntityKind::Generic;
    EntityId parent = InvalidEntityId;
    int depth = 0;
    bool visible = true;
    bool effectivelyVisible = true;
    bool locked = false;
    bool effectivelyLocked = false;
    bool selected = false;
    bool active = false;
    bool hasChildren = false;
    bool hasMesh = false;
    bool hasLight = false;
    bool hasModelingObject = false;
};

class SceneDocument {
public:
    EntityId createEntity(std::string name, EntityKind kind = EntityKind::Generic);
    EntityId createEmpty(std::string name);
    bool destroyEntity(EntityId id);
    bool exists(EntityId id) const;

    bool renameEntity(EntityId id, std::string name);
    bool setEntityKind(EntityId id, EntityKind kind);
    bool setVisible(EntityId id, bool visible);
    bool setLocked(EntityId id, bool locked);
    bool setSelected(EntityId id, bool selected);
    bool setActiveSelection(EntityId id);
    void clearSelection();
    bool setParent(EntityId child, EntityId parent, bool preserveWorldTransform = true);

    void setTransform(EntityId id, TransformComponent transform);
    void setModelingObject(EntityId id, ModelingObjectComponent object);
    void setMesh(EntityId id, MeshComponent mesh);
    void setMaterial(EntityId id, MaterialComponent material);
    void setLight(EntityId id, LightComponent light);

    NameComponent* name(EntityId id);
    TransformComponent* transform(EntityId id);
    EntityKindComponent* entityKind(EntityId id);
    HierarchyComponent* hierarchy(EntityId id);
    VisibilityComponent* visibility(EntityId id);
    LockComponent* lock(EntityId id);
    SelectionComponent* selection(EntityId id);
    ModelingObjectComponent* modelingObject(EntityId id);
    MeshComponent* mesh(EntityId id);
    MaterialComponent* material(EntityId id);
    LightComponent* light(EntityId id);

    const NameComponent* name(EntityId id) const;
    const TransformComponent* transform(EntityId id) const;
    const EntityKindComponent* entityKind(EntityId id) const;
    const HierarchyComponent* hierarchy(EntityId id) const;
    const VisibilityComponent* visibility(EntityId id) const;
    const LockComponent* lock(EntityId id) const;
    const SelectionComponent* selection(EntityId id) const;
    const ModelingObjectComponent* modelingObject(EntityId id) const;
    const MeshComponent* mesh(EntityId id) const;
    const MaterialComponent* material(EntityId id) const;
    const LightComponent* light(EntityId id) const;

    std::vector<EntityId> entities() const;
    std::vector<EntityId> rootEntities() const;
    std::vector<EntityId> children(EntityId id) const;
    std::vector<EntityId> descendants(EntityId id, bool includeSelf = true) const;
    std::vector<EntityId> entityTreeOrder() const;
    std::vector<EntityRecord> entityRecords() const;
    std::vector<EntityRecord> entityRecordsTreeOrder() const;
    std::vector<EntityId> selectedEntities() const;
    EntityId primarySelection() const;
    EntityId parent(EntityId id) const;
    bool hasChildren(EntityId id) const;
    bool isDescendantOf(EntityId entity, EntityId possibleAncestor) const;
    EntityKind kind(EntityId id) const;
    bool isEffectivelyVisible(EntityId id) const;
    bool isEffectivelyLocked(EntityId id) const;
    Mat4 localTransformMatrix(EntityId id) const;
    Mat4 worldTransformMatrix(EntityId id) const;

    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }
    entt::entity enttEntity(EntityId id) const;
    EntityId entityId(entt::entity entity) const;

    std::uint64_t revision() const { return revision_; }

private:
    void markDirty();
    entt::entity requireEntity(EntityId id) const;
    bool isSelected(EntityId id) const;
    EntityId firstSelectedEntity() const;
    EntityRecord makeEntityRecord(EntityId id, int depth, EntityId active) const;
    void appendTreeOrder(EntityId id, std::vector<EntityId>& ids) const;
    void appendEntityRecords(EntityId id, int depth, EntityId active, std::vector<EntityRecord>& records) const;

    EntityId nextId_ = 1;
    EntityId activeSelection_ = InvalidEntityId;
    std::uint64_t revision_ = 0;
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    std::unordered_map<std::uint32_t, EntityId> idsByEntity_;
};

} // namespace quader
