#include "Scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
std::atomic<uint64_t> nextGeneration{1};
bool safeReference(const std::string& id) {
    if (id.empty()) return false;
    const std::filesystem::path path(std::u8string(reinterpret_cast<const char8_t*>(id.data()), id.size()));
    if (path.has_root_path()) return false;
    for (const auto& part : path) if (part == "..") return false;
    return true;
}
template<class T> bool sameLight(const std::optional<T>& a, const std::optional<T>& b) {
    return a.has_value() == b.has_value() && (!a ||
        (a->color == b->color && a->intensity == b->intensity && a->enabled == b->enabled));
}
template<class T> void validateLight(const std::optional<T>& light) {
    if (!light) return;
    if (!math::finite(light->color) || !std::isfinite(light->intensity) || light->intensity < 0 ||
        std::any_of(light->color.begin(), light->color.end(), [](float value) { return value < 0; }))
        throw std::runtime_error("Light color and intensity must be finite and nonnegative.");
}
}

bool sameEntity(const EntityRecord& a, const EntityRecord& b) {
    return a.id == b.id && a.name == b.name && a.parent == b.parent && a.order == b.order &&
        a.local == b.local && a.model == b.model && sameLight(a.directional,b.directional) && sameLight(a.point,b.point);
}
bool sameProperties(const SceneProperties& a, const SceneProperties& b) {
    return a.exposure == b.exposure && a.environment.map == b.environment.map &&
        a.environment.enabled == b.environment.enabled && a.environment.intensity == b.environment.intensity &&
        a.environment.rotation == b.environment.rotation;
}

Scene::Scene() : generation_(nextGeneration.fetch_add(1)) {}

std::optional<EntityRecord> Scene::find(EntityId id) const {
    const auto it = entities_.find(id);
    if (it == entities_.end()) return std::nullopt;
    const entt::entity entity = it->second;
    const auto& identity = registry_.get<Identity>(entity);
    const auto& relation = registry_.get<Relationship>(entity);
    EntityRecord record{id,identity.name,relation.parent,relation.order,registry_.get<Transform>(entity).local};
    if (const auto* model = registry_.try_get<ModelReference>(entity)) record.model = model->assetId;
    if (const auto* light = registry_.try_get<SceneDirectionalLight>(entity)) record.directional = *light;
    if (const auto* light = registry_.try_get<ScenePointLight>(entity)) record.point = *light;
    return record;
}

