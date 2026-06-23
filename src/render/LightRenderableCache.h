////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
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

/**
 * Caches Light Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class LightRenderableCache {
public:
    ~LightRenderableCache();

    void initialize(filament::Engine& engine, filament::Scene& scene);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr;
    }

    void syncLight(EntityId entityId, const LightComponent& light, const QMat4& worldTransform);
    void remove(EntityId entityId);
    void removeExcept(const std::unordered_set<EntityId>& liveLightEntities);

private:
    /**
     * Represents a Light Proxy value used by the render extraction and Filament resource layer.
     */
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
