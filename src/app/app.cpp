////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/project_asset_bootstrap_service.hpp"
#include "platform/platform_file_dialog.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QMainWindow>
#include <QRect>
#include <QScreen>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kIdleFrameIntervalMs = 16;
constexpr double kFallbackDisplayRefreshRateHz = 60.0;
constexpr double kMinimumDisplayRefreshRateHz = 30.0;
constexpr double kMaximumDisplayRefreshRateHz = 240.0;

int presentation_frame_interval_ms(const QMainWindow *window) {
  const QScreen *screen =
      window != nullptr ? window->screen() : QGuiApplication::primaryScreen();
  double refresh_rate =
      screen != nullptr ? screen->refreshRate() : kFallbackDisplayRefreshRateHz;
  if (!std::isfinite(refresh_rate) || refresh_rate <= 0.0) {
    refresh_rate = kFallbackDisplayRefreshRateHz;
  }
  refresh_rate = std::clamp(refresh_rate, kMinimumDisplayRefreshRateHz,
                            kMaximumDisplayRefreshRateHz);
  return std::max(
      1, static_cast<int>(std::lround(1000.0 / refresh_rate)));
}

std::filesystem::path
default_project_save_file_name(const QuaderProjectApi &project) {
  std::string name = project.metadata().name.empty() ? "Untitled Project"
                                                     : project.metadata().name;
  name.append(".qdr");
  return std::filesystem::path(std::move(name));
}

std::filesystem::path
default_obj_export_file_name(const QuaderProjectApi &project) {
  std::string name = project.metadata().name.empty() ? "Selection"
                                                     : project.metadata().name;
  name.append(".obj");
  return std::filesystem::path(std::move(name));
}

PlatformFileDialogOptions
project_file_dialog_options(void *owner_window_handle,
                            const QuaderProjectApi &project) {
  PlatformFileDialogOptions options;
  options.owner_window_handle = owner_window_handle;
  const std::filesystem::path &current_path = project.path();
  if (!current_path.empty()) {
    options.initial_directory = current_path.parent_path();
    options.initial_file_name = current_path.filename();
  } else {
    options.initial_file_name = default_project_save_file_name(project);
  }
  return options;
}

PlatformFileDialogOptions
obj_export_file_dialog_options(void *owner_window_handle,
                               const QuaderProjectApi &project) {
  PlatformFileDialogOptions options;
  options.owner_window_handle = owner_window_handle;
  const std::filesystem::path &current_path = project.path();
  if (!current_path.empty()) {
    options.initial_directory = current_path.parent_path();
  }
  options.initial_file_name = default_obj_export_file_name(project);
  return options;
}

} // namespace

App::App() = default;

App::~App() { shutdown(); }

bool App::initialize(QApplication &application,
                     const std::filesystem::path &executable_path,
                     AppOptions options) {
  shutdown_complete_ = false;
  running_ = false;
  application_ = &application;
  const std::filesystem::path executable_dir = executable_path.parent_path();
  window_settings_path_ = AppWindowSettings::resolve_path(executable_dir);
  window_settings_ = AppWindowSettings::load(window_settings_path_);

  if (!render_system_.initialize(
          nullptr, executable_dir,
          RenderConfig{.backend = options.render_backend})) {
    LOG_ERROR("Renderer initialization failed");
    shutdown();
    return false;
  }

  LOG_INFO("Initializing scene render bridge");
  render_system_.initialize_scene_render_bridge(render_bridge_,
                                                executable_dir);
  LOG_INFO("Scene render bridge initialized");

  LOG_INFO("Initializing Qt Widgets UI system");
  if (!ui_.initialize(render_system_, executable_dir)) {
    LOG_ERROR("Qt Widgets UI initialization failed");
    shutdown();
    return false;
  }

  const ProjectDefaultAssets default_assets =
      ProjectAssetBootstrapService{}.bootstrap_default_assets(project_session_);
  default_material_ = default_assets.default_material;
  editor_ = std::make_unique<QuaderEditorApi>(
      QuaderApiSessionInternalAccess::from_project_session(project_session_));
  ui_.install_runtime_hooks(
      *editor_, project_session_.scene(), project_session_.assets(),
      project_session_.modeling(), project_session_.selection(),
      project_session_.pivot(), project_session_.linear_array(),
      default_material_);
  editor_->project().set_runtime_hooks(QuaderProjectRuntimeHooks{
      .choose_open_path =
          [this]() {
            return platform_open_project_file_dialog(
                project_file_dialog_options(ui_.native_window_handle(),
                                            editor_->project()));
          },
      .choose_save_as_path =
          [this]() {
            return platform_save_project_file_dialog(
                project_file_dialog_options(ui_.native_window_handle(),
                                            editor_->project()));
          },
      .choose_export_obj_path =
          [this]() {
            return platform_save_obj_file_dialog(
                obj_export_file_dialog_options(ui_.native_window_handle(),
                                               editor_->project()));
          },
  });

  apply_window_settings();
  ui_.show();
  frame_timer_ = std::make_unique<QTimer>();
  frame_timer_->setTimerType(Qt::PreciseTimer);
  QObject::connect(frame_timer_.get(), &QTimer::timeout, application_,
                   [this]() {
                     if (running_ && !shutdown_complete_) {
                       (void)render_frame(frame_clock_.tick());
                       schedule_next_frame();
                     }
                   });
  QObject::connect(application_, &QApplication::aboutToQuit, application_,
                   [this]() { running_ = false; });
  running_ = true;
  return true;
}

