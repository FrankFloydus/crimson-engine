#include "scene/SceneDocument.h"

#include <algorithm>
#include <utility>

#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace quader {
namespace {

std::uint32_t entityKey(entt::entity entity) {
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}

Mat4 identityMatrix() {
    return Mat4{};
}

Mat4 multiply(const Mat4& left, const Mat4& right) {
    return fromGlm(toGlm(left) * toGlm(right));
}

TransformComponent transformFromMatrix(const Mat4& matrix) {
    glm::vec3 scale{1.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 translation{0.0f};
    glm::vec3 skew{0.0f};
    glm::vec4 perspective{0.0f};
    if (!glm::decompose(toGlm(matrix), scale, orientation, translation, skew, perspective)) {
        return TransformComponent{};
    }

    orientation = glm::normalize(orientation);
    const glm::vec3 euler = glm::eulerAngles(orientation);
    return TransformComponent{
            .position = fromGlm(translation),
            .rotationEuler = fromGlm(euler),
            .scale = fromGlm(scale),
    };
}

void removeChild(std::vector<EntityId>& children, EntityId child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
}

} // namespace

EntityId SceneDocument::createEntity(std::string name, EntityKind kind) {
    const EntityId id = nextId_++;
    const entt::entity entity = registry_.create();
    registry_.emplace<EntityIdentityComponent>(entity, EntityIdentityComponent{.id = id});
    registry_.emplace<NameComponent>(entity, NameComponent{.name = std::move(name)});
    registry_.emplace<TransformComponent>(entity, TransformComponent{});
    registry_.emplace<EntityKindComponent>(entity, EntityKindComponent{.kind = kind});
    registry_.emplace<HierarchyComponent>(entity, HierarchyComponent{});
    registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
    registry_.emplace<LockComponent>(entity, LockComponent{});
    registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
    entities_.emplace(id, entity);
    idsByEntity_.emplace(entityKey(entity), id);
    markDirty();
    return id;
}

EntityId SceneDocument::createEmpty(std::string name) {
    return createEntity(std::move(name), EntityKind::Empty);
}

bool SceneDocument::destroyEntity(EntityId id) {
    const std::vector<EntityId> toDestroy = descendants(id, true);
    if (toDestroy.empty()) {
        return false;
    }

    for (auto it = toDestroy.rbegin(); it != toDestroy.rend(); ++it) {
        const EntityId entityId = *it;
        const entt::entity entity = requireEntity(entityId);
        if (entity == entt::null) {
            continue;
        }
        if (HierarchyComponent* component = hierarchy(entityId)) {
            if (HierarchyComponent* parentHierarchy = hierarchy(component->parent)) {
                removeChild(parentHierarchy->children, entityId);
            }
        }
        registry_.destroy(entity);
        entities_.erase(entityId);
        idsByEntity_.erase(entityKey(entity));
    }

    activeSelection_ = firstSelectedEntity();
    markDirty();
    return true;
}

bool SceneDocument::exists(EntityId id) const {
    return requireEntity(id) != entt::null;
}

bool SceneDocument::renameEntity(EntityId id, std::string name) {
    if (NameComponent* component = this->name(id)) {
        component->name = std::move(name);
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setEntityKind(EntityId id, EntityKind kind) {
    if (EntityKindComponent* component = entityKind(id)) {
        component->kind = kind;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setVisible(EntityId id, bool visible) {
    if (VisibilityComponent* component = visibility(id)) {
        component->visible = visible;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setLocked(EntityId id, bool locked) {
    if (LockComponent* component = lock(id)) {
        component->locked = locked;
        markDirty();
        return true;
    }
    return false;
}

bool SceneDocument::setSelected(EntityId id, bool selected) {
    if (SelectionComponent* component = selection(id)) {
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
    if (newParent != InvalidEntityId && requireEntity(newParent) == entt::null) {
        return false;
    }
    if (child == newParent || isDescendantOf(newParent, child)) {
        return false;
    }

    HierarchyComponent* childHierarchy = hierarchy(child);
    if (!childHierarchy) {
        return false;
    }

    const EntityId oldParent = childHierarchy->parent;
    if (oldParent == newParent) {
        return true;
    }

    const Mat4 childWorld = preserveWorldTransform ? worldTransformMatrix(child) : localTransformMatrix(child);

    if (HierarchyComponent* oldParentHierarchy = hierarchy(oldParent)) {
        removeChild(oldParentHierarchy->children, child);
    }

    childHierarchy->parent = newParent;
    if (HierarchyComponent* newParentHierarchy = hierarchy(newParent)) {
        if (std::find(newParentHierarchy->children.begin(), newParentHierarchy->children.end(), child) ==
                newParentHierarchy->children.end()) {
            newParentHierarchy->children.push_back(child);
        }
    }

    if (preserveWorldTransform) {
        const Mat4 parentWorld = newParent == InvalidEntityId ? identityMatrix() : worldTransformMatrix(newParent);
        if (TransformComponent* localTransform = transform(child)) {
            *localTransform = transformFromMatrix(multiply(inverse(parentWorld), childWorld));
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

void SceneDocument::setLight(EntityId id, LightComponent light) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return;
    }
    registry_.emplace_or_replace<LightComponent>(entity, light);
    registry_.emplace_or_replace<EntityKindComponent>(entity, EntityKindComponent{.kind = EntityKind::Light});
    markDirty();
}

NameComponent* SceneDocument::name(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<NameComponent>(entity);
}

TransformComponent* SceneDocument::transform(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<TransformComponent>(entity);
}

EntityKindComponent* SceneDocument::entityKind(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<EntityKindComponent>(entity);
}

HierarchyComponent* SceneDocument::hierarchy(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<HierarchyComponent>(entity);
}

VisibilityComponent* SceneDocument::visibility(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<VisibilityComponent>(entity);
}

LockComponent* SceneDocument::lock(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<LockComponent>(entity);
}

SelectionComponent* SceneDocument::selection(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<SelectionComponent>(entity);
}

ModelingObjectComponent* SceneDocument::modelingObject(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<ModelingObjectComponent>(entity);
}

MeshComponent* SceneDocument::mesh(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<MeshComponent>(entity);
}

MaterialComponent* SceneDocument::material(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<MaterialComponent>(entity);
}

LightComponent* SceneDocument::light(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<LightComponent>(entity);
}

const NameComponent* SceneDocument::name(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<NameComponent>(entity);
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

std::vector<EntityId> SceneDocument::entities() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        ids.push_back(id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<EntityId> SceneDocument::rootEntities() const {
    std::vector<EntityId> roots;
    const std::vector<EntityId> ids = entities();
    roots.reserve(ids.size());
    for (const EntityId id : ids) {
        if (parent(id) == InvalidEntityId) {
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

Mat4 SceneDocument::localTransformMatrix(EntityId id) const {
    if (const TransformComponent* component = transform(id)) {
        return composeTransform(component->spatialTransform());
    }
    return identityMatrix();
}

Mat4 SceneDocument::worldTransformMatrix(EntityId id) const {
    if (requireEntity(id) == entt::null) {
        return identityMatrix();
    }
    const EntityId parentId = parent(id);
    if (parentId == InvalidEntityId) {
        return localTransformMatrix(id);
    }
    return multiply(worldTransformMatrix(parentId), localTransformMatrix(id));
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
