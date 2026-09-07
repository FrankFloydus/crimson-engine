////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/light_renderable_cache.hpp"

#include <gtest/gtest.h>

#include <filament/LightManager.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <iostream>

namespace quader {
namespace {

/// Uses the real light manager while avoiding a window or graphics driver.
class LightRenderableCacheRuntimeTests : public ::testing::Test {
protected:
  void SetUp() override {
    engine_ = filament::Engine::create(filament::Engine::Backend::NOOP);
    ASSERT_NE(engine_, nullptr);
    scene_ = engine_->createScene();
    ASSERT_NE(scene_, nullptr);
    cache_.initialize(*engine_, *scene_);
  }

  void TearDown() override {
    cache_.shutdown();
    if (engine_ != nullptr) {
      if (scene_ != nullptr) {
        engine_->destroy(scene_);
      }
      filament::Engine::destroy(&engine_);
    }
  }

  static RenderSceneLightRecord point_light() {
    RenderSceneLightRecord light;
    light.entity_id = 42u;
    light.light.type = LightType::Point;
    light.light.color = {0.25f, 0.5f, 0.75f};
    light.light.intensity = 2.0f;
    light.light.range = 2.0f;
    light.light.casts_shadows = true;
    light.world_transform.values[12] = 3.0f;
    refresh_revisions(light);
    return light;
  }

  static void refresh_revisions(RenderSceneLightRecord &light) {
    light.rebuild_revision = LightRenderableCache::light_signature(light.light);
    light.transform_revision =
        LightRenderableCache::light_transform_signature(light.world_transform);
  }

  utils::Entity only_light_entity() const {
    const auto &manager = engine_->getLightManager();
    EXPECT_EQ(manager.getComponentCount(), 1u);
    EXPECT_EQ(scene_->getLightCount(), 1u);
    return manager.empty() ? utils::Entity{} : manager.getEntities()[0];
  }

  double measure_radius_updates(bool recreate, std::size_t updates) {
    using Clock = std::chrono::steady_clock;
    RenderSceneLightRecord light = point_light();
    cache_.sync_light(light);
    const auto begin = Clock::now();
    for (std::size_t update = 0u; update < updates; ++update) {
      light.light.range = 3.0f + static_cast<float>(update % 1000u) * 0.01f;
      light.rebuild_revision = LightRenderableCache::light_signature(light.light);
      if (recreate) {
        // Replay the previous cache behavior in this same executable and scene.
        cache_.remove(light.entity_id);
      }
      cache_.sync_light(light);
    }
    return std::chrono::duration<double, std::micro>(Clock::now() - begin)
               .count() /
           static_cast<double>(updates);
  }

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  LightRenderableCache cache_;
};

TEST_F(LightRenderableCacheRuntimeTests,
       RadiusChangesRetainLightEntityAndOtherProperties) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  const utils::Entity entity = only_light_entity();
  auto &manager = engine_->getLightManager();
  const auto instance = manager.getInstance(entity);
  const float original_intensity = manager.getIntensity(instance);
  const auto original_color = manager.getColor(instance);
  const auto original_direction = manager.getDirection(instance);

  for (const float radius : {3.0f, 17.0f, 0.0001f, 2.0f}) {
    light.light.range = radius;
    refresh_revisions(light);
    cache_.sync_light(light);
    EXPECT_EQ(only_light_entity(), entity);
    EXPECT_FLOAT_EQ(manager.getFalloff(instance), std::max(radius, 0.001f));
    EXPECT_EQ(manager.getColor(instance), original_color);
    EXPECT_FLOAT_EQ(manager.getIntensity(instance), original_intensity);
    EXPECT_EQ(manager.getDirection(instance), original_direction);
    EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 3.0f);
    EXPECT_TRUE(manager.isShadowCaster(instance));
    EXPECT_FALSE(cache_.needs_rebuild(light));
  }
}

