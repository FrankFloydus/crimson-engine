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

    bool contains(EntityId entity_id) const;
    bool needs_rebuild(EntityId entity_id, const LightComponent &light) const;

    void sync_light(EntityId entity_id, const LightComponent &light,
                    const QMat4 &world_transform);
    void remove(EntityId entity_id);
    void remove_except(const std::unordered_set<EntityId> &live_light_entities);

    [[nodiscard]] static std::uint64_t
    light_signature(const LightComponent &light);
    [[nodiscard]] static std::uint64_t
    light_transform_signature(const QMat4 &world_transform);
    [[nodiscard]] static float filament_intensity(const LightComponent &light);

  private:
    /**
     * Represents a Light Proxy value used by the render extraction and Filament resource layer.
     */
    struct LightProxy {
        LightProxy() = default;
        LightProxy(const LightProxy&) = delete;
        LightProxy& operator=(const LightProxy&) = delete;
        LightProxy(LightProxy &&other) noexcept
            : entity(other.entity), source_entity(std::exchange(
                                        other.source_entity, kInvalidEntityId)),
              rebuild_signature(std::exchange(other.rebuild_signature, 0)),
              transform_signature(std::exchange(other.transform_signature, 0)) {
          other.entity.clear();
        }
        LightProxy& operator=(LightProxy&&) noexcept = delete;

        utils::Entity entity;
        EntityId source_entity = kInvalidEntityId;
        std::uint64_t rebuild_signature = 0;
        std::uint64_t transform_signature = 0;
    };

    [[nodiscard]] LightProxy
    create_light_proxy(EntityId entity_id, const LightComponent &light,
                       const QMat4 &world_transform,
                       std::uint64_t rebuild_signature,
                       std::uint64_t transform_signature);
    void update_light_transform(const LightProxy &proxy,
                                const QMat4 &world_transform);
    void destroy_light_proxy(LightProxy &proxy);

    filament::Engine* engine_ = nullptr;
    filament::Scene* scene_ = nullptr;
    std::unordered_map<EntityId, LightProxy> light_proxies_;
};

} // namespace quader
