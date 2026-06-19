#include "quader/scene/SceneDocument.h"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

std::uint32_t entityKey(entt::entity entity) {
    return static_cast<std::uint32_t>(entt::to_integral(entity));
}

} // namespace

EntityId SceneDocument::createEntity(std::string name) {
    const EntityId id = nextId_++;
    const entt::entity entity = registry_.create();
    registry_.emplace<EntityIdentityComponent>(entity, EntityIdentityComponent{.id = id});
    registry_.emplace<NameComponent>(entity, NameComponent{.name = std::move(name)});
    registry_.emplace<TransformComponent>(entity, TransformComponent{});
    registry_.emplace<VisibilityComponent>(entity, VisibilityComponent{});
    registry_.emplace<SelectionComponent>(entity, SelectionComponent{});
    entities_.emplace(id, entity);
    idsByEntity_.emplace(entityKey(entity), id);
    markDirty();
    return id;
}

bool SceneDocument::destroyEntity(EntityId id) {
    const entt::entity entity = requireEntity(id);
    if (entity == entt::null) {
        return false;
    }
    const bool destroyedActiveSelection = activeSelection_ == id;
    registry_.destroy(entity);
    entities_.erase(id);
    idsByEntity_.erase(entityKey(entity));
    if (destroyedActiveSelection) {
        activeSelection_ = firstSelectedEntity();
    }
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

bool SceneDocument::setVisible(EntityId id, bool visible) {
    if (VisibilityComponent* component = visibility(id)) {
        component->visible = visible;
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

VisibilityComponent* SceneDocument::visibility(EntityId id) {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<VisibilityComponent>(entity);
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

const VisibilityComponent* SceneDocument::visibility(EntityId id) const {
    const entt::entity entity = requireEntity(id);
    return entity == entt::null ? nullptr : registry_.try_get<VisibilityComponent>(entity);
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

std::vector<EntityRecord> SceneDocument::entityRecords() const {
    std::vector<EntityRecord> records;
    const std::vector<EntityId> ids = entities();
    const EntityId active = primarySelection();
    records.reserve(ids.size());

    for (const EntityId id : ids) {
        EntityRecord record;
        record.id = id;
        if (const NameComponent* component = name(id)) {
            record.name = component->name;
        }
        if (const VisibilityComponent* component = visibility(id)) {
            record.visible = component->visible;
        }
        if (const SelectionComponent* component = selection(id)) {
            record.selected = component->selected;
        }
        record.active = id == active;
        record.hasMesh = mesh(id) != nullptr;
        record.hasLight = light(id) != nullptr;
        record.hasModelingObject = modelingObject(id) != nullptr;
        records.push_back(std::move(record));
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

} // namespace quader
