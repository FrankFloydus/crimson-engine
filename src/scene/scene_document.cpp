////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "scene/scene_document.hpp"

#include "scene/scene_transform.hpp"

#include <stduuid/uuid.h>

#include <algorithm>
#include <array>
#include <random>
#include <unordered_set>
#include <string_view>
#include <utility>

namespace quader {
namespace {

std::uint32_t entityKey(entt::entity entity) {
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}

void removeChild(std::vector<EntityId>& children, EntityId child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}

std::mt19937& entityUuidRandomEngine() {
    thread_local std::mt19937 engine = [] {
        std::random_device randomDevice;
        std::array<std::uint32_t, 8> seedData{};
        for (std::uint32_t& value : seedData) {
            value = randomDevice();
        }
        std::seed_seq seed(seedData.begin(), seedData.end());
        return std::mt19937(seed);
    }();
    return engine;
}

std::string generateUniqueEntityUuid(const std::unordered_map<std::string, EntityId>& existing) {
    uuids::uuid_random_generator generator{entityUuidRandomEngine()};
    for (;;) {
        const std::string uuid = uuids::to_string(generator());
        if (!uuid.empty() && !existing.contains(uuid)) {
            return uuid;
        }
    }
}

bool validEntityUuid(std::string_view uuid) {
    return uuids::uuid::from_string(uuid).has_value();
}

} // namespace

void SceneDocument::setEditorObjectIdAllocator(EditorObjectIdAllocator allocator) {
    editorObjectIdAllocator_ = std::move(allocator);
}

EntityId SceneDocument::createEntity(std::string name, EntityKind kind, EditorObjectId editorObjectId) {
    const EntityId id = nextId_++;
    const EditorObjectId objectId = allocateEditorObjectId(editorObjectId);
    if (objectId == InvalidEditorObjectId) {
        return InvalidEntityId;
    }
    const entt::entity entity = registry_.create();
    registry_.emplace<EntityIdentityComponent>(entity, EntityIdentityComponent{.id = id});
    const std::string uuid = generateUniqueEntityUuid(entityByUuid_);
    registry_.emplace<EntityUuidComponent>(entity, EntityUuidComponent{.uuid = uuid});
    registry_.emplace<EditorObjectIdentityComponent>(entity, EditorObjectIdentityComponent{.id = objectId});
    registry_.emplace<NameComponent>(entity, NameComponent{.name = std::move(name)});
    registry_.emplace<TransformComponent>(entity, TransformComponent{});
    registry_.emplace<EntityKindComponent>(entity, EntityKindComponent{.kind = kind});
    registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
    registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
    registry_.emplace<LockComponent>(entity, LockComponent{});
    registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
    entities_.emplace(id, entity);
    idsByEntity_.emplace(entityKey(entity), id);
    entityByUuid_.emplace(uuid, id);
    entityByEditorObjectId_.emplace(objectId, id);
    markDirty();
    return id;
}

EntityId SceneDocument::createEntityWithId(
        EntityId id,
        std::string name,
        EntityKind kind,
        EditorObjectId editorObjectId) {
    if (id == InvalidEntityId) {
        return createEntity(std::move(name), kind, editorObjectId);
    }
    if (exists(id)) {
        return InvalidEntityId;
    }
    nextId_ = std::max(nextId_, id + 1u);
    const EditorObjectId objectId = allocateEditorObjectId(editorObjectId);
    if (objectId == InvalidEditorObjectId) {
        return InvalidEntityId;
    }
    const entt::entity entity = registry_.create();
    registry_.emplace<EntityIdentityComponent>(entity, EntityIdentityComponent{.id = id});
    const std::string uuid = generateUniqueEntityUuid(entityByUuid_);
    registry_.emplace<EntityUuidComponent>(entity, EntityUuidComponent{.uuid = uuid});
    registry_.emplace<EditorObjectIdentityComponent>(entity, EditorObjectIdentityComponent{.id = objectId});
    registry_.emplace<NameComponent>(entity, NameComponent{.name = std::move(name)});
    registry_.emplace<TransformComponent>(entity, TransformComponent{});
    registry_.emplace<EntityKindComponent>(entity, EntityKindComponent{.kind = kind});
    registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
    registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
    registry_.emplace<LockComponent>(entity, LockComponent{});
    registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
    entities_.emplace(id, entity);
    idsByEntity_.emplace(entityKey(entity), id);
    entityByUuid_.emplace(uuid, id);
    entityByEditorObjectId_.emplace(objectId, id);
    markDirty();
    return id;
}

EntityId SceneDocument::createEmpty(std::string name, EditorObjectId editorObjectId) {
    return createEntity(std::move(name), EntityKind::Empty, editorObjectId);
}

bool SceneDocument::destroyEntity(EntityId id) {
    if (isSceneRoot(id)) {
        return false;
    }
    const std::vector<EntityId> toDestroy = descendants(id, true);
    if (toDestroy.empty()) {
        return false;
    }

    const bool activeDestroyed =
            std::find(toDestroy.begin(), toDestroy.end(), activeSelection_) != toDestroy.end();
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        const EntityId entityId = *it;
        const entt::entity entity = requireEntity(entityId);
        if (entity == entt::null) {
            continue;
        }
        if (HierarchyComponent* component = mutableHierarchy(entityId)) {
            if (HierarchyComponent* parentHierarchy = mutableHierarchy(component->parent)) {
                removeChild(parentHierarchy->children, entityId);
            }
        }
        if (const EditorObjectIdentityComponent* identity = editorObjectIdentity(entityId)) {
            entityByEditorObjectId_.erase(identity->id);
        }
        if (const EntityUuidComponent* identity = entityUuid(entityId)) {
            entityByUuid_.erase(identity->uuid);
        }
        registry_.destroy(entity);
        entities_.erase(entityId);
        idsByEntity_.erase(entityKey(entity));
    }

