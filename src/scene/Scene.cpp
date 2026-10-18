#include "Scene.h"
#include "BuiltinModels.h"
#include "EnvironmentSerialization.h"
#include "assets/MaterialAsset.h"
#include "resources/AssetId.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <unordered_set>

namespace crimson {
namespace {
std::atomic<uint64_t> nextGeneration{1};
struct Visibility { bool authored{true},effective{true},resolved{}; };
struct OutlinerStyle { std::optional<SceneOutlinerStyle> value; };
void cacheVisibility(entt::registry& registry,const std::unordered_map<EntityId,entt::entity>& entities) {
    std::vector<EntityId> chain;
    for(const auto& [id,entity]:entities) {
        if(registry.get<Visibility>(entity).resolved)continue;
        chain.clear();auto current=id;
        while(current&&!registry.get<Visibility>(entities.at(current)).resolved) {
            chain.push_back(current);current=registry.get<Relationship>(entities.at(current)).parent;
        }
        bool visible=!current||registry.get<Visibility>(entities.at(current)).effective;
        for(auto entry=chain.rbegin();entry!=chain.rend();++entry) {
            auto& visibility=registry.get<Visibility>(entities.at(*entry));
            visible=visible&&visibility.authored;visibility.effective=visible;visibility.resolved=true;
        }
    }
}
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
    if (!validIndirectEnergy(light->indirectEnergy))
        throw std::runtime_error("Light GI energy must be finite and between zero and sixteen.");
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
            if(leaving){if(current.cameraFollow)result.push_back(id);continue;}
            if(!visited.insert(id).second)continue;
            pending.emplace_back(id,true);
            if(current.cameraFollow)pending.emplace_back(current.cameraFollow->target,false);
            pending.emplace_back(current.parent,false);
        }
    }
    return result;
}
}

bool hasSceneComponent(const EntityRecord& record,SceneComponentKind kind) {
    switch(kind) {
    case SceneComponentKind::Transform:return true;
    case SceneComponentKind::Actor:return record.actor.has_value();
    case SceneComponentKind::Movement:return record.movement.has_value();
    case SceneComponentKind::Controller:return record.controller.has_value();
    case SceneComponentKind::Navigation:return record.navigation.has_value();
    case SceneComponentKind::Enemy:return record.enemy.has_value();
    case SceneComponentKind::Attributes:return false; // Retired authored capability.
    case SceneComponentKind::Tags:return record.tags.has_value();
    case SceneComponentKind::Effects:return record.effects.has_value();
    case SceneComponentKind::NavigationVolume:return record.navigationVolume.has_value();
    case SceneComponentKind::PlayerInput:return record.playerInput.has_value();
    case SceneComponentKind::Camera:return record.camera.has_value();
    case SceneComponentKind::CameraFollow:return record.cameraFollow.has_value();
    case SceneComponentKind::Model:return record.model.has_value();
    case SceneComponentKind::Collider:return record.collider.has_value();
    case SceneComponentKind::Directional:return record.directional.has_value();
    case SceneComponentKind::Point:return record.point.has_value();
    case SceneComponentKind::Environment:return record.environment.has_value();
    case SceneComponentKind::Particle:return record.particle.has_value();
    case SceneComponentKind::Quader:return record.quader.has_value();
    }
    return false;
}
std::vector<SceneComponentKind> sceneComponents(const EntityRecord& record) {
    std::vector<SceneComponentKind> result{SceneComponentKind::Transform};
    const auto append=[&](SceneComponentKind kind) {
        if(hasSceneComponent(record,kind) && std::find(result.begin(),result.end(),kind)==result.end())result.push_back(kind);
    };
    for(const auto kind:AllSceneComponentKinds)append(kind);
    return result;
}
void normalizeSceneOutlinerStyle(EntityRecord& record) {
    if(record.outlinerStyle&&!record.outlinerStyle->color)record.outlinerStyle.reset();
}

