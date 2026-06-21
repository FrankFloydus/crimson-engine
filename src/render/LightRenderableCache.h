#pragma once

#include "core/Types.h"
#include "math/Types.h"
#include "scene/Components.h"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <utils/Entity.h>

#include <unordered_map>
#include <unordered_set>

namespace quader {

class LightRenderableCache {
public:
    ~LightRenderableCache();

    void initialize(filament::Engine& engine, filament::Scene& scene);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr;
    }

    void syncLight(EntityId entityId, const LightComponent& light, const Mat4& worldTransform);
    void remove(EntityId entityId);
    void removeExcept(const std::unordered_set<EntityId>& liveLightEntities);

private:
    struct LightProxy {
        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
    };

    void destroyLightProxy(LightProxy& proxy);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    std::unordered_map<EntityId, LightProxy> lightProxies_;
};

} // namespace quader
