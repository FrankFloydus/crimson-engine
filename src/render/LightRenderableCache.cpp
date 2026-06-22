////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/LightRenderableCache.h"

#include "render/FilamentUtils.h"

#include <filament/LightManager.h>
#include <utils/EntityManager.h>

namespace quader {
namespace {

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

filament::math::float3 directionFromMatrix(const Mat4& transform) {
    return toFilament(normalize(transformVector(transform, {0.0f, -1.0f, 1.0f})));
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

void LightRenderableCache::syncLight(EntityId entityId, const LightComponent& light, const Mat4& worldTransform) {
    if (!engine_ || !scene_) {
        return;
    }

    remove(entityId);

    LightProxy proxy;
    proxy.entity = utils::EntityManager::get().create();
    proxy.sourceEntity = entityId;

    filament::LightManager::Builder(toFilamentLightType(light.type))
            .color({light.color.x, light.color.y, light.color.z})
            .intensity(light.intensity)
            .direction(directionFromMatrix(worldTransform))
            .castShadows(light.castsShadows)
            .build(*engine_, proxy.entity);
    scene_->addEntity(proxy.entity);
    lightProxies_.emplace(entityId, proxy);
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