    if (activeDestroyed || !isSelected(activeSelection_)) {
        activeSelection_ = firstSelectedEntity();
    }
    markDirty();
    return true;
}

bool SceneDocument::destroyEntities(std::span<const EntityId> ids) {
    std::unordered_set<EntityId> destroySet;
    for (const EntityId id : ids) {
        if (isSceneRoot(id)) {
            continue;
        }
        const std::vector<EntityId> descendantsToDestroy = descendants(id, true);
        destroySet.insert(descendantsToDestroy.begin(), descendantsToDestroy.end());
    }
    if (destroySet.empty()) {
        return false;
    }

    std::vector<EntityId> toDestroy;
    toDestroy.reserve(destroySet.size());
    for (const EntityId entityId : entityTreeOrder()) {
        if (destroySet.contains(entityId)) {
            toDestroy.push_back(entityId);
        }
    }
    if (toDestroy.empty()) {
        return false;
    }

    const bool activeDestroyed = destroySet.contains(activeSelection_);
    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        const EntityId entityId = *it;
        const entt::entity entity = requireEntity(entityId);
        if (entity == entt::null) {
            continue;
        }
        if (HierarchyComponent* component = mutableHierarchy(entityId)) {
            if (HierarchyComponent* parentHierarchy = mutableHierarchy(component->parent)) {
                removeChild(parentHierarchy->children, entityId);
            }
        }
        if (const EditorObjectIdentityComponent* identity = editorObjectIdentity(entityId)) {
            entityByEditorObjectId_.erase(identity->id);
        }
        if (const EntityUuidComponent* identity = entityUuid(entityId)) {
            entityByUuid_.erase(identity->uuid);
        }
        registry_.destroy(entity);
        entities_.erase(entityId);
        idsByEntity_.erase(entityKey(entity));
    }

    if (activeDestroyed || !isSelected(activeSelection_)) {
        activeSelection_ = firstSelectedEntity();
    }
    markDirty();
    return true;
}

bool SceneDocument::exists(EntityId id) const {
    return requireEntity(id) != entt::null;
}

