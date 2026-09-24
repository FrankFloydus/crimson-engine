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
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/png_texture_loader.hpp"
#include "render/texture_binding_service.hpp"
#include "render/viewport_renderer.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>

#include <QDir>
#include <QImage>
#include <QString>

#include <backend/PixelBufferDescriptor.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Skybox.h>
#include <filament/SwapChain.h>
#include <utils/EntityManager.h>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace quader {
namespace {

/// Reads production viewport fog and grid pixels through a windowless Vulkan
/// swapchain, including the normal scene and overlay render passes.
class FogRuntimeHarness final {
public:
  ~FogRuntimeHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(10'000'000'000ull);
    engine_->pumpMessageQueues();
    viewport_.shutdown();
    destroy_patch();
    overlay_factory_.shutdown();
    factory_.shutdown();
    materials_.shutdown();
    textures_.shutdown();
    library_.shutdown();
    registry_.clear();
    if (scene_) {
      scene_->setIndirectLight(nullptr);
      scene_->setSkybox(nullptr);
    }
    if (indirect_light_) engine_->destroy(indirect_light_);
    if (skybox_) engine_->destroy(skybox_);
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

  bool initialize(std::uint32_t size = kDefaultSize, bool srgb = false) {
    size_ = size;
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(
        size_, size_, filament::SwapChain::CONFIG_READABLE |
            (srgb ? filament::SwapChain::CONFIG_SRGB_COLORSPACE : 0u));
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
                          srgb ? ViewportRenderSurface::SrgbSwapchain
                               : ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(size_, size_);
    camera_.view_axis(ViewportViewAxis::Z, 1);
    camera_.focus_origin(4.0f);
    camera_.set_clip_planes(0.05f, 100.0f);
    viewport_.resize(size_, size_);
    viewport_.update_camera(camera_);
    viewport_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(),
                                 false);
    viewport_.set_overlay_visible(false);
    viewport_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    viewport_.set_shadowing_enabled(false);
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.environment.clear_color = {};
    presentation_settings.set_color(ViewportColorSettingId::Background,
                                     {0.0f, 0.0f, 0.0f, 1.0f});
    settings.post_process.enabled = false;
    settings.post_process.anti_aliasing = false;
    settings.post_process.dithering = false;
    settings.fog.height_falloff = 0.0f;
    return create_patch(false);
  }

  bool create_patch(bool overlay) {
    destroy_patch();
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

  bool create_grid_sample(ViewportProjection projection, bool rendered,
                           bool bright, float surface_height,
                           bool grazing = false,
                           bool textured = false) {
    destroy_patch();
    camera_.view_axis(ViewportViewAxis::Y, 1);
    camera_.focus_origin(8.0f);
    if (projection == ViewportProjection::Perspective) {
      if (grazing) {
        if (!camera_.restore_perspective_pose(
                {.eye = {8.0f, surface_height + 0.25f, 2.0f},
                 .target = {8.0f, 0.0f, -8.0f},
                 .up = {0.0f, 1.0f, 0.0f}})) return false;
      } else if (!camera_.restore_perspective_pose(
                     {.eye = {0.0f, 8.0f, 0.0f}, .target = {},
                      .up = {0.0f, 0.0f, -1.0f}})) return false;
    }
    viewport_.update_camera(camera_);
    const float extent = grazing ? 128.0f : 8.0f;
    const std::array<MeshVertex, 4u> vertices{{
        {.position = {-extent, surface_height, -extent}, .normal = {0, 1, 0}, .uv = {0, 0},
         .uv_geometric_normal = {0, 1, 0}},
        {.position = {extent, surface_height, -extent}, .normal = {0, 1, 0}, .uv = {1, 0},
         .uv_geometric_normal = {0, 1, 0}},
        {.position = {extent, surface_height, extent}, .normal = {0, 1, 0}, .uv = {1, 1},
         .uv_geometric_normal = {0, 1, 0}},
        {.position = {-extent, surface_height, extent}, .normal = {0, 1, 0}, .uv = {0, 1},
         .uv_geometric_normal = {0, 1, 0}},
    }};
    const std::array<std::uint32_t, 6u> indices{0u, 2u, 1u, 0u, 3u, 2u};
    RenderMaterialBindingSnapshot material;
    material.present = true;
    material.primary_material.present = true;
    material.component.material = MaterialReference::project(
        *ProjectAssetId::parse("2be3dd4a-336b-4c6d-bced-f5e586ec6c35"));
    material.primary_material.reference = material.component.material;
    const float albedo = bright ? 1.0f : 0.0f;
    material.primary_material.document.base_color = {albedo, albedo, albedo};
    material.primary_material.document.roughness = 1.0f;
    material.primary_material.document.metallic = bright ? 0.0f : 1.0f;
    if (textured) {
      constexpr std::uint32_t kTextureWidth = 512u;
      constexpr std::uint32_t kTextureHeight = 64u;
      auto pixels = std::make_shared<std::vector<std::byte>>(
          kTextureWidth * kTextureHeight * 4u);
      for (std::uint32_t y = 0u; y < kTextureHeight; ++y) {
        for (std::uint32_t x = 0u; x < kTextureWidth; ++x) {
          const float u = (static_cast<float>(x) + 0.5f) / kTextureWidth;
          const float forward_ramp = std::clamp((u - 0.29f) / 0.42f, 0.0f, 1.0f);
          // Opposing ramps across V expose a vertically flipped capture:
          // projected samples would otherwise inspect the opposite tone.
          const float ramp = y < kTextureHeight / 2u ? 1.0f - forward_ramp
                                                    : forward_ramp;
          QVec3 color{ramp * ramp, ramp * ramp, ramp * ramp};
          if (ramp >= 0.50f && ramp <= 0.78f) {
            const float grain = (static_cast<float>((x * 13u + y * 17u) % 17u) - 8.0f) / 1020.0f;
            const float warmth = 0.12f + (ramp - 0.50f) * (0.73f / 0.28f);
            // This beige ramp includes the user's roughly (140,125,110)
            // displayed texture as well as its brighter variants. Sampling
            // the off image below identifies those tones in either lighting.
            color = {warmth + grain, warmth * 0.70f + grain,
                     warmth * 0.46f + grain};
          }
          const std::size_t offset = (y * kTextureWidth + x) * 4u;
          (*pixels)[offset] = static_cast<std::byte>(qRound(color.x * 255.0f));
          (*pixels)[offset + 1u] = static_cast<std::byte>(qRound(color.y * 255.0f));
          (*pixels)[offset + 2u] = static_cast<std::byte>(qRound(color.z * 255.0f));
          (*pixels)[offset + 3u] = std::byte{255u};
        }
      }
      RenderTextureAssetSnapshot texture;
      texture.slot = material_texture_slot::kBaseColor;
      texture.project_texture_id = *ProjectAssetId::parse(
          "7b381812-243a-4b8e-a1e7-6552581688ba");
      texture.content_revision = 1u;
      texture.texture.color_space = TextureColorSpace::Linear;
      texture.decoded_rgba8 = std::move(pixels);
      texture.width = kTextureWidth;
      texture.height = kTextureHeight;
      texture.present = true;
      material.primary_material.textures.push_back(std::move(texture));
    }
    const RenderMeshAssetSnapshot mesh{
        .mesh_id = 1u, .mesh_revision = ++mesh_revision_, .vertices = vertices,
        .indices = indices, .present = true};
    const std::uint8_t scene_layer = rendered
        ? kViewportRenderedSceneLayerMask : kViewportShadedSceneLayerMask;
    patch_.emplace(factory_.create(
        1u, mesh, &material, {}, false, true, scene_layer, !rendered));
    // MeshRenderableCache publishes the same companion into the independently
    // rendered overlay depth pass. A scene-only patch cannot reproduce grid
    // occlusion because the two views do not share depth.
    overlay_depth_patch_.emplace(factory_.create(
        1u, mesh, &material, {}, false, true, kViewportOverlayLayerMask,
        true, true, kViewportOverlayDepthChannel));
    if (!patch_->entity || patch_->material_instances.empty() ||
        !overlay_depth_patch_->entity) return false;
    ViewportSettings viewport_settings = ViewportSettings::defaults();
    viewport_settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, false);
    factory_.update_surface_grid(*patch_, 1.0f, viewport_settings);
    factory_.update_surface_grid(*overlay_depth_patch_, 1.0f, viewport_settings);
    viewport_.set_scene_layer_mask(scene_layer);
    viewport_.set_overlay_visible(true);
    if (!sun_) {
      sun_ = utils::EntityManager::get().create();
      filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
          .color({1.0f, 1.0f, 1.0f}).intensity(384000.0f)
          .direction({0.0f, -1.0f, 0.0f}).castShadows(false)
          .build(*engine_, sun_);
      scene_->addEntity(sun_);
    }
    return engine_->getLightManager().hasComponent(sun_);
  }

