#include "SceneExtraction.h"

#include <algorithm>

namespace crimson {

std::optional<Camera> resolveGameCamera(const Scene& scene) {
    const auto record=scene.find(scene.properties().activeCamera);
    const auto world=record?scene.worldTransform(record->id):std::nullopt;
    if(!record || !record->camera || !world)return std::nullopt;
    const auto forward=math::transformDirection(*world,{0,0,-1});
    const auto up=math::transformDirection(*world,{0,1,0});
    if(math::length(forward)<1e-8 || math::length(math::cross(forward,up))<1e-8)return std::nullopt;
    Camera result;
    result.position=math::transformPoint(*world,{0,0,0});
    result.target=result.position+math::normalized(forward,{0,0,-1});
    result.up=math::normalized(up,{0,1,0});
    result.fov=record->camera->verticalFov;result.nearPlane=record->camera->nearPlane;result.farPlane=record->camera->farPlane;
    result.fovAxis=CameraFovAxis::Vertical;
    return result;
}

Vec3 directionalLightDirection(const Mat4& world) {
    return math::normalized(math::transformDirection(world,{0,0,-1}),{0,0,-1});
}

ExtractedScene extractScene(const Scene& scene) {
    ExtractedScene result;
    result.environment=resolveEnvironment(scene);
    const auto& registry = scene.registry();
    const auto view = registry.view<const Identity,const Transform>();
    std::vector<entt::entity> ordered;
    ordered.reserve(view.size_hint());
    for (auto entity : view) ordered.push_back(entity);
    std::sort(ordered.begin(),ordered.end(),[&](auto a, auto b) {
        return registry.get<Identity>(a).id < registry.get<Identity>(b).id;
    });
    for (auto entity : ordered) {
        const auto id = registry.get<Identity>(entity).id;
        const auto world = scene.worldTransform(id);
        if (!world) continue;
        if (const auto* model = registry.try_get<ModelReference>(entity))
            result.models.push_back({id,model->assetId.id,*world,model->material.id,model->castShadows,model->receiveShadows});
        if (const auto* particle = registry.try_get<ParticleReference>(entity))
            result.particles.push_back({id,particle->assetId.id,*world});
        if (const auto* light = registry.try_get<SceneDirectionalLight>(entity)) {
            result.directional.push_back({directionalLightDirection(*world),light->color,light->intensity,light->enabled});
        }
        if (const auto* light = registry.try_get<ScenePointLight>(entity)) {
            const auto position = math::transformPoint(*world,{0,0,0});
            result.points.push_back({position,light->color,light->intensity,light->enabled,light->radius,id,
                light->castShadows,light->shadowDepthBias,light->shadowNormalBias});
        }
    }
    return result;
}

Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds,const ParticleBoundsQuery& particleBounds) {
    Aabb result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for (auto root : scene.topmost(selected)) {
        for (auto id : scene.subtree(root)) {
            const auto record = scene.find(id);
            const auto world = scene.worldTransform(id);
            if (!record || !world) continue;
            bool hasBounds = false;
            if (record->model && modelBounds) {
                if (const auto local = modelBounds(record->model->id)) {
                    const auto bounds = math::transformBounds(*local,*world);
                    math::include(result,bounds);
                    hasBounds = bounds.valid;
                }
            }
            if (record->particle && particleBounds) {
                if (const auto bounds=particleBounds(id);bounds && bounds->valid && math::finite(bounds->minimum) && math::finite(bounds->maximum)) {
                    math::include(result,*bounds);hasBounds=true;
                }
            }
            if (!hasBounds || record->directional || record->point) {
                const auto position = math::transformPoint(*world,{0,0,0});
                if (!math::finite(position)) continue;
                math::include(result,position-Vec3{.5f,.5f,.5f});
                math::include(result,position+Vec3{.5f,.5f,.5f});
            }
            if(record->collider)math::include(result,math::transformBounds(
                Aabb{record->collider->size*-.5,record->collider->size*.5,true},*world));
            if(record->character)math::include(result,math::transformBounds(Aabb{
                {-record->character->radius,0,-record->character->radius},
                {record->character->radius,record->character->height,record->character->radius},true},*world));
        }
    }
    return result;
}

} // namespace crimson
