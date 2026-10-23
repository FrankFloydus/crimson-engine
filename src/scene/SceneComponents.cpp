#include "SceneComponents.h"
#include "assets/GameplayEffectAsset.h"
#include <algorithm>
#include <exception>
#include <unordered_map>

namespace crimson {
void initializePlayerEffects(EntityRecord& entity) {
    if(!entity.effects)entity.effects.emplace();
    const AssetRef<GameplayEffectAsset> starter{PlayerStartingEffectId};
    if(std::find(entity.effects->starting.begin(),entity.effects->starting.end(),starter)==entity.effects->starting.end())
        entity.effects->starting.insert(entity.effects->starting.begin(),starter);
}
void initializeEnemyEffects(EntityRecord& entity) {
    if(!entity.effects)entity.effects.emplace();
    const AssetRef<GameplayEffectAsset> starter{EnemyStartingEffectId};
    if(std::find(entity.effects->starting.begin(),entity.effects->starting.end(),starter)==entity.effects->starting.end())
        entity.effects->starting.insert(entity.effects->starting.begin(),starter);
}
const char* sceneComponentName(SceneComponentKind kind) {
    switch(kind) {
    case SceneComponentKind::Transform:return "Transform";
    case SceneComponentKind::Actor:return "Visual Root";
    case SceneComponentKind::Movement:return "Movement";
    case SceneComponentKind::Controller:return "Character Controller";
    case SceneComponentKind::Navigation:return "Navigation Agent";
    case SceneComponentKind::Enemy:return "Enemy";
    case SceneComponentKind::Attributes:return "Attributes";
    case SceneComponentKind::Tags:return "Tags";
    case SceneComponentKind::Effects:return "Effects";
    case SceneComponentKind::NavigationVolume:return "Navigation Volume";
    case SceneComponentKind::PlayerInput:return "Player Input";
    case SceneComponentKind::Camera:return "Camera";
    case SceneComponentKind::CameraFollow:return "Camera Follow";
    case SceneComponentKind::Model:return "Model";
    case SceneComponentKind::Collider:return "Collision";
    case SceneComponentKind::Directional:return "Directional light";
    case SceneComponentKind::Point:return "Point light";
    case SceneComponentKind::Environment:return "Environment";
    case SceneComponentKind::Particle:return "Particle effect";
    case SceneComponentKind::Quader:return "Quader";
    case SceneComponentKind::Decal:return "Decal";
    }
    return "Component";
}
bool editableSceneComponent(SceneComponentKind kind) {
    return std::find(EditableActorComponents.begin(),EditableActorComponents.end(),kind)!=EditableActorComponents.end();
}
bool addSceneComponent(SceneDocument& document,EntityId id,SceneComponentKind kind) {
    const auto record=document.scene().find(id);
    if(!record || !editableSceneComponent(kind) || hasSceneComponent(*record,kind))return false;
    if(kind==SceneComponentKind::CameraFollow) {
        const auto world=document.scene().worldTransform(id);if(!world)return false;
        auto basis=*world;basis.at(0,3)=basis.at(1,3)=basis.at(2,3)=0;
        const auto current=document.scene().records();
        std::vector<EntityId> targets{document.scene().properties().playerCharacter};
        for(const auto& candidate:current)if(candidate.id!=targets.front())targets.push_back(candidate.id);
        std::optional<SceneCameraFollow> selected;
        for(const auto target:targets) {
            if(!target || target==id)continue;
            const auto targetWorld=document.scene().worldTransform(target);if(!targetWorld)continue;
            const auto offset=math::transformPoint(*world,{})-math::transformPoint(*targetWorld,{});
            auto candidate=current;auto properties=document.scene().properties();
            for(auto& entity:candidate)if(entity.id==id) {
                if(!entity.camera)entity.camera.emplace();
                entity.cameraFollow=SceneCameraFollow{target,offset,basis};
                if(!properties.activeCamera)properties.activeCamera=id;
            }
            try{Scene::validate(candidate,properties);selected=SceneCameraFollow{target,offset,basis};break;}
            catch(const std::exception&){continue;}
        }
        if(!selected)return false;
        return document.edit("Add Camera Follow",[=](auto& records,auto& properties) {
            for(auto& entity:records)if(entity.id==id) {
                if(!entity.camera)entity.camera.emplace();
                entity.cameraFollow=selected;
                if(!properties.activeCamera)properties.activeCamera=id;
            }
        });
    }
    return document.edit(std::string("Add ")+sceneComponentName(kind),[=](auto& records,auto& properties) {
        for(auto& entity:records)if(entity.id==id) {
            const auto body=[&](std::uint32_t layer=CharacterCollisionLayer) {if(!entity.controller){entity.controller.emplace();entity.controller->collision.layers=layer;}};
            const auto movement=[&](std::uint32_t layer=CharacterCollisionLayer) {body(layer);if(!entity.movement)entity.movement.emplace();};
            switch(kind) {
            case SceneComponentKind::Actor:entity.actor.emplace();break;
            case SceneComponentKind::Tags:entity.tags.emplace();break;
            case SceneComponentKind::Effects:entity.effects.emplace();break;
            case SceneComponentKind::Controller:body();break;
            case SceneComponentKind::Movement:movement();break;
            case SceneComponentKind::Navigation:movement();entity.navigation.emplace();break;
            case SceneComponentKind::Enemy:
                if(!entity.actor)entity.actor.emplace();movement(EnemyCollisionLayer);
                if(!entity.navigation)entity.navigation.emplace();entity.enemy.emplace();
                if(entity.playerInput||properties.playerCharacter==entity.id)initializePlayerEffects(entity);
                else initializeEnemyEffects(entity);break;
            case SceneComponentKind::PlayerInput:movement(PlayerCollisionLayer);entity.playerInput.emplace();initializePlayerEffects(entity);break;
            case SceneComponentKind::Camera:entity.camera.emplace();if(!properties.activeCamera)properties.activeCamera=id;break;
            default:break;
            }
        }
    });
}
bool removeSceneComponent(SceneDocument& document,EntityId id,SceneComponentKind kind) {
    const std::array targets{SceneComponentTarget{id,kind}};
    return removeSceneComponents(document,targets);
}
bool removeSceneComponents(SceneDocument& document,std::span<const SceneComponentTarget> requested) {
    if(requested.empty())return false;
    std::unordered_map<EntityId,std::vector<SceneComponentKind>> targets;
    for(const auto& target:requested) {
        const auto record=document.scene().find(target.entity);
        if(!record||!editableSceneComponent(target.component)||!hasSceneComponent(*record,target.component))return false;
        auto& kinds=targets[target.entity];if(std::find(kinds.begin(),kinds.end(),target.component)==kinds.end())kinds.push_back(target.component);
    }
    const auto label=requested.size()==1?std::string("Remove ")+sceneComponentName(requested.front().component):std::string("Remove Components");
    return document.edit(label,[&](auto& records,auto& properties) {
        for(auto& entity:records)if(const auto found=targets.find(entity.id);found!=targets.end()) {
            for(const auto kind:found->second)switch(kind) {
            case SceneComponentKind::Actor:entity.actor.reset();entity.enemy.reset();break;
            case SceneComponentKind::Tags:entity.tags.reset();break;
            case SceneComponentKind::Effects:entity.effects.reset();break;
            case SceneComponentKind::Controller:entity.controller.reset();[[fallthrough]];
            case SceneComponentKind::Movement:entity.movement.reset();entity.navigation.reset();entity.playerInput.reset();entity.enemy.reset();break;
            case SceneComponentKind::Navigation:entity.navigation.reset();entity.enemy.reset();break;
            case SceneComponentKind::Enemy:entity.enemy.reset();break;
            case SceneComponentKind::PlayerInput:entity.playerInput.reset();break;
            case SceneComponentKind::Camera:entity.camera.reset();entity.cameraFollow.reset();if(properties.activeCamera==entity.id)properties.activeCamera=NoEntity;break;
            case SceneComponentKind::CameraFollow:entity.cameraFollow.reset();break;
            default:break;
            }
            if(properties.playerCharacter==entity.id && (!entity.controller || !entity.movement || !entity.playerInput))properties.playerCharacter=NoEntity;
        }
    });
}
}
