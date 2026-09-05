////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app_frame_schedule.hpp"
#include "render/render_layers.hpp"
#include "render/render_system.hpp"
#include "render/viewport_renderer.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
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

} // namespace
} // namespace quader
