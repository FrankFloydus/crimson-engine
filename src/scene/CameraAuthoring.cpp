#include "CameraAuthoring.h"

#include <algorithm>
#include <stdexcept>

namespace crimson {
EntityId resolveOwnedActorCamera(const Scene& scene,EntityId actor) {
    const auto owner=scene.find(actor);
    if(!owner || !owner->actor)return NoEntity;
    EntityId first{},following{};
    for(const auto child:scene.children(actor)) {
        const auto record=scene.find(child);
        if(!record || !record->camera)continue;
        if(!first)first=child;
        if(record->cameraFollow && record->cameraFollow->target==actor) {
            if(child==scene.properties().activeCamera)return child;
            if(!following)following=child;
        }
    }
    return following?following:first;
}

bool updateCameraAuthoring(SceneDocument& document,EntityId camera,const CameraTemplate& settings,
                           const std::string& label) {
    const auto mutation=[&document,camera,settings](auto& records,auto&) {
        if(!validCameraTemplate(settings))throw std::runtime_error("Camera settings are invalid.");
        const auto record=std::find_if(records.begin(),records.end(),[camera](const auto& candidate){return candidate.id==camera;});
        if(record==records.end() || !record->camera)throw std::runtime_error("Choose an existing Camera.");
        Vec3 origin{};
        if(record->cameraFollow) {
            const auto target=document.scene().worldTransform(record->cameraFollow->target);
            if(!target)throw std::runtime_error("The camera follow target is missing.");
            origin=math::transformPoint(*target,{});
        }
        const auto parent=record->parent?document.scene().worldTransform(record->parent):std::optional{Mat4{}};
        const auto inverseParent=parent?math::inverse(*parent):std::nullopt;
        const auto world=math::withAffinePosition(settings.worldBasis,origin+settings.offset);
        if(!inverseParent || !world)throw std::runtime_error("Camera authoring requires a valid parent transform.");
        const auto local=*inverseParent * *world;
        if(!math::finite(local))throw std::runtime_error("Camera pose is outside the supported range.");
        record->camera=settings.lens;record->local=local;
        if(record->cameraFollow) {
            record->cameraFollow->offset=settings.offset;
            record->cameraFollow->worldBasis=settings.worldBasis;
        }
    };
    return document.transactionActive()?document.update(mutation):document.edit(label,mutation);
}
}