  bool create_textured_grid_sample(bool rendered, bool oblique,
                                   float surface_height = 0.0f) {
    if (!create_grid_sample(ViewportProjection::Perspective, rendered, true,
                             surface_height, false, true)) return false;
    if (oblique) {
      if (!camera_.restore_perspective_pose(
              {.eye = {0.0f, 5.0f, 7.0f}, .target = {},
               .up = {0.0f, 1.0f, 0.0f}})) return false;
      viewport_.update_camera(camera_);
    }
    auto &lights = engine_->getLightManager();
    lights.setIntensity(lights.getInstance(sun_), 100000.0f);
    return true;
  }

  struct ToolHandlePixelSample {
    QVec2 center;
    float radius = 0.0f;
  };

  std::optional<ToolHandlePixelSample>
  create_tool_handle_grid_sample(ViewportProjection projection) {
    if (!create_grid_sample(projection, false, true, 0.0f)) {
      return std::nullopt;
    }
    if (!overlay_factory_.initialized()) {
      const std::filesystem::path resources(
          qEnvironmentVariable("QT_PLUGIN_PATH").toStdWString());
      overlay_factory_.initialize(
          *engine_, *scene_, library_.vertex_color_overlay(),
          library_.overlay_line(), library_.edit_wire_line(),
          library_.overlay_point(), library_.overlay_icon(), resources);
    }
    const auto camera =
        ViewportCameraAdapter::overlay_camera(camera_, camera_.viewport_size());
    // Place an opaque tool disk above a major ground-grid line. The real
    // ToolHandle batch uses overlay ordering and does not write scene depth.
    const QVec3 center{2.0f, 0.25f, 0.5f};
    constexpr float kRadius = 0.75f;
    constexpr int kSegments = 32;
    constexpr float kTau = 6.2831853071795864769f;
    const auto rim = [&](int index) {
      const float angle = kTau * static_cast<float>(index) / kSegments;
      return center + camera.right * (std::cos(angle) * kRadius) +
             camera.up * (std::sin(angle) * kRadius);
    };
    ViewportOverlayFrame frame;
    for (int index = 0; index < kSegments; ++index) {
      frame.triangles.push_back({
          .layer = ViewportOverlayLayer::ToolHandle,
          .a = center,
          .b = rim(index),
          .c = rim(index + 1),
          .color = rgba8(255, 255, 132, 255),
          .style_role = ViewportOverlayStyleRole::ExplicitColor,
          .source = ViewportOverlaySource::Tool,
      });
    }
    const auto plan = OverlayRenderableBuilder::build_layer(
        frame, camera, ViewportOverlayLayer::ToolHandle);
    tool_handle_proxies_ =
        overlay_factory_.create_layer_proxies(plan, kViewportOverlayLayerMask);
    if (tool_handle_proxies_.empty() ||
        std::any_of(tool_handle_proxies_.begin(), tool_handle_proxies_.end(),
                    [](const auto &proxy) { return proxy.entity_id == 0u; })) {
      return std::nullopt;
    }
    const auto projected_center = camera_.project_world_point(center);
    const auto projected_rim = camera_.project_world_point(rim(0));
    if (!projected_center.visible || !projected_rim.visible) {
      return std::nullopt;
    }
    const QVec2 radius = projected_rim.position - projected_center.position;
    return ToolHandlePixelSample{
        .center = projected_center.position,
        .radius = std::hypot(radius.x, radius.y),
    };
  }

  void set_grid_scene_visible(bool visible) {
    viewport_.set_scene_visible(visible);
  }

  bool create_empty_grid_sample(QVec3 eye, QVec3 target, float far_plane) {
    destroy_patch();
    if (!camera_.restore_perspective_pose(
            {.eye = eye, .target = target, .up = {0.0f, 1.0f, 0.0f}})) return false;
    camera_.set_clip_planes(0.05f, far_plane);
    viewport_.update_camera(camera_);
    viewport_.set_scene_layer_mask(kViewportShadedSceneLayerMask);
    viewport_.set_scene_visible(true);
    viewport_.set_overlay_visible(true);
    return true;
  }

  bool create_empty_orthographic_grid_sample(float zoom_steps) {
    destroy_patch();
    // Pose restoration permits a framing distance beyond the interactive
    // focus-origin cap, then axis conversion preserves that camera distance.
    if (!camera_.restore_perspective_pose(
            {.eye = {0.0f, 2000.0f, 1500.0f}, .target = {},
             .up = {0.0f, 1.0f, 0.0f}})) return false;
    camera_.view_axis(ViewportViewAxis::Y, 1);
    camera_.scroll(zoom_steps);
    camera_.set_view_direction({0.0f, -0.8f, -0.6f});
    camera_.set_clip_planes(0.05f, 10000.0f);
    viewport_.update_camera(camera_);
    viewport_.set_scene_layer_mask(kViewportShadedSceneLayerMask);
    viewport_.set_scene_visible(true);
    viewport_.set_overlay_visible(true);
    return true;
  }

  QImage capture_grid(bool rendered, bool visible, bool axes_visible = true,
                      const ViewportSettings *authored_settings = nullptr) {
    ViewportSettings viewport_settings = authored_settings
        ? *authored_settings : ViewportSettings::defaults();
    // Grid contrast fixtures use an authored black backdrop independently of
    // the editor's default background preference.
    viewport_settings.set_color(ViewportColorSettingId::Background,
                                  {0.0f, 0.0f, 0.0f, 1.0f});
    viewport_settings.set_bool_value(ViewportBoolSettingId::ShowMeshGrid, false);
    // Preserve the user's original ground-grid opacity. In particular, this
    // exercises dense coverage without the rejected fully opaque white sheet.
    if (authored_settings == nullptr) {
      viewport_settings.set_color(ViewportColorSettingId::GridMinor,
                                   {1.0f, 1.0f, 1.0f, 5.0f / 255.0f});
      viewport_settings.set_color(ViewportColorSettingId::GridMajor,
                                   {210.0f / 255.0f, 210.0f / 255.0f,
                                    210.0f / 255.0f, 15.0f / 255.0f});
    }
    if (!axes_visible) {
      for (const auto id : {ViewportColorSettingId::GridXAxis,
                            ViewportColorSettingId::GridYAxis,
                            ViewportColorSettingId::GridZAxis}) {
        auto color = viewport_settings.color(id);
        color.a = 0.0f;
        viewport_settings.set_color(id, color);
      }
    }
    presentation_settings = viewport_settings;
    viewport_.update_ground_grid(camera_, 1.0f, viewport_settings, visible);
    return capture_image(rendered, 0u, 0u, size_, size_);
  }