bool SceneDocument::renameEntity(EntityId id, std::string name) {
    if (NameComponent* component = mutableName(id)) {
        component->name = std::move(name);
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setEntityUuid(EntityId id, std::string uuid) {
    if (uuid.empty() || !validEntityUuid(uuid)) {
        return false;
    }
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return false;
    }
    if (const auto existing = entityByUuid_.find(uuid); existing != entityByUuid_.end() && existing->second != id) {
        return false;
    }

    EntityUuidComponent* identity = registry_.try_get<EntityUuidComponent>(entity);
    if (identity != nullptr && identity->uuid == uuid) {
        return true;
    }
    if (identity != nullptr && !identity->uuid.empty()) {
        entityByUuid_.erase(identity->uuid);
        identity->uuid = std::move(uuid);
    } else {
        registry_.emplace_or_replace<EntityUuidComponent>(entity, EntityUuidComponent{.uuid = std::move(uuid)});
        identity = registry_.try_get<EntityUuidComponent>(entity);
    }
    if (identity != nullptr) {
        entityByUuid_[identity->uuid] = id;
    }
    markDirty();
    return true;
}

bool SceneDocument::setEditorObjectId(EntityId id, EditorObjectId editorObjectId) {
    if (editorObjectId == InvalidEditorObjectId) {
        return false;
    }
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return false;
    }
    if (const auto existing = entityByEditorObjectId_.find(editorObjectId);
            existing != entityByEditorObjectId_.end() && existing->second != id) {
        return false;
    }

    EditorObjectIdentityComponent* identity = registry_.try_get<EditorObjectIdentityComponent>(entity);
    if (identity != nullptr && identity->id == editorObjectId) {
        return true;
    }
    if (editorObjectIdAllocator_) {
        const EditorObjectId reserved = editorObjectIdAllocator_(editorObjectId);
        if (reserved != editorObjectId) {
            return false;
        }
    }
    if (identity != nullptr && identity->id != InvalidEditorObjectId) {
        entityByEditorObjectId_.erase(identity->id);
        identity->id = editorObjectId;
    } else {
        registry_.emplace_or_replace<EditorObjectIdentityComponent>(
                entity,
                EditorObjectIdentityComponent{.id = editorObjectId});
    }
    entityByEditorObjectId_[editorObjectId] = id;
    nextEditorObjectId_ = std::max(nextEditorObjectId_, editorObjectId + 1u);
    markDirty();
    return true;
}

