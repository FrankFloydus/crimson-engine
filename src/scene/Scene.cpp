#include "Scene.h"
#include "BuiltinModels.h"
#include "assets/MaterialAsset.h"

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
    return a==b;
}
template<class T> void validateLight(const std::optional<T>& light) {
    if (!light) return;
    if (!math::finite(light->color) || !std::isfinite(light->intensity) || light->intensity < 0 ||
        std::any_of(light->color.begin(), light->color.end(), [](float value) { return value < 0; }))
        throw std::runtime_error("Light color and intensity must be finite and nonnegative.");
}
std::vector<EntityId> cameraFollowOrder(const std::vector<EntityRecord>& records) {
    std::unordered_map<EntityId,const EntityRecord*> indexed;
    for(const auto& record:records)indexed.emplace(record.id,&record);
    std::unordered_set<EntityId> visited;
    std::vector<EntityId> result;
    for(const auto& record:records) {
        std::vector<std::pair<EntityId,bool>> pending{{record.id,false}};
        while(!pending.empty()) {
            const auto [id,leaving]=pending.back();pending.pop_back();
            if(!id)continue;
            const auto& current=*indexed.at(id);
            if(leaving){if(current.camera && current.camera->followTarget)result.push_back(id);continue;}
            if(!visited.insert(id).second)continue;
            pending.emplace_back(id,true);
            if(current.camera && current.camera->followTarget)pending.emplace_back(current.camera->followTarget,false);
            pending.emplace_back(current.parent,false);
        }
    }
    return result;
}
}

bool sameEntity(const EntityRecord& a, const EntityRecord& b) {
    return a.id == b.id && a.name == b.name && a.parent == b.parent && a.order == b.order &&
        a.local == b.local && a.model == b.model && a.particle == b.particle && sameLight(a.directional,b.directional) && sameLight(a.point,b.point) &&
        a.environment.has_value()==b.environment.has_value() && (!a.environment || sameEnvironment(*a.environment,*b.environment)) &&
        a.character==b.character && a.collider==b.collider && a.camera==b.camera && a.modelMaterial==b.modelMaterial &&
        a.castShadows==b.castShadows && a.receiveShadows==b.receiveShadows;
}
bool sameProperties(const SceneProperties& a, const SceneProperties& b) {
    return a.activeEnvironment == b.activeEnvironment && a.activeCamera==b.activeCamera && a.playerCharacter==b.playerCharacter;
}
bool sameEnvironment(const SceneEnvironment& a, const SceneEnvironment& b) {
    return a.exposure == b.exposure && a.environment.map == b.environment.map &&
        a.environment.enabled == b.environment.enabled && a.environment.intensity == b.environment.intensity &&
        a.environment.rotation == b.environment.rotation && a.environment.toneMapping == b.environment.toneMapping &&
        a.environment.glow == b.environment.glow;
}
SceneEnvironment resolveEnvironment(const Scene& scene) {
    if (const auto active=scene.find(scene.properties().activeEnvironment); active && active->environment)
        return *active->environment;
    return {};
}

Scene::Scene() : generation_(nextGeneration.fetch_add(1)) {}