  QPoint grid_pixel(QVec3 position) const {
    const ViewportScreenPoint point = camera_.project_world_point(position);
    return point.visible ? QPoint(qRound(point.position.x),
                                   qRound(point.position.y)) : QPoint(-1, -1);
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
    viewport_.apply_scene_settings(settings, rendered, presentation_settings);
  }

  std::uint64_t application_count() const {
    return viewport_.scene_settings_apply_count();
  }

  std::optional<QVec3> capture(bool rendered = true) {
    const QImage image = capture_image(rendered, size_ / 2u, size_ / 2u, 1u, 1u);
    if (image.isNull()) return std::nullopt;
    const QRgb pixel = image.pixel(0, 0);
    return QVec3{float(qRed(pixel)), float(qGreen(pixel)), float(qBlue(pixel))};
  }

  bool create_background_skybox() {
    skybox_ = filament::Skybox::Builder()
                  .color({0.02f, 0.05f, 0.40f, 1.0f})
                  .intensity(30000.0f)
                  .showSun(false)
                  .build(*engine_);
    if (!skybox_) return false;
    skybox_->setLayerMask(kViewportAllLayerMask, kViewportRenderedSceneLayerMask);
    scene_->setSkybox(skybox_);
    return true;
  }

  void set_material_debug_mode(ViewportMaterialDebugMode mode) {
    viewport_.set_material_debug_mode(mode);
  }

  QImage capture_background(bool rendered, bool use_rendered_scene_layer = false) {
    destroy_patch();
    viewport_.set_scene_visible(true);
    viewport_.set_overlay_visible(false);
    viewport_.set_scene_layer_mask(rendered || use_rendered_scene_layer
        ? kViewportRenderedSceneLayerMask : kViewportShadedSceneLayerMask);
    return capture_image(rendered, 0u, 0u, size_, size_);
  }

  SceneSettings settings;
  ViewportSettings presentation_settings = ViewportSettings::defaults();

private:
  void destroy_patch() {
    for (auto &proxy : tool_handle_proxies_) {
      overlay_factory_.destroy(proxy);
    }
    tool_handle_proxies_.clear();
    for (auto *proxy : {&patch_, &overlay_depth_patch_}) {
      if (*proxy) factory_.destroy(**proxy);
      proxy->reset();
    }
  }

