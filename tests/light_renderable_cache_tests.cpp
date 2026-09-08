////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/light_renderable_cache.hpp"
#include "render/tool_preview_renderable_cache.hpp"

#include <gtest/gtest.h>

#include <filament/LightManager.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

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
    preview_cache_.initialize(*engine_, *scene_);
  }

  void TearDown() override {
    preview_cache_.shutdown();
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

  static RenderToolPreviewLightRecord preview_light(
      const RenderSceneLightRecord &light, std::size_t count = 1u) {
    RenderToolPreviewLightRecord preview;
    preview.source_entity = light.entity_id;
    preview.light = light.light;
    preview.instance_transforms.assign(count, light.world_transform);
    return preview;
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
  ToolPreviewLightCache preview_cache_;
};

TEST_F(LightRenderableCacheRuntimeTests,
       ExtremePhysicalIntensitiesStayFiniteForEveryShape) {
  for (const LightType type : {LightType::Point, LightType::Spot,
                               LightType::Directional}) {
    RenderSceneLightRecord light = point_light();
    light.light.type = type;
    light.light.intensity = std::numeric_limits<float>::max();
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light.light),
                    std::numeric_limits<float>::max());
    refresh_revisions(light);
    cache_.sync_light(light);
    auto &manager = engine_->getLightManager();
    const auto instance = manager.getInstance(only_light_entity());
    EXPECT_TRUE(std::isfinite(manager.getIntensity(instance)));
    EXPECT_GT(manager.getIntensity(instance), 0.0f);
    EXPECT_FLOAT_EQ(light.light.intensity, std::numeric_limits<float>::max());

    for (const float invalid_intensity :
         {-std::numeric_limits<float>::max(),
          -std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::quiet_NaN()}) {
      light.light.intensity = invalid_intensity;
      EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light.light),
                      0.0f);
    }
    light.light.intensity = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light.light),
                    std::numeric_limits<float>::max());
    light.light.intensity = 2.0f;
    // I=2 is one eighth of reference energy; point and spot accept flux,
    // while directional accepts illuminance at the fixed preview exposure.
    EXPECT_FLOAT_EQ(LightRenderableCache::filament_intensity(light.light),
                    type == LightType::Directional ? 15079.645f :
                    type == LightType::Point ? 189496.41f : 47374.102f);
  }
}

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
  manager.setColor(instance, {0.8f, 0.7f, 0.6f});
  manager.setIntensity(instance, 1234.0f);
  const float sentinel_intensity = manager.getIntensity(instance);
  manager.setDistanceAttenuation(instance, 0.5f);
  manager.setPosition(instance, {9.0f, 8.0f, 7.0f});
  for (std::size_t frame = 0u; frame < 8u; ++frame) {
    cache_.sync_light(light);
  }
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 99.0f);
  EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.8f);
  EXPECT_FLOAT_EQ(manager.getIntensity(instance), sentinel_intensity);
  EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.5f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 9.0f);

  light.light.range = 7.0f;
  refresh_revisions(light);
  cache_.sync_light(light);
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 7.0f);
  EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.8f);
  EXPECT_FLOAT_EQ(manager.getIntensity(instance), sentinel_intensity);
  EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.5f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 9.0f);
}