std::vector<EntityRecord> Scene::records() const {
    std::vector<EntityRecord> result;
    result.reserve(entities_.size());
    for (const auto& [id, entity] : entities_) {
        (void)entity;
        result.push_back(*find(id));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    return result;
}

std::optional<Mat4> Scene::worldTransform(EntityId id) const {
    if (!contains(id)) return std::nullopt;
    Mat4 result;
    size_t remaining = entities_.size();
    while (id != NoEntity && remaining--) {
        const auto entity = entities_.at(id);
        result = registry_.get<Transform>(entity).local * result;
        id = registry_.get<Relationship>(entity).parent;
    }
    if (id != NoEntity || !math::finite(result)) return std::nullopt;
    return result;
}

std::vector<EntityId> Scene::children(EntityId parent) const {
    std::vector<std::pair<uint32_t, EntityId>> ordered;
    for (const auto& [id,entity] : entities_) {
        const auto& relationship = registry_.get<Relationship>(entity);
        if (relationship.parent == parent) ordered.emplace_back(relationship.order,id);
    }
    std::sort(ordered.begin(),ordered.end());
    std::vector<EntityId> result;
    result.reserve(ordered.size());
    for (const auto& [order,id] : ordered) { (void)order; result.push_back(id); }
    return result;
}

std::vector<EntityId> Scene::subtree(EntityId root) const {
    if (!contains(root)) return {};
    std::vector<EntityId> result, pending{root};
    while (!pending.empty()) {
        const EntityId id = pending.back(); pending.pop_back(); result.push_back(id);
        const auto descendants = children(id);
        pending.insert(pending.end(),descendants.rbegin(),descendants.rend());
    }
    return result;
}
std::vector<EntityId> Scene::preorder() const {
    std::vector<EntityId> result;
    for (const auto id : children()) {
        const auto branch = subtree(id);
        result.insert(result.end(),branch.begin(),branch.end());
    }
    return result;
}
std::vector<EntityId> Scene::topmost(const std::vector<EntityId>& selection) const {
    std::unordered_set<EntityId> selected;
    for (auto id : selection) if (contains(id)) selected.insert(id);
    std::vector<EntityId> result;
    for (auto id : preorder()) {
        if (!selected.contains(id)) continue;
        bool ancestorSelected = false;
        auto parent = registry_.get<Relationship>(entities_.at(id)).parent;
        while (parent) {
            if (selected.contains(parent)) { ancestorSelected = true; break; }
            parent = registry_.get<Relationship>(entities_.at(parent)).parent;
        }
        if (!ancestorSelected) result.push_back(id);
    }
    return result;
}

void Scene::validate(const std::vector<EntityRecord>& records, const SceneProperties& properties) {
    if (records.size() > 100000) throw std::runtime_error("Scene exceeds the 100000 entity limit.");
    if (!std::isfinite(properties.exposure) || properties.exposure <= 0 ||
        !std::isfinite(properties.environment.intensity) || properties.environment.intensity < 0 ||
        !std::isfinite(properties.environment.rotation))
        throw std::runtime_error("Scene lighting settings must be finite and exposure must be positive.");
    if (!properties.environment.map.empty() && !safeReference(properties.environment.map))
        throw std::runtime_error("Environment asset reference must stay inside the project.");
    std::unordered_map<EntityId,EntityId> parents;
    size_t directionalCount{}, pointCount{};
    for (const auto& record : records) {
        if (!record.id || !parents.emplace(record.id,record.parent).second)
            throw std::runtime_error("Scene has a zero or duplicate entity ID.");
        if (record.name.empty() || record.name.size() > 1024)
            throw std::runtime_error("Entity names must contain 1 to 1024 UTF-8 bytes.");
        if (!math::finite(record.local) || !math::isAffine(record.local))
            throw std::runtime_error("Entity local transform must be a finite affine matrix.");
        if (record.model && !safeReference(*record.model))
            throw std::runtime_error("Model asset reference must be a nonempty project-relative path.");
        validateLight(record.directional); validateLight(record.point);
        directionalCount += record.directional.has_value(); pointCount += record.point.has_value();
    }
    if (directionalCount > MaximumDirectionalLights || pointCount > MaximumPointLights)
        throw std::runtime_error("Scenes support at most 4 directional lights and 16 point lights.");
    std::unordered_map<EntityId,uint8_t> colors;
    for (const auto& [id,parent] : parents) {
        if (parent && !parents.contains(parent)) throw std::runtime_error("Scene references a missing parent entity.");
        if (colors[id] == 2) continue;
        std::vector<EntityId> chain;
        auto current = id;
        while (current && colors[current] == 0) {
            colors[current] = 1; chain.push_back(current);
            const auto next = parents.find(current);
            if (next == parents.end()) throw std::runtime_error("Scene references a missing parent entity.");
            current = next->second;
        }
        if (current && colors[current] == 1) throw std::runtime_error("Scene hierarchy contains a cycle.");
        for (auto visited : chain) colors[visited] = 2;
    }
}

void Scene::apply(const std::vector<EntityRecord>& records, const SceneProperties& properties) {
    validate(records,properties);
    entt::registry candidate;
    std::unordered_map<EntityId,entt::entity> entities;
    entities.reserve(records.size());
    for (const auto& record : records) {
        const auto entity = candidate.create();
        candidate.emplace<Identity>(entity,record.id,record.name);
        candidate.emplace<Transform>(entity,record.local);
        candidate.emplace<Relationship>(entity,record.parent,record.order);
        if (record.model) candidate.emplace<ModelReference>(entity,*record.model);
        if (record.directional) candidate.emplace<SceneDirectionalLight>(entity,*record.directional);
        if (record.point) candidate.emplace<ScenePointLight>(entity,*record.point);
        entities.emplace(record.id,entity);
    }
    SceneProperties prepared = properties;
    registry_ = std::move(candidate);
    entities_ = std::move(entities);
    properties_ = std::move(prepared);
    generation_ = nextGeneration.fetch_add(1);
    ++revision_;
}

} // namespace crimson