  QImage capture_image(bool rendered, std::uint32_t x, std::uint32_t y,
                        std::uint32_t width, std::uint32_t height) {
    apply(rendered);
    for (int frame = 0; frame < 3; ++frame) {
      if (!begin_frame()) return {};
      viewport_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    viewport_.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    readback->pixels.resize(width * height * 4u);
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        x, y, width, height,
        filament::backend::PixelBufferDescriptor(
            readback->pixels.data(), readback->pixels.size(),
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
      if (!engine_->flushAndWait(1'000'000'000ull)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    // This windowless Vulkan swapchain returns rows in camera-screen order.
    // Own the pixels without flipping the projected grid sample positions.
    return QImage(readback->pixels.data(), static_cast<int>(width),
                  static_cast<int>(height), QImage::Format_RGBA8888)
        .copy();
  }

  struct Readback {
    std::vector<unsigned char> pixels;
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

  static constexpr std::uint32_t kDefaultSize = 64u;
  std::uint32_t size_ = kDefaultSize;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *neutral_ = nullptr;
  filament::Texture *environment_ = nullptr;
  filament::IndirectLight *indirect_light_ = nullptr;
  filament::Skybox *skybox_ = nullptr;
  utils::Entity sun_;
  FilamentMaterialLibrary library_;
  TextureBindingService textures_;
  MaterialBindingService materials_;
  MeshRenderableFactory factory_;
  OverlayProxyFactory overlay_factory_;
  std::vector<OverlayRenderableProxy> tool_handle_proxies_;
  TextureRegistry registry_;
  ViewportRenderer viewport_;
  ViewportCameraController camera_;
  std::optional<MeshRenderableProxy> patch_;
  std::optional<MeshRenderableProxy> overlay_depth_patch_;
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

TEST(RenderGroundGridRuntimeTests, OpaqueToolHandleDiskStaysAboveTheGroundGrid) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "ground-grid compositing coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(256u, true));
  ViewportSettings grid_settings = ViewportSettings::defaults();
  for (const auto color : {ViewportColorSettingId::GridMinor,
                           ViewportColorSettingId::GridMajor}) {
    grid_settings.set_color(color, {0.05f, 0.05f, 0.05f, 1.0f});
  }
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output_directory));
  }
  for (const ViewportProjection projection :
       {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    const QString name = projection == ViewportProjection::Perspective
        ? "tool_handle_perspective" : "tool_handle_orthographic";
    SCOPED_TRACE(name.toStdString());
    const auto disk = harness.create_tool_handle_grid_sample(projection);
    ASSERT_TRUE(disk.has_value());
    ASSERT_GT(disk->radius, 12.0f);
    const QImage without_grid =
        harness.capture_grid(false, false, false, &grid_settings);
    const QImage with_grid =
        harness.capture_grid(false, true, false, &grid_settings);
    ASSERT_FALSE(without_grid.isNull());
    ASSERT_FALSE(with_grid.isNull());
    ASSERT_EQ(with_grid.size(), without_grid.size());
    if (!output_directory.isEmpty()) {
      ASSERT_TRUE(without_grid.save(QDir(output_directory).filePath(
          name + "_grid_off.png")));
      ASSERT_TRUE(with_grid.save(QDir(output_directory).filePath(
          name + "_grid_on.png")));
    }
    int maximum_interior_difference = 0;
    int maximum_exterior_difference = 0;
    std::size_t interior_pixels = 0u;
    std::size_t yellow_interior_pixels = 0u;
    std::size_t changed_exterior_pixels = 0u;
    for (int y = 0; y < with_grid.height(); ++y) {
      for (int x = 0; x < with_grid.width(); ++x) {
        const QRgb before = without_grid.pixel(x, y);
        const QRgb after = with_grid.pixel(x, y);
        const int difference = std::max({
            std::abs(qRed(after) - qRed(before)),
            std::abs(qGreen(after) - qGreen(before)),
            std::abs(qBlue(after) - qBlue(before)),
            std::abs(qAlpha(after) - qAlpha(before))});
        const float distance = std::hypot(
            static_cast<float>(x) + 0.5f - disk->center.x,
            static_cast<float>(y) + 0.5f - disk->center.y);
        if (distance <= disk->radius - 3.0f) {
          ++interior_pixels;
          yellow_interior_pixels +=
              qRed(before) > 200 && qGreen(before) > 200 &&
                      qBlue(before) + 30 < qRed(before)
                  ? 1u : 0u;
          maximum_interior_difference =
              std::max(maximum_interior_difference, difference);
        } else if (distance >= disk->radius + 3.0f) {
          changed_exterior_pixels += difference > 1 ? 1u : 0u;
          maximum_exterior_difference =
              std::max(maximum_exterior_difference, difference);
        }
      }
    }
    ASSERT_GT(interior_pixels, 300u);
    ASSERT_EQ(yellow_interior_pixels, interior_pixels)
        << "The baseline must contain the actual opaque yellow tool fill";
    EXPECT_LE(maximum_interior_difference, 1)
        << "Ground-grid lines must not tint an opaque ToolHandle interior";
    EXPECT_GT(changed_exterior_pixels, 20u)
        << "Grid toggling must visibly change pixels outside the tool disk";
    EXPECT_GT(maximum_exterior_difference, 4);
    std::cout << "Ground grid GPU " << name.toStdString()
              << ": disk_max_delta=" << maximum_interior_difference
              << ", exterior_changed=" << changed_exterior_pixels << '\n';
  }
}

TEST(RenderGroundGridRuntimeTests,
     DepthBiasShowsCoplanarGridAndPerspectiveOccludesRaisedSurfaces) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "ground-grid compositing coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(256u));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output_directory));
  }
  const auto luminance = [](QRgb pixel) {
    return (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3.0f;
  };
  for (const ViewportProjection projection :
       {ViewportProjection::Perspective, ViewportProjection::Orthographic}) {
    for (const bool rendered : {false, true}) {
      for (const bool bright : {false, true}) {
       for (const float surface_height : {0.0f, 1.0f}) {
        const QString name =
            QString(projection == ViewportProjection::Perspective
                        ? "perspective" : "orthographic") +
            (rendered ? "_rendered" : "_shaded") +
            (bright ? "_white" : "_dark") +
            (surface_height == 0.0f ? "_coplanar" : "_raised");
        SCOPED_TRACE(name.toStdString());
        ASSERT_TRUE(harness.create_grid_sample(projection, rendered, bright,
                                                surface_height));
        const QImage without_grid = harness.capture_grid(rendered, false);
        const QImage with_grid = harness.capture_grid(rendered, true);
        ASSERT_FALSE(without_grid.isNull());
        ASSERT_FALSE(with_grid.isNull());
        ASSERT_EQ(with_grid.size(), without_grid.size());
        if (!output_directory.isEmpty()) {
          ASSERT_TRUE(without_grid.save(QDir(output_directory).filePath(
              name + "_grid_off.png")));
          ASSERT_TRUE(with_grid.save(QDir(output_directory).filePath(
              name + "_grid_on.png")));
        }
        const bool occluded = projection == ViewportProjection::Perspective &&
                              surface_height > 0.0f;
        if (occluded) {
          int maximum_difference = 0;
          for (int y = 0; y < with_grid.height(); ++y) {
            for (int x = 0; x < with_grid.width(); ++x) {
              maximum_difference = std::max(maximum_difference,
                  std::abs(qRed(with_grid.pixel(x, y)) - qRed(without_grid.pixel(x, y))));
              maximum_difference = std::max(maximum_difference,
                  std::abs(qGreen(with_grid.pixel(x, y)) - qGreen(without_grid.pixel(x, y))));
              maximum_difference = std::max(maximum_difference,
                  std::abs(qBlue(with_grid.pixel(x, y)) - qBlue(without_grid.pixel(x, y))));
            }
          }
          EXPECT_LE(maximum_difference, 1)
              << "Perspective grid must not show through a raised opaque mesh";
          continue;
        }
        // Perspective bias keeps the Y=0 ground visible on its coplanar mesh;
        // restoring the legacy Y=-0.002 offset breaks the grazing case below.
        // Orthographic views intentionally draw on top.
        // Samples avoid colored origin axes and line intersections.
        for (const auto &[line_name, x] :
             std::array<std::pair<const char *, float>, 2u>{{
                 {"minor", 1.0f}, {"major", 2.0f}}}) {
          SCOPED_TRACE(line_name);
          const QPoint center = harness.grid_pixel({x, 0.0f, 0.5f});
          const QRect sample(center - QPoint(1, 1), QSize(3, 3));
          ASSERT_TRUE(with_grid.rect().contains(sample));
          const float baseline = luminance(without_grid.pixel(center));
          if (bright) {
            EXPECT_GT(baseline, 230.0f);
          } else {
            EXPECT_LT(baseline, 40.0f);
          }
          // Take the strongest nearby pixel to accommodate subpixel line
          // alignment; the signed difference must lighten dark surfaces and
          // darken white surfaces using the actual displayed pixel values.
          float contrast = 0.0f;
          for (int py = sample.top(); py <= sample.bottom(); ++py) {
            for (int px = sample.left(); px <= sample.right(); ++px) {
              const float delta = luminance(with_grid.pixel(px, py)) -
                                  luminance(without_grid.pixel(px, py));
              contrast = std::max(contrast, bright ? -delta : delta);
            }
          }
          // White 2%-opacity minor lines can match a white surface exactly.
          // The 6%-opacity gray major line still darkens it; both brighten dark.
          if (!bright || x == 2.0f) {
            EXPECT_GE(contrast, 0.3f)
                << "Low-opacity grid disappeared at its coplanar depth";
          }
          std::cout << "Ground grid GPU " << name.toStdString() << ' '
                    << line_name << ": background=" << baseline
                    << ", signed_contrast=" << contrast << '\n';
        }
        const QPoint between_lines = harness.grid_pixel({1.5f, 0.0f, 0.5f});
        ASSERT_TRUE(with_grid.rect().contains(between_lines));
        EXPECT_NEAR(luminance(with_grid.pixel(between_lines)),
                    luminance(without_grid.pixel(between_lines)), 1.0f);
       }
      }
    }
  }
}

TEST(RenderGroundGridRuntimeTests,
     GrazingDenseGridPreservesRestoredLowOpacityWithoutAWhiteSheet) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for windowless Vulkan "
                   "ground-grid grazing coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(256u));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(output_directory));
  for (const bool rendered : {false, true}) {
    for (const bool bright : {false, true}) {
      const QString name = QString("grazing_") +
          (rendered ? "rendered" : "shaded") + (bright ? "_white" : "_dark");
      SCOPED_TRACE(name.toStdString());
      ASSERT_TRUE(harness.create_grid_sample(ViewportProjection::Perspective,
                                               rendered, bright, 0.0f, true));
      const QImage off = harness.capture_grid(rendered, false, false);
      const QImage on = harness.capture_grid(rendered, true, false);
      ASSERT_FALSE(off.isNull());
      ASSERT_FALSE(on.isNull());
      if (!output_directory.isEmpty()) {
        ASSERT_TRUE(off.save(QDir(output_directory).filePath(name + "_grid_off.png")));
        ASSERT_TRUE(on.save(QDir(output_directory).filePath(name + "_grid_on.png")));
      }
      int maximum_change = 0;
      std::size_t changed_pixels = 0u;
      for (int y = 0; y < on.height(); ++y) {
        for (int x = 0; x < on.width(); ++x) {
          const QRgb before = off.pixel(x, y);
          const QRgb after = on.pixel(x, y);
          const int difference = std::max({std::abs(qRed(after) - qRed(before)),
              std::abs(qGreen(after) - qGreen(before)),
              std::abs(qBlue(after) - qBlue(before))});
          maximum_change = std::max(maximum_change, difference);
          changed_pixels += difference > 1 ? 1u : 0u;
        }
      }
      // Hammer permits dense lines to merge. It must still preserve the
      // restored 2%/6% alpha, rather than turn a dark ground into opaque white.
      EXPECT_LT(maximum_change, 85);
      if (!bright) EXPECT_GT(changed_pixels, 20u);
      for (const float x : {9.0f, 10.0f}) {
        const QPoint center = harness.grid_pixel({x, 0.0f, -4.5f});
        const QRect sample(center - QPoint(2, 2), QSize(5, 5));
        ASSERT_TRUE(on.rect().contains(sample));
        int visible_change = 0;
        for (int y = sample.top(); y <= sample.bottom(); ++y) {
          for (int px = sample.left(); px <= sample.right(); ++px) {
            visible_change = std::max(visible_change,
                std::abs(qRed(on.pixel(px, y)) - qRed(off.pixel(px, y))));
          }
        }
        if (!bright || x == 10.0f) EXPECT_GE(visible_change, 1);
      }
      std::cout << "Ground grid GPU " << name.toStdString()
                << ": maximum_change=" << maximum_change
                << ", changed_pixels=" << changed_pixels << '\n';

      ASSERT_TRUE(harness.create_grid_sample(ViewportProjection::Perspective,
                                               rendered, bright, 1.0f, true));
      const QImage raised_off = harness.capture_grid(rendered, false, false);
      const QImage raised_on = harness.capture_grid(rendered, true, false);
      ASSERT_FALSE(raised_off.isNull());
      ASSERT_FALSE(raised_on.isNull());
      int raised_difference = 0;
      for (int y = 0; y < raised_on.height(); ++y) {
        for (int x = 0; x < raised_on.width(); ++x) {
          const QRgb before = raised_off.pixel(x, y);
          const QRgb after = raised_on.pixel(x, y);
          raised_difference = std::max(raised_difference,
              std::max({std::abs(qRed(after) - qRed(before)),
                  std::abs(qGreen(after) - qGreen(before)),
                  std::abs(qBlue(after) - qBlue(before))}));
        }
      }
      EXPECT_LE(raised_difference, 1)
          << "Grazing camera must not see the ground grid through a raised plane";
      if (!output_directory.isEmpty()) {
        ASSERT_TRUE(raised_off.save(QDir(output_directory).filePath(
            name + "_raised_grid_off.png")));
        ASSERT_TRUE(raised_on.save(QDir(output_directory).filePath(
            name + "_raised_grid_on.png")));
      }
    }
  }
}

TEST(RenderGroundGridRuntimeTests,
     PerspectiveGridPreservesPaletteAcrossEqualForwardDepth) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for distant perspective "
                   "ground-grid coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(512u, true));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(output_directory));
  const ViewportSettings authored = ViewportSettings::defaults();
  const auto maximum_difference = [](const QImage &left, const QImage &right,
                                      const QRect &region) {
    int maximum = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QRgb a = left.pixel(x, y);
        const QRgb b = right.pixel(x, y);
        maximum = std::max(maximum, std::max({std::abs(qRed(a) - qRed(b)),
            std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))}));
      }
    }
    return maximum;
  };

  std::array<QImage, 2u> off;
  std::array<QImage, 2u> on;
  for (std::size_t index = 0u; index < on.size(); ++index) {
    const float far_plane = index == 0u ? 200.0f : 250.0f;
    SCOPED_TRACE(far_plane);
    ASSERT_TRUE(harness.create_empty_grid_sample(
        {0.0f, 60.0f, 0.0f}, {0.0f, 0.0f, -80.0f}, far_plane));
    off[index] = harness.capture_grid(false, false, false, &authored);
    on[index] = harness.capture_grid(false, true, false, &authored);
    ASSERT_FALSE(off[index].isNull());
    ASSERT_FALSE(on[index].isNull());
    if (!output_directory.isEmpty()) {
      const QString prefix = QString("distant_grid_far_%1").arg(far_plane);
      ASSERT_TRUE(off[index].save(QDir(output_directory).filePath(prefix + "_off.png")));
      ASSERT_TRUE(on[index].save(QDir(output_directory).filePath(prefix + "_on.png")));
    }

    // Both points have forward depth 179.6. The second point's Euclidean
    // distance is greater than 200, so a spherical fade erases a valid line.
    const QPoint center = harness.grid_pixel({3.0f, 0.0f, -179.5f});
    const QPoint corner = harness.grid_pixel({71.0f, 0.0f, -179.5f});
    ASSERT_EQ(center.y(), corner.y());
    for (const QPoint point : {center, corner}) {
      const QRect sample(point - QPoint(2, 2), QSize(5, 5));
      ASSERT_TRUE(on[index].rect().contains(sample));
      const int contrast = maximum_difference(off[index], on[index], sample);
      EXPECT_GE(contrast, 12)
          << "A ground line inside the view must retain its palette at x=" << point.x();
      std::cout << "Distant grid GPU far=" << far_plane << " pixel="
                << point.x() << ',' << point.y() << " contrast=" << contrast << '\n';
    }
  }

  EXPECT_LE(maximum_difference(off[0], off[1], off[0].rect()), 1);
  const QPoint depth_sample =
      harness.grid_pixel({3.0f, 0.0f, -179.5f});
  const QRect common_depth(8, depth_sample.y() - 2, on[0].width() - 16, 5);
  ASSERT_TRUE(on[0].rect().contains(common_depth));
  const int palette_change = maximum_difference(on[0], on[1], common_depth);
  EXPECT_LE(palette_change, 1)
      << "Moving the far plane must not change grid opacity at a shared visible depth";
  std::cout << "Distant grid GPU common-depth maximum_palette_change="
            << palette_change << '\n';
}

