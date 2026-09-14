////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_material_library.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/png_texture_loader.hpp"
#include "render/texture_binding_service.hpp"
#include "render/viewport_renderer.hpp"

#include <gtest/gtest.h>

#include <QString>

#include <backend/PixelBufferDescriptor.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

namespace quader {
namespace {

/// Reads production viewport pixels through a windowless Vulkan swapchain.
class FogRuntimeHarness final {
public:
  ~FogRuntimeHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(10'000'000'000ull);
    engine_->pumpMessageQueues();
    viewport_.shutdown();
    if (patch_) factory_.destroy(*patch_);
    factory_.shutdown();
    materials_.shutdown();
    textures_.shutdown();
    library_.shutdown();
    registry_.clear();
    if (scene_) scene_->setIndirectLight(nullptr);
    if (indirect_light_) engine_->destroy(indirect_light_);
    if (sun_) {
      if (scene_) scene_->remove(sun_);
      engine_->destroy(sun_);
      utils::EntityManager::get().destroy(sun_);
    }
    for (filament::Texture *texture : {neutral_, environment_}) {
      if (texture) engine_->destroy(texture);
    }
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    if (scene_) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize() {
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(
        kSize, kSize, filament::SwapChain::CONFIG_READABLE);
    // The windowless fixture has no QCoreApplication. Test main records the
    // actual executable directory before any temporary Qt test applications.
    const std::filesystem::path resources(
        qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
    if (!scene_ || !renderer_ || !swapchain_ ||
        !library_.initialize(*engine_, resources)) return false;
    const std::array<std::byte, 4u> gray{
        std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
    neutral_ = load_rgba_texture(*engine_, gray, 1u, 1u);
    if (neutral_ == nullptr) return false;
    textures_.initialize(*engine_, resources, *neutral_);
    materials_.initialize(library_.quader_mesh(), library_.quader_mesh_lit(),
                           textures_);
    materials_.bind_scene({.scene_session_id = 1u});
    factory_.initialize(*engine_, *scene_, materials_);
    viewport_.initialize(*engine_, *scene_, registry_, library_.ground_grid(),
                          viewport_grid_layer_mask(0u),
                          ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(kSize, kSize);
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(4.0f);
    camera_.set_clip_planes(0.05f, 100.0f);
    viewport_.resize(kSize, kSize);
    viewport_.update_camera(camera_);
    viewport_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(),
                                 false);
    viewport_.set_overlay_visible(false);
    viewport_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    viewport_.set_shadowing_enabled(false);
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.environment.clear_color = {};
    settings.post_process.enabled = false;
    settings.post_process.anti_aliasing = false;
    settings.post_process.dithering = false;
    settings.fog.height_falloff = 0.0f;
    return create_patch(false);
  }

  bool create_patch(bool overlay) {
    if (patch_) {
      factory_.destroy(*patch_);
      patch_.reset();
    }
    RenderMaterialBindingSnapshot material;
    material.present = true;
    material.primary_material.present = true;
    material.component.material = MaterialReference::project(
        *ProjectAssetId::parse("2be3dd4a-336b-4c6d-bced-f5e586ec6c35"));
    material.primary_material.reference = material.component.material;
    material.primary_material.document.base_color =
        overlay ? QVec3{0.2f, 0.4f, 0.1f} : QVec3{};
    material.primary_material.document.roughness = 1.0f;
    // Black metal eliminates diffuse and dielectric IBL reflections so the
    // test's pixel measures fog, independently of the environment's lighting.
    material.primary_material.document.metallic = overlay ? 0.0f : 1.0f;
    const RenderMeshAssetSnapshot mesh{
        .mesh_id = 1u, .mesh_revision = ++mesh_revision_, .vertices = vertices_,
        .indices = indices_, .present = true};
    patch_.emplace(factory_.create(
        1u, mesh, &material, {}, false, true,
        overlay ? kViewportOverlayLayerMask : kViewportRenderedSceneLayerMask,
        overlay));
    if (!patch_->entity || patch_->material_instances.empty()) return false;
    for (filament::MaterialInstance *instance : patch_->material_instances) {
      instance->setParameter("surfaceGridEnabled", 0.0f);
    }
    viewport_.set_overlay_visible(overlay);
    return true;
  }

  bool create_environment() {
    environment_ = filament::Texture::Builder()
                       .width(1u).height(1u).levels(1u)
                       .sampler(filament::Texture::Sampler::SAMPLER_CUBEMAP)
                       .format(filament::Texture::InternalFormat::RGBA8)
                       .build(*engine_);
    if (environment_ == nullptr) return false;
    std::array<unsigned char, 24u> pixels{};
    for (std::size_t face = 0u; face < 6u; ++face) {
      pixels[face * 4u] = 16u;
      pixels[face * 4u + 1u] = 32u;
      pixels[face * 4u + 2u] = 255u;
      pixels[face * 4u + 3u] = 255u;
    }
    environment_->setImage(
        *engine_, 0u, 0u, 0u, 0u, 1u, 1u, 6u,
        FilamentBufferDescriptorFactory::rgba_pixels(pixels.data(),
                                                       pixels.size()));
    indirect_light_ = filament::IndirectLight::Builder()
                          .reflections(environment_).intensity(30000.0f)
                          .build(*engine_);
    return indirect_light_ != nullptr;
  }

  void attach_environment(bool attach) {
    scene_->setIndirectLight(attach ? indirect_light_ : nullptr);
  }

  bool create_sun() {
    sun_ = utils::EntityManager::get().create();
    filament::LightManager::Builder(filament::LightManager::Type::SUN)
        .color({1.0f, 0.0f, 0.0f}).intensity(38400.0f)
        .direction({0.6f, 0.0f, 0.8f}).castShadows(false)
        .build(*engine_, sun_);
    scene_->addEntity(sun_);
    return engine_->getLightManager().hasComponent(sun_);
  }

  void apply(bool rendered = true) {
    viewport_.apply_scene_settings(settings, rendered);
  }

  std::uint64_t application_count() const {
    return viewport_.scene_settings_apply_count();
  }

  std::optional<QVec3> capture(bool rendered = true) {
    apply(rendered);
    for (int frame = 0; frame < 3; ++frame) {
      if (!begin_frame()) return std::nullopt;
      viewport_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return std::nullopt;
    viewport_.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        kSize / 2u, kSize / 2u, 1u, 1u,
        filament::backend::PixelBufferDescriptor(
            readback->pixel.data(), readback->pixel.size(),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            }, owner));
    renderer_->endFrame();
    for (int attempt = 0; attempt < 10 &&
         !readback->complete.load(std::memory_order_acquire); ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ull)) return std::nullopt;
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return std::nullopt;
    return QVec3{float(readback->pixel[0u]), float(readback->pixel[1u]),
                  float(readback->pixel[2u])};
  }

  SceneSettingsComponent settings;

private:
  struct Readback {
    std::array<unsigned char, 4u> pixel{};
    std::atomic<bool> complete{false};
  };

  bool begin_frame() {
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) return true;
      if (!engine_->flushAndWait(1'000'000'000ull)) return false;
      engine_->pumpMessageQueues();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  static constexpr std::uint32_t kSize = 64u;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *neutral_ = nullptr;
  filament::Texture *environment_ = nullptr;
  filament::IndirectLight *indirect_light_ = nullptr;
  utils::Entity sun_;
  FilamentMaterialLibrary library_;
  TextureBindingService textures_;
  MaterialBindingService materials_;
  MeshRenderableFactory factory_;
  TextureRegistry registry_;
  ViewportRenderer viewport_;
  ViewportCameraController camera_;
  std::optional<MeshRenderableProxy> patch_;
  std::uint64_t mesh_revision_ = 0u;
  const std::array<MeshVertex, 4u> vertices_{{
      {.position = {-2, -2, 0}, .normal = {0, 0, 1}, .uv = {0, 0},
       .uv_geometric_normal = {0, 0, 1}},
      {.position = {2, -2, 0}, .normal = {0, 0, 1}, .uv = {1, 0},
       .uv_geometric_normal = {0, 0, 1}},
      {.position = {2, 2, 0}, .normal = {0, 0, 1}, .uv = {1, 1},
       .uv_geometric_normal = {0, 0, 1}},
      {.position = {-2, 2, 0}, .normal = {0, 0, 1}, .uv = {0, 1},
       .uv_geometric_normal = {0, 0, 1}},
  }};
  const std::array<std::uint32_t, 6u> indices_{0u, 1u, 2u, 0u, 2u, 3u};
};

TEST(RenderFogRuntimeTests, FogControlsAndRenderedModeAffectProductionPixels) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_FOG_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_FOG_TESTS=1 for windowless Vulkan fog coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const auto clear = harness.capture();
  ASSERT_TRUE(clear);
  EXPECT_LT(clear->x, 3.0f);
  harness.settings.fog.enabled = true;
  harness.settings.fog.density = 0.25f;
  const auto fog = harness.capture();
  ASSERT_TRUE(fog);
  EXPECT_GT(fog->x, clear->x + 20.0f);
  const std::uint64_t applied = harness.application_count();
  harness.apply();
  harness.apply();
  EXPECT_EQ(harness.application_count(), applied);
  const std::array<float SceneFogSettings::*, 9u> scalar_fields{
      &SceneFogSettings::distance, &SceneFogSettings::cut_off_distance,
      &SceneFogSettings::maximum_opacity, &SceneFogSettings::height,
      &SceneFogSettings::height_falloff, &SceneFogSettings::density,
      &SceneFogSettings::in_scattering_start,
      &SceneFogSettings::in_scattering_size,
      // Revisit a previously changed scalar to catch stale retained state.
      &SceneFogSettings::density};
  for (const auto field : scalar_fields) {
    const float saved = harness.settings.fog.*field;
    const std::uint64_t before = harness.application_count();
    harness.settings.fog.*field = saved + 0.05f;
    harness.apply();
    EXPECT_EQ(harness.application_count(), before + 1u);
    harness.apply();
    EXPECT_EQ(harness.application_count(), before + 1u);
    harness.settings.fog.*field = saved;
    harness.apply();
    EXPECT_EQ(harness.application_count(), before + 2u);
  }

  harness.settings.fog.density = 0.5f;
  const auto denser = harness.capture();
  ASSERT_TRUE(denser);
  EXPECT_GT(denser->x, fog->x + 10.0f);
  harness.settings.fog.maximum_opacity = 0.1f;
  const auto capped = harness.capture();
  ASSERT_TRUE(capped);
  EXPECT_LT(capped->x, fog->x - 10.0f);
  harness.settings.fog.maximum_opacity = 1.0f;
  harness.settings.fog.color = {0.0f, 1.0f, 0.0f};
  const auto green = harness.capture();
  ASSERT_TRUE(green);
  EXPECT_GT(green->y, green->x + 20.0f);
  harness.settings.fog.color = {1.0f, 1.0f, 1.0f};

  harness.settings.fog.distance = 100.0f;
  const auto delayed = harness.capture();
  ASSERT_TRUE(delayed);
  EXPECT_NEAR(delayed->x, clear->x, 2.0f);
  harness.settings.fog.distance = 0.0f;
  harness.settings.fog.cut_off_distance = 0.1f;
  const auto beyond_cutoff = harness.capture();
  ASSERT_TRUE(beyond_cutoff);
  EXPECT_NEAR(beyond_cutoff->x, clear->x, 2.0f);
  harness.settings.fog.cut_off_distance = 0.0f;
  harness.settings.fog.height_falloff = 1.0f;
  harness.settings.fog.height = -10.0f;
  const auto above_fog = harness.capture();
  ASSERT_TRUE(above_fog);
  EXPECT_LT(above_fog->x, fog->x - 10.0f);
  harness.settings.fog.height = 2.0f;
  const auto below_fog = harness.capture();
  ASSERT_TRUE(below_fog);
  EXPECT_GT(below_fog->x, above_fog->x + 20.0f);

  const auto editing = harness.capture(false);
  ASSERT_TRUE(editing);
  EXPECT_NEAR(editing->x, clear->x, 2.0f);
  const auto editing_count = harness.application_count();
  harness.settings.fog.height = 3.0f;
  harness.apply(false);
  EXPECT_EQ(harness.application_count(), editing_count);
  const auto restored = harness.capture(true);
  ASSERT_TRUE(restored);
  EXPECT_GT(restored->x, clear->x + 20.0f);
  harness.settings.post_process.enabled = true;
  const auto post_processed = harness.capture();
  ASSERT_TRUE(post_processed);
  EXPECT_GT(post_processed->x, clear->x + 20.0f);
  harness.settings.fog.enabled = false;
  const auto disabled = harness.capture();
  ASSERT_TRUE(disabled);
  EXPECT_NEAR(disabled->x, clear->x, 2.0f);
}

TEST(RenderFogRuntimeTests, EnvironmentSunAndOverlayRemainIndependent) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_FOG_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_FOG_TESTS=1 for windowless Vulkan fog coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  harness.settings.fog.enabled = true;
  harness.settings.fog.density = 1.0f;
  harness.settings.fog.color_from_environment = true;
  harness.settings.environment.enabled = true;
  const auto unavailable = harness.capture();
  ASSERT_TRUE(unavailable);
  EXPECT_GT(unavailable->x, 20.0f);
  EXPECT_NEAR(unavailable->x, unavailable->z, 2.0f);
  ASSERT_TRUE(harness.create_environment());
  harness.attach_environment(true);
  const auto blue = harness.capture();
  ASSERT_TRUE(blue);
  EXPECT_GT(blue->z, blue->x + 20.0f);
  const auto available_count = harness.application_count();
  harness.apply();
  EXPECT_EQ(harness.application_count(), available_count);
  // Even before the environment resource is detached, its disabled setting
  // selects the authored constant fog color.
  harness.settings.environment.enabled = false;
  const auto disabled_flag = harness.capture();
  ASSERT_TRUE(disabled_flag);
  EXPECT_NEAR(disabled_flag->x, disabled_flag->z, 2.0f);
  EXPECT_EQ(harness.application_count(), available_count + 1u);
  harness.settings.environment.enabled = true;
  harness.apply();
  harness.settings.fog.color_from_environment = false;
  const auto constant = harness.capture();
  ASSERT_TRUE(constant);
  EXPECT_NEAR(constant->x, constant->z, 2.0f);
  harness.settings.fog.color_from_environment = true;
  harness.settings.environment.enabled = false;
  harness.attach_environment(false);
  const auto disabled_environment = harness.capture();
  ASSERT_TRUE(disabled_environment);
  EXPECT_NEAR(disabled_environment->x, unavailable->x, 2.0f);

  harness.settings.fog.color = {};
  ASSERT_TRUE(harness.create_sun());
  const auto no_scattering = harness.capture();
  ASSERT_TRUE(no_scattering);
  harness.settings.fog.in_scattering_size = 2.0f;
  const auto broad_sun = harness.capture();
  ASSERT_TRUE(broad_sun);
  EXPECT_GT(broad_sun->x, no_scattering->x + 10.0f);
  EXPECT_GT(broad_sun->x, broad_sun->y + 10.0f);
  harness.settings.fog.in_scattering_size = 16.0f;
  const auto narrow_sun = harness.capture();
  ASSERT_TRUE(narrow_sun);
  EXPECT_LT(narrow_sun->x, broad_sun->x - 10.0f);
  harness.settings.fog.in_scattering_start = 100.0f;
  const auto delayed_sun = harness.capture();
  ASSERT_TRUE(delayed_sun);
  EXPECT_NEAR(delayed_sun->x, no_scattering->x, 2.0f);

  ASSERT_TRUE(harness.create_patch(true));
  harness.settings.fog.enabled = false;
  const auto overlay_clear = harness.capture();
  ASSERT_TRUE(overlay_clear);
  EXPECT_GT(overlay_clear->y, 5.0f);
  harness.settings.fog.enabled = true;
  harness.settings.fog.color = {1.0f, 0.0f, 0.0f};
  harness.settings.fog.in_scattering_start = 0.0f;
  const auto overlay_fog = harness.capture();
  ASSERT_TRUE(overlay_fog);
  EXPECT_NEAR(overlay_fog->x, overlay_clear->x, 2.0f);
  EXPECT_NEAR(overlay_fog->y, overlay_clear->y, 2.0f);
  EXPECT_NEAR(overlay_fog->z, overlay_clear->z, 2.0f);
}

} // namespace
} // namespace quader