bool SceneDocument::setEntityKind(EntityId id, EntityKind kind) {
    if (EntityKindComponent* component = mutableEntityKind(id)) {
        component->kind = kind;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setVisible(EntityId id, bool visible) {
    if (VisibilityComponent* component = mutableVisibility(id)) {
        component->visible = visible;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setSelectable(EntityId id, bool selectable) {
    if (VisibilityComponent* component = mutableVisibility(id)) {
        component->selectable = selectable;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setLocked(EntityId id, bool locked) {
    if (LockComponent* component = mutableLock(id)) {
        component->locked = locked;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setSelected(EntityId id, bool selected) {
    if (SelectionComponent* component = mutableSelection(id)) {
        const bool wasSelected = component->selected;
        const EntityId previousActiveSelection = activeSelection_;
        component->selected = selected;
        if (selected) {
            activeSelection_ = id;
        } else if (activeSelection_ == id) {
            activeSelection_ = firstSelectedEntity();
        }
        if (wasSelected != selected || previousActiveSelection != activeSelection_) {
            markDirty();
        }
        return true;
    }
    return false;
}

bool SceneDocument::setActiveSelection(EntityId id) {
    if (!isSelected(id)) {
        return false;
    }
    if (activeSelection_ != id) {
        activeSelection_ = id;
        markDirty();
    }
    return true;
}

void SceneDocument::clearSelection() {
    bool changed = false;
    for (auto [entity, selection] : registry_.view<SelectionComponent>().each()) {
        if (selection.selected) {
            selection.selected = false;
            changed = true;
        }
    }
    if (activeSelection_ != InvalidEntityId) {
        activeSelection_ = InvalidEntityId;
        changed = true;
    }
    if (changed) {
        markDirty();
    }
}

bool SceneDocument::setParent(EntityId child, EntityId newParent, bool preserveWorldTransform) {
    if (child == InvalidEntityId || requireEntity(child) == entt::null) {
        return false;
    }
    if (isSceneRoot(child) && newParent != InvalidEntityId) {
        return false;
    }
    if (newParent != InvalidEntityId && requireEntity(newParent) == entt::null) {
        return false;
    }
    if (child == newParent || isDescendantOf(newParent, child)) {
        return false;
    }

    HierarchyComponent* childHierarchy = mutableHierarchy(child);
    if (!childHierarchy) {
        return false;
    }

    const EntityId oldParent = childHierarchy->parent;
    if (oldParent == newParent) {
        return true;
    }

    const QMat4 childWorld = preserveWorldTransform ? worldTransformMatrix(child) : localTransformMatrix(child);

    if (HierarchyComponent* oldParentHierarchy = mutableHierarchy(oldParent)) {
        removeChild(oldParentHierarchy->children, child);
    }

    childHierarchy->parent = newParent;
    if (HierarchyComponent* newParentHierarchy = mutableHierarchy(newParent)) {
        if (std::find(newParentHierarchy->children.begin(), newParentHierarchy->children.end(), child) ==
                newParentHierarchy->children.end()) {
            newParentHierarchy->children.push_back(child);
        }
    }

    if (preserveWorldTransform) {
        const QMat4 parentWorld = newParent == InvalidEntityId
                ? sceneTransformIdentityMatrix()
                : worldTransformMatrix(newParent);
        if (TransformComponent* localTransform = mutableTransform(child)) {
            *localTransform = transformComponentFromMatrix(
                    sceneLocalMatrixFromWorldMatrix(parentWorld, childWorld),
                    localTransform->rotationEuler);
        }
    }

    markDirty();
    return true;
}

void SceneDocument::setTransform(EntityId id, TransformComponent transform) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<TransformComponent>(entity, transform);
    markDirty();
}

void SceneDocument::setModelingObject(EntityId id, ModelingObjectComponent object) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<ModelingObjectComponent>(entity, object);
    markDirty();
}

void SceneDocument::setMesh(EntityId id, MeshComponent mesh) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<MeshComponent>(entity, mesh);
    registry_.emplace_or_replace<EntityKindComponent>(entity, EntityKindComponent{.kind = EntityKind::Mesh});
    markDirty();
}

void SceneDocument::setMaterial(EntityId id, MaterialComponent material) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<MaterialComponent>(entity, material);
    markDirty();
}

bool SceneDocument::clearMaterial(EntityId id) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null || !registry_.all_of<MaterialComponent>(entity)) {
        return false;
    }
    registry_.remove<MaterialComponent>(entity);
    markDirty();
    return true;
}

void SceneDocument::setLight(EntityId id, LightComponent light) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<LightComponent>(entity, light);
    registry_.emplace_or_replace<EntityKindComponent>(entity, EntityKindComponent{.kind = EntityKind::Light});
    markDirty();
}

bool SceneDocument::setSceneSettings(EntityId id, SceneSettingsComponent settings) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return false;
    }
    const EntityId root = sceneRoot();
    if (root != InvalidEntityId && root != id) {
        return false;
    }
    if (const SceneSettingsComponent* current = sceneSettings(id); current != nullptr && *current == settings) {
        return true;
    }
    registry_.emplace_or_replace<SceneSettingsComponent>(entity, std::move(settings));
    registry_.emplace_or_replace<EntityKindComponent>(entity, EntityKindComponent{.kind = EntityKind::Empty});
    markDirty();
    return true;
}

NameComponent* SceneDocument::mutableName(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<NameComponent>(entity);
}

TransformComponent* SceneDocument::mutableTransform(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<TransformComponent>(entity);
}

EntityKindComponent* SceneDocument::mutableEntityKind(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<EntityKindComponent>(entity);
}

HierarchyComponent* SceneDocument::mutableHierarchy(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<HierarchyComponent>(entity);
}

VisibilityComponent* SceneDocument::mutableVisibility(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<VisibilityComponent>(entity);
}

LockComponent* SceneDocument::mutableLock(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<LockComponent>(entity);
}

SelectionComponent* SceneDocument::mutableSelection(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<SelectionComponent>(entity);
}