TEST(RenderGroundGridRuntimeTests,
     PerspectiveGridEndsAtFixedWorldRectangleWithoutAnEdgeFade) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for fixed perspective "
                   "ground-grid bounds";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(512u, true));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(output_directory));
  const ViewportSettings authored = ViewportSettings::defaults();
  const auto maximum_difference = [](const QImage &left, const QImage &right,
                                      const QRect &region) {
    int maximum = 0;
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QRgb a = left.pixel(x, y);
        const QRgb b = right.pixel(x, y);
        maximum = std::max(maximum, std::max({std::abs(qRed(a) - qRed(b)),
            std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))}));
      }
    }
    return maximum;
  };

  // Observe both sides meeting at the same world corner, then translate the
  // camera. The rectangle belongs to world space; it must not follow the eye.
  for (const float camera_x : {480.0f, 500.0f}) {
    SCOPED_TRACE(camera_x);
    ASSERT_TRUE(harness.create_empty_grid_sample(
        {camera_x, 100.0f, 600.0f}, {camera_x, 0.0f, 500.0f}, 1000.0f));
    const QImage off = harness.capture_grid(false, false, false, &authored);
    const QImage on = harness.capture_grid(false, true, false, &authored);
    ASSERT_FALSE(off.isNull());
    ASSERT_FALSE(on.isNull());
    if (!output_directory.isEmpty()) {
      const QString prefix = QString("fixed_grid_corner_camera_x_%1").arg(camera_x);
      ASSERT_TRUE(off.save(QDir(output_directory).filePath(prefix + "_off.png")));
      ASSERT_TRUE(on.save(QDir(output_directory).filePath(prefix + "_on.png")));
    }

    const QPoint x_edge = harness.grid_pixel({512.0f, 0.0f, 500.5f});
    const QPoint z_edge = harness.grid_pixel({500.5f, 0.0f, 512.0f});
    const std::array<QRect, 2u> inside{{
        QRect(x_edge.x() - 4, x_edge.y() - 4, 4, 9),
        QRect(z_edge.x() - 4, z_edge.y() - 4, 9, 4),
    }};
    const std::array<QRect, 2u> outside{{
        QRect(x_edge.x() + 2, x_edge.y() - 4, 8, 9),
        QRect(z_edge.x() - 4, z_edge.y() + 2, 9, 8),
    }};
    for (std::size_t edge = 0u; edge < inside.size(); ++edge) {
      SCOPED_TRACE(edge == 0u ? "x=512 edge" : "z=512 edge");
      ASSERT_TRUE(on.rect().contains(inside[edge]));
      ASSERT_TRUE(on.rect().contains(outside[edge]));
      const int inside_contrast = maximum_difference(off, on, inside[edge]);
      const int outside_contrast = maximum_difference(off, on, outside[edge]);
      EXPECT_GE(inside_contrast, 12)
          << "The final few pixels inside the grid retain visible lines";
      EXPECT_LE(outside_contrast, 1)
          << "The same world edge must expose the uniform background after camera movement";
      std::cout << "Fixed grid GPU camera_x=" << camera_x << " edge=" << edge
                << " inside_contrast=" << inside_contrast
                << " outside_contrast=" << outside_contrast << '\n';
    }
  }

  ASSERT_TRUE(harness.create_empty_grid_sample(
      {8192.0f, 100.0f, 100.0f}, {8192.0f, 0.0f, 0.0f}, 1000.0f));
  const QImage far_off = harness.capture_grid(false, false, false, &authored);
  const QImage far_on = harness.capture_grid(false, true, false, &authored);
  ASSERT_FALSE(far_off.isNull());
  ASSERT_FALSE(far_on.isNull());
  EXPECT_LE(maximum_difference(far_off, far_on, far_on.rect()), 1)
      << "Moving far outside the fixed world rectangle must not create another grid under the camera";
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(far_off.save(QDir(output_directory).filePath("fixed_grid_outside_world_off.png")));
    ASSERT_TRUE(far_on.save(QDir(output_directory).filePath("fixed_grid_outside_world_on.png")));
  }
}

