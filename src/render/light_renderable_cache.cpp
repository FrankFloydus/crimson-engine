////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/light_renderable_cache.hpp"

#include "render/filament_utils.hpp"
#include "render/light_render_policy.hpp"
#include "render/render_signature_builder.hpp"

#include <filament/LightManager.h>
#include <utils/EntityManager.h>

#include <utility>

namespace quader {
namespace {

constexpr float kGodotPunctualLightLumensPerIntensity = 1000.0f;
constexpr float kViewportPunctualLightExposureCompensation = 10000.0f;
constexpr float kGodotDirectionalLightLuxPerIntensity = 100000.0f;
static_assert(kGodotPunctualLightLumensPerIntensity ==
              kFilamentPunctualLightLumensPerIntensity);
static_assert(kViewportPunctualLightExposureCompensation ==
              kFilamentPunctualLightExposureCompensation);
static_assert(kGodotDirectionalLightLuxPerIntensity ==
              kFilamentDirectionalLightLuxPerIntensity);
filament::LightManager::Type to_filament_light_type(LightType type) {
  return filament_light_type(type);
}

filament::math::float3 position_from_matrix(const QMat4 &transform) {
  return filament_light_position(transform);
}

filament::math::float3 direction_from_matrix(const QMat4 &transform) {
  return filament_light_direction(transform);
}

float light_falloff_radius(const LightComponent &light) {
  return filament_light_falloff(light);
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
    preview_suppressed_entities_.clear();
    engine_ = nullptr;
    scene_ = nullptr;
}

bool LightRenderableCache::contains(EntityId entity_id) const {
  return light_proxies_.contains(entity_id);
}

bool LightRenderableCache::needs_rebuild(
    const RenderSceneLightRecord &light) const {
  const auto existing = light_proxies_.find(light.entity_id);
  return existing == light_proxies_.end() ||
         existing->second.rebuild_signature != light.rebuild_revision;
}

void LightRenderableCache::sync_light(const RenderSceneLightRecord &light) {
  if (!engine_ || !scene_) {
    return;
  }
  if (preview_suppressed_entities_.contains(light.entity_id)) {
    remove(light.entity_id);
    return;
  }

  if (needs_rebuild(light)) {
    const auto existing = light_proxies_.find(light.entity_id);
    if (existing != light_proxies_.end() &&
        try_update_point_light_range(existing->second, light.light)) {
      existing->second.rebuild_signature = light.rebuild_revision;
    } else {
      remove(light.entity_id);
      LightProxy proxy =
          create_light_proxy(light.entity_id, light.light, light.world_transform,
                             light.rebuild_revision, light.transform_revision);
      light_proxies_.emplace(light.entity_id, std::move(proxy));
      return;
    }
  }

  auto existing = light_proxies_.find(light.entity_id);
  if (existing != light_proxies_.end() &&
      existing->second.transform_signature != light.transform_revision) {
    update_light_transform(existing->second, light.world_transform);
    existing->second.transform_signature = light.transform_revision;
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

void LightRenderableCache::set_preview_suppressed_entities(
    std::unordered_set<EntityId> entities,
    std::span<const RenderSceneLightRecord> retained_lights) {
  if (preview_suppressed_entities_ == entities) {
    return;
  }
  preview_suppressed_entities_ = std::move(entities);
  for (auto it = light_proxies_.begin(); it != light_proxies_.end();) {
    if (preview_suppressed_entities_.contains(it->first)) {
      destroy_light_proxy(it->second);
      it = light_proxies_.erase(it);
    } else {
      ++it;
    }
  }
  for (const RenderSceneLightRecord &light : retained_lights) {
    if (!preview_suppressed_entities_.contains(light.entity_id) &&
        !contains(light.entity_id)) {
      sync_light(light);
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
  return filament_light_intensity(light);
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
  proxy.authored_light = light;
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

bool LightRenderableCache::try_update_point_light_range(
    LightProxy &proxy, const LightComponent &light) {
  if (light.type != LightType::Point) {
    return false;
  }
  LightComponent previous_with_new_range = proxy.authored_light;
  previous_with_new_range.range = light.range;
  if (!(previous_with_new_range == light)) {
    return false;
  }
  auto &light_manager = engine_->getLightManager();
  const filament::LightManager::Instance instance =
      light_manager.getInstance(proxy.entity);
  if (!instance) {
    return false;
  }
  // Keep the light and its shadow resources stable throughout radius dragging.
  light_manager.setFalloff(instance, light_falloff_radius(light));
  proxy.authored_light = light;
  return true;
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