const NameComponent* SceneDocument::name(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<NameComponent>(entity);
}

const EntityUuidComponent* SceneDocument::entityUuid(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<EntityUuidComponent>(entity);
}

const EditorObjectIdentityComponent* SceneDocument::editorObjectIdentity(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<EditorObjectIdentityComponent>(entity);
}

const TransformComponent* SceneDocument::transform(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<TransformComponent>(entity);
}

const EntityKindComponent* SceneDocument::entityKind(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<EntityKindComponent>(entity);
}

const HierarchyComponent* SceneDocument::hierarchy(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<HierarchyComponent>(entity);
}

const VisibilityComponent* SceneDocument::visibility(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<VisibilityComponent>(entity);
}

const LockComponent* SceneDocument::lock(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<LockComponent>(entity);
}

const SelectionComponent* SceneDocument::selection(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<SelectionComponent>(entity);
}

const ModelingObjectComponent* SceneDocument::modelingObject(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<ModelingObjectComponent>(entity);
}

const MeshComponent* SceneDocument::mesh(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<MeshComponent>(entity);
}

const MaterialComponent* SceneDocument::material(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<MaterialComponent>(entity);
}

const LightComponent* SceneDocument::light(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<LightComponent>(entity);
}

const SceneSettingsComponent* SceneDocument::sceneSettings(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<SceneSettingsComponent>(entity);
}

std::vector<EntityId> SceneDocument::entities() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

EntityId SceneDocument::sceneRoot() const {
    EntityId root = InvalidEntityId;
    for (auto [entity, settings, identity] :
            registry_.view<SceneSettingsComponent, EntityIdentityComponent>().each()) {
        (void)settings;
        if (root == InvalidEntityId || identity.id < root) {
            root = identity.id;
        }
    }
    return exists(root) ? root : InvalidEntityId;
}

bool SceneDocument::isSceneRoot(EntityId id) const {
    return id != InvalidEntityId && sceneSettings(id) != nullptr;
}

std::vector<EntityId> SceneDocument::rootEntities() const {
    std::vector<EntityId> roots;
    const std::vector<EntityId> ids = entities();
    roots.reserve(ids.size());
    const EntityId root = sceneRoot();
    if (root != InvalidEntityId && parent(root) == InvalidEntityId) {
        roots.push_back(root);
    }
    for (const EntityId id : ids) {
        if (id != root && parent(id) == InvalidEntityId) {
            roots.push_back(id);
        }
    }
    return roots;
}

std::vector<EntityId> SceneDocument::children(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->children;
    }
    return {};
}

std::vector<EntityId> SceneDocument::descendants(EntityId id, bool includeSelf) const {
    if (requireEntity(id) == entt::null) {
        return {};
    }

    std::vector<EntityId> result;
    if (includeSelf) {
        result.push_back(id);
    }
    for (const EntityId child : children(id)) {
        std::vector<EntityId> childDescendants = descendants(child, true);
        result.insert(result.end(), childDescendants.begin(), childDescendants.end());
    }
    return result;
}

std::vector<EntityId> SceneDocument::entityTreeOrder() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const EntityId root : rootEntities()) {
        appendTreeOrder(root, ids);
    }
    return ids;
}

std::vector<EntityRecord> SceneDocument::entityRecords() const {
    std::vector<EntityRecord> records;
    const std::vector<EntityId> ids = entities();
    const EntityId active = primarySelection();
    records.reserve(ids.size());

    for (const EntityId id : ids) {
        records.push_back(makeEntityRecord(id, 0, active));
    }

    return records;
}

std::vector<EntityRecord> SceneDocument::entityRecordsTreeOrder() const {
    std::vector<EntityRecord> records;
    records.reserve(entities_.size());
    const EntityId active = primarySelection();
    for (const EntityId root : rootEntities()) {
        appendEntityRecords(root, 0, active, records);
    }
    return records;
}