TEST_F(LightRenderableCacheRuntimeTests,
       ColorIntensityRangeAndFalloffRetainResourcesForEveryShape) {
  auto &manager = engine_->getLightManager();
  for (const LightType type : {LightType::Point, LightType::Spot,
                               LightType::Directional}) {
    RenderSceneLightRecord light = point_light();
    light.light.type = type;
    refresh_revisions(light);
    cache_.sync_light(light);
    const utils::Entity entity = only_light_entity();
    const auto instance = manager.getInstance(entity);
    const float original_intensity = manager.getIntensity(instance);

    light.light.color = {0.9f, 0.8f, 0.7f};
    light.light.intensity *= 2.0f;
    light.light.range = 4.0f;
    light.light.distance_attenuation = 0.5f;
    refresh_revisions(light);
    cache_.sync_light(light);

    EXPECT_EQ(only_light_entity(), entity);
    EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.9f);
    EXPECT_FLOAT_EQ(manager.getColor(instance).y, 0.8f);
    EXPECT_FLOAT_EQ(manager.getColor(instance).z, 0.7f);
    EXPECT_FLOAT_EQ(manager.getIntensity(instance), original_intensity * 2.0f);
    if (type != LightType::Directional) {
      EXPECT_FLOAT_EQ(manager.getFalloff(instance), 4.0f);
    }
    EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.5f);
    EXPECT_TRUE(manager.isShadowCaster(instance));
    EXPECT_FALSE(cache_.needs_rebuild(light));
  }
}

TEST_F(LightRenderableCacheRuntimeTests,
       ShadowAndShapeChangesKeepRebuildBehavior) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  utils::Entity entity = only_light_entity();
  auto &manager = engine_->getLightManager();

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
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getFalloff(manager.getInstance(only_light_entity())),
                  8.0f);
}

TEST_F(LightRenderableCacheRuntimeTests,
       PreviewEmissionUpdatesAreAtomicAndMatchPersistentLights) {
  auto &manager = engine_->getLightManager();
  for (const LightType type : {LightType::Point, LightType::Spot,
                               LightType::Directional}) {
    RenderSceneLightRecord light = point_light();
    light.light.type = type;
    refresh_revisions(light);
    cache_.sync_light(light);
    const utils::Entity persistent = manager.getEntities()[0];
    RenderToolPreviewLightRecord preview = preview_light(light);
    ASSERT_EQ(preview_cache_.prepare({preview}),
              ToolPreviewSubmissionResult::Published);
    preview_cache_.publish_prepared();
    ASSERT_EQ(manager.getComponentCount(), 2u);
    const utils::Entity transient = manager.getEntities()[1];
    const auto transient_instance = manager.getInstance(transient);
    const float original_intensity = manager.getIntensity(transient_instance);
    const auto original_color = manager.getColor(transient_instance);
    manager.setPosition(transient_instance, {9.0f, 8.0f, 7.0f});
    const auto creations = preview_cache_.created_proxy_count();
    const auto rebuilds = preview_cache_.rebuild_count();

    preview.light.color = {0.9f, 0.1f, 0.3f};
    preview.light.intensity *= 3.0f;
    preview.light.range = 18.0f;
    preview.light.distance_attenuation = 1.5f;
    ASSERT_EQ(preview_cache_.prepare({preview}),
              ToolPreviewSubmissionResult::Published);
    EXPECT_EQ(manager.getColor(transient_instance), original_color);
    EXPECT_FLOAT_EQ(manager.getIntensity(transient_instance),
                    original_intensity);
    preview_cache_.discard_prepared();
    EXPECT_EQ(manager.getColor(transient_instance), original_color);
    EXPECT_FLOAT_EQ(manager.getIntensity(transient_instance),
                    original_intensity);

    ASSERT_EQ(preview_cache_.prepare({preview}),
              ToolPreviewSubmissionResult::Published);
    preview_cache_.publish_prepared();
    light.light = preview.light;
    refresh_revisions(light);
    cache_.sync_light(light);
    ASSERT_TRUE(manager.getInstance(persistent));
    ASSERT_TRUE(manager.getInstance(transient));
    const auto persistent_instance = manager.getInstance(persistent);
    EXPECT_EQ(manager.getColor(transient_instance),
              manager.getColor(persistent_instance));
    EXPECT_FLOAT_EQ(manager.getIntensity(transient_instance),
                    manager.getIntensity(persistent_instance));
    EXPECT_FLOAT_EQ(manager.getFalloff(transient_instance),
                    manager.getFalloff(persistent_instance));
    EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(transient_instance),
                    manager.getDistanceAttenuation(persistent_instance));
    EXPECT_FLOAT_EQ(manager.getPosition(transient_instance).x, 9.0f);
    EXPECT_EQ(preview_cache_.created_proxy_count(), creations);
    EXPECT_EQ(preview_cache_.rebuild_count(), rebuilds);
    preview_cache_.clear();
    cache_.remove(light.entity_id);
  }
}