int App::run() {
  if (application_ == nullptr || !running_) {
    return 1;
  }
  LOG_INFO("Entering Qt application event loop");
  frame_clock_.reset();
  frame_timer_->start(0);
  return application_->exec();
}

void App::shutdown() {
  if (shutdown_complete_) {
    return;
  }

  shutdown_complete_ = true;
  running_ = false;
  if (frame_timer_) {
    frame_timer_->stop();
  }
  save_window_settings();
  ui_.shutdown();
  frame_timer_.reset();
  render_overlay_snapshot_cache_.reset();
  editor_.reset();
  render_bridge_.shutdown();
  render_system_.shutdown();
  application_ = nullptr;
}

void App::apply_window_settings() {
  QMainWindow *window = ui_.main_window();
  if (window == nullptr) {
    return;
  }
  const std::optional<PlatformWindowPlacement> placement =
      window_settings_.placement();
  if (!placement.has_value()) {
    window->resize(1600, 960);
    return;
  }
  std::vector<PlatformWindowChromeFrame> work_areas;
  for (QScreen *screen : QGuiApplication::screens()) {
    if (screen == nullptr) {
      continue;
    }
    const QRect area = screen->availableGeometry();
    work_areas.push_back({
        .x = area.x(),
        .y = area.y(),
        .width = area.width(),
        .height = area.height(),
    });
  }
  const PlatformWindowChromeFrame frame =
      platform_window_recover_frame_to_display_work_area(
          placement->normal_frame, work_areas);
  window->setGeometry(frame.x, frame.y,
                      std::max(kPlatformWindowMinimumWidth, frame.width),
                      std::max(kPlatformWindowMinimumHeight, frame.height));
  if (placement->maximized) {
    // Configure placement while hidden so UISystem remains the sole owner of
    // mapping the composed shell and suppresses painting until the native
    // maximized state and retained dock layout have both stabilized.
    window->setWindowState(window->windowState() | Qt::WindowMaximized);
  }
}

void App::save_window_settings() {
  if (window_settings_path_.empty()) {
    return;
  }
  QMainWindow *window = ui_.main_window();
  if (window == nullptr) {
    return;
  }
  const QRect frame = window->normalGeometry();
  window_settings_.set_placement(PlatformWindowPlacement{
      .normal_frame =
          PlatformWindowChromeFrame{
              .x = frame.x(),
              .y = frame.y(),
              .width = frame.width(),
              .height = frame.height(),
          },
      .maximized = window->isMaximized(),
  });
  if (!window_settings_.save(window_settings_path_)) {
    LOG_WARN("Unable to save window settings to {}",
             window_settings_path_.string());
  }
}