TEST_F(LightRenderableCacheRuntimeTests,
       RadiusAndTransformCanChangeTogetherWithoutRecreation) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  const utils::Entity entity = only_light_entity();
  light.light.range = 8.0f;
  light.world_transform.values[12] = 12.0f;
  light.world_transform.values[13] = 5.0f;
  refresh_revisions(light);
  cache_.sync_light(light);

  EXPECT_EQ(only_light_entity(), entity);
  const auto &manager = engine_->getLightManager();
  const auto instance = manager.getInstance(entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 8.0f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 12.0f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).y, 5.0f);
  EXPECT_FALSE(cache_.needs_rebuild(light));
}

TEST_F(LightRenderableCacheRuntimeTests,
       UnchangedRevisionDoesNotRepeatBackendSetters) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  light.light.range = 6.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  const utils::Entity entity = only_light_entity();
  auto &manager = engine_->getLightManager();
  const auto instance = manager.getInstance(entity);
  // Sentinel values expose any setter replay when the source is unchanged.
  manager.setFalloff(instance, 99.0f);
  manager.setPosition(instance, {9.0f, 8.0f, 7.0f});
  for (std::size_t frame = 0u; frame < 8u; ++frame) {
    cache_.sync_light(light);
  }
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 99.0f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 9.0f);

  light.light.range = 7.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 7.0f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 9.0f);
}

TEST_F(LightRenderableCacheRuntimeTests,
       OtherPropertiesAndLightTypesKeepRebuildBehavior) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  utils::Entity entity = only_light_entity();
  auto &manager = engine_->getLightManager();

  light.light.color = {0.9f, 0.8f, 0.7f};
  light.light.range = 4.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_NE(only_light_entity(), entity);
  entity = only_light_entity();
  EXPECT_FLOAT_EQ(manager.getColor(manager.getInstance(entity)).x, 0.9f);

  const float old_intensity = manager.getIntensity(manager.getInstance(entity));
  light.light.intensity *= 2.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_NE(only_light_entity(), entity);
  entity = only_light_entity();
  EXPECT_FLOAT_EQ(manager.getIntensity(manager.getInstance(entity)),
                  old_intensity * 2.0f);

  light.light.casts_shadows = false;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_NE(only_light_entity(), entity);
  entity = only_light_entity();
  EXPECT_FALSE(manager.isShadowCaster(manager.getInstance(entity)));

  light.light.type = LightType::Spot;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_NE(only_light_entity(), entity);
  entity = only_light_entity();
  EXPECT_EQ(manager.getType(manager.getInstance(entity)),
            filament::LightManager::Type::SPOT);

  light.light.range = 8.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_NE(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(manager.getInstance(only_light_entity())),
                  8.0f);
}

TEST_F(LightRenderableCacheRuntimeTests, DISABLED_PointRadiusSyncCpuBenchmark) {
  constexpr std::size_t kSamples = 7u;
  constexpr std::size_t kUpdates = 100'000u;
  std::array<double, kSamples> recreate_us{};
  std::array<double, kSamples> retained_us{};
  (void)measure_radius_updates(true, 1000u);
  (void)measure_radius_updates(false, 1000u);
  for (std::size_t sample = 0u; sample < kSamples; ++sample) {
    if ((sample % 2u) == 0u) {
      recreate_us[sample] = measure_radius_updates(true, kUpdates);
      retained_us[sample] = measure_radius_updates(false, kUpdates);
    } else {
      retained_us[sample] = measure_radius_updates(false, kUpdates);
      recreate_us[sample] = measure_radius_updates(true, kUpdates);
    }
  }
  std::sort(recreate_us.begin(), recreate_us.end());
  std::sort(retained_us.begin(), retained_us.end());
  const double before_us = recreate_us[kSamples / 2u];
  const double after_us = retained_us[kSamples / 2u];
  std::cout << "[point-light-sync-cpu] backend=NOOP lights=1 updates=" << kUpdates
            << " samples=" << kSamples << " recreate_median_us=" << before_us
            << " retained_median_us=" << after_us
            << " speedup=" << before_us / after_us << '\n';
  EXPECT_TRUE(cache_.contains(point_light().entity_id));
  EXPECT_EQ(scene_->getLightCount(), 1u);
}

} // namespace
} // namespace quader