TEST_F(LightRenderableCacheRuntimeTests,
       PreviewShadowAndShapeChangesReplaceResourcesOnlyAtPublication) {
  RenderToolPreviewLightRecord preview = preview_light(point_light());
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  auto &manager = engine_->getLightManager();
  utils::Entity entity = only_light_entity();

  preview.light.casts_shadows = false;
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  EXPECT_TRUE(manager.getInstance(entity));
  EXPECT_TRUE(manager.isShadowCaster(manager.getInstance(entity)));
  EXPECT_EQ(manager.getComponentCount(), 2u);
  preview_cache_.publish_prepared();
  EXPECT_FALSE(manager.getInstance(entity));
  entity = only_light_entity();
  EXPECT_FALSE(manager.isShadowCaster(manager.getInstance(entity)));

  preview.light.type = LightType::Spot;
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  EXPECT_EQ(manager.getType(manager.getInstance(entity)),
            filament::LightManager::Type::POINT);
  preview_cache_.publish_prepared();
  EXPECT_FALSE(manager.getInstance(entity));
  EXPECT_EQ(manager.getType(manager.getInstance(only_light_entity())),
            filament::LightManager::Type::SPOT);
  EXPECT_EQ(preview_cache_.rebuild_count(), 3u);
}

TEST_F(LightRenderableCacheRuntimeTests,
       PreviewUnchangedSubmissionDoesNotRepeatBackendSetters) {
  const RenderToolPreviewLightRecord preview = preview_light(point_light());
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  const utils::Entity entity = only_light_entity();
  auto &manager = engine_->getLightManager();
  const auto instance = manager.getInstance(entity);
  manager.setColor(instance, {0.2f, 0.3f, 0.4f});
  manager.setIntensity(instance, 1234.0f);
  const float sentinel_intensity = manager.getIntensity(instance);
  manager.setFalloff(instance, 99.0f);
  manager.setDistanceAttenuation(instance, 0.5f);
  manager.setPosition(instance, {9.0f, 8.0f, 7.0f});

  for (std::size_t frame = 0u; frame < 8u; ++frame) {
    EXPECT_EQ(preview_cache_.prepare({preview}),
              ToolPreviewSubmissionResult::Unchanged);
    preview_cache_.publish_prepared();
  }
  EXPECT_EQ(only_light_entity(), entity);
  EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.2f);
  EXPECT_FLOAT_EQ(manager.getIntensity(instance), sentinel_intensity);
  EXPECT_FLOAT_EQ(manager.getFalloff(instance), 99.0f);
  EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.5f);
  EXPECT_FLOAT_EQ(manager.getPosition(instance).x, 9.0f);
  EXPECT_EQ(preview_cache_.rebuild_count(), 1u);
  EXPECT_EQ(preview_cache_.transform_update_count(), 0u);
}

