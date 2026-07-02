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

filament::LightManager::Type to_filament_light_type(LightType type) {
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

filament::math::float3 direction_from_matrix(const QMat4 &transform) {
  QVec3 direction = transform_vector(transform, {0.0f, -1.0f, 0.0f});
  if (length(direction) <= 0.000001f) {
    direction = {0.0f, -1.0f, 0.0f};
  }
  return to_filament(normalize(direction));
}

filament::math::float3 position_from_matrix(const QMat4 &transform) {
  return to_filament(transform_point(transform, {}));
}

float light_falloff_radius(const LightComponent &light) {
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

    for (auto &[id, proxy] : light_proxies_) {
      destroy_light_proxy(proxy);
    }
    light_proxies_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

bool LightRenderableCache::contains(EntityId entity_id) const {
  return light_proxies_.contains(entity_id);
}

bool LightRenderableCache::needs_rebuild(EntityId entity_id,
                                         const LightComponent &light) const {
  const auto existing = light_proxies_.find(entity_id);
  return existing == light_proxies_.end() ||
         existing->second.rebuild_signature != light_signature(light);
}

void LightRenderableCache::sync_light(EntityId entity_id,
                                      const LightComponent &light,
                                      const QMat4 &world_transform) {
  if (!engine_ || !scene_) {
    return;
  }

  const std::uint64_t next_rebuild_signature = light_signature(light);
  const std::uint64_t next_transform_signature =
      light_transform_signature(world_transform);
  if (needs_rebuild(entity_id, light)) {
    remove(entity_id);
    LightProxy proxy =
        create_light_proxy(entity_id, light, world_transform,
                           next_rebuild_signature, next_transform_signature);
    light_proxies_.emplace(entity_id, std::move(proxy));
    return;
  }

  auto existing = light_proxies_.find(entity_id);
  if (existing != light_proxies_.end() &&
      existing->second.transform_signature != next_transform_signature) {
    update_light_transform(existing->second, world_transform);
    existing->second.transform_signature = next_transform_signature;
  }
}

void LightRenderableCache::remove(EntityId entity_id) {
  const auto existing = light_proxies_.find(entity_id);
  if (existing == light_proxies_.end()) {
    return;
  }

  destroy_light_proxy(existing->second);
  light_proxies_.erase(existing);
}

void LightRenderableCache::remove_except(
    const std::unordered_set<EntityId> &live_light_entities) {
  for (auto it = light_proxies_.begin(); it != light_proxies_.end();) {
    if (!live_light_entities.contains(it->first)) {
      destroy_light_proxy(it->second);
      it = light_proxies_.erase(it);
    } else {
      ++it;
    }
  }
}

std::uint64_t
LightRenderableCache::light_signature(const LightComponent &light) {
  return RenderSignatureBuilder::light_signature(light);
}

std::uint64_t
LightRenderableCache::light_transform_signature(const QMat4 &world_transform) {
  return RenderSignatureBuilder::light_transform_signature(world_transform);
}

float LightRenderableCache::filament_intensity(const LightComponent &light) {
  const float energy = std::max(light.intensity, 0.0f);
  switch (light.type) {
  case LightType::Directional:
    return energy * kGodotDirectionalLightLuxPerIntensity;
  case LightType::Point:
  case LightType::Spot:
    return energy * kGodotPunctualLightLumensPerIntensity *
           kViewportPunctualLightExposureCompensation;
  }
  return energy * kGodotPunctualLightLumensPerIntensity *
         kViewportPunctualLightExposureCompensation;
}

LightRenderableCache::LightProxy LightRenderableCache::create_light_proxy(
    EntityId entity_id, const LightComponent &light,
    const QMat4 &world_transform, std::uint64_t rebuild_signature,
    std::uint64_t transform_signature) {
  /**
   * Rolls back a partially-created light proxy until ownership is recorded in
   * the cache.
   */
  struct LightProxyCreationGuard {
    filament::Engine &engine;
    filament::Scene &scene;
    LightProxy &proxy;
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

    void release() { released = true; }
  };

  LightProxy proxy;
  proxy.entity = utils::EntityManager::get().create();
  proxy.source_entity = entity_id;
  proxy.rebuild_signature = rebuild_signature;
  proxy.transform_signature = transform_signature;
  LightProxyCreationGuard creation_guard{*engine_, *scene_, proxy};

  filament::LightManager::Builder builder(to_filament_light_type(light.type));
  builder.color({light.color.x, light.color.y, light.color.z})
      .intensity(filament_intensity(light))
      .position(position_from_matrix(world_transform))
      .direction(direction_from_matrix(world_transform))
      .falloff(light_falloff_radius(light))
      .castShadows(light.casts_shadows);
  builder.build(*engine_, proxy.entity);
  scene_->addEntity(proxy.entity);
  creation_guard.release();
  return proxy;
}

void LightRenderableCache::update_light_transform(
    const LightProxy &proxy, const QMat4 &world_transform) {
  if (!engine_ || !proxy.entity) {
    return;
  }

  auto &light_manager = engine_->getLightManager();
  const filament::LightManager::Instance instance =
      light_manager.getInstance(proxy.entity);
  if (!instance) {
    return;
  }
  light_manager.setPosition(instance, position_from_matrix(world_transform));
  light_manager.setDirection(instance, direction_from_matrix(world_transform));
}

void LightRenderableCache::destroy_light_proxy(LightProxy &proxy) {
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
