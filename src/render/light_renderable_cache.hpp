////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <filament/Engine.h>
#include <filament/Scene.h>
#include <utils/Entity.h>

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {

/**
 * Caches Light Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class LightRenderableCache {
public:
    LightRenderableCache() = default;
    LightRenderableCache(const LightRenderableCache&) = delete;
    LightRenderableCache& operator=(const LightRenderableCache&) = delete;
    LightRenderableCache(LightRenderableCache&&) = delete;
    LightRenderableCache& operator=(LightRenderableCache&&) = delete;
    ~LightRenderableCache();

    void initialize(filament::Engine& engine, filament::Scene& scene);
    void shutdown();

    bool initialized() const {
        return engine_ != nullptr && scene_ != nullptr;
    }

    bool contains(EntityId entityId) const;
    bool needsRebuild(EntityId entityId, const LightComponent& light) const;

    void syncLight(EntityId entityId, const LightComponent& light, const QMat4& worldTransform);
    void remove(EntityId entityId);
    void removeExcept(const std::unordered_set<EntityId>& liveLightEntities);

    [[nodiscard]] static std::uint64_t lightSignature(const LightComponent& light);
    [[nodiscard]] static std::uint64_t lightTransformSignature(const QMat4& worldTransform);

private:
    /**
     * Represents a Light Proxy value used by the render extraction and Filament resource layer.
     */
    struct LightProxy {
        LightProxy() = default;
        LightProxy(const LightProxy&) = delete;
        LightProxy& operator=(const LightProxy&) = delete;
        LightProxy(LightProxy&& other) noexcept
            : entity(other.entity)
            , sourceEntity(std::exchange(other.sourceEntity, InvalidEntityId))
            , rebuildSignature(std::exchange(other.rebuildSignature, 0))
            , transformSignature(std::exchange(other.transformSignature, 0)) {
            other.entity.clear();
        }
        LightProxy& operator=(LightProxy&&) noexcept = delete;

        utils::Entity entity;
        EntityId sourceEntity = InvalidEntityId;
        std::uint64_t rebuildSignature = 0;
        std::uint64_t transformSignature = 0;
    };

    [[nodiscard]] LightProxy createLightProxy(
            EntityId entityId,
            const LightComponent& light,
            const QMat4& worldTransform,
            std::uint64_t rebuildSignature,
            std::uint64_t transformSignature);
    void updateLightTransform(const LightProxy& proxy, const QMat4& worldTransform);
    void destroyLightProxy(LightProxy& proxy);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    std::unordered_map<EntityId, LightProxy> lightProxies_;
};

} // namespace quader