TEST_F(LightRenderableCacheRuntimeTests,
       AuthoringMetadataDoesNotChangeBackendPropertiesOrResources) {
  RenderSceneLightRecord light = point_light();
  cache_.sync_light(light);
  auto &manager = engine_->getLightManager();
  const utils::Entity persistent = only_light_entity();
  RenderToolPreviewLightRecord preview = preview_light(light);
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  ASSERT_EQ(manager.getComponentCount(), 2u);
  const utils::Entity transient = manager.getEntities()[1];
  for (const utils::Entity entity : {persistent, transient}) {
    const auto instance = manager.getInstance(entity);
    manager.setColor(instance, {0.8f, 0.7f, 0.6f});
    manager.setIntensity(instance, 0.0f);
    manager.setFalloff(instance, 99.0f);
    manager.setDistanceAttenuation(instance, 0.5f);
  }

  const auto render_signature = light.rebuild_revision;
  light.light.light_unit = LightUnit::Lumens;
  light.light.color_mode = LightColorMode::ColorTemperature;
  light.light.color_temperature_kelvin = 1900.0f;
  light.light.source_preset = LightSourcePreset::Candle;
  EXPECT_EQ(LightRenderableCache::light_signature(light.light),
            render_signature);
  // A broad publication token may advance even without an emission change.
  ++light.rebuild_revision;
  cache_.sync_light(light);
  preview.light = light.light;
  EXPECT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Unchanged);
  preview_cache_.publish_prepared();

  EXPECT_EQ(manager.getComponentCount(), 2u);
  for (const utils::Entity entity : {persistent, transient}) {
    const auto instance = manager.getInstance(entity);
    ASSERT_TRUE(instance);
    EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.8f);
    EXPECT_FLOAT_EQ(manager.getIntensity(instance), 0.0f);
    EXPECT_FLOAT_EQ(manager.getFalloff(instance), 99.0f);
    EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.5f);
  }
  EXPECT_EQ(preview_cache_.created_proxy_count(), 1u);
  EXPECT_EQ(preview_cache_.rebuild_count(), 1u);
  EXPECT_EQ(preview_cache_.transform_update_count(), 0u);
}

TEST_F(LightRenderableCacheRuntimeTests,
       PreviewPoolReactivationKeepsEditedEmissionAndRetainedEntities) {
  RenderToolPreviewLightRecord preview = preview_light(point_light(), 2u);
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  auto &manager = engine_->getLightManager();
  ASSERT_EQ(manager.getComponentCount(), 2u);
  const utils::Entity first = manager.getEntities()[0];
  const utils::Entity second = manager.getEntities()[1];
  const float original_intensity =
      manager.getIntensity(manager.getInstance(first));

  preview.instance_transforms.resize(1u);
  preview.light.intensity *= 3.0f;
  preview.light.color = {0.6f, 0.7f, 0.8f};
  preview.light.range = 12.0f;
  preview.light.distance_attenuation = 0.25f;
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  EXPECT_FLOAT_EQ(manager.getIntensity(manager.getInstance(first)),
                  original_intensity * 3.0f);
  EXPECT_FLOAT_EQ(manager.getIntensity(manager.getInstance(second)), 0.0f);

  preview.instance_transforms.resize(2u);
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  preview_cache_.publish_prepared();
  EXPECT_EQ(manager.getComponentCount(), 2u);
  EXPECT_EQ(preview_cache_.created_proxy_count(), 2u);
  EXPECT_EQ(preview_cache_.rebuild_count(), 1u);
  for (const utils::Entity entity : {first, second}) {
    const auto instance = manager.getInstance(entity);
    ASSERT_TRUE(instance);
    EXPECT_FLOAT_EQ(manager.getIntensity(instance), original_intensity * 3.0f);
    EXPECT_FLOAT_EQ(manager.getColor(instance).x, 0.6f);
    EXPECT_FLOAT_EQ(manager.getFalloff(instance), 12.0f);
    EXPECT_FLOAT_EQ(manager.getDistanceAttenuation(instance), 0.25f);
  }

  preview.instance_transforms.resize(3u);
  ASSERT_EQ(preview_cache_.prepare({preview}),
            ToolPreviewSubmissionResult::Published);
  ASSERT_EQ(manager.getComponentCount(), 3u);
  const utils::Entity third = manager.getEntities()[2];
  EXPECT_FLOAT_EQ(manager.getIntensity(manager.getInstance(third)), 0.0f);
  preview_cache_.publish_prepared();
  EXPECT_FLOAT_EQ(manager.getIntensity(manager.getInstance(third)),
                  original_intensity * 3.0f);
  EXPECT_EQ(preview_cache_.created_proxy_count(), 3u);
  EXPECT_EQ(preview_cache_.rebuild_count(), 1u);
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