bool App::render_frame(const FrameTiming &frame_timing) {
  QDR_PROFILE_FUNCTION();
  if (rendering_frame_ || !editor_) {
    return false;
  }
  const auto frame_begin = std::chrono::steady_clock::now();
  rendering_frame_ = true;

  ui_.refresh(frame_timing, render_system_.frame_performance_stats());
  const auto ui_refresh_end = std::chrono::steady_clock::now();
  {
    QDR_PROFILE_SCOPE("UI process viewport input");
    ui_.process_viewport_input();
  }
  const auto viewport_input_end = std::chrono::steady_clock::now();
  {
    QDR_PROFILE_SCOPE("Render sync scene");
    (void)QuaderRenderApiRuntimeAccess::sync_scene(editor_->render(),
                                                   render_bridge_);
  }
  const auto scene_sync_end = std::chrono::steady_clock::now();
  const ProjectSessionReadView session_view = project_session_.read_view();
  if (const EntityId root = session_view.scene.scene_root();
      root != kInvalidEntityId) {
    if (const SceneSettingsComponent *settings =
            session_view.scene.scene_settings(root)) {
      render_system_.apply_scene_settings(*settings);
    }
  }
  const auto scene_settings_end = std::chrono::steady_clock::now();
  ui_.prepare_viewports(render_bridge_, render_overlay_snapshot_cache_);
  const auto viewport_prepare_end = std::chrono::steady_clock::now();
  const std::size_t submitted = ui_.can_render_viewports()
                                    ? ui_.render_viewports()
                                    : 0u;
  const auto viewport_render_end = std::chrono::steady_clock::now();
  if (std::getenv("QUADER_FRAME_DIAGNOSTICS") != nullptr) {
    /// Accumulates opt-in frame-stage timings for periodic diagnostic logs.
    struct FrameDiagnostics {
      std::chrono::steady_clock::time_point sample_begin =
          std::chrono::steady_clock::now();
      std::size_t frames = 0u;
      std::size_t submitted = 0u;
      double total_ms = 0.0;
      double maximum_ms = 0.0;
      double ui_refresh_ms = 0.0;
      double viewport_input_ms = 0.0;
      double scene_sync_ms = 0.0;
      double scene_settings_ms = 0.0;
      double viewport_prepare_ms = 0.0;
      double viewport_render_ms = 0.0;
    };
    static FrameDiagnostics diagnostics;
    const auto now = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>(now - frame_begin).count();
    ++diagnostics.frames;
    diagnostics.submitted += submitted;
    diagnostics.total_ms += elapsed_ms;
    diagnostics.maximum_ms = std::max(diagnostics.maximum_ms, elapsed_ms);
    diagnostics.ui_refresh_ms +=
        std::chrono::duration<double, std::milli>(ui_refresh_end - frame_begin)
            .count();
    diagnostics.viewport_input_ms +=
        std::chrono::duration<double, std::milli>(viewport_input_end -
                                                 ui_refresh_end)
            .count();
    diagnostics.scene_sync_ms +=
        std::chrono::duration<double, std::milli>(scene_sync_end -
                                                 viewport_input_end)
            .count();
    diagnostics.scene_settings_ms +=
        std::chrono::duration<double, std::milli>(scene_settings_end -
                                                 scene_sync_end)
            .count();
    diagnostics.viewport_prepare_ms +=
        std::chrono::duration<double, std::milli>(viewport_prepare_end -
                                                 scene_settings_end)
            .count();
    diagnostics.viewport_render_ms +=
        std::chrono::duration<double, std::milli>(viewport_render_end -
                                                 viewport_prepare_end)
            .count();
    const double sample_seconds =
        std::chrono::duration<double>(now - diagnostics.sample_begin).count();
    if (sample_seconds >= 1.0) {
      const RenderFramePerformanceStats render_stats =
          render_system_.frame_performance_stats();
      LOG_INFO(
          "Frame diagnostics: {:.1f} app fps, {:.2f} ms average, {:.2f} ms "
          "maximum, {} submissions, stages ui={:.2f} input={:.2f} sync={:.2f} "
          "settings={:.2f} prepare={:.2f} render={:.2f} ms, renderer {:.1f} "
          "fps, CPU {:.2f} ms, GPU {:.2f} ms",
          static_cast<double>(diagnostics.frames) / sample_seconds,
          diagnostics.total_ms / static_cast<double>(diagnostics.frames),
          diagnostics.maximum_ms, diagnostics.submitted,
          diagnostics.ui_refresh_ms / static_cast<double>(diagnostics.frames),
          diagnostics.viewport_input_ms /
              static_cast<double>(diagnostics.frames),
          diagnostics.scene_sync_ms / static_cast<double>(diagnostics.frames),
          diagnostics.scene_settings_ms /
              static_cast<double>(diagnostics.frames),
          diagnostics.viewport_prepare_ms /
              static_cast<double>(diagnostics.frames),
          diagnostics.viewport_render_ms /
              static_cast<double>(diagnostics.frames),
          render_stats.frames_per_second, render_stats.cpu_frame_ms,
          render_stats.gpu_frame_ms);
      diagnostics = {};
    }
  }
  rendering_frame_ = false;
  QDR_PROFILE_FRAME_MARK();
  return submitted > 0u;
}

void App::schedule_next_frame() {
  if (!frame_timer_ || !running_) {
    return;
  }
  const int interval =
      ui_.needs_continuous_rendering()
          ? presentation_frame_interval_ms(ui_.main_window())
          : kIdleFrameIntervalMs;
  if (frame_timer_->interval() != interval) {
    frame_timer_->setInterval(interval);
  }
}

} // namespace quader