TEST(RenderGroundGridRuntimeTests,
     ResizedOrthographicGridRemainsVisibleOutsideItsInitialBounds) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for resized "
                   "orthographic ground-grid culling coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(512u, true));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(output_directory));
  const ViewportSettings authored = ViewportSettings::defaults();
  for (const float zoom_steps : {4.0f, 4.25f}) {
    SCOPED_TRACE(zoom_steps);
    // Eye (0,2000,1500), target origin, orthographic height about 1500.
    // The initial 1024-unit box is entirely below this view. Resizing the
    // actual plane exposes its near edge, so its culling bounds must grow too.
    ASSERT_TRUE(harness.create_empty_orthographic_grid_sample(zoom_steps));
    const QImage off = harness.capture_grid(false, false, false, &authored);
    const QImage on = harness.capture_grid(false, true, false, &authored);
    ASSERT_FALSE(off.isNull());
    ASSERT_FALSE(on.isNull());
    const QPoint center = harness.grid_pixel({3.0f, 0.0f, 700.0f});
    const QRect sample(center - QPoint(3, 3), QSize(7, 7));
    ASSERT_TRUE(on.rect().contains(sample));
    int maximum_contrast = 0;
    std::size_t visible_pixels = 0u;
    for (int y = sample.top(); y <= sample.bottom(); ++y) {
      for (int x = sample.left(); x <= sample.right(); ++x) {
        const QRgb before = off.pixel(x, y);
        const QRgb after = on.pixel(x, y);
        const int contrast = std::max({std::abs(qRed(after) - qRed(before)),
            std::abs(qGreen(after) - qGreen(before)),
            std::abs(qBlue(after) - qBlue(before))});
        maximum_contrast = std::max(maximum_contrast, contrast);
        visible_pixels += contrast >= 12 ? 1u : 0u;
      }
    }
    EXPECT_GE(maximum_contrast, 12);
    EXPECT_GE(visible_pixels, 8u)
        << "A stale perspective-sized box must not cull the visible resized orthographic grid";
    std::cout << "Resized orthographic grid GPU zoom_steps=" << zoom_steps
              << " maximum_contrast=" << maximum_contrast
              << " visible_pixels=" << visible_pixels << '\n';
    if (!output_directory.isEmpty()) {
      const QString prefix = QString("resized_orthographic_grid_zoom_%1").arg(zoom_steps);
      ASSERT_TRUE(off.save(QDir(output_directory).filePath(prefix + "_off.png")));
      ASSERT_TRUE(on.save(QDir(output_directory).filePath(prefix + "_on.png")));
    }
  }
}