std::vector<EntityId> SceneDocument::selectedEntities() const {
    std::vector<EntityId> ids;
    for (auto [entity, selection, identity] :
            registry_.view<SelectionComponent, EntityIdentityComponent>().each()) {
        if (selection.selected) {
            ids.push_back(identity.id);
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

EntityId SceneDocument::primarySelection() const {
    if (isSelected(activeSelection_)) {
        return activeSelection_;
    }
    return firstSelectedEntity();
}

EntityId SceneDocument::parent(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return component->parent;
    }
    return InvalidEntityId;
}

std::string SceneDocument::uuid(EntityId id) const {
    const EntityUuidComponent* identity = entityUuid(id);
    return identity == nullptr ? std::string{} : identity->uuid;
}

EntityId SceneDocument::entityForUuid(std::string_view uuid) const {
    const auto existing = entityByUuid_.find(std::string(uuid));
    if (existing == entityByUuid_.end() || !exists(existing->second)) {
        return InvalidEntityId;
    }
    return existing->second;
}

EditorObjectId SceneDocument::editorObjectId(EntityId id) const {
    const EditorObjectIdentityComponent* identity = editorObjectIdentity(id);
    return identity == nullptr ? InvalidEditorObjectId : identity->id;
}

EntityId SceneDocument::entityForEditorObjectId(EditorObjectId editorObjectId) const {
    const auto existing = entityByEditorObjectId_.find(editorObjectId);
    if (existing == entityByEditorObjectId_.end() || !exists(existing->second)) {
        return InvalidEntityId;
    }
    return existing->second;
}

bool SceneDocument::hasChildren(EntityId id) const {
    if (const HierarchyComponent* component = hierarchy(id)) {
        return !component->children.empty();
    }
    return false;
}

bool SceneDocument::isDescendantOf(EntityId entity, EntityId possibleAncestor) const {
    if (entity == InvalidEntityId || possibleAncestor == InvalidEntityId) {
        return false;
    }
    EntityId current = parent(entity);
    std::size_t guard = entities_.size() + 1u;
    while (current != InvalidEntityId && guard-- > 0u) {
        if (current == possibleAncestor) {
            return true;
        }
        current = parent(current);
    }
    return false;
}

EntityKind SceneDocument::kind(EntityId id) const {
    if (const EntityKindComponent* component = entityKind(id)) {
        return component->kind;
    }
    return EntityKind::Generic;
}

bool SceneDocument::isEffectivelyVisible(EntityId id) const {
    if (requireEntity(id) == entt::null) {
        return false;
    }
    EntityId current = id;
    std::size_t guard = entities_.size() + 1u;
    while (current != InvalidEntityId && guard-- > 0u) {
        if (const VisibilityComponent* component = visibility(current)) {
            if (!component->visible) {
                return false;
            }
        }
        current = parent(current);
    }
    return true;
}

bool SceneDocument::isEffectivelyLocked(EntityId id) const {
    if (requireEntity(id) == entt::null) {
        return false;
    }
    EntityId current = id;
    std::size_t guard = entities_.size() + 1u;
    while (current != InvalidEntityId && guard-- > 0u) {
        if (const LockComponent* component = lock(current)) {
            if (component->locked) {
                return true;
            }
        }
        current = parent(current);
    }
    return false;
}

QMat4 SceneDocument::localTransformMatrix(EntityId id) const {
    if (const TransformComponent* component = transform(id)) {
        return composeTransform(component->spatialTransform());
    }
    return sceneTransformIdentityMatrix();
}

QMat4 SceneDocument::worldTransformMatrix(EntityId id) const {
    if (requireEntity(id) == entt::null) {
        return sceneTransformIdentityMatrix();
    }
    const EntityId parentId = parent(id);
    if (parentId == InvalidEntityId) {
        return localTransformMatrix(id);
    }
    return sceneTransformMultiply(worldTransformMatrix(parentId), localTransformMatrix(id));
}

void SceneDocument::advanceRevisionForReplacement(std::uint64_t previousRevision) {
    revision_ = std::max(revision_ + 1u, previousRevision + 1u);
}

entt::entity SceneDocument::enttEntity(EntityId id) const {
    return requireEntity(id);
}

EntityId SceneDocument::entityId(entt::entity entity) const {
    if (entity == entt::null || !registry_.valid(entity)) {
        return InvalidEntityId;
    }
    const auto it = idsByEntity_.find(entityKey(entity));
    return it == idsByEntity_.end() ? InvalidEntityId : it->second;
}

void SceneDocument::markDirty() {
    ++revision_;
}

EditorObjectId SceneDocument::allocateEditorObjectId(EditorObjectId requested) {
    if (requested != InvalidEditorObjectId) {
        if (entityByEditorObjectId_.contains(requested)) {
            return InvalidEditorObjectId;
        }
        if (editorObjectIdAllocator_) {
            const EditorObjectId allocated = editorObjectIdAllocator_(requested);
            if (allocated != requested) {
                return InvalidEditorObjectId;
            }
        }
        nextEditorObjectId_ = std::max(nextEditorObjectId_, requested + 1u);
        return requested;
    }
    if (editorObjectIdAllocator_) {
        const std::size_t guard = entityByEditorObjectId_.size() + 2u;
        for (std::size_t attempt = 0; attempt < guard; ++attempt) {
            const EditorObjectId allocated = editorObjectIdAllocator_(InvalidEditorObjectId);
            if (allocated != InvalidEditorObjectId && !entityByEditorObjectId_.contains(allocated)) {
                nextEditorObjectId_ = std::max(nextEditorObjectId_, allocated + 1u);
                return allocated;
            }
        }
        return InvalidEditorObjectId;
    }
    while (entityByEditorObjectId_.contains(nextEditorObjectId_)) {
        ++nextEditorObjectId_;
    }
    return nextEditorObjectId_++;
}

entt::entity SceneDocument::requireEntity(EntityId id) const {
    const auto it = entities_.find(id);
    if (it == entities_.end() || it->second == entt::null || !registry_.valid(it->second)) {
        return entt::null;
    }
    return it->second;
}

bool SceneDocument::isSelected(EntityId id) const {
    if (id == InvalidEntityId) {
        return false;
    }
    if (const SelectionComponent* component = selection(id)) {
        return component->selected;
    }
    return false;
}

EntityId SceneDocument::firstSelectedEntity() const {
    const std::vector<EntityId> selected = selectedEntities();
    return selected.empty() ? InvalidEntityId : selected.front();
}

EntityRecord SceneDocument::makeEntityRecord(EntityId id, int depth, EntityId active) const {
    EntityRecord record;
    record.id = id;
    record.uuid = uuid(id);
    record.editorObjectId = editorObjectId(id);
    record.kind = kind(id);
    record.parent = parent(id);
    record.depth = depth;
    if (const NameComponent* component = name(id)) {
        record.name = component->name;
    }
    if (const VisibilityComponent* component = visibility(id)) {
        record.visible = component->visible;
    }
    record.effectivelyVisible = isEffectivelyVisible(id);
    if (const LockComponent* component = lock(id)) {
        record.locked = component->locked;
    }
    record.effectivelyLocked = isEffectivelyLocked(id);
    if (const SelectionComponent* component = selection(id)) {
        record.selected = component->selected;
    }
    record.active = id == active;
    record.hasChildren = hasChildren(id);
    record.hasMesh = mesh(id) != nullptr;
    record.hasLight = light(id) != nullptr;
    record.isSceneRoot = isSceneRoot(id);
    record.hasSceneSettings = sceneSettings(id) != nullptr;
    record.hasModelingObject = modelingObject(id) != nullptr;
    return record;
}

void SceneDocument::appendTreeOrder(EntityId id, std::vector<EntityId>& ids) const {
    if (requireEntity(id) == entt::null) {
        return;
    }
    ids.push_back(id);
    for (const EntityId child : children(id)) {
        appendTreeOrder(child, ids);
    }
}

void SceneDocument::appendEntityRecords(
        EntityId id,
        int depth,
        EntityId active,
        std::vector<EntityRecord>& records) const {
    if (requireEntity(id) == entt::null) {
        return;
    }
    records.push_back(makeEntityRecord(id, depth, active));
    for (const EntityId child : children(id)) {
        appendEntityRecords(child, depth + 1, active, records);
    }
}

} // namespace quader