bool sameEntity(const EntityRecord& a, const EntityRecord& b) {
    return a.id == b.id && a.name == b.name && a.parent == b.parent && a.order == b.order &&
        a.local == b.local && a.model == b.model && a.particle == b.particle && sameLight(a.directional,b.directional) && sameLight(a.point,b.point) &&
        a.environment.has_value()==b.environment.has_value() && (!a.environment || sameEnvironment(*a.environment,*b.environment)) &&
        a.actor==b.actor && a.movement==b.movement && a.controller==b.controller && a.navigation==b.navigation && a.enemy==b.enemy && a.navigationVolume==b.navigationVolume &&
        a.tags==b.tags && a.effects==b.effects &&
        a.playerInput==b.playerInput &&
        a.collider==b.collider && a.camera==b.camera && a.cameraFollow==b.cameraFollow && a.modelMaterials==b.modelMaterials &&
        a.castShadows==b.castShadows && a.receiveShadows==b.receiveShadows && a.lightingScopes==b.lightingScopes &&
        a.quader==b.quader && a.quaderGenerated==b.quaderGenerated &&
        a.modelAnimation==b.modelAnimation && a.visible==b.visible &&
        a.outlinerStyle==b.outlinerStyle;
}
bool sameProperties(const SceneProperties& a, const SceneProperties& b) {
    return a.activeEnvironment == b.activeEnvironment && a.activeCamera==b.activeCamera && a.playerCharacter==b.playerCharacter;
}
bool sameEnvironment(const SceneEnvironment& a, const SceneEnvironment& b) {
    return a.exposure==b.exposure && a.sun==b.sun && a.environment==b.environment;
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
    record.visible=registry_.get<Visibility>(entity).authored;
    record.outlinerStyle=registry_.get<OutlinerStyle>(entity).value;
    if (const auto* model = registry_.try_get<ModelReference>(entity)) {
        record.model=model->assetId;record.modelMaterials=model->materials;
        record.castShadows=model->castShadows;record.receiveShadows=model->receiveShadows;
        record.lightingScopes=model->lightingScopes;
        record.modelAnimation=model->animation;
    }
    if (const auto* particle = registry_.try_get<ParticleReference>(entity)) record.particle = particle->assetId;
    if (const auto* light = registry_.try_get<SceneDirectionalLight>(entity)) record.directional = *light;
    if (const auto* light = registry_.try_get<ScenePointLight>(entity)) record.point = *light;
    if (const auto* environment = registry_.try_get<SceneEnvironment>(entity)) record.environment = *environment;
    if (registry_.all_of<SceneActor>(entity)) record.actor.emplace();
    if (const auto* value = registry_.try_get<MovementComponent>(entity)) record.movement = *value;
    if (const auto* value = registry_.try_get<SceneCharacterController>(entity)) record.controller = *value;
    if (const auto* value = registry_.try_get<SceneNavigationAgent>(entity)) record.navigation = *value;
    if (const auto* value = registry_.try_get<SceneEnemy>(entity)) record.enemy = *value;
    if (const auto* value = registry_.try_get<GameplayTagContainer>(entity)) record.tags = *value;
    if (const auto* value = registry_.try_get<SceneEffects>(entity)) record.effects = *value;
    if (const auto* value = registry_.try_get<SceneNavigationVolume>(entity)) record.navigationVolume = *value;
    if (const auto* value = registry_.try_get<ScenePlayerInput>(entity)) record.playerInput = *value;
    if (const auto* value = registry_.try_get<SceneCameraFollow>(entity)) record.cameraFollow = *value;
    if (const auto* collider = registry_.try_get<SceneCollider>(entity)) record.collider = *collider;
    if (const auto* camera = registry_.try_get<SceneCamera>(entity)) record.camera = *camera;
    if (const auto* link = registry_.try_get<QuaderLink>(entity)) record.quader = *link;
    if (const auto* generated = registry_.try_get<QuaderGenerated>(entity)) record.quaderGenerated = *generated;
    return record;
}
bool Scene::isVisible(EntityId id) const {
    const auto found=entities_.find(id);
    return found!=entities_.end()&&registry_.get<Visibility>(found->second).effective;
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
    size_t directionalCount{}, pointCount{},navigationVolumeCount{};
    for (const auto& record : records) {
        if (!record.id || !parents.emplace(record.id,record.parent).second)
            throw std::runtime_error("Scene has a zero or duplicate entity ID.");
        if (record.name.empty() || record.name.size() > 1024)
            throw std::runtime_error("Entity names must contain 1 to 1024 UTF-8 bytes.");
        if (!math::finite(record.local) || !math::isAffine(record.local))
            throw std::runtime_error("Entity local transform must be a finite affine matrix.");
        if (record.model && !isBuiltinModel(record.model->id) && (isEngineAssetId(record.model->id) || record.model->id.starts_with("builtin:") || !safeReference(record.model->id)))
            throw std::runtime_error("Model asset reference must be a nonempty project-relative path.");
        if (!record.model && (!record.castShadows || !record.receiveShadows))
            throw std::runtime_error("Model shadow settings require a model entity.");
        const auto& animation=record.modelAnimation;
        if(animation.clip<-1 || !std::isfinite(animation.speed) || animation.speed<0 || animation.speed>100 ||
            !std::isfinite(animation.startTime) || animation.startTime<0 || animation.startTime>1e9f)
            throw std::runtime_error("Model animation needs a valid clip, Speed from 0 to 100 and Start Time from 0 to 1000000000 seconds.");
        if(!record.model && animation!=SceneModelAnimation{})
            throw std::runtime_error("Model animation settings require a model entity.");
        if(record.modelMaterials.size()>MaximumModelMaterialSlots)
            throw std::runtime_error("Model material slots exceed the supported limit.");
        if(!record.modelMaterials.empty() && !record.model)
            throw std::runtime_error("Material slot assignment requires a model entity.");
        if(record.model && isBuiltinModel(record.model->id) && record.modelMaterials.size()>1)
            throw std::runtime_error("The built-in Box has only one material slot.");
        for(const auto& material:record.modelMaterials)if(!material.empty()) {
            const auto& id=material.id;
            const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(id.data()),id.size()));
            if (canonicalAssetId(id)!=DefaultMaterialId && (isEngineAssetId(id) || (!isAssetUuid(id) && (!safeReference(id) || path.parent_path()!=std::filesystem::path("assets/materials") || path.extension()!=".crimson-material"))))
                throw std::runtime_error("Model material must reference a managed .crimson-material asset or the protected PBR default.");
        }
        if (record.particle && !record.particle->empty()) {
            const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(record.particle->id.data()),record.particle->id.size()));
            if (isEngineAssetId(record.particle->id) || (!isAssetUuid(record.particle->id) && (!safeReference(record.particle->id) || path.parent_path()!=std::filesystem::path("assets/particles") || path.extension()!=".crimson-particle")))
                throw std::runtime_error("Particle asset reference must be a managed .crimson-particle path under assets/particles.");
        }
        if(record.outlinerStyle) {
            const auto validColor=[](const auto& color) {
                return !color||(math::finite(*color)&&std::all_of(color->begin(),color->end(),[](float channel){return channel>=0&&channel<=1;}));
            };
            if(!validColor(record.outlinerStyle->color))
                throw std::runtime_error("Outliner colors require finite RGBA values from zero to one.");
        }
        validateLight(record.directional); validateLight(record.point);
        if (!validSurfaceLightingScopes(record.lightingScopes) ||
            (record.directional && !validLightRoles(record.directional->roles)) ||
            (record.point && !validLightRoles(record.point->roles)))
            throw std::runtime_error("Lighting scopes require World/Character masks and finite nonnegative reveal strength.");
        if(!record.model && record.lightingScopes!=SurfaceLightingScopes{})
            throw std::runtime_error("Surface lighting scopes require a model entity.");
        if (record.directional && (!std::isfinite(record.directional->shadowDistance) || record.directional->shadowDistance<=0 ||
            !std::isfinite(record.directional->shadowDepthBias) || record.directional->shadowDepthBias<0 ||
            !std::isfinite(record.directional->shadowNormalBias) || record.directional->shadowNormalBias<0 ||
            !std::isfinite(record.directional->shadowAngularDiameter) || record.directional->shadowAngularDiameter<0 || record.directional->shadowAngularDiameter>2))
            throw std::runtime_error("Directional shadow distance must be positive, biases finite and nonnegative, and angular diameter from 0 to 2 degrees.");
        if (record.point && (!std::isfinite(record.point->radius) || record.point->radius<.01f || record.point->radius>100000.f))
            throw std::runtime_error("Point light radius must be finite and between 0.01 and 100000 meters.");
        if (record.point && !validPointLightResponse(record.point->nearFalloffRadius,record.point->specularStrength,
            record.point->rangeFalloffExponent,record.point->edgeSoftness,record.point->sourceRadius,record.point->attenuation))
            throw std::runtime_error("Point light attenuation or specular response settings are invalid.");
        if (record.point && (!std::isfinite(record.point->shadowDepthBias) || record.point->shadowDepthBias<0 ||
            !std::isfinite(record.point->shadowNormalBias) || record.point->shadowNormalBias<0))
            throw std::runtime_error("Point shadow biases must be finite and nonnegative.");
        if(record.tags)for(const auto& tag:record.tags->tags())if(!tag.valid())throw std::runtime_error("Tags require valid qualified names.");
        if(record.effects) {
            if(record.effects->starting.size()>4096)throw std::runtime_error("Starting Effects exceeds the 4096 application limit.");
            for(const auto& effect:record.effects->starting)if(!isAssetUuid(effect.id))
                throw std::runtime_error("Starting Effects must reference managed Gameplay Effect identities.");
        }
        if (record.movement && !record.controller)
            throw std::runtime_error("Movement requires a Character Controller.");
        if (record.navigation && (!record.movement || !record.controller))
            throw std::runtime_error("Navigation Agent requires Movement and Character Controller components.");
        if(record.enemy) {
            const auto& enemy=*record.enemy;
            if(!record.actor||!record.controller||!record.movement||!record.navigation)
                throw std::runtime_error("Enemy requires a Visual Root, Character Controller, Movement and Navigation Agent.");
            if(enemy.behaviour!=EnemyBehaviour::Idle&&enemy.behaviour!=EnemyBehaviour::ChasePlayer&&enemy.behaviour!=EnemyBehaviour::RandomMove)
                throw std::runtime_error("Enemy behaviour is invalid.");
            if(!std::isfinite(enemy.radius)||enemy.radius<0||!std::isfinite(enemy.minWait)||enemy.minWait<0||!std::isfinite(enemy.maxWait)||enemy.maxWait<enemy.minWait)
                throw std::runtime_error("Enemy radius and wait times must be finite and nonnegative, with maximum wait at least minimum wait.");
        }
        if (record.playerInput && (!record.movement || !record.controller))
            throw std::runtime_error("Player Input requires Movement and Character Controller components.");
        if(record.playerInput && (!std::isfinite(record.playerInput->mouseSensitivity) || record.playerInput->mouseSensitivity<.001f || record.playerInput->mouseSensitivity>10.f))
            throw std::runtime_error("Player Input requires finite mouse sensitivity between 0.001 and 10.");
        if(record.movement) {
            const auto& value=*record.movement;
            const auto inRange=[](float v,float low,float high){return std::isfinite(v)&&v>=low&&v<=high;};
            if(!inRange(value.jumpHeight,0,100)||!inRange(value.groundAcceleration,0,1000)||!inRange(value.groundFriction,0,100)||
               !inRange(value.airAcceleration,0,1000)||!inRange(value.airControl,0,1)||!inRange(value.strafeTilt,0,45)||
               !inRange(value.tiltResponse,0,100)||!inRange(value.stairSmoothingTime,0,1))
                throw std::runtime_error("Movement requires finite, bounded jump, acceleration, friction and camera response settings.");
        }
        if (record.controller) {
            if(!isAssetUuid(record.controller->profile.id)||(isEngineAssetId(record.controller->profile.id)&&record.controller->profile.id!=DefaultNavigationAgentProfileId))throw std::runtime_error("Character Controller requires a Navigation Agent Profile UUID.");
            if((record.controller->collision.layers&WorldCollisionLayer)||!(record.controller->collision.mask&WorldCollisionLayer))
                throw std::runtime_error("Character Controller cannot belong to World and must include World in its collision mask.");
        }
        if(record.navigationVolume) {
            if(record.navigationVolume->enabled&&++navigationVolumeCount>1)throw std::runtime_error("A scene supports one enabled Navigation Volume.");
            const auto& volume=*record.navigationVolume;
            if(!math::finite(volume.size)||std::any_of(volume.size.begin(),volume.size.end(),[](float value){return value<=0||value>100000;}))
                throw std::runtime_error("Navigation Volume size must be finite and positive, at most 100000 meters per axis.");
            if(!validNavigationBakeSettings(volume.bake))throw std::runtime_error("Navigation Volume bake settings are invalid.");
            if(volume.profiles.size()>256)throw std::runtime_error("Navigation Volume supports at most 256 profiles.");
            std::unordered_set<std::string> profiles;
            for(const auto& profile:volume.profiles)if(!isAssetUuid(profile.id)||(isEngineAssetId(profile.id)&&profile.id!=DefaultNavigationAgentProfileId)||!profiles.insert(profile.id).second)
                throw std::runtime_error("Navigation Volume profiles require unique profile UUIDs.");
        }
        if (record.collider) {
            const auto& c=*record.collider;
            if(c.mode!=ColliderMode::Solid&&c.mode!=ColliderMode::Trigger)throw std::runtime_error("Collider mode is invalid.");
            if(c.mode==ColliderMode::Solid&&(!(c.collision.layers&WorldCollisionLayer)||c.collision.mask!=AllCollisionLayers))
                throw std::runtime_error("Solid Collider must belong to World and block all collision layers.");
            if(c.mode==ColliderMode::Trigger&&((c.collision.layers&WorldCollisionLayer)||c.shape==ColliderShape::Trimesh))
                throw std::runtime_error("Trigger Collider requires a primitive shape and cannot belong to World.");
            if(c.mesh && !c.mesh->validated())throw std::runtime_error("Native collider triangles must be constructed through the validated mesh factory.");
            if(c.shape>ColliderShape::Trimesh)throw std::runtime_error("Collider shape is invalid.");
            if(!math::finite(c.size)||std::any_of(c.size.begin(),c.size.end(),[](float size){return size<=0;})||
                !std::isfinite(c.radius)||c.radius<=0||!std::isfinite(c.height)||c.height<=0)
                throw std::runtime_error("Collider dimensions must be finite and positive.");
            if(c.shape==ColliderShape::Capsule&&c.height<2*c.radius)
                throw std::runtime_error("Capsule collider total height must be at least its diameter.");
            const auto& source=c.source.id;
            if(!source.empty()&&!isBuiltinModel(source)&&(isEngineAssetId(source)||source.starts_with("builtin:")||!safeReference(source)))
                throw std::runtime_error("Trimesh collision requires a managed model asset reference.");
            if(record.controller&&c.shape==ColliderShape::Trimesh)throw std::runtime_error("Trimesh collision cannot share an entity with a Character Controller.");
        }
        const auto validCollisionColor=[](const Vec4& color) {
            return math::finite(color) && std::all_of(color.begin(),color.end(),[](float value){return value>=0 && value<=1;});
        };
        if ((record.controller && !validCollisionColor(record.controller->collisionColor)) ||
            (record.collider && !validCollisionColor(record.collider->collisionColor)))
            throw std::runtime_error("Collision color must have finite RGBA values between zero and one.");
        if (record.quader && (!std::isfinite(record.quader->importScale) || record.quader->importScale<=0 ||
            record.quader->projectPath.size()>32768 || record.quader->projectId.size()>128 || record.quader->sceneId.size()>128 || record.quader->saveKey.size()>65536))
            throw std::runtime_error("Quader import scale must be positive and finite, with bounded source identities.");
        if (record.quaderGenerated && (!record.quaderGenerated->owner || record.quaderGenerated->sourceKey.empty() || record.quaderGenerated->sourceKey.size()>1024))
            throw std::runtime_error("Generated Quader content requires its owner and source identity.");
        if (record.camera) {
            const auto& c=*record.camera;
            if (!std::isfinite(c.verticalFov) || c.verticalFov<1 || c.verticalFov>179 ||
                !std::isfinite(c.nearPlane) || c.nearPlane<=0 || !std::isfinite(c.farPlane) || c.farPlane<=c.nearPlane)
                throw std::runtime_error("Camera needs a finite vertical FOV from 1 to 179 degrees and positive clipping planes.");
        }
        if (record.cameraFollow && (!record.camera || !record.cameraFollow->target ||
            !math::finite(record.cameraFollow->offset) || !math::finite(record.cameraFollow->worldBasis) ||
            !math::isAffine(record.cameraFollow->worldBasis) || math::transformPoint(record.cameraFollow->worldBasis,{0,0,0})!=Vec3{}))
            throw std::runtime_error("Camera Follow requires a Camera, a target, a finite offset and a translation-free affine world basis.");
        if (record.environment) {
            const auto& environment=*record.environment;
            if (!std::isfinite(environment.exposure) || environment.exposure<=0)
                throw std::runtime_error("Environment exposure must be finite and positive.");
            validateEnvironmentSettings(environment.environment);
        }
        directionalCount += record.directional.has_value(); pointCount += record.point.has_value();
    }
    if (directionalCount > MaximumDirectionalLights || pointCount > MaximumPointLights)
        throw std::runtime_error("Scenes support at most 4 directional lights and 4096 point lights, including enabled Player light rig sources.");
    if (properties.activeEnvironment && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.activeEnvironment && record.environment.has_value();
    })) throw std::runtime_error("The active environment must identify an Environment entity.");
    if (properties.activeCamera && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.activeCamera && record.camera.has_value();
    })) throw std::runtime_error("The active camera must identify a Camera entity.");
    if (properties.playerCharacter && std::none_of(records.begin(),records.end(),[&](const auto& record) {
        return record.id==properties.playerCharacter && record.playerInput && record.movement && record.controller;
    })) throw std::runtime_error("The player must identify an entity with Player Input, Movement and Character Controller components.");
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
    std::unordered_map<EntityId,std::unordered_set<std::string>> generatedKeys;
    for(const auto& record:records)if(record.quaderGenerated) {
        const auto owner=indexed.find(record.quaderGenerated->owner);
        if(owner==indexed.end() || !owner->second->quader || !generatedKeys[owner->first].insert(record.quaderGenerated->sourceKey).second)
            throw std::runtime_error("Generated Quader content requires a live Quader owner and unique source identities.");
    }
    for (const auto& record:records) if(record.environment && record.environment->sun) {
        const auto sun=indexed.find(record.environment->sun);
        if (sun==indexed.end() || !sun->second->directional)
            throw std::runtime_error("Environment sun must identify a directional light entity.");
    }
    for (const auto& record:records) if(record.cameraFollow) {
        if (!parents.contains(record.cameraFollow->target)) throw std::runtime_error("Camera follow target does not exist.");
        Mat4 parentWorld;auto parent=record.parent;
        while(parent){const auto& ancestor=*indexed.at(parent);parentWorld=ancestor.local*parentWorld;parent=ancestor.parent;}
        if(!math::inverse(parentWorld))throw std::runtime_error("A following camera requires an invertible parent transform.");
        follows.emplace(record.id,record.cameraFollow->target);
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
    // Sibling order and row colors are editor presentation data. Publish them
    // in place without invalidating navigation, particle clocks or GPU resources.
    bool presentationOnly=records.size()==entities_.size() && sameProperties(properties,properties_);
    bool orderChanged=false;
    if(presentationOnly)for(const auto& record:records) {
        auto previous=find(record.id);
        if(!previous){presentationOnly=false;break;}
        orderChanged=orderChanged || previous->order!=record.order||previous->outlinerStyle!=record.outlinerStyle;
        previous->order=record.order;
        previous->outlinerStyle=record.outlinerStyle;
        if(!sameEntity(*previous,record)){presentationOnly=false;break;}
    }
    if(presentationOnly && orderChanged) {
        for(const auto& record:records) {
            registry_.get<OutlinerStyle>(entities_.at(record.id)).value=record.outlinerStyle;
            registry_.get<Relationship>(entities_.at(record.id)).order=record.order;
        }
        return;
    }
    auto followOrder=cameraFollowOrder(records);
    entt::registry candidate;
    std::unordered_map<EntityId,entt::entity> entities;
    entities.reserve(records.size());
    for (const auto& record : records) {
        const auto entity = candidate.create();
        candidate.emplace<Identity>(entity,record.id,record.name);
        candidate.emplace<Transform>(entity,record.local);
        candidate.emplace<Relationship>(entity,record.parent,record.order);
        candidate.emplace<Visibility>(entity,record.visible,record.visible,false);
        candidate.emplace<OutlinerStyle>(entity,record.outlinerStyle);
        if (record.model) candidate.emplace<ModelReference>(entity,*record.model,record.modelMaterials,record.castShadows,record.receiveShadows,record.lightingScopes,record.modelAnimation);
        if (record.particle) candidate.emplace<ParticleReference>(entity,*record.particle);
        if (record.directional) candidate.emplace<SceneDirectionalLight>(entity,*record.directional);
        if (record.point) candidate.emplace<ScenePointLight>(entity,*record.point);
        if (record.environment) candidate.emplace<SceneEnvironment>(entity,*record.environment);
        if (record.actor) candidate.emplace<SceneActor>(entity);
        if (record.movement) candidate.emplace<MovementComponent>(entity,*record.movement);
        if (record.controller) candidate.emplace<SceneCharacterController>(entity,*record.controller);
        if (record.navigation) candidate.emplace<SceneNavigationAgent>(entity,*record.navigation);
        if (record.enemy) candidate.emplace<SceneEnemy>(entity,*record.enemy);
        if (record.tags) candidate.emplace<GameplayTagContainer>(entity,*record.tags);
        if (record.effects) candidate.emplace<SceneEffects>(entity,*record.effects);
        if (record.navigationVolume) candidate.emplace<SceneNavigationVolume>(entity,*record.navigationVolume);
        if (record.playerInput) candidate.emplace<ScenePlayerInput>(entity,*record.playerInput);
        if (record.cameraFollow) candidate.emplace<SceneCameraFollow>(entity,*record.cameraFollow);
        if (record.collider) candidate.emplace<SceneCollider>(entity,*record.collider);
        if (record.camera) candidate.emplace<SceneCamera>(entity,*record.camera);
        if (record.quader) candidate.emplace<QuaderLink>(entity,*record.quader);
        if (record.quaderGenerated) candidate.emplace<QuaderGenerated>(entity,*record.quaderGenerated);
        entities.emplace(record.id,entity);
    }
    cacheVisibility(candidate,entities);
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

void Scene::applyRuntimeCameraPose(EntityId camera,const Mat4& world) {
    const auto found=entities_.find(camera);
    if(found==entities_.end()||!registry_.all_of<SceneCamera>(found->second)||
       !math::finite(world)||!math::isAffine(world)||!math::inverse(world))
        throw std::runtime_error("Runtime camera pose requires an existing camera and a finite, invertible affine matrix.");
    const auto parent=registry_.get<Relationship>(found->second).parent;
    const auto parentWorld=parent?worldTransform(parent):std::optional<Mat4>{Mat4{}};
    const auto inverseParent=parentWorld?math::inverse(*parentWorld):std::nullopt;
    const auto next=inverseParent?std::optional<Mat4>{*inverseParent * world}:std::nullopt;
    if(!next||!math::finite(*next)||!math::isAffine(*next))
        throw std::runtime_error("Runtime camera pose has an invalid parent transform.");
    auto& local=registry_.get<Transform>(found->second).local;
    if(local!=*next){local=*next;++poseRevision_;}
}

bool Scene::resolveCameraFollow() {
    bool changed=false;
    for(const auto id:cameraFollowOrder_) {
        const auto entity=entities_.at(id);
        const auto& follow=registry_.get<SceneCameraFollow>(entity);
        const auto target=worldTransform(follow.target);
        const auto parent=registry_.get<Relationship>(entity).parent;
        const auto parentWorld=parent?worldTransform(parent):std::optional<Mat4>{Mat4{}};
        const auto inverse=parentWorld?math::inverse(*parentWorld):std::nullopt;
        if(!target || !inverse)throw std::runtime_error("Cannot resolve camera follow through an invalid transform.");
        const auto world=math::withAffinePosition(follow.worldBasis,math::transformPoint(*target,{0,0,0})+follow.offset);
        auto& local=registry_.get<Transform>(entity).local;
        const auto next=world?std::optional<Mat4>{*inverse * *world}:std::nullopt;
        if(!next || !math::finite(*next) || !math::isAffine(*next))throw std::runtime_error("Camera follow produces a nonfinite transform.");
        if(*next!=local){local=*next;changed=true;}
    }
    return changed;
}

} // namespace crimson