TEST(RenderGroundGridRuntimeTests,
     NativeSrgbGridUsesFixedHammerPaletteAcrossTexturedSurfaces) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_GRID_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_GRID_TESTS=1 for native sRGB "
                   "fixed ground-grid palette coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(512u, true));
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_GRID_OUTPUT_DIR");
  if (!output_directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(output_directory));
  const auto luminance = [](QRgb pixel) {
    return qRed(pixel) * 0.2126f + qGreen(pixel) * 0.7152f + qBlue(pixel) * 0.0722f;
  };
  const auto linear_channel = [](int value) {
    const float srgb = static_cast<float>(value) / 255.0f;
    return srgb <= 0.04045f ? srgb / 12.92f
                           : std::pow((srgb + 0.055f) / 1.055f, 2.4f);
  };
  const auto linear_pixel = [&](QRgb pixel) {
    return QVec3{linear_channel(qRed(pixel)), linear_channel(qGreen(pixel)),
                  linear_channel(qBlue(pixel))};
  };
  const auto display_channel = [](float value) {
    value = std::clamp(value, 0.0f, 1.0f);
    return 255.0f * (value <= 0.0031308f ? value * 12.92f
        : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f);
  };
  const auto mean = [](QVec3 value) { return (value.x + value.y + value.z) / 3.0f; };
  const auto maximum_difference = [](const QImage &left, const QImage &right) {
    int maximum = 0;
    for (int y = 0; y < left.height(); ++y) {
      for (int x = 0; x < left.width(); ++x) {
        const QRgb a = left.pixel(x, y);
        const QRgb b = right.pixel(x, y);
        maximum = std::max(maximum, std::max({std::abs(qRed(a) - qRed(b)),
            std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))}));
      }
    }
    return maximum;
  };
  ViewportSettings authored = ViewportSettings::defaults();
  authored.set_float_value(ViewportFloatSettingId::GridMinorLineSize, 0.25f);
  authored.set_float_value(ViewportFloatSettingId::GridMajorLineSize, 0.35f);
  authored.set_color(ViewportColorSettingId::GridMinor,
                      {76.0f / 255.0f, 76.0f / 255.0f, 76.0f / 255.0f, 1.0f});
  authored.set_color(ViewportColorSettingId::GridMajor,
                      {114.0f / 255.0f, 114.0f / 255.0f, 114.0f / 255.0f, 1.0f});
  ViewportSettings transparent = authored;
  ViewportSettings white_control = authored;
  for (const auto id : {ViewportColorSettingId::GridMinor,
                        ViewportColorSettingId::GridMajor}) {
    auto color = authored.color(id);
    color.a = 0.0f;
    transparent.set_color(id, color);
    white_control.set_color(id, {1.0f, 1.0f, 1.0f, 1.0f});
  }
  const auto save = [&](const QImage &image, const QString &name) {
    if (!output_directory.isEmpty()) {
      EXPECT_TRUE(image.save(QDir(output_directory).filePath(name + ".png")));
    }
  };
  struct Candidate final {
    QPoint pixel;
    float score;
  };
  const std::array<const char *, 5u> tone_names{
      "dark-material", "matching-grid-gray", "bright", "beige", "screenshot-mid-beige"};
  for (const bool rendered : {false, true}) {
    for (const bool oblique : {false, true}) {
      for (const bool post_processing : {false, true}) {
        const QString name = QString("fixed_palette_") +
            (rendered ? "rendered" : "shaded") +
            (oblique ? "_oblique" : "_topdown") +
            (post_processing ? "_pp_on" : "_pp_off");
        SCOPED_TRACE(name.toStdString());
        harness.settings.post_process.enabled = post_processing;
        harness.set_grid_scene_visible(true);
        ASSERT_TRUE(harness.create_textured_grid_sample(rendered, oblique));
        const QImage off = harness.capture_grid(rendered, false, false, &authored);
        const QImage on = harness.capture_grid(rendered, true, false, &authored);
        const QImage zero_alpha = harness.capture_grid(rendered, true, false, &transparent);
        const QImage restored = harness.capture_grid(rendered, false, false, &authored);
        ASSERT_FALSE(off.isNull());
        ASSERT_FALSE(on.isNull());
        ASSERT_FALSE(zero_alpha.isNull());
        ASSERT_FALSE(restored.isNull());
        ASSERT_EQ(off.size(), on.size());
        EXPECT_LE(maximum_difference(off, zero_alpha), 1);
        EXPECT_LE(maximum_difference(off, restored), 1);
        save(off, name + "_grid_off");
        save(on, name + "_grid_on");

        // Keep camera, geometry and depth fixed while hiding the scene's
        // color. White over measured black directly reveals raster coverage;
        // no test copy of line widths or the nine-sample shader is involved.
        harness.set_grid_scene_visible(false);
        const QImage black = harness.capture_grid(rendered, false, false, &authored);
        const QImage on_black = harness.capture_grid(rendered, true, false, &authored);
        const QImage white = harness.capture_grid(rendered, true, false, &white_control);
        ASSERT_FALSE(black.isNull());
        ASSERT_FALSE(on_black.isNull());
        ASSERT_FALSE(white.isNull());
        save(on_black, name + "_palette_over_black");
        save(white, name + "_white_coverage_control");
        int maximum_background = 0;
        for (int y = 0; y < black.height(); ++y) {
          for (int x = 0; x < black.width(); ++x) {
            const QRgb pixel = black.pixel(x, y);
            maximum_background = std::max(maximum_background,
                std::max({qRed(pixel), qGreen(pixel), qBlue(pixel)}));
          }
        }
        EXPECT_LE(maximum_background, 1) << "Hiding the scene must clear its rendered colors";
        const auto measured_alpha = [&](QPoint pixel) {
          const QVec3 background = linear_pixel(black.pixel(pixel));
          return mean(linear_pixel(white.pixel(pixel)) - background) /
                 (1.0f - mean(background));
        };

        for (const float line_z : {1.0f, 2.0f}) {
          // Quader uses 32 map units per world unit. These lines exercise
          // the small tier and the 64-map-unit highlight, away from the axes.
          SCOPED_TRACE(line_z == 1.0f ? "small-76" : "highlight64-114");
          const int source_display = line_z == 1.0f ? 76 : 114;
          const float source_linear = linear_channel(source_display);
          const QVec3 source{source_linear, source_linear, source_linear};
          std::array<std::vector<Candidate>, 5u> candidates;
          for (int step = -192; step <= 192; ++step) {
            const float x = static_cast<float>(step) / 64.0f;
            if (std::abs(x - std::round(x)) < 0.15f) continue;
            const QPoint pixel = harness.grid_pixel({x, 0.0f, line_z});
            const QRect region(pixel - QPoint(2, 2), QSize(5, 5));
            if (!off.rect().contains(region)) continue;
            const QRgb background = off.pixel(pixel);
            const float level = luminance(background);
            const int spread = std::max({qRed(background), qGreen(background), qBlue(background)}) -
                               std::min({qRed(background), qGreen(background), qBlue(background)});
            if (spread < 4) {
              if (level < 75.0f) candidates[0u].push_back({pixel, std::abs(level - 65.0f)});
              if (std::abs(level - source_display) <= 6.0f) {
                candidates[1u].push_back({pixel, std::abs(level - source_display)});
              }
              if (level > 180.0f) candidates[2u].push_back({pixel, std::abs(level - 210.0f)});
            }
            if (qRed(background) > qBlue(background) + 18 &&
                qRed(background) > qGreen(background) + 6) {
              if (level > 150.0f) candidates[3u].push_back({pixel, std::abs(level - 185.0f)});
              if (!rendered && level >= 117.0f && level <= 137.0f) {
                candidates[4u].push_back({pixel, std::abs(level - 128.0f)});
              }
            }
          }
          for (std::size_t tone = 0u; tone < candidates.size(); ++tone) {
            if (tone == 4u && rendered) continue;
            SCOPED_TRACE(tone_names[tone]);
            auto &samples = candidates[tone];
            std::sort(samples.begin(), samples.end(), [](const Candidate &a, const Candidate &b) {
              return a.score < b.score;
            });
            std::vector<int> selected_x;
            std::vector<float> changes;
            float maximum_palette_error = 0.0f;
            for (const Candidate &sample : samples) {
              if (std::any_of(selected_x.begin(), selected_x.end(), [&](int x) {
                    return std::abs(x - sample.pixel.x()) < 4;
                  })) continue;
              QPoint pixel = sample.pixel;
              float alpha = 0.0f;
              for (int row = -2; row <= 2; ++row) {
                for (int column = -1; column <= 1; ++column) {
                  const QPoint nearby = sample.pixel + QPoint(column, row);
                  const float coverage = measured_alpha(nearby);
                  if (coverage > alpha) {
                    alpha = coverage;
                    pixel = nearby;
                  }
                }
              }
              if (alpha < 0.15f) continue;
              ASSERT_LE(alpha, 1.01f);
              for (const bool over_black : {true, false}) {
                const QRgb background = (over_black ? black : off).pixel(pixel);
                const QRgb observed = (over_black ? on_black : on).pixel(pixel);
                const QVec3 expected = linear_pixel(background) * (1.0f - alpha) + source * alpha;
                const float error = std::max({
                    std::abs(qRed(observed) - display_channel(expected.x)),
                    std::abs(qGreen(observed) - display_channel(expected.y)),
                    std::abs(qBlue(observed) - display_channel(expected.z))});
                EXPECT_LE(error, 2.5f)
                    << "source=" << source_display << ", coverage=" << alpha
                    << ", over_black=" << over_black << ", pixel="
                    << pixel.x() << ',' << pixel.y();
                maximum_palette_error = std::max(maximum_palette_error, error);
              }
              EXPECT_GT(luminance(on_black.pixel(pixel)) - luminance(black.pixel(pixel)), 5.0f);
              const float change = luminance(on.pixel(pixel)) - luminance(off.pixel(pixel));
              if (tone == 2u || tone == 3u || tone == 4u) {
                EXPECT_LT(change, 0.0f) << "The fixed grid must darken this brighter surface";
              }
              // Matching gray may have zero contrast. Its measured pixel must
              // still obey the same fixed-color blend instead of being recolored.
              changes.push_back(change);
              selected_x.push_back(sample.pixel.x());
              if (selected_x.size() == 3u) break;
            }
            EXPECT_GE(selected_x.size(), 2u)
                << "The fixture must expose this tone and a visible line";
            std::sort(changes.begin(), changes.end());
            const float median = changes.empty() ? 0.0f : changes[changes.size() / 2u];
            std::cout << "Fixed palette GPU " << name.toStdString() << " source=" << source_display
                      << ' ' << tone_names[tone] << ": samples=" << selected_x.size()
                      << ", median_display_change=" << median
                      << ", maximum_palette_error=" << maximum_palette_error << '\n';
          }
        }

        harness.set_grid_scene_visible(true);
        ASSERT_TRUE(harness.create_textured_grid_sample(rendered, oblique, 1.0f));
        const QImage raised_off = harness.capture_grid(rendered, false, false, &authored);
        const QImage raised_on = harness.capture_grid(rendered, true, false, &authored);
        ASSERT_FALSE(raised_off.isNull());
        ASSERT_FALSE(raised_on.isNull());
        int occluded_difference = 0;
        for (const float x : {-2.0f, -0.5f, 0.5f, 2.0f}) {
          for (const float z : {0.25f, 1.0f, 1.75f}) {
            const QPoint center = harness.grid_pixel({x, 0.0f, z});
            const QRect region(center - QPoint(2, 2), QSize(5, 5));
            ASSERT_TRUE(raised_on.rect().contains(region));
            for (int y = region.top(); y <= region.bottom(); ++y) {
              for (int px = region.left(); px <= region.right(); ++px) {
                const QRgb before = raised_off.pixel(px, y);
                const QRgb after = raised_on.pixel(px, y);
                occluded_difference = std::max(occluded_difference,
                    std::max({std::abs(qRed(after) - qRed(before)),
                        std::abs(qGreen(after) - qGreen(before)),
                        std::abs(qBlue(after) - qBlue(before))}));
              }
            }
          }
        }
        EXPECT_LE(occluded_difference, 1) << "Ground grid must respect raised mesh depth";
        save(raised_on, name + "_raised_grid_on");
      }
    }
  }
}

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

