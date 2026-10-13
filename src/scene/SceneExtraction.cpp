#include "SceneExtraction.h"

#include <algorithm>

namespace crimson {

Vec3 directionalLightDirection(const Mat4& world) {
    return math::normalized(math::transformDirection(world,{0,0,-1}),{0,0,-1});
}

ExtractedScene extractScene(const Scene& scene) {
    ExtractedScene result;
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
            result.models.push_back({id,model->assetId,*world});
        if (const auto* light = registry.try_get<SceneDirectionalLight>(entity)) {
            result.directional.push_back({directionalLightDirection(*world),light->color,light->intensity,light->enabled});
        }
        if (const auto* light = registry.try_get<ScenePointLight>(entity)) {
            const auto position = math::transformPoint(*world,{0,0,0});
            result.points.push_back({position,light->color,light->intensity,light->enabled});
        }
    }
    return result;
}

Aabb selectionBounds(const Scene& scene, std::span<const EntityId> selection,
                     const ModelBoundsQuery& modelBounds) {
    Aabb result;
    const std::vector<EntityId> selected(selection.begin(),selection.end());
    for (auto root : scene.topmost(selected)) {
        for (auto id : scene.subtree(root)) {
            const auto record = scene.find(id);
            const auto world = scene.worldTransform(id);
            if (!record || !world) continue;
            bool hasBounds = false;
            if (record->model && modelBounds) {
                if (const auto local = modelBounds(*record->model)) {
                    const auto bounds = math::transformBounds(*local,*world);
                    math::include(result,bounds);
                    hasBounds = bounds.valid;
                }
            }
            if (!hasBounds || record->directional || record->point) {
                const auto position = math::transformPoint(*world,{0,0,0});
                if (!math::finite(position)) continue;
                math::include(result,position-Vec3{.5f,.5f,.5f});
                math::include(result,position+Vec3{.5f,.5f,.5f});
            }
        }
    }
    return result;
}

} // namespace crimson
