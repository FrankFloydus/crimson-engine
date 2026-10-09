////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_schedule.hpp"
#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "editor/commands/asset_command_service.hpp"
#include "render/filament_material_library.hpp"
#include "render/material_compiler_service.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"
#include "scene/light_properties.hpp"

#include <gtest/gtest.h>

#include <backend/PixelBufferDescriptor.h>
#include <filament/IndirectLight.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

filament::Texture *create_test_solid_color_texture(
    filament::Engine &engine) {
  const std::array<std::byte, 4u> neutral_gray{
      std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
  return load_rgba_texture(
      engine, neutral_gray, 1u, 1u,
      PngTextureLoadOptions{
          .color_space = PngTextureColorSpace::Srgb,
          .generate_mipmaps = true,
          .label = "light runtime Solid viewport texture",
      });
}

/// Counts accepted presentation frames once, independently of surface count.
struct VSyncRuntimeSample {
  std::size_t accepted_frames = 0u;
  std::array<std::size_t, 4> surface_submissions{};
  double elapsed_seconds = 0.0;
  double cpu_submission_ms = 0.0;
  RenderFramePerformanceStats final_renderer_stats;

  double frames_per_second() const {
    return elapsed_seconds > 0.0
               ? static_cast<double>(accepted_frames) / elapsed_seconds
               : 0.0;
  }
};

/// Owns only temporary test windows and renderer resources, never editor state.
class VSyncRuntimeHarness final {
public:
  ~VSyncRuntimeHarness() { shutdown_rendering(); }

  bool initialize() {
    const QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
      return false;
    }
    const double reported_hz = screen->refreshRate();
    refresh_hz_ = std::isfinite(reported_hz) && reported_hz > 0.0
                      ? reported_hz
                      : 60.0;
    const QPoint origin = screen->availableGeometry().topLeft() + QPoint(24, 24);
    for (std::size_t index = 0u; index < windows_.size(); ++index) {
      QWindow &window = windows_[index];
      window.setFlags(Qt::Tool | Qt::WindowDoesNotAcceptFocus);
      window.setTitle(QStringLiteral("Quader Vulkan VSync test %1")
                          .arg(static_cast<int>(index + 1u)));
      window.setSurfaceType(QSurface::RasterSurface);
      window.resize(320, 240);
      window.setPosition(origin + QPoint(static_cast<int>(index % 2u) * 340,
                                         static_cast<int>(index / 2u) * 280));
      window.show();
    }
    QCoreApplication::processEvents();

    const std::filesystem::path resource_root(
        QCoreApplication::applicationDirPath().toStdWString());
    renderer_.set_display_refresh_rate_hz(static_cast<float>(refresh_hz_));
    if (!renderer_.initialize(native_window(0u), resource_root)) {
      return false;
    }
    for (std::size_t index = 0u; index < additional_.size(); ++index) {
      additional_[index] =
          renderer_.create_additional_swap_chain(native_window(index + 1u));
      if (!additional_[index]) {
        return false;
      }
    }
    for (std::size_t index = 0u; index < views_.size(); ++index) {
      renderer_.initialize_viewport_renderer(
          views_[index], viewport_grid_layer_mask(index),
          ViewportRenderSurface::Swapchain);
      update_view_size(index);
    }
    return true;
  }

  RenderSystem &renderer() { return renderer_; }
  RenderAdditionalSwapChain &additional(std::size_t index) {
    return additional_[index];
  }
  double refresh_hz() const { return refresh_hz_; }

  void resize_surface(std::size_t index, int width, int height) {
    windows_[index].resize(width, height);
    QCoreApplication::processEvents();
    update_view_size(index);
  }

  /// Uses the app's cadence and nonblocking readiness policy with a Qt timer.
  VSyncRuntimeSample sample(std::size_t active_surfaces,
                            int measured_ms = 1500) {
    using Clock = std::chrono::steady_clock;
    constexpr int kWarmupMs = 350;
    VSyncRuntimeSample result;
    AppFrameSchedule schedule;
    QEventLoop loop;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    const int display_interval_ms = std::max(
        1, static_cast<int>(std::lround(
               1000.0 / std::clamp(refresh_hz_, 30.0, 240.0))));
    const auto measurement_begin = Clock::now() +
                                   std::chrono::milliseconds(kWarmupMs);
    const auto sample_end = measurement_begin +
                            std::chrono::milliseconds(measured_ms);
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
      const auto now = Clock::now();
      if (now >= sample_end) {
        result.elapsed_seconds =
            std::chrono::duration<double>(now - measurement_begin).count();
        timer.stop();
        loop.quit();
        return;
      }

      bool submitted = false;
      if (renderer_.presentation_is_synchronized() ||
          renderer_.ready_for_next_frame()) {
        const auto submission_begin = Clock::now();
        std::size_t submissions = 0u;
        renderer_.begin_presentation_frame();
        for (std::size_t index = 0u; index < active_surfaces; ++index) {
          const bool accepted = index == 0u
                                    ? renderer_.begin_frame()
                                    : renderer_.begin_frame(additional_[index - 1u]);
          if (!accepted) {
            continue;
          }
          if (index == 0u) {
            renderer_.render_viewport(views_[index]);
            renderer_.end_frame();
          } else {
            renderer_.render_viewport(views_[index], additional_[index - 1u]);
            renderer_.end_frame(additional_[index - 1u]);
          }
          ++submissions;
          if (now >= measurement_begin) {
            ++result.surface_submissions[index];
          }
        }
        renderer_.complete_presentation_frame(submissions);
        submitted = submissions != 0u;
        if (submitted && now >= measurement_begin) {
          ++result.accepted_frames;
          result.cpu_submission_ms +=
              std::chrono::duration<double, std::milli>(
                  Clock::now() - submission_begin).count();
        }
      }
      const int interval_ms = schedule.next_interval_ms(
          true, renderer_.presentation_is_synchronized(), display_interval_ms,
          submitted);
      if (timer.interval() != interval_ms) {
        timer.setInterval(interval_ms);
      }
    });
    timer.start(0);
    loop.exec();
    result.final_renderer_stats = renderer_.frame_performance_stats();
    if (result.accepted_frames != 0u) {
      result.cpu_submission_ms /= static_cast<double>(result.accepted_frames);
    }
    return result;
  }

  void shutdown_rendering() {
    for (ViewportRenderer &view : views_) {
      view.shutdown();
    }
    // Additional handles deliberately remain alive after this call.
    renderer_.shutdown();
  }

private:
  void *native_window(std::size_t index) {
    return reinterpret_cast<void *>(windows_[index].winId());
  }

  void update_view_size(std::size_t index) {
    const QWindow &window = windows_[index];
    const auto width = static_cast<std::uint32_t>(
        std::max(1, qRound(window.width() * window.devicePixelRatio())));
    const auto height = static_cast<std::uint32_t>(
        std::max(1, qRound(window.height() * window.devicePixelRatio())));
    ViewportCameraController camera;
    camera.set_viewport_size(static_cast<float>(width),
                             static_cast<float>(height));
    views_[index].resize(width, height);
    views_[index].update_camera(camera);
    views_[index].update_ground_grid(camera, 1.0f, ViewportSettings::defaults());
  }

  std::array<QWindow, 4> windows_;
  RenderSystem renderer_;
  std::array<RenderAdditionalSwapChain, 3> additional_;
  std::array<ViewportRenderer, 4> views_;
  double refresh_hz_ = 60.0;
};

