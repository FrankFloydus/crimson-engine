////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_schedule.hpp"
#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "render/render_layers.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>

namespace quader {
namespace {

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
    view_.apply_scene_settings(settings_, true);
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
  SceneSettingsComponent settings_;
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

} // namespace
} // namespace quader
