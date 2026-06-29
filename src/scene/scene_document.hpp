////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "scene/components.hpp"

#include <entt/entt.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

/**
 * Stores the Entity Record data contract used by the scene graph document layer.
 */
struct EntityRecord {
    EntityId id = InvalidEntityId;
    std::string uuid;
    EditorObjectId editorObjectId = InvalidEditorObjectId;
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
    bool isSceneRoot = false;
    bool hasSceneSettings = false;
    bool hasModelingObject = false;
};

/**
 * Owns Scene Document behavior within the scene graph document layer.
 */
class SceneDocument {
public:
    using EditorObjectIdAllocator = std::function<EditorObjectId(EditorObjectId requested)>;

    void setEditorObjectIdAllocator(EditorObjectIdAllocator allocator);

    EntityId createEntity(
            std::string name,
            EntityKind kind = EntityKind::Generic,
            EditorObjectId editorObjectId = InvalidEditorObjectId);
    EntityId createEntityWithId(
            EntityId id,
            std::string name,
            EntityKind kind = EntityKind::Generic,
            EditorObjectId editorObjectId = InvalidEditorObjectId);
    EntityId createEmpty(std::string name, EditorObjectId editorObjectId = InvalidEditorObjectId);
    bool destroyEntity(EntityId id);
    bool exists(EntityId id) const;

    bool renameEntity(EntityId id, std::string name);
    bool setEntityUuid(EntityId id, std::string uuid);
    bool setEditorObjectId(EntityId id, EditorObjectId editorObjectId);
    bool setEntityKind(EntityId id, EntityKind kind);
    bool setVisible(EntityId id, bool visible);
    bool setSelectable(EntityId id, bool selectable);
    bool setLocked(EntityId id, bool locked);
    bool setSelected(EntityId id, bool selected);
    bool setActiveSelection(EntityId id);
    void clearSelection();
    bool setParent(EntityId child, EntityId parent, bool preserveWorldTransform = true);

    void setTransform(EntityId id, TransformComponent transform);
    void setModelingObject(EntityId id, ModelingObjectComponent object);
    void setMesh(EntityId id, MeshComponent mesh);
    void setMaterial(EntityId id, MaterialComponent material);
    bool clearMaterial(EntityId id);
    void setLight(EntityId id, LightComponent light);
    bool setSceneSettings(EntityId id, SceneSettingsComponent settings);

    const NameComponent* name(EntityId id) const;
    const EntityUuidComponent* entityUuid(EntityId id) const;
    const EditorObjectIdentityComponent* editorObjectIdentity(EntityId id) const;
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
    const SceneSettingsComponent* sceneSettings(EntityId id) const;

    std::vector<EntityId> entities() const;
    EntityId sceneRoot() const;
    bool isSceneRoot(EntityId id) const;
    std::vector<EntityId> rootEntities() const;
    std::vector<EntityId> children(EntityId id) const;
    std::vector<EntityId> descendants(EntityId id, bool includeSelf = true) const;
    std::vector<EntityId> entityTreeOrder() const;
    std::vector<EntityRecord> entityRecords() const;
    std::vector<EntityRecord> entityRecordsTreeOrder() const;
    std::vector<EntityId> selectedEntities() const;
    EntityId primarySelection() const;
    EntityId parent(EntityId id) const;
    std::string uuid(EntityId id) const;
    EntityId entityForUuid(std::string_view uuid) const;
    EditorObjectId editorObjectId(EntityId id) const;
    EntityId entityForEditorObjectId(EditorObjectId editorObjectId) const;
    bool hasChildren(EntityId id) const;
    bool isDescendantOf(EntityId entity, EntityId possibleAncestor) const;
    EntityKind kind(EntityId id) const;
    bool isEffectivelyVisible(EntityId id) const;
    bool isEffectivelyLocked(EntityId id) const;
    QMat4 localTransformMatrix(EntityId id) const;
    QMat4 worldTransformMatrix(EntityId id) const;

    entt::entity enttEntity(EntityId id) const;
    EntityId entityId(entt::entity entity) const;

    std::uint64_t revision() const { return revision_; }

private:
    NameComponent* mutableName(EntityId id);
    TransformComponent* mutableTransform(EntityId id);
    EntityKindComponent* mutableEntityKind(EntityId id);
    HierarchyComponent* mutableHierarchy(EntityId id);
    VisibilityComponent* mutableVisibility(EntityId id);
    LockComponent* mutableLock(EntityId id);
    SelectionComponent* mutableSelection(EntityId id);

    void markDirty();
    EditorObjectId allocateEditorObjectId(EditorObjectId requested);
    entt::entity requireEntity(EntityId id) const;
    bool isSelected(EntityId id) const;
    EntityId firstSelectedEntity() const;
    EntityRecord makeEntityRecord(EntityId id, int depth, EntityId active) const;
    void appendTreeOrder(EntityId id, std::vector<EntityId>& ids) const;
    void appendEntityRecords(EntityId id, int depth, EntityId active, std::vector<EntityRecord>& records) const;

    EntityId nextId_ = 1;
    EditorObjectId nextEditorObjectId_ = 1;
    EntityId activeSelection_ = InvalidEntityId;
    std::uint64_t revision_ = 0;
    EditorObjectIdAllocator editorObjectIdAllocator_;
    entt::registry registry_;
    std::unordered_map<EntityId, entt::entity> entities_;
    std::unordered_map<std::uint32_t, EntityId> idsByEntity_;
    std::unordered_map<std::string, EntityId> entityByUuid_;
    std::unordered_map<EditorObjectId, EntityId> entityByEditorObjectId_;
};

} // namespace quader
