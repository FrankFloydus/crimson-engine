////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/light_renderable_cache.hpp"

#include "math/glm_bridge.hpp"
#include "render/filament_utils.hpp"
#include "render/render_signature_builder.hpp"

#include <filament/LightManager.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <utility>

namespace quader {
namespace {

constexpr float kGodotPunctualLightLumensPerIntensity = 1000.0f;
constexpr float kViewportPunctualLightExposureCompensation = 10000.0f;
constexpr float kGodotDirectionalLightLuxPerIntensity = 100000.0f;

filament::LightManager::Type toFilamentLightType(LightType type) {
    switch (type) {
    case LightType::Directional:
        return filament::LightManager::Type::DIRECTIONAL;
    case LightType::Point:
        return filament::LightManager::Type::POINT;
    case LightType::Spot:
        return filament::LightManager::Type::SPOT;
    }
    return filament::LightManager::Type::DIRECTIONAL;
}

filament::math::float3 directionFromMatrix(const QMat4& transform) {
    QVec3 direction = transformVector(transform, {0.0f, -1.0f, 0.0f});
    if (length(direction) <= 0.000001f) {
        direction = {0.0f, -1.0f, 0.0f};
    }
    return toFilament(normalize(direction));
}

filament::math::float3 positionFromMatrix(const QMat4& transform) {
    return toFilament(transformPoint(transform, {}));
}

float lightFalloffRadius(const LightComponent& light) {
    return std::max(light.range, 0.001f);
}

} // namespace

LightRenderableCache::~LightRenderableCache() {
    shutdown();
}

void LightRenderableCache::initialize(filament::Engine& engine, filament::Scene& scene) {
    engine_ = &engine;
    scene_ = &scene;
}

void LightRenderableCache::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto& [id, proxy] : lightProxies_) {
        destroyLightProxy(proxy);
    }
    lightProxies_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

bool LightRenderableCache::contains(EntityId entityId) const {
    return lightProxies_.contains(entityId);
}

bool LightRenderableCache::needsRebuild(EntityId entityId, const LightComponent& light) const {
    const auto existing = lightProxies_.find(entityId);
    return existing == lightProxies_.end() ||
            existing->second.rebuildSignature != lightSignature(light);
}

void LightRenderableCache::syncLight(EntityId entityId, const LightComponent& light, const QMat4& worldTransform) {
    if (!engine_ || !scene_) {
        return;
    }

    const std::uint64_t nextRebuildSignature = lightSignature(light);
    const std::uint64_t nextTransformSignature = lightTransformSignature(worldTransform);
    if (needsRebuild(entityId, light)) {
        remove(entityId);
        LightProxy proxy = createLightProxy(
                entityId,
                light,
                worldTransform,
                nextRebuildSignature,
                nextTransformSignature);
        lightProxies_.emplace(entityId, std::move(proxy));
        return;
    }

    auto existing = lightProxies_.find(entityId);
    if (existing != lightProxies_.end() && existing->second.transformSignature != nextTransformSignature) {
        updateLightTransform(existing->second, worldTransform);
        existing->second.transformSignature = nextTransformSignature;
    }
}

void LightRenderableCache::remove(EntityId entityId) {
    const auto existing = lightProxies_.find(entityId);
    if (existing == lightProxies_.end()) {
        return;
    }

    destroyLightProxy(existing->second);
    lightProxies_.erase(existing);
}

void LightRenderableCache::removeExcept(const std::unordered_set<EntityId>& liveLightEntities) {
    for (auto it = lightProxies_.begin(); it != lightProxies_.end();) {
        if (!liveLightEntities.contains(it->first)) {
            destroyLightProxy(it->second);
            it = lightProxies_.erase(it);
        } else {
            ++it;
        }
    }
}

std::uint64_t LightRenderableCache::lightSignature(const LightComponent& light) {
    return RenderSignatureBuilder::lightSignature(light);
}

std::uint64_t LightRenderableCache::lightTransformSignature(const QMat4& worldTransform) {
    return RenderSignatureBuilder::lightTransformSignature(worldTransform);
}

float LightRenderableCache::filamentIntensity(const LightComponent& light) {
    const float energy = std::max(light.intensity, 0.0f);
    switch (light.type) {
    case LightType::Directional:
        return energy * kGodotDirectionalLightLuxPerIntensity;
    case LightType::Point:
    case LightType::Spot:
        return energy *
                kGodotPunctualLightLumensPerIntensity *
                kViewportPunctualLightExposureCompensation;
    }
    return energy *
            kGodotPunctualLightLumensPerIntensity *
            kViewportPunctualLightExposureCompensation;
}

LightRenderableCache::LightProxy LightRenderableCache::createLightProxy(
        EntityId entityId,
        const LightComponent& light,
        const QMat4& worldTransform,
        std::uint64_t rebuildSignature,
        std::uint64_t transformSignature) {
    /**
     * Rolls back a partially-created light proxy until ownership is recorded in the cache.
     */
    struct LightProxyCreationGuard {
        filament::Engine& engine;
        filament::Scene& scene;
        LightProxy& proxy;
        bool released = false;

        ~LightProxyCreationGuard() {
            if (released || !proxy.entity) {
                return;
            }
            scene.remove(proxy.entity);
            engine.getLightManager().destroy(proxy.entity);
            utils::EntityManager::get().destroy(proxy.entity);
            proxy.entity.clear();
        }

        void release() {
            released = true;
        }
    };

    LightProxy proxy;
    proxy.entity = utils::EntityManager::get().create();
    proxy.sourceEntity = entityId;
    proxy.rebuildSignature = rebuildSignature;
    proxy.transformSignature = transformSignature;
    LightProxyCreationGuard creationGuard{*engine_, *scene_, proxy};

    filament::LightManager::Builder builder(toFilamentLightType(light.type));
    builder.color({light.color.x, light.color.y, light.color.z})
            .intensity(filamentIntensity(light))
            .position(positionFromMatrix(worldTransform))
            .direction(directionFromMatrix(worldTransform))
            .falloff(lightFalloffRadius(light))
            .castShadows(light.castsShadows);
    builder.build(*engine_, proxy.entity);
    scene_->addEntity(proxy.entity);
    creationGuard.release();
    return proxy;
}

void LightRenderableCache::updateLightTransform(const LightProxy& proxy, const QMat4& worldTransform) {
    if (!engine_ || !proxy.entity) {
        return;
    }

    auto& lightManager = engine_->getLightManager();
    const filament::LightManager::Instance instance = lightManager.getInstance(proxy.entity);
    if (!instance) {
        return;
    }
    lightManager.setPosition(instance, positionFromMatrix(worldTransform));
    lightManager.setDirection(instance, directionFromMatrix(worldTransform));
}

void LightRenderableCache::destroyLightProxy(LightProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        scene_->remove(proxy.entity);
        engine_->getLightManager().destroy(proxy.entity);
        utils::EntityManager::get().destroy(proxy.entity);
        proxy.entity.clear();
    }
}

} // namespace quader