std::optional<EntityRecord> Scene::find(EntityId id) const {
    const auto it = entities_.find(id);
    if (it == entities_.end()) return std::nullopt;
    const entt::entity entity = it->second;
    const auto& identity = registry_.get<Identity>(entity);
    const auto& relation = registry_.get<Relationship>(entity);
    EntityRecord record{id,identity.name,relation.parent,relation.order,registry_.get<Transform>(entity).local};
    if (const auto* model = registry_.try_get<ModelReference>(entity)) {
        record.model=model->assetId;record.modelMaterial=model->material;
        record.castShadows=model->castShadows;record.receiveShadows=model->receiveShadows;
    }
    if (const auto* particle = registry_.try_get<ParticleReference>(entity)) record.particle = particle->assetId;
    if (const auto* light = registry_.try_get<SceneDirectionalLight>(entity)) record.directional = *light;
    if (const auto* light = registry_.try_get<ScenePointLight>(entity)) record.point = *light;
    if (const auto* environment = registry_.try_get<SceneEnvironment>(entity)) record.environment = *environment;
    if (const auto* character = registry_.try_get<SceneCharacter>(entity)) record.character = *character;
    if (const auto* collider = registry_.try_get<SceneCollider>(entity)) record.collider = *collider;
    if (const auto* camera = registry_.try_get<SceneCamera>(entity)) record.camera = *camera;
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
    std::unordered_map<EntityId,EntityId> parents;
    size_t directionalCount{}, pointCount{};
    for (const auto& record : records) {
        if (!record.id || !parents.emplace(record.id,record.parent).second)
            throw std::runtime_error("Scene has a zero or duplicate entity ID.");
        if (record.name.empty() || record.name.size() > 1024)
            throw std::runtime_error("Entity names must contain 1 to 1024 UTF-8 bytes.");
        if (!math::finite(record.local) || !math::isAffine(record.local))
            throw std::runtime_error("Entity local transform must be a finite affine matrix.");
        if (record.model && !isBuiltinModel(record.model->id) && (record.model->id.starts_with("builtin:") || !safeReference(record.model->id)))
            throw std::runtime_error("Model asset reference must be a nonempty project-relative path.");
        if (!record.model && (!record.castShadows || !record.receiveShadows))
            throw std::runtime_error("Model shadow settings require a model entity.");
        if (!record.modelMaterial.empty()) {
            if (!record.model || !isBuiltinModel(record.model->id))
                throw std::runtime_error("Instance material assignment is supported only on built-in Box models.");
            const auto& id=record.modelMaterial.id;
            const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size()));
            if (id!=DefaultMaterialId && (!safeReference(id) || path.parent_path()!=std::filesystem::path("assets/materials") || path.extension()!=".crimson-material"))
                throw std::runtime_error("Model material must reference a managed .crimson-material asset or the protected PBR default.");
        }
        if (record.particle && !record.particle->empty()) {
            const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(record.particle->id.data()),record.particle->id.size()));
            if (!safeReference(record.particle->id) || path.parent_path()!=std::filesystem::path("assets/particles") || path.extension()!=".crimson-particle")
                throw std::runtime_error("Particle asset reference must be a managed .crimson-particle path under assets/particles.");
        }
        validateLight(record.directional); validateLight(record.point);
        if (record.point && (!std::isfinite(record.point->radius) || record.point->radius<.01f || record.point->radius>100000.f))
            throw std::runtime_error("Point light radius must be finite and between 0.01 and 100000 meters.");
        if (record.point && (!std::isfinite(record.point->shadowDepthBias) || record.point->shadowDepthBias<0 ||
            !std::isfinite(record.point->shadowNormalBias) || record.point->shadowNormalBias<0))
            throw std::runtime_error("Point shadow biases must be finite and nonnegative.");
        if (record.character) {
            const auto& c=*record.character;
            if (!std::isfinite(c.height) || !std::isfinite(c.radius) || !std::isfinite(c.moveSpeed) ||
                c.radius<=0 || c.height<2*c.radius || c.moveSpeed<0)
                throw std::runtime_error("Character capsule height must be at least its diameter; radius must be positive and speed nonnegative.");
        }
        if (record.collider && (!math::finite(record.collider->size) ||
            std::any_of(record.collider->size.begin(),record.collider->size.end(),[](float size){return size<=0;})))
            throw std::runtime_error("Box collider size must be finite and positive on every axis.");
        if (record.camera) {
            const auto& c=*record.camera;
            if (!std::isfinite(c.verticalFov) || c.verticalFov<1 || c.verticalFov>179 ||
                !std::isfinite(c.nearPlane) || c.nearPlane<=0 || !std::isfinite(c.farPlane) || c.farPlane<=c.nearPlane ||
                !math::finite(c.followOffset))
                throw std::runtime_error("Camera needs a finite vertical FOV from 1 to 179 degrees, positive clipping planes and a finite follow offset.");
        }
        if (record.environment) {
            const auto& environment=*record.environment;
            if (!std::isfinite(environment.exposure) || environment.exposure<=0 ||
                !std::isfinite(environment.environment.intensity) || environment.environment.intensity<0 ||
                !std::isfinite(environment.environment.rotation) || !validToneMapping(environment.environment.toneMapping) ||
                !validGlowSettings(environment.environment.glow))
                throw std::runtime_error("Environment settings must be finite, exposure positive, and tone mapping and Glow valid.");
            if (!environment.environment.map.empty() && !safeReference(environment.environment.map))
                throw std::runtime_error("Environment asset reference must stay inside the project.");
        }
        directionalCount += record.directional.has_value(); pointCount += record.point.has_value();
    }
    if (directionalCount > MaximumDirectionalLights || pointCount > MaximumPointLights)
        throw std::runtime_error("Scenes support at most 4 directional lights and 16 point lights.");
    if (properties.activeEnvironment && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.activeEnvironment && record.environment.has_value();
    })) throw std::runtime_error("The active environment must identify an Environment entity.");
    if (properties.activeCamera && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.activeCamera && record.camera.has_value();
    })) throw std::runtime_error("The active camera must identify a Camera entity.");
    if (properties.playerCharacter && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.playerCharacter && record.character.has_value();
    })) throw std::runtime_error("The player must identify a Character entity.");
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
    // Follow and hierarchy together form pose dependencies. Iterative traversal
    // also rejects indirect camera loops without risking deep-scene recursion.
    std::unordered_map<EntityId,EntityId> follows;
    std::unordered_map<EntityId,const EntityRecord*> indexed;
    for(const auto& record:records)indexed.emplace(record.id,&record);
    for (const auto& record:records) if(record.camera && record.camera->followTarget) {
        if (!parents.contains(record.camera->followTarget)) throw std::runtime_error("Camera follow target does not exist.");
        Mat4 parentWorld;auto parent=record.parent;
        while(parent){const auto& ancestor=*indexed.at(parent);parentWorld=ancestor.local*parentWorld;parent=ancestor.parent;}
        if(!math::inverse(parentWorld))throw std::runtime_error("A following camera requires an invertible parent transform.");
        follows.emplace(record.id,record.camera->followTarget);
    }
    colors.clear();
    for (const auto& record:records) {
        std::vector<std::pair<EntityId,bool>> pending{{record.id,false}};
        while (!pending.empty()) {
            const auto [id,leaving]=pending.back();pending.pop_back();
            if (!id) continue;
            if (leaving) { colors[id]=2;continue; }
            if (colors[id]==2) continue;
            if (colors[id]==1) throw std::runtime_error("Camera follow and parenting contain a dependency cycle.");
            colors[id]=1;pending.emplace_back(id,true);
            if(const auto target=follows.find(id);target!=follows.end())pending.emplace_back(target->second,false);
            pending.emplace_back(parents.at(id),false);
        }
    }
}

