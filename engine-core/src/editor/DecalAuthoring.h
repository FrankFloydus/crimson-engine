#pragma once
#include "decals/DecalTransform.h"
#include "scene/Scene.h"
#include <optional>

namespace crimson {
// Use the live resolved surface when available; the saved seed supplies an
// editing basis while its original geometry is still being prepared.
inline std::optional<Mat4> decalEditFrame(const Scene& scene,EntityId id,std::optional<Mat4> resolved={}) {
    const auto entity=scene.find(id);const auto world=scene.worldTransform(id);
    if(!entity||!entity->decal||entity->decal->projection==SceneDecalProjection::Box||!world)return {};
    Vec3 normal{};
    if(resolved)normal=math::transformDirection(*resolved,{0,0,1});
    else if(const auto receiver=scene.worldTransform(entity->decal->seed.receiver)) {
        if(const auto basis=math::normalMatrix(*receiver))normal=math::transformDirection(*basis,entity->decal->seed.localNormal);
    }
    if(!math::finite(normal)||math::length(normal)<1e-6f)
        normal=math::cross(math::transformDirection(*world,{1,0,0}),math::transformDirection(*world,{0,1,0}));
    return decals::decalAuthoringFrame(*world,normal);
}
}