void print_vsync_sample(const char *label, const VSyncRuntimeSample &sample) {
  std::cout << "VSync runtime " << label << ": accepted_fps="
            << sample.frames_per_second()
            << ", cpu_submission_ms=" << sample.cpu_submission_ms
            << ", accepted_frames=" << sample.accepted_frames
            << ", sample_seconds=" << sample.elapsed_seconds;
  if (sample.final_renderer_stats.gpu_frame_ms_available) {
    std::cout << ", gpu_frame_ms="
              << sample.final_renderer_stats.gpu_frame_ms;
  }
  std::cout << '\n';
}

/// Samples actual native presentation, including the swapchain transfer function.
/// Albedo diagnostics make production material patches spatially uniform before
/// post-processing, so lighting, textures and geometry cannot masquerade as grain.
class ColorRuntimeHarness final {
public:
  static constexpr std::array<float, 6> kPatchValues = {
      0.0f, 0.0003f, 0.001f, 0.005f, 0.02f, 0.18f};

  ~ColorRuntimeHarness() {
    view_.shutdown();
    bridge_.shutdown();
    surface_.reset();
    renderer_.shutdown();
  }

  bool initialize() {
    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
      return false;
    }
    window_.setFlags(Qt::Tool | Qt::WindowDoesNotAcceptFocus |
                     Qt::WindowStaysOnTopHint);
    window_.setTitle(QStringLiteral("Quader Vulkan dark color regression"));
    window_.setSurfaceType(QSurface::RasterSurface);
    window_.resize(384, 256);
    window_.setPosition(screen->availableGeometry().topLeft() + QPoint(32, 32));
    window_.show();
    QCoreApplication::processEvents();

    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    if (!renderer_.initialize(nullptr, resources)) {
      return false;
    }
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    for (std::size_t index = 0u; index < kPatchValues.size(); ++index) {
      const float center = patch_center(index);
      const auto mesh = api.modeling().create_box(
          QuaderBounds{.min = {center - 0.6f, -0.1f, -1.0f},
                       .max = {center + 0.6f, 0.0f, 1.0f}},
          "Dithering patch " + std::to_string(index));
      if (!mesh.ok()) {
        return false;
      }
      const float value = kPatchValues[index];
      const auto material = api.assets().add_material(QuaderMaterialAsset{
          .name = "Dithering material " + std::to_string(index),
          .base_color = {value, value, value},
          .roughness = 1.0f});
      if (!material.ok() ||
          !api.assets()
               .assign_material_to_entity(mesh.value().entity,
                                          material.value().id)
               .ok()) {
        return false;
      }
    }
    renderer_.initialize_scene_render_bridge(bridge_, resources);
    bridge_.sync(session_.scene(), session_.assets());
    settings_.environment.enabled = false;
    settings_.environment.show_background = false;
    settings_.environment.clear_color = {};
    settings_.camera.use_exposure = false;
    settings_.post_process.ambient_occlusion = false;
    settings_.post_process.bloom = false;
    renderer_.apply_scene_settings(settings_);