void Scene::apply(const std::vector<EntityRecord>& records, const SceneProperties& properties) {
    validate(records,properties);
    auto followOrder=cameraFollowOrder(records);
    entt::registry candidate;
    std::unordered_map<EntityId,entt::entity> entities;
    entities.reserve(records.size());
    for (const auto& record : records) {
        const auto entity = candidate.create();
        candidate.emplace<Identity>(entity,record.id,record.name);
        candidate.emplace<Transform>(entity,record.local);
        candidate.emplace<Relationship>(entity,record.parent,record.order);
        if (record.model) candidate.emplace<ModelReference>(entity,*record.model,record.modelMaterial,record.castShadows,record.receiveShadows);
        if (record.particle) candidate.emplace<ParticleReference>(entity,*record.particle);
        if (record.directional) candidate.emplace<SceneDirectionalLight>(entity,*record.directional);
        if (record.point) candidate.emplace<ScenePointLight>(entity,*record.point);
        if (record.environment) candidate.emplace<SceneEnvironment>(entity,*record.environment);
        if (record.character) candidate.emplace<SceneCharacter>(entity,*record.character);
        if (record.collider) candidate.emplace<SceneCollider>(entity,*record.collider);
        if (record.camera) candidate.emplace<SceneCamera>(entity,*record.camera);
        entities.emplace(record.id,entity);
    }
    Scene prepared;
    prepared.registry_=std::move(candidate);prepared.entities_=std::move(entities);prepared.properties_=properties;
    prepared.cameraFollowOrder_=std::move(followOrder);prepared.resolveCameraFollow();
    registry_.swap(prepared.registry_);entities_.swap(prepared.entities_);properties_=properties;
    cameraFollowOrder_.swap(prepared.cameraFollowOrder_);
    generation_ = nextGeneration.fetch_add(1);
    ++revision_;
    ++poseRevision_;
}
void Scene::adoptPrepared(Scene& candidate) noexcept {
    registry_.swap(candidate.registry_);entities_.swap(candidate.entities_);properties_=candidate.properties_;
    cameraFollowOrder_.swap(candidate.cameraFollowOrder_);
    generation_=candidate.generation_;++revision_;++poseRevision_;
}

void Scene::applyRuntimePoses(std::span<const ScenePoseUpdate> poses) {
    for(const auto& pose:poses) if(!contains(pose.id) || !math::finite(pose.local) || !math::isAffine(pose.local))
        throw std::runtime_error("Runtime pose requires an existing entity and a finite affine matrix.");
    poseRollback_.clear();poseRollback_.reserve(poses.size()+cameraFollowOrder_.size());
    for(const auto& pose:poses)poseRollback_.push_back({pose.id,registry_.get<Transform>(entities_.at(pose.id)).local});
    for(const auto id:cameraFollowOrder_)poseRollback_.push_back({id,registry_.get<Transform>(entities_.at(id)).local});
    bool changed=false;
    try {
        for(const auto& pose:poses) {
            auto& local=registry_.get<Transform>(entities_.at(pose.id)).local;
            if(local!=pose.local){local=pose.local;changed=true;}
        }
        changed=resolveCameraFollow() || changed;
    }catch(...) {
        for(const auto& previous:poseRollback_)registry_.get<Transform>(entities_.at(previous.id)).local=previous.local;
        throw;
    }
    if(changed)++poseRevision_;
}

bool Scene::resolveCameraFollow() {
    bool changed=false;
    for(const auto id:cameraFollowOrder_) {
        const auto entity=entities_.at(id);
        const auto& camera=registry_.get<SceneCamera>(entity);
        const auto target=worldTransform(camera.followTarget);
        const auto parent=registry_.get<Relationship>(entity).parent;
        const auto parentWorld=parent?worldTransform(parent):std::optional<Mat4>{Mat4{}};
        const auto inverse=parentWorld?math::inverse(*parentWorld):std::nullopt;
        if(!target || !inverse)throw std::runtime_error("Cannot resolve camera follow through an invalid transform.");
        const auto position=math::transformPoint(*inverse,math::transformPoint(*target,{0,0,0})+camera.followOffset);
        auto& local=registry_.get<Transform>(entity).local;
        const auto next=math::withAffinePosition(local,position);
        if(!next)throw std::runtime_error("Camera follow produces a nonfinite transform.");
        if(*next!=local){local=*next;changed=true;}
    }
    return changed;
}

} // namespace crimson