TEST(RenderFogRuntimeTests,
     EditorBackgroundAndSceneClearColorStayIndependentAndCached) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_FOG_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_FOG_TESTS=1 for native sRGB "
                   "viewport-background coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(64u, true));
  const auto display_channel = [](float linear) {
    return 255.0f * (linear <= 0.0031308f ? 12.92f * linear
        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f);
  };
  const auto scene_display = [&](QVec3 linear) {
    return QVec3{display_channel(linear.x), display_channel(linear.y),
                  display_channel(linear.z)};
  };
  const auto expect_background = [](const QImage &image, QVec3 expected) {
    ASSERT_FALSE(image.isNull());
    float maximum_error = 0.0f;
    int minimum_alpha = 255;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QRgb pixel = image.pixel(x, y);
        maximum_error = std::max(maximum_error, std::max({
            std::abs(static_cast<float>(qRed(pixel)) - expected.x),
            std::abs(static_cast<float>(qGreen(pixel)) - expected.y),
            std::abs(static_cast<float>(qBlue(pixel)) - expected.z)}));
        minimum_alpha = std::min(minimum_alpha, qAlpha(pixel));
      }
    }
    EXPECT_LE(maximum_error, 1.5f) << "Every empty-frame pixel uses the selected background";
    EXPECT_EQ(minimum_alpha, 255) << "The editor backdrop is opaque";
  };
  const auto set_editor_color = [&](QVec3 display) {
    harness.presentation_settings.set_color(ViewportColorSettingId::Background,
        {display.x / 255.0f, display.y / 255.0f, display.z / 255.0f, 0.2f});
  };

  const QVec3 editor_first{63.0f, 121.0f, 177.0f};
  const QVec3 editor_second{183.0f, 76.0f, 41.0f};
  const QVec3 editor_third{37.0f, 164.0f, 95.0f};
  set_editor_color(editor_first);
  harness.settings.environment.clear_color = {0.45f, 0.07f, 0.02f};
  expect_background(harness.capture_background(false), editor_first);
  const std::uint64_t first_applied = harness.application_count();
  expect_background(harness.capture_background(false), editor_first);
  EXPECT_EQ(harness.application_count(), first_applied);

  // Scene changes cannot tint the modeling backdrop or invalidate its cache.
  harness.settings.environment.clear_color = {0.03f, 0.40f, 0.09f};
  expect_background(harness.capture_background(false), editor_first);
  EXPECT_EQ(harness.application_count(), first_applied);
  set_editor_color(editor_second);
  expect_background(harness.capture_background(false), editor_second);
  EXPECT_EQ(harness.application_count(), first_applied + 1u);

  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  const std::uint64_t rendered_applied = harness.application_count();
  EXPECT_EQ(rendered_applied, first_applied + 2u);
  // The global editor preference is likewise irrelevant to a Rendered frame.
  set_editor_color(editor_third);
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), rendered_applied);
  harness.settings.environment.clear_color = {0.26f, 0.045f, 0.50f};
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), rendered_applied + 1u);

  // Mode changes must immediately publish the latest color from that owner.
  expect_background(harness.capture_background(false), editor_third);
  EXPECT_EQ(harness.application_count(), rendered_applied + 2u);
  expect_background(harness.capture_background(false), editor_third);
  EXPECT_EQ(harness.application_count(), rendered_applied + 2u);
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), rendered_applied + 3u);

  // Normal Rendered mode enables post-processing. Its empty framebuffer still
  // uses the authored linear clear color, independently of editor preferences.
  harness.settings.post_process.enabled = true;
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  const std::uint64_t post_processed_applied = harness.application_count();
  EXPECT_EQ(post_processed_applied, rendered_applied + 4u);
  set_editor_color(editor_first);
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), post_processed_applied);
  harness.settings.environment.clear_color = {0.12f, 0.32f, 0.06f};
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), post_processed_applied + 1u);
  expect_background(harness.capture_background(true),
                      scene_display(harness.settings.environment.clear_color));
  EXPECT_EQ(harness.application_count(), post_processed_applied + 1u);
}

TEST(RenderFogRuntimeTests,
     DiagnosticBackgroundTemporarilyHidesAndRestoresSharedSkybox) {
  if (qEnvironmentVariableIntValue("QUADER_RUN_VULKAN_FOG_TESTS") != 1) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_FOG_TESTS=1 for diagnostic "
                   "skybox/background coverage";
  }
  FogRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize(64u, true));
  harness.presentation_settings.set_color(ViewportColorSettingId::Background,
      {171.0f / 255.0f, 45.0f / 255.0f, 71.0f / 255.0f, 1.0f});
  harness.settings.environment.clear_color = {0.35f, 0.02f, 0.015f};
  harness.settings.environment.enabled = true;
  harness.settings.environment.show_background = true;
  const auto maximum_difference = [](const QImage &left, const QImage &right) {
    int maximum = 0;
    for (int y = 0; y < left.height(); ++y) {
      for (int x = 0; x < left.width(); ++x) {
        const QRgb a = left.pixel(x, y);
        const QRgb b = right.pixel(x, y);
        maximum = std::max(maximum, std::max({std::abs(qRed(a) - qRed(b)),
            std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b)),
            std::abs(qAlpha(a) - qAlpha(b))}));
      }
    }
    return maximum;
  };

  const QImage clear = harness.capture_background(true);
  ASSERT_FALSE(clear.isNull());
  ASSERT_TRUE(harness.create_background_skybox());
  const QImage sky = harness.capture_background(true);
  ASSERT_FALSE(sky.isNull());
  ASSERT_GE(maximum_difference(clear, sky), 20)
      << "The ordinary Rendered reference must actually contain the skybox";
  const std::uint64_t rendered_applied = harness.application_count();
  QImage editor(sky.size(), QImage::Format_ARGB32);
  editor.fill(qRgba(171, 45, 71, 255));

  harness.set_material_debug_mode(ViewportMaterialDebugMode::DiffuseOnly);
  const QImage diffuse = harness.capture_background(true, true);
  ASSERT_FALSE(diffuse.isNull());
  EXPECT_LE(maximum_difference(diffuse, editor), 1);
  const std::uint64_t diagnostic_applied = harness.application_count();
  EXPECT_EQ(diagnostic_applied, rendered_applied + 1u);
  const QImage diffuse_repeat = harness.capture_background(true, true);
  ASSERT_FALSE(diffuse_repeat.isNull());
  EXPECT_LE(maximum_difference(diffuse_repeat, editor), 1);
  EXPECT_EQ(harness.application_count(), diagnostic_applied);

  // Albedo disables scene lighting but still uses the shared rendered layer.
  // Skybox suppression must therefore belong to the diagnostic view itself.
  harness.set_material_debug_mode(ViewportMaterialDebugMode::Albedo);
  const QImage albedo = harness.capture_background(false, true);
  ASSERT_FALSE(albedo.isNull());
  EXPECT_LE(maximum_difference(albedo, editor), 1);
  EXPECT_EQ(harness.application_count(), diagnostic_applied)
      << "Identical effective background, fog and post-processing remain cached";

  harness.set_material_debug_mode(ViewportMaterialDebugMode::None);
  const QImage restored = harness.capture_background(true);
  ASSERT_FALSE(restored.isNull());
  EXPECT_LE(maximum_difference(restored, sky), 1)
      << "A diagnostic view must restore the shared skybox for the next Rendered view";
  EXPECT_EQ(harness.application_count(), diagnostic_applied + 1u);
  const QImage restored_repeat = harness.capture_background(true);
  ASSERT_FALSE(restored_repeat.isNull());
  EXPECT_LE(maximum_difference(restored_repeat, sky), 1);
  EXPECT_EQ(harness.application_count(), diagnostic_applied + 1u);
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