    const float pixel_width = static_cast<float>(
        qRound(window_.width() * window_.devicePixelRatio()));
    const float pixel_height = static_cast<float>(
        qRound(window_.height() * window_.devicePixelRatio()));
    camera_.set_viewport_size(pixel_width, pixel_height);
    camera_.view_axis(ViewportViewAxis::Y, 1);
    camera_.focus_origin(6.0f);
    return true;
  }

  bool select_surface(bool srgb) {
    view_.shutdown();
    surface_.reset();
    surface_ = renderer_.create_additional_swap_chain(
        reinterpret_cast<void *>(window_.winId()),
        srgb ? SwapChainPresentationColorSpace::Srgb
             : SwapChainPresentationColorSpace::Native);
    if (!surface_) {
      return false;
    }
    renderer_.initialize_viewport_renderer(
        view_, viewport_grid_layer_mask(0u),
        srgb ? ViewportRenderSurface::SrgbSwapchain
             : ViewportRenderSurface::Swapchain);
    const QVec2 size = camera_.viewport_size();
    view_.resize(static_cast<std::uint32_t>(size.x),
                 static_cast<std::uint32_t>(size.y));
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    view_.set_overlay_visible(false);
    view_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    view_.set_shadowing_enabled(false);
    view_.set_material_debug_mode(ViewportMaterialDebugMode::Albedo);
    return true;
  }

  QImage capture(SceneToneMapping tone_mapping, bool fxaa, bool dithering) {
    settings_.post_process.tone_mapping = tone_mapping;
    settings_.post_process.anti_aliasing = fxaa;
    settings_.post_process.dithering = dithering;
    view_.apply_scene_settings(settings_, true, ViewportSettings::defaults());
    std::cout << "Vulkan color scene settings: tone_mapping="
              << (tone_mapping == SceneToneMapping::Linear ? "Linear" : "ACES")
              << ", use_exposure=" << settings_.camera.use_exposure
              << ", effective_exposure_scale=1, aperture="
              << settings_.camera.aperture
              << ", shutter_seconds=" << settings_.camera.shutter_speed
              << ", ISO=" << settings_.camera.sensitivity
              << ", post_processing=" << settings_.post_process.enabled
              << ", FXAA=" << settings_.post_process.anti_aliasing
              << ", dithering=" << settings_.post_process.dithering
              << ", ambient_occlusion="
              << settings_.post_process.ambient_occlusion
              << ", bloom=" << settings_.post_process.bloom << '\n';

    // Let queued Vulkan work and DWM presentation settle before the OS capture.
    // The bounded event loop retains the application's native presentation path.
    QEventLoop loop;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    std::size_t frames = 0u;
    QObject::connect(&timer, &QTimer::timeout, &loop, [&]() {
      renderer_.begin_presentation_frame();
      const bool accepted = renderer_.begin_frame(surface_);
      if (accepted) {
        renderer_.render_viewport(view_, surface_);
        renderer_.end_frame(surface_);
        ++frames;
      }
      renderer_.complete_presentation_frame(accepted ? 1u : 0u);
      if (frames >= 12u) {
        timer.stop();
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
      }
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    timer.start(16);
    loop.exec();
    if (frames < 12u || window_.screen() == nullptr) {
      return {};
    }
    // A Vulkan surface has no GDI backing pixels. Capturing its HWND reads an
    // empty legacy DC; capture the composed desktop area occupied by this window.
    // QScreen takes screen-local logical coordinates and returns device pixels.
    QScreen *screen = window_.screen();
    const QPoint screen_origin =
        window_.mapToGlobal(QPoint(0, 0)) - screen->geometry().topLeft();
    return screen
        ->grabWindow(0, screen_origin.x(), screen_origin.y(), window_.width(),
                     window_.height())
        .toImage()
        .convertToFormat(QImage::Format_RGB32);
  }

  QRect patch_region(const QImage &image, std::size_t index) const {
    const ViewportScreenPoint center =
        camera_.project_world_point({patch_center(index), 0.0f, 0.0f});
    if (!center.visible) {
      return {};
    }
    const QVec2 size = camera_.viewport_size();
    const int x = qRound(center.position.x * image.width() / size.x);
    const int y = qRound(center.position.y * image.height() / size.y);
    // Stay well inside each planar face even at the smallest supported UI scale.
    const QRect region(x - 8, y - 8, 16, 16);
    return image.rect().contains(region) ? region : QRect{};
  }

private:
  static float patch_center(std::size_t index) {
    return -3.75f + static_cast<float>(index) * 1.5f;
  }

  QWindow window_;
  SceneSession session_;
  RenderSystem renderer_;
  RenderAdditionalSwapChain surface_;
  SceneRenderBridge bridge_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  SceneSettings settings_;
};

/// Exercises production scene resources and viewport shading without a native
/// window. Readable swapchain pixels are encoded sRGB, matching the Godot probe.
class LightCalibrationRuntimeHarness final {
public:
  static constexpr std::uint32_t kWidth = 384u;
  static constexpr std::uint32_t kHeight = 256u;

  ~LightCalibrationRuntimeHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(5'000'000'000ULL);
    engine_->pumpMessageQueues();
    view_.shutdown();
    bridge_.shutdown();
    registry_.clear();
    materials_.shutdown();
    if (opaque_lit_material_) engine_->destroy(opaque_lit_material_);
    if (indirect_light_) {
      scene_->setIndirectLight(nullptr);
      engine_->destroy(indirect_light_);
    }
    if (solid_color_texture_) engine_->destroy(solid_color_texture_);
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    if (scene_) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize(std::uint32_t backing_width = kWidth,
                  std::uint32_t backing_height = kHeight,
                  bool opaque_ao_fixture = false) {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    backing_width_ = std::max(backing_width, kWidth);
    backing_height_ = std::max(backing_height, kHeight);
    swapchain_ = engine_->createSwapChain(
        backing_width_, backing_height_, filament::SwapChain::CONFIG_READABLE |
                            filament::SwapChain::CONFIG_HAS_STENCIL_BUFFER);
    if (!scene_ || !renderer_ || !swapchain_ ||
        !materials_.initialize(*engine_, resources)) return false;
    filament::Material *lit_material = &materials_.quader_mesh_lit();
    if (opaque_ao_fixture) {
      // Production's transparent blend mode deliberately bypasses Filament
      // SSAO. This opaque test package retains the production surface shader.
      const auto package = MaterialCompilerService::load_package(
          std::filesystem::path(QUADER_TEST_OPAQUE_LIT_MATERIAL_PATH));
      opaque_lit_material_ = filament::Material::Builder()
                                 .package(package.data(), package.size())
                                 .build(*engine_);
      if (opaque_lit_material_ == nullptr) return false;
      lit_material = opaque_lit_material_;
    }
    solid_color_texture_ = create_test_solid_color_texture(*engine_);
    if (solid_color_texture_ == nullptr) return false;
    renderer_->setClearOptions({.clearColor = {0, 0, 0, 1}, .clear = true});
    bridge_.initialize(
        *engine_, *scene_, materials_.quader_mesh(),
        *lit_material, materials_.vertex_color_overlay(),
        materials_.instance_selection_fill(), materials_.overlay_line(),
        materials_.selected_object_line(), materials_.edit_wire_line(),
        materials_.overlay_point(), materials_.overlay_icon(),
        *solid_color_texture_, resources);
    view_.initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
                     materials_.overlay_stencil_reset(),
                     viewport_grid_layer_mask(0u),
                     ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(kWidth, kHeight);
    camera_.view_axis(ViewportViewAxis::Y, 1);
    camera_.focus_origin(6.0f);
    camera_.set_clip_planes(0.05f, 60.0f);
    view_.resize(kWidth, kHeight);
    view_.set_presentation_bounds(backing_width_, backing_height_, 0u, 0u);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    view_.set_overlay_visible(false);
    view_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    view_.set_shadowing_enabled(false);
    view_.set_material_debug_mode(ViewportMaterialDebugMode::None);
    settings_.environment.enabled = false;
    settings_.environment.show_background = false;
    settings_.environment.clear_color = {};
    settings_.post_process.anti_aliasing = false;
    settings_.post_process.dithering = false;
    settings_.post_process.ambient_occlusion = false;
    settings_.post_process.bloom = false;
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto mesh = api.modeling().create_box(
        QuaderBounds{.min = {-0.6f, -0.1f, -1.0f},
                     .max = {0.6f, 0.0f, 1.0f}}, "Calibration patch");
    if (!mesh.ok()) return false;
    patch_ = mesh.value().entity;
    for (std::size_t index = 0; index < kAlbedos.size(); ++index) {
      const float albedo = kAlbedos[index];
      const auto material = api.assets().add_material(QuaderMaterialAsset{
          .name = "Calibration material " + std::to_string(index),
          .base_color = {albedo, albedo, albedo}, .roughness = 1.0f});
      if (!material.ok()) return false;
      patch_materials_[index] = material.value().id;
    }
    QuaderCreatePointLightArgs args;
    args.name = "Calibration light";
    args.transform.location = {0, 3, 0};
    const auto light = api.scene().create_point_light(args);
    if (!light.ok()) {
      ADD_FAILURE() << "Creating calibration light: " << light.error_message();
      return false;
    }
    light_ = light.value().id;
    return true;
  }

  bool set_sample(LightType shape, float intensity, std::size_t albedo,
                  MaterialDiffuseModel diffuse_model = MaterialDiffuseModel::Burley,
                  float roughness = 1.0f, float metallic = 0.0f) {
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    LightComponent light;
    light.type = shape;
    light.intensity = intensity;
    light.range = 12.0f;
    light.distance_attenuation = 1.0f;
    const auto changed =
        api.scene().set_light_component(light_, to_api_light_component(light));
    if (!changed.ok()) {
      ADD_FAILURE() << "Setting calibration light: " << changed.error_message();
      return false;
    }
    const auto material_changed = session_.asset_commands().update_material(
        patch_materials_[albedo], [=](MaterialDocument &material) {
          material.diffuse_model = diffuse_model;
          material.roughness = roughness;
          material.metallic = metallic;
        });
    if (!material_changed.ok) {
      ADD_FAILURE() << "Updating calibration material: "
                    << material_changed.message;
      return false;
    }
    const auto assigned =
        api.assets().assign_material_to_entity(patch_, patch_materials_[albedo]);
    if (!assigned.ok()) {
      ADD_FAILURE() << "Assigning calibration material: " << assigned.error_message();
      return false;
    }
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  bool prepare_ambient_occlusion_scene() {
    if (!set_sample(LightType::Point, 0.0f, 1u)) return false;
    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    if (!api.scene().set_transform(patch_, {.scale = {5.0f, 1.0f, 3.0f}}).ok()) {
      return false;
    }
    const auto occluder = api.modeling().create_box(
        QuaderBounds{.min = {-0.65f, 0.0f, -0.65f},
                     .max = {0.65f, 1.4f, 0.65f}}, "AO contact occluder");
    if (!occluder.ok() ||
        !api.assets().assign_material_to_entity(
            occluder.value().entity, patch_materials_[0u]).ok()) return false;
    // SSAO attenuates indirect diffuse light. Direct-only calibration lighting
    // would make crop parity pass without exercising ambient occlusion.
    const filament::math::float3 irradiance{1.0f, 1.0f, 1.0f};
    indirect_light_ = filament::IndirectLight::Builder()
                          .irradiance(1u, &irradiance).intensity(30000.0f)
                          .build(*engine_);
    if (indirect_light_ == nullptr) return false;
    scene_->setIndirectLight(indirect_light_);
    settings_.post_process.enabled = true;
    settings_.post_process.ambient_occlusion_intensity = 2.0f;
    settings_.post_process.ambient_occlusion_radius = 0.8f;
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  void use_ambient_occlusion_view(ViewportProjection projection) {
    camera_.view_axis(ViewportViewAxis::Y, 1);
    camera_.focus_origin(6.0f);
    if (projection == ViewportProjection::Perspective) {
      camera_.unlock_orthographic_view();
    }
    camera_.set_view_direction({-0.45f, -0.65f, -0.6f});
    view_.update_camera(camera_);
  }

  QImage capture_ambient_occlusion(bool enabled) {
    settings_.post_process.ambient_occlusion = enabled;
    return capture(SceneToneMapping::Linear, {}, true);
  }

  void set_ambient_occlusion_crop(std::uint32_t crop_top) {
    crop_top_ = std::min(crop_top, backing_height_ - kHeight);
    view_.set_presentation_bounds(backing_width_, backing_height_, 0u, crop_top_);
    view_.update_camera(camera_);
  }

  QImage capture(SceneToneMapping tone, SceneCameraSettings camera = {},
                 bool screen_order = false) {
    settings_.camera = camera;
    settings_.post_process.tone_mapping = tone;
    view_.apply_scene_settings(settings_, true, ViewportSettings::defaults());
    for (int frame = 0; frame < 6; ++frame) {
      if (!begin_frame()) return {};
      view_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    view_.render(*renderer_);
    auto readback = std::make_shared<Readback>(backing_width_, backing_height_);
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        0, 0, backing_width_, backing_height_,
        filament::backend::PixelBufferDescriptor(
            readback->bytes.data(), readback->bytes.size(),
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
      if (!engine_->flushAndWait(1'000'000'000ULL)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    const QImage image(readback->bytes.data(), backing_width_, backing_height_,
                       QImage::Format_RGBA8888);
    // Vulkan's windowless readback rows are in camera-screen order even though
    // readPixels addresses rectangles from the lower left. Retain the existing
    // calibration export orientation; crop AO images in physical screen order.
    if (screen_order) return image.copy(0, crop_top_, kWidth, kHeight);
    return image.mirrored(false, true);
  }

  const ViewportCameraController &camera() const { return camera_; }
  QRect region() const { return QRect(kWidth / 2 - 8, kHeight / 2 - 8, 16, 16); }
  /// Retains orthographic framing while exposing rough diffuse grazing response.
  void use_grazing_view() {
    camera_.set_view_direction({0.0f, -0.2f, -std::sqrt(0.96f)});
    view_.update_camera(camera_);
  }
  static constexpr std::array<float, 3> kAlbedos = {0.18f, 0.8f, 0.005f};

private:
  struct Readback {
    Readback(std::uint32_t width, std::uint32_t height)
        : bytes(static_cast<std::size_t>(width) * height * 4u) {}
    std::vector<unsigned char> bytes;
    std::atomic<bool> complete = false;
  };

  bool begin_frame() {
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) return true;
      if (!engine_->flushAndWait(1'000'000'000ULL)) return false;
      engine_->pumpMessageQueues();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  SceneSession session_;
  EntityId patch_ = kInvalidEntityId;
  EntityId light_ = kInvalidEntityId;
  std::array<AssetId, kAlbedos.size()> patch_materials_{};
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *solid_color_texture_ = nullptr;
  filament::Material *opaque_lit_material_ = nullptr;
  filament::IndirectLight *indirect_light_ = nullptr;
  std::uint32_t backing_width_ = kWidth;
  std::uint32_t backing_height_ = kHeight;
  std::uint32_t crop_top_ = 0u;
  FilamentMaterialLibrary materials_;
  TextureRegistry registry_;
  SceneRenderBridge bridge_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  SceneSettings settings_;
};

struct ColorRuntimePatchStats {
  int minimum = 255;
  int maximum = 0;
  int maximum_channel_range = 0;
  double mean = 0.0;
  // Spatial deviation of the noisiest channel; a uniform ACES tint is not noise.
  double standard_deviation = 0.0;
};

ColorRuntimePatchStats color_patch_stats(const QImage &image,
                                         const QRect &region) {
  ColorRuntimePatchStats result;
  std::array<int, 3> minima = {255, 255, 255};
  std::array<int, 3> maxima = {};
  std::array<double, 3> sums = {};
  std::array<double, 3> square_sums = {};
  std::size_t count = 0u;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const QRgb pixel = image.pixel(x, y);
      const std::array<int, 3> channels = {
          qRed(pixel), qGreen(pixel), qBlue(pixel)};
      for (std::size_t channel = 0u; channel < channels.size(); ++channel) {
        const int value = channels[channel];
        minima[channel] = std::min(minima[channel], value);
        maxima[channel] = std::max(maxima[channel], value);
        sums[channel] += value;
        square_sums[channel] += static_cast<double>(value) * value;
      }
      ++count;
    }
  }
  if (count != 0u) {
    for (std::size_t channel = 0u; channel < sums.size(); ++channel) {
      const double mean = sums[channel] / static_cast<double>(count);
      const double deviation = std::sqrt(std::max(
          0.0, square_sums[channel] / static_cast<double>(count) - mean * mean));
      result.minimum = std::min(result.minimum, minima[channel]);
      result.maximum = std::max(result.maximum, maxima[channel]);
      result.maximum_channel_range = std::max(
          result.maximum_channel_range, maxima[channel] - minima[channel]);
      result.mean += mean / static_cast<double>(sums.size());
      result.standard_deviation =
          std::max(result.standard_deviation, deviation);
    }
  }
  return result;
}

int color_patch_maximum_difference(const QImage &left, const QImage &right,
                                    const QRect &region) {
  int maximum = 0;
  for (int y = region.top(); y <= region.bottom(); ++y) {
    for (int x = region.left(); x <= region.right(); ++x) {
      const QRgb a = left.pixel(x, y);
      const QRgb b = right.pixel(x, y);
      maximum = std::max({maximum, std::abs(qRed(a) - qRed(b)),
                          std::abs(qGreen(a) - qGreen(b)),
                          std::abs(qBlue(a) - qBlue(b))});
    }
  }
  return maximum;
}

TEST(RenderVSyncRuntimeTests, NativeVulkanTogglePacingAndSurfaceLifecycle) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_PRESENTATION_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_PRESENTATION_TESTS=1 for the opt-in "
                    "test using temporary native Vulkan windows.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-vulkan-presentation-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  VSyncRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  RenderSystem &renderer = harness.renderer();
  EXPECT_TRUE(renderer.vsync_enabled());
  EXPECT_TRUE(renderer.presentation_is_synchronized());
  for (std::size_t index = 0u; index < 3u; ++index) {
    EXPECT_TRUE(harness.additional(index).needs_initial_frame());
  }

  const VSyncRuntimeSample synchronized = harness.sample(1u);
  EXPECT_GT(synchronized.accepted_frames, 0u);
  print_vsync_sample("on, one grid surface", synchronized);

  renderer.set_vsync_enabled(false);
  EXPECT_FALSE(renderer.vsync_enabled());
  const VSyncRuntimeSample unsynchronized = harness.sample(1u);
  EXPECT_GT(unsynchronized.accepted_frames, 0u);
  print_vsync_sample("off, same grid surface", unsynchronized);
  const bool immediate_supported = !renderer.presentation_is_synchronized();
  std::cout << "VSync runtime display_hz=" << harness.refresh_hz()
            << ", immediate_supported=" << immediate_supported;
  if (synchronized.frames_per_second() > 0.0) {
    std::cout << ", off_on_fps_ratio=" << unsynchronized.frames_per_second() /
                                            synchronized.frames_per_second();
  }
  std::cout << '\n';
  if (immediate_supported) {
    EXPECT_FALSE(renderer.presentation_is_synchronized());
  } else {
    std::cout << "VSync runtime: driver retained FIFO; off remains "
                 "display-paced.\n";
  }

  RenderAdditionalSwapChain moved(std::move(harness.additional(0u)));
  EXPECT_FALSE(harness.additional(0u));
  ASSERT_TRUE(moved);
  renderer.set_vsync_enabled(true);
  EXPECT_TRUE(renderer.presentation_is_synchronized());
  EXPECT_NE(moved.get(), nullptr);
  EXPECT_TRUE(moved.needs_initial_frame());
  harness.additional(0u) = std::move(moved);
  EXPECT_FALSE(moved);
  ASSERT_TRUE(harness.additional(0u));

  for (bool enabled : {true, false, true, false, true}) {
    renderer.set_vsync_enabled(enabled);
    EXPECT_EQ(renderer.vsync_enabled(), enabled);
    for (std::size_t index = 0u; index < 3u; ++index) {
      EXPECT_TRUE(harness.additional(index).needs_initial_frame());
    }
    const VSyncRuntimeSample four_surfaces = harness.sample(4u, 250);
    for (std::size_t submissions : four_surfaces.surface_submissions) {
      EXPECT_GT(submissions, 0u);
    }
    for (std::size_t index = 0u; index < 3u; ++index) {
      EXPECT_FALSE(harness.additional(index).needs_initial_frame());
    }
    EXPECT_EQ(renderer.presentation_is_synchronized(),
              enabled || !immediate_supported);
    renderer.set_vsync_enabled(enabled);
    for (std::size_t index = 0u; index < 3u; ++index) {
      EXPECT_FALSE(harness.additional(index).needs_initial_frame())
          << "an unchanged preference must preserve the rendered surface";
    }
  }

  harness.resize_surface(0u, 400, 300);
  harness.resize_surface(2u, 280, 220);
  renderer.set_vsync_enabled(false);
  const VSyncRuntimeSample resized = harness.sample(4u, 500);
  for (std::size_t submissions : resized.surface_submissions) {
    EXPECT_GT(submissions, 0u);
  }
  print_vsync_sample("off, four resized grid surfaces", resized);

  harness.shutdown_rendering();
  EXPECT_FALSE(renderer.initialized());
  for (std::size_t index = 0u; index < 3u; ++index) {
    EXPECT_FALSE(harness.additional(index));
    EXPECT_EQ(harness.additional(index).get(), nullptr);
    EXPECT_FALSE(harness.additional(index).needs_initial_frame());
    harness.additional(index).reset();
    harness.additional(index).reset();
  }
  EXPECT_TRUE(renderer.ready_for_next_frame());
}

TEST(RenderColorRuntimeTests, NativeVulkanDarkDitheringStaysWithinDisplayCodes) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_COLOR_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_COLOR_TESTS=1 for the opt-in "
                    "test capturing a temporary native Vulkan window.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-vulkan-color-test";
  char *argv[] = {application_name, nullptr};
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  ColorRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());

  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_COLOR_OUTPUT_DIR");
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output_directory));
  }
  constexpr std::array<SceneToneMapping, 2> kToneMappings = {
      SceneToneMapping::Linear, SceneToneMapping::Aces};
  // [native UNORM / native sRGB][tone mapping][FXAA][dithering].
  std::array<std::array<std::array<std::array<QImage, 2>, 2>, 2>, 2> images;
  std::array<bool, 2> changed_by_dithering{};
  for (std::size_t surface = 0u; surface < 2u; ++surface) {
    ASSERT_TRUE(harness.select_surface(surface != 0u));
    for (std::size_t tone = 0u; tone < kToneMappings.size(); ++tone) {
      for (std::size_t fxaa = 0u; fxaa < 2u; ++fxaa) {
        for (std::size_t dithering = 0u; dithering < 2u; ++dithering) {
          const std::string label =
              std::string(surface == 0u ? "unorm" : "srgb") + "-" +
              (tone == 0u ? "linear" : "aces") + "-fxaa-" +
              std::to_string(fxaa) + "-dither-" + std::to_string(dithering);
          SCOPED_TRACE(label);
          QImage &image = images[surface][tone][fxaa][dithering];
          image = harness.capture(kToneMappings[tone], fxaa != 0u,
                                  dithering != 0u);
          ASSERT_FALSE(image.isNull());
          if (!output_directory.isEmpty()) {
            ASSERT_TRUE(image.save(QDir(output_directory).filePath(
                QString::fromStdString(label) + QStringLiteral(".png"))));
          }
          for (std::size_t patch = 0u;
               patch < ColorRuntimeHarness::kPatchValues.size(); ++patch) {
            SCOPED_TRACE(patch);
            const QRect region = harness.patch_region(image, patch);
            ASSERT_FALSE(region.isEmpty());
            const ColorRuntimePatchStats stats = color_patch_stats(image, region);
            std::cout << "Vulkan color " << label << " patch=" << patch
                      << " linear_input="
                      << ColorRuntimeHarness::kPatchValues[patch]
                      << " min=" << stats.minimum << " max=" << stats.maximum
                      << " max_channel_range=" << stats.maximum_channel_range
                      << " mean=" << stats.mean
                      << " max_channel_standard_deviation="
                      << stats.standard_deviation;
            // Filament's triangular noise spans two final 8-bit display codes.
            // Applying that noise before sRGB encoding amplifies dark pixels
            // dramatically and fails both the range and on/off delta below.
            EXPECT_LE(stats.maximum_channel_range, dithering == 0u ? 1 : 2);
            if (patch == 0u) {
              EXPECT_LE(stats.maximum, dithering == 0u ? 0 : 1);
            } else if (patch + 1u == ColorRuntimeHarness::kPatchValues.size()) {
              EXPECT_GT(stats.mean, 32.0)
                  << "The sampled patches must be rendered, not black/occluded";
            } else if (patch == 4u) {
              EXPECT_GT(stats.mean, 1.0)
                  << "The dark gray patch must retain a nonzero signal";
            }
            if (dithering != 0u) {
              const QImage &off = images[surface][tone][fxaa][0];
              ASSERT_EQ(image.size(), off.size());
              const ColorRuntimePatchStats off_stats =
                  color_patch_stats(off, region);
              const int maximum_delta =
                  color_patch_maximum_difference(image, off, region);
              std::cout << " on_off_max_delta=" << maximum_delta;
              EXPECT_LE(maximum_delta, 2);
              EXPECT_LE(std::abs(stats.mean - off_stats.mean), 1.0);
              changed_by_dithering[surface] =
                  changed_by_dithering[surface] || maximum_delta != 0;
            }
            if (surface != 0u) {
              const QImage &reference = images[0][tone][fxaa][dithering];
              ASSERT_EQ(image.size(), reference.size());
              const ColorRuntimePatchStats reference_stats =
                  color_patch_stats(reference, region);
              const double reference_delta =
                  std::abs(stats.mean - reference_stats.mean);
              std::cout << " srgb_unorm_mean_delta=" << reference_delta;
              EXPECT_LE(reference_delta, 1.0)
                  << "Native sRGB presentation must match the encoded UNORM "
                     "reference without encoding the scene twice";
            }
            std::cout << '\n';
          }
        }
      }
    }
  }
  EXPECT_TRUE(changed_by_dithering[0])
      << "The UNORM reference must exercise dithering rather than bypass it";
  EXPECT_TRUE(changed_by_dithering[1])
      << "The production native sRGB path must keep dithering active";
}

TEST(RenderColorRuntimeTests,
     NativeVulkanReservedBackingCropPreservesAmbientOcclusion) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_LIGHTING_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_LIGHTING_TESTS=1 for the opt-in "
                    "test reading production offscreen Vulkan lighting.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-vulkan-ao-crop-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  LightCalibrationRuntimeHarness centered;
  LightCalibrationRuntimeHarness cropped;
  ASSERT_TRUE(centered.initialize(384u, 256u, true));
  ASSERT_TRUE(cropped.initialize(384u, 384u, true));
  ASSERT_TRUE(centered.prepare_ambient_occlusion_scene());
  ASSERT_TRUE(cropped.prepare_ambient_occlusion_scene());
  // Keep the sampled contacts farther from the crop boundary than the AO disk.
  const QRect region(64, 64, 256, 128);
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_COLOR_OUTPUT_DIR");
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output_directory));
  }
  constexpr std::array<ViewportProjection, 2u> kProjections = {
      ViewportProjection::Perspective, ViewportProjection::Orthographic};
  for (const auto projection : kProjections) {
    const QString label = projection == ViewportProjection::Perspective
                              ? QStringLiteral("perspective")
                              : QStringLiteral("orthographic");
    SCOPED_TRACE(label.toStdString());
    centered.use_ambient_occlusion_view(projection);
    cropped.use_ambient_occlusion_view(projection);
    cropped.set_ambient_occlusion_crop(0u);
    const QImage centered_off = centered.capture_ambient_occlusion(false);
    const QImage cropped_off = cropped.capture_ambient_occlusion(false);
    const QImage centered_on = centered.capture_ambient_occlusion(true);
    const QImage cropped_on = cropped.capture_ambient_occlusion(true);
    cropped.set_ambient_occlusion_crop(128u);
    const QImage bottom_off = cropped.capture_ambient_occlusion(false);
    const QImage bottom_on = cropped.capture_ambient_occlusion(true);
    for (const auto *image :
         {&centered_off, &cropped_off, &centered_on, &cropped_on,
          &bottom_off, &bottom_on}) {
      ASSERT_FALSE(image->isNull());
      ASSERT_EQ(image->size(), QSize(384, 256));
    }
    if (!output_directory.isEmpty()) {
      const QString prefix = output_directory + "/ao_crop_" + label;
      ASSERT_TRUE(centered_off.save(prefix + "_centered_off.png"));
      ASSERT_TRUE(cropped_off.save(prefix + "_cropped_off.png"));
      ASSERT_TRUE(centered_on.save(prefix + "_centered_on.png"));
      ASSERT_TRUE(cropped_on.save(prefix + "_cropped_on.png"));
      ASSERT_TRUE(bottom_off.save(prefix + "_bottom_off.png"));
      ASSERT_TRUE(bottom_on.save(prefix + "_bottom_on.png"));
    }
    std::size_t darkened_pixels = 0u;
    double raw_top_difference = 0.0;
    const auto brightness = [](QRgb pixel) {
      return (qRed(pixel) + qGreen(pixel) + qBlue(pixel)) / 3.0;
    };
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const double off = brightness(centered_off.pixel(x, y));
        const double on = brightness(centered_on.pixel(x, y));
        raw_top_difference +=
            std::abs(on - brightness(cropped_on.pixel(x, y)));
        if (off >= 16.0 && off <= 240.0 && off - on >= 4.0) {
          ++darkened_pixels;
        }
      }
    }
    // AO-off parity independently proves the larger backing's camera framing
    // and both readback crops before any SSAO comparison can pass.
    EXPECT_LE(color_patch_maximum_difference(centered_off, cropped_off, region),
              2);
    EXPECT_LE(color_patch_maximum_difference(centered_off, bottom_off, region),
              2);
    ASSERT_GE(darkened_pixels, 80u)
        << "The lit contact fixture must visibly darken when AO is enabled";
    std::cout << "Vulkan AO crop " << label.toStdString()
              << " darkened_pixels=" << darkened_pixels
              << " raw_top_mean_difference="
              << raw_top_difference /
                     static_cast<double>(region.width() * region.height())
              << std::endl;
    const auto sample_mean = [&](const QImage &image, int x, int y, int size) {
      double sum = 0.0;
      for (int row = y; row < y + size; ++row) {
        for (int column = x; column < x + size; ++column) {
          sum += brightness(image.pixel(column, row));
        }
      }
      return sum / static_cast<double>(size * size);
    };
    const auto compare_crop = [&](const QImage &image, const char *crop_name,
                                  int sample_size) {
      SCOPED_TRACE(crop_name);
      std::size_t sample_count = 0u;
      std::size_t contact_count = 0u;
      std::size_t contact_mismatches = 0u;
      double total_difference = 0.0;
      double contact_difference = 0.0;
      for (int y = region.top(); y <= region.bottom(); y += sample_size) {
        for (int x = region.left(); x <= region.right(); x += sample_size) {
          const double off = sample_mean(centered_off, x, y, sample_size);
          const double on = sample_mean(centered_on, x, y, sample_size);
          const double difference =
              std::abs(on - sample_mean(image, x, y, sample_size));
          ++sample_count;
          total_difference += difference;
          if (off >= 16.0 && off <= 240.0 && off - on >= 4.0) {
            ++contact_count;
            contact_difference += difference;
            if (difference > 4.0) ++contact_mismatches;
          }
        }
      }
      ASSERT_GT(contact_count, 0u);
      const double mean_difference = total_difference / sample_count;
      const double mean_contact_difference = contact_difference / contact_count;
      const double contact_mismatch_fraction =
          static_cast<double>(contact_mismatches) / contact_count;
      std::cout << "Vulkan AO crop " << label.toStdString() << ' ' << crop_name
                << " sample_size=" << sample_size
                << " contact_count=" << contact_count
                << " mean_difference=" << mean_difference
                << " mean_contact_difference=" << mean_contact_difference
                << " contact_mismatch_fraction=" << contact_mismatch_fraction
                << std::endl;
      // Compare actual AO contacts separately so unchanged background cannot
      // conceal an incorrect projection-dependent reconstruction.
      EXPECT_LE(mean_difference, 0.75);
      EXPECT_LE(mean_contact_difference, 1.0);
      EXPECT_LE(contact_mismatch_fraction, 0.02);
    };
    // SAO seeds its spiral from lower-left fragment coordinates. The bottom
    // crop aligns those coordinates with the ordinary visible-sized raster,
    // testing the opposite shift without changing the spiral sampling phase.
    compare_crop(bottom_on, "bottom_noise_aligned", 1);
    // The production top crop changes that phase by 64 half-resolution rows.
    // Compare local means at a smaller scale than the box/contact footprint,
    // retaining the same display-code bounds while cancelling sampling noise.
    compare_crop(cropped_on, "top_anchored_tiles", 16);
  }
}

