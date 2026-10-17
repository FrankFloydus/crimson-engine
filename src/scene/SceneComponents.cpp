#include "SceneComponents.h"
#include <algorithm>
#include <exception>
#include <unordered_map>

namespace crimson {
const char* sceneComponentName(SceneComponentKind kind) {
    switch(kind) {
    case SceneComponentKind::Transform:return "Transform";
    case SceneComponentKind::Actor:return "Visual Root";
    case SceneComponentKind::Motor:return "Motor";
    case SceneComponentKind::Controller:return "Character Controller";
    case SceneComponentKind::Navigation:return "Navigation Agent";
    case SceneComponentKind::NavigationVolume:return "Navigation Volume";
    case SceneComponentKind::PlayerInput:return "Player Input";
    case SceneComponentKind::LightRig:return "Light Rig";
    case SceneComponentKind::DarknessReveal:return "Darkness Reveal";
    case SceneComponentKind::Camera:return "Camera";
    case SceneComponentKind::CameraFollow:return "Camera Follow";
    case SceneComponentKind::Model:return "Model";
    case SceneComponentKind::Collider:return "Collision";
    case SceneComponentKind::Directional:return "Directional light";
    case SceneComponentKind::Point:return "Point light";
    case SceneComponentKind::Environment:return "Environment";
    case SceneComponentKind::Particle:return "Particle effect";
    case SceneComponentKind::Quader:return "Quader";
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
            const auto body=[&] {if(!entity.controller)entity.controller.emplace();};
            const auto motor=[&] {body();if(!entity.motor)entity.motor.emplace();};
            switch(kind) {
            case SceneComponentKind::Actor:entity.actor.emplace();break;
            case SceneComponentKind::Controller:body();break;
            case SceneComponentKind::Motor:motor();break;
            case SceneComponentKind::Navigation:motor();entity.navigation.emplace();break;
            case SceneComponentKind::PlayerInput:motor();entity.playerInput.emplace();break;
            case SceneComponentKind::LightRig:
                if(!entity.actor)entity.actor.emplace();
                entity.lightRig.emplace();entity.lightRig->localWorld.enabled=false;break;
            case SceneComponentKind::DarknessReveal:entity.darknessReveal.emplace();break;
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
            case SceneComponentKind::Actor:entity.actor.reset();entity.lightRig.reset();break;
            case SceneComponentKind::Controller:entity.controller.reset();[[fallthrough]];
            case SceneComponentKind::Motor:entity.motor.reset();entity.navigation.reset();entity.playerInput.reset();break;
            case SceneComponentKind::Navigation:entity.navigation.reset();break;
            case SceneComponentKind::PlayerInput:entity.playerInput.reset();break;
            case SceneComponentKind::LightRig:entity.lightRig.reset();break;
            case SceneComponentKind::DarknessReveal:entity.darknessReveal.reset();break;
            case SceneComponentKind::Camera:entity.camera.reset();entity.cameraFollow.reset();if(properties.activeCamera==entity.id)properties.activeCamera=NoEntity;break;
            case SceneComponentKind::CameraFollow:entity.cameraFollow.reset();break;
            default:break;
            }
            if(properties.playerCharacter==entity.id && (!entity.controller || !entity.motor || !entity.playerInput))properties.playerCharacter=NoEntity;
        }
    });
}
}