TEST(RenderColorRuntimeTests,
     NativeVulkanSimpleLightsUseStableExposureAndExportGodotSamples) {
  if (qEnvironmentVariable("QUADER_RUN_VULKAN_LIGHTING_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_VULKAN_LIGHTING_TESTS=1 for the opt-in "
                    "test reading production offscreen Vulkan lighting.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-vulkan-lighting-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  LightCalibrationRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  const QString output_directory =
      qEnvironmentVariable("QUADER_VULKAN_COLOR_OUTPUT_DIR");
  if (!output_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(output_directory));
  }
  const auto json_vector = [](QVec3 value) {
    return QJsonArray{value.x, value.y, value.z};
  };
  const auto rgb_means = [](const QImage &image, QRect region, bool linear) {
    std::array<double, 3> result{};
    const double count = static_cast<double>(region.width() * region.height());
    for (int y = region.top(); y <= region.bottom(); ++y) {
      for (int x = region.left(); x <= region.right(); ++x) {
        const QRgb pixel = image.pixel(x, y);
        const std::array<int, 3> channels = {
            qRed(pixel), qGreen(pixel), qBlue(pixel)};
        for (std::size_t channel = 0; channel < channels.size(); ++channel) {
          double value = static_cast<double>(channels[channel]) / 255.0;
          if (linear) {
            value = value <= 0.04045 ? value / 12.92
                                     : std::pow((value + 0.055) / 1.055, 2.4);
          }
          result[channel] += value / count;
        }
      }
    }
    return result;
  };
  const QRect region = harness.region();
  const ViewportCameraPose pose = harness.camera().pose();
  QJsonObject report{
      {"width", static_cast<int>(LightCalibrationRuntimeHarness::kWidth)},
      {"height", static_cast<int>(LightCalibrationRuntimeHarness::kHeight)},
      {"orthographic_height", harness.camera().state().orthographic_size},
      {"camera_position", json_vector(pose.eye)},
      {"camera_target", json_vector(pose.target)},
      {"camera_up", json_vector(pose.up)},
      {"light_position", QJsonArray{0, 3, 0}},
      {"range", 12.0}, {"falloff", 1.0}, {"rows_flipped", false},
      {"backend", "Vulkan"}, {"desktop_input", false},
      {"desktop_capture", false},
      {"material_source_mesh", "calibration-receiver-burley"},
      {"fixture_scene_uuid", "13131313-1313-4313-8313-131313131313"}};
  const QString fixture_qproj =
      qEnvironmentVariable("QUADER_VULKAN_LIGHTING_FIXTURE_QPROJ");
  if (!fixture_qproj.isEmpty()) report["fixture_qproj"] = fixture_qproj;
  constexpr std::array<LightType, 3> kShapes = {
      LightType::Directional, LightType::Point, LightType::Spot};
  constexpr std::array<const char *, 3> kShapeNames = {
      "directional", "point", "spot"};
  constexpr std::array<SceneToneMapping, 2> kTones = {
      SceneToneMapping::Linear, SceneToneMapping::Agx};
  constexpr std::array<const char *, 2> kToneNames = {"linear", "agx"};
  constexpr std::array<float, 3> kIntensities = {1.0f, 16.0f, 32.0f};
  constexpr std::array<const char *, 3> kAlbedoNames = {"gray", "white", "dark"};
  QJsonArray samples;
  int exposure_invariance_cases = 0;
  std::array<QImage, kTones.size()> normal_burley;
  for (std::size_t shape = 0; shape < kShapes.size(); ++shape) {
    ASSERT_TRUE(harness.set_sample(kShapes[shape], 0.0f, 0));
    const QImage off = harness.capture(SceneToneMapping::Linear);
    ASSERT_FALSE(off.isNull());
    EXPECT_LE(color_patch_stats(off, region).maximum, 1)
        << kShapeNames[shape] << ": no light or IBL must render black";
    for (std::size_t tone = 0; tone < kTones.size(); ++tone) {
      for (std::size_t albedo = 0;
           albedo < LightCalibrationRuntimeHarness::kAlbedos.size(); ++albedo) {
        std::array<double, 3> linear_means{};
        std::array<double, 3> display_means{};
        for (std::size_t intensity = 0; intensity < kIntensities.size(); ++intensity) {
          const QString label = QStringLiteral("calibration-%1-%2-%3-%4")
              .arg(QString::fromLatin1(kShapeNames[shape]),
                   QString::fromLatin1(kToneNames[tone]))
              .arg(kIntensities[intensity], 0, 'f', 0)
              .arg(QString::fromLatin1(kAlbedoNames[albedo]));
          SCOPED_TRACE(label.toStdString());
          ASSERT_TRUE(harness.set_sample(kShapes[shape], kIntensities[intensity], albedo));
          const QImage image = harness.capture(kTones[tone]);
          ASSERT_FALSE(image.isNull());
          if (shape == 0 && albedo == 0 && intensity == 1) {
            normal_burley[tone] = image;
          }
          const QString filename = label + QStringLiteral(".png");
          if (!output_directory.isEmpty()) {
            ASSERT_TRUE(image.save(QDir(output_directory).filePath(filename)));
          }
          const auto srgb = rgb_means(image, region, false);
          const auto linear = rgb_means(image, region, true);
          const auto stats = color_patch_stats(image, region);
          display_means[intensity] = stats.mean;
          linear_means[intensity] = (linear[0] + linear[1] + linear[2]) / 3.0;
          const QString light_name = QStringLiteral("calibration-%1-%2")
              .arg(QString::fromLatin1(kShapeNames[shape]))
              .arg(kIntensities[intensity], 0, 'f', 0);
          samples.append(QJsonObject{
              {"shape", QString::fromLatin1(kShapeNames[shape])},
              {"view", "normal"},
              {"diffuse_model", "burley"},
              {"material_source_mesh", "calibration-receiver-burley"},
              {"roughness", 1.0}, {"metallic", 0.0},
              {"intensity", kIntensities[intensity]},
              {"albedo", LightCalibrationRuntimeHarness::kAlbedos[albedo]},
              {"tone_mapping", QString::fromLatin1(kToneNames[tone])},
              {"light_name", light_name}, {"native_png", filename},
              {"region", QJsonArray{region.x(), region.y(), region.width(), region.height()}},
              {"rgb_srgb", QJsonArray{srgb[0], srgb[1], srgb[2]}},
              {"rgb_linearized", QJsonArray{linear[0], linear[1], linear[2]}},
              {"minimum", stats.minimum}, {"maximum", stats.maximum}});
          std::cout << "Vulkan Simple lighting " << label.toStdString()
                    << " mean_srgb8=" << stats.mean
                    << " mean_linear=" << linear_means[intensity]
                    << " min=" << stats.minimum << " max=" << stats.maximum << '\n';
          if (albedo == 0 && intensity == 1) {
            SceneCameraSettings fast;
            fast.aperture = 64.0f;
            fast.shutter_speed = 1.0f / 25000.0f;
            fast.sensitivity = 10.0f;
            SceneCameraSettings disabled;
            disabled.use_exposure = false;
            disabled.aperture = 0.5f;
            disabled.shutter_speed = 60.0f;
            disabled.sensitivity = 204800.0f;
            SceneCameraSettings invalid;
            invalid.aperture = std::numeric_limits<float>::quiet_NaN();
            invalid.shutter_speed = -1.0f;
            invalid.sensitivity = std::numeric_limits<float>::infinity();
            for (const SceneCameraSettings &camera : {fast, disabled, invalid}) {
              const QImage unchanged = harness.capture(kTones[tone], camera);
              ASSERT_FALSE(unchanged.isNull());
              EXPECT_EQ(color_patch_maximum_difference(image, unchanged, region), 0)
                  << "Stored photographic settings must not change Simple lighting";
              ++exposure_invariance_cases;
            }
          }
        }
        if (albedo == 0) {
          EXPECT_GT(display_means[1], display_means[0] + 8.0);
          EXPECT_GT(display_means[2], display_means[1] + 4.0);
          if (tone == 0) {
            EXPECT_NEAR(linear_means[1], linear_means[0] * 16.0,
                        std::max(0.008, linear_means[1] * 0.1));
            EXPECT_NEAR(linear_means[2], linear_means[1] * 2.0,
                        std::max(0.004, linear_means[2] * 0.07));
          }
        }
      }
    }
  }
  const auto material_sample = [&](const char *view_name, QRect sample_region,
                                   std::size_t tone, std::size_t albedo,
                                   MaterialDiffuseModel model, float roughness,
                                   float metallic) -> QImage {
    const QString model_name = model == MaterialDiffuseModel::Burley
                                   ? QStringLiteral("burley")
                                   : QStringLiteral("lambert");
    const QString label = QStringLiteral("calibration-directional-%1-16-%2-%3-%4-r%5-m%6")
        .arg(QString::fromLatin1(kToneNames[tone]),
             QString::fromLatin1(kAlbedoNames[albedo]),
             QString::fromLatin1(view_name), model_name)
        .arg(roughness, 0, 'f', 1).arg(metallic, 0, 'f', 0);
    if (!harness.set_sample(LightType::Directional, 16.0f, albedo,
                            model, roughness, metallic)) return {};
    const QImage image = harness.capture(kTones[tone]);
    if (image.isNull()) return {};
    const QString filename = label + QStringLiteral(".png");
    if (!output_directory.isEmpty()) {
      EXPECT_TRUE(image.save(QDir(output_directory).filePath(filename)));
    }
    const auto srgb = rgb_means(image, sample_region, false);
    const auto linear = rgb_means(image, sample_region, true);
    const auto stats = color_patch_stats(image, sample_region);
    const ViewportCameraPose sample_pose = harness.camera().pose();
    EXPECT_GT(stats.mean, 32.0) << label.toStdString() << ": must sample the lit top face";
    if (albedo == 0) {
      EXPECT_LT(stats.maximum, 252)
          << label.toStdString() << ": gray diffuse comparisons must remain unclipped";
    }
    samples.append(QJsonObject{
        {"shape", "directional"}, {"view", QString::fromLatin1(view_name)},
        {"intensity", 16.0}, {"diffuse_model", model_name},
        {"material_source_mesh", QStringLiteral("calibration-receiver-") + model_name},
        {"roughness", roughness}, {"metallic", metallic},
        {"albedo", LightCalibrationRuntimeHarness::kAlbedos[albedo]},
        {"tone_mapping", QString::fromLatin1(kToneNames[tone])},
        {"light_name", "calibration-directional-16"}, {"native_png", filename},
        {"region", QJsonArray{sample_region.x(), sample_region.y(),
                               sample_region.width(), sample_region.height()}},
        {"camera_position", json_vector(sample_pose.eye)},
        {"camera_target", json_vector(sample_pose.target)},
        {"camera_up", json_vector(sample_pose.up)},
        {"orthographic_height", harness.camera().state().orthographic_size},
        {"rgb_srgb", QJsonArray{srgb[0], srgb[1], srgb[2]}},
        {"rgb_linearized", QJsonArray{linear[0], linear[1], linear[2]}},
        {"minimum", stats.minimum}, {"maximum", stats.maximum}});
    std::cout << "Vulkan material " << label.toStdString()
              << " mean_srgb8=" << stats.mean
              << " mean_linear=" << (linear[0] + linear[1] + linear[2]) / 3.0
              << " min=" << stats.minimum << " max=" << stats.maximum << '\n';
    return image;
  };
  for (std::size_t tone = 0; tone < kTones.size(); ++tone) {
    const QImage lambert = material_sample("normal", region, tone, 0,
                                           MaterialDiffuseModel::Lambert, 1.0f, 0.0f);
    ASSERT_FALSE(lambert.isNull());
    EXPECT_LE(color_patch_maximum_difference(normal_burley[tone], lambert, region), 1)
        << "The perpendicular diffuse response is unchanged by the model";
  }
  // Grazing samples expose both model selection and perceptual roughness. Each
  // comparison edits the same native material; returning to Burley must restore
  // its pixels exactly, proving the retained uniform follows authored changes.
  harness.use_grazing_view();
  const QRect grazing_region(LightCalibrationRuntimeHarness::kWidth / 2 - 4,
                             LightCalibrationRuntimeHarness::kHeight / 2 - 2,
                             8, 4);
  int material_round_trip_cases = 0;
  int metallic_invariance_cases = 0;
  for (std::size_t tone = 0; tone < kTones.size(); ++tone) {
    for (float metallic : {0.0f, 1.0f}) {
      for (float roughness : {0.5f, 1.0f}) {
        for (std::size_t albedo = 0; albedo < 2; ++albedo) {
          if (metallic == 1.0f && (roughness != 0.5f || albedo != 1)) continue;
          const QImage burley = material_sample("grazing", grazing_region, tone, albedo,
                                                 MaterialDiffuseModel::Burley,
                                                 roughness, metallic);
          ASSERT_FALSE(burley.isNull());
          const QImage lambert = material_sample("grazing", grazing_region, tone, albedo,
                                                  MaterialDiffuseModel::Lambert,
                                                  roughness, metallic);
          ASSERT_FALSE(lambert.isNull());
          if (metallic == 1.0f) {
            EXPECT_EQ(color_patch_maximum_difference(burley, lambert, grazing_region), 0)
                << "A fully metallic surface has no diffuse contribution";
            ++metallic_invariance_cases;
          } else if (roughness == 1.0f && albedo == 0) {
            EXPECT_GT(color_patch_stats(burley, grazing_region).mean,
                      color_patch_stats(lambert, grazing_region).mean + 2.0)
                << "Rough Burley diffuse must show its grazing response";
          }
          ASSERT_TRUE(harness.set_sample(LightType::Directional, 16.0f, albedo,
                                         MaterialDiffuseModel::Burley, roughness, metallic));
          const QImage restored = harness.capture(kTones[tone]);
          ASSERT_FALSE(restored.isNull());
          EXPECT_EQ(color_patch_maximum_difference(burley, restored, grazing_region), 0)
              << "Changing Lambert back to Burley must restore the same material pixels";
          ++material_round_trip_cases;
        }
      }
    }
  }
  EXPECT_EQ(samples.size(), 76);
  report["samples"] = samples;
  report["exposure_invariance_cases"] = exposure_invariance_cases;
  report["material_round_trip_cases"] = material_round_trip_cases;
  report["metallic_invariance_cases"] = metallic_invariance_cases;
  report["native_assertions_passed"] = !::testing::Test::HasFailure();
  if (!output_directory.isEmpty()) {
    QFile output(QDir(output_directory).filePath(QStringLiteral("calibration.json")));
    ASSERT_TRUE(output.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    ASSERT_EQ(output.write(json), json.size());
  }
}

} // namespace
} // namespace quader
