////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"
#include "app/app_frame_presentation.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "editor/session/project_asset_bootstrap_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "assets/material_document.hpp"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMargins>
#include <QRect>
#include <QScreen>
#include <QTimer>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {

AppFramePresentationGate::AppFramePresentationGate(
    QApplication &application, QObject *parent)
    : QObject(parent), application_(&application) {
  application.installEventFilter(this);
}

AppFramePresentationGate::~AppFramePresentationGate() {
  if (application_ != nullptr) {
    application_->removeEventFilter(this);
  }
}

bool AppFramePresentationGate::blocks_frame_submission() const noexcept {
  return std::ranges::any_of(
      pending_transients_, [](const PendingTransient &pending) {
        return pending.widget != nullptr;
      });
}

bool AppFramePresentationGate::eventFilter(QObject *watched, QEvent *event) {
  if (watched == nullptr || event == nullptr) {
    return QObject::eventFilter(watched, event);
  }

  switch (event->type()) {
  case QEvent::Show: {
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (widget != nullptr && is_top_level_transient(*widget)) {
      begin_first_paint_wait(*widget);
    }
    break;
  }
  case QEvent::Paint: {
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (widget != nullptr) {
      queue_first_paint_release(*widget);
    }
    break;
  }
  case QEvent::Hide:
  case QEvent::Close:
  case QEvent::Destroy:
    forget_transient(watched);
    break;
  default:
    return QObject::eventFilter(watched, event);
  }
  return QObject::eventFilter(watched, event);
}

bool AppFramePresentationGate::is_top_level_transient(
    const QWidget &widget) {
  if (!widget.isWindow()) {
    return false;
  }
  switch (widget.windowType()) {
  case Qt::Dialog:
  case Qt::Popup:
  case Qt::Tool:
  case Qt::Sheet:
  case Qt::Drawer:
    return true;
  default:
    return false;
  }
}

void AppFramePresentationGate::begin_first_paint_wait(QWidget &widget) {
  forget_transient(&widget);
  pending_transients_.push_back(PendingTransient{
      .widget = &widget,
      .generation = next_generation_++,
      .release_queued = false,
  });
}

void AppFramePresentationGate::queue_first_paint_release(QWidget &widget) {
  auto pending = std::ranges::find_if(
      pending_transients_, [&widget](const PendingTransient &candidate) {
        return candidate.widget == &widget;
      });
  if (pending == pending_transients_.end() || pending->release_queued) {
    return;
  }

  pending->release_queued = true;
  const QPointer<QWidget> guarded_widget = pending->widget;
  const std::uint64_t generation = pending->generation;
  QTimer::singleShot(0, this, [this, guarded_widget, generation]() {
    release_if_current(guarded_widget.data(), generation);
  });
}

void AppFramePresentationGate::release_if_current(
    QWidget *widget, std::uint64_t generation) {
  std::erase_if(pending_transients_,
                [widget, generation](const PendingTransient &pending) {
                  return pending.widget == widget &&
                         pending.generation == generation;
                });
}

void AppFramePresentationGate::forget_transient(const QObject *widget) {
  std::erase_if(pending_transients_,
                [widget](const PendingTransient &pending) {
                  return pending.widget == nullptr ||
                         pending.widget.data() == widget;
                });
}

namespace {

constexpr double kFallbackDisplayRefreshRateHz = 60.0;
constexpr double kMinimumDisplayRefreshRateHz = 30.0;
constexpr double kMaximumDisplayRefreshRateHz = 240.0;

QScreen *active_display_screen(const QMainWindow *window) {
  if (window != nullptr) {
    if (QWindow *native_window = window->windowHandle();
        native_window != nullptr && native_window->screen() != nullptr) {
      return native_window->screen();
    }
    if (window->screen() != nullptr) {
      return window->screen();
    }
  }
  return QGuiApplication::primaryScreen();
}

float display_refresh_rate_hz(const QScreen *screen) {
  return screen != nullptr ? static_cast<float>(screen->refreshRate())
                           : static_cast<float>(kFallbackDisplayRefreshRateHz);
}

int presentation_frame_interval_ms(const QMainWindow *window) {
  const QScreen *screen = active_display_screen(window);
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
  }
  return options;
}

PlatformFileDialogOptions
project_folder_dialog_options(void *owner_window_handle,
                              const QuaderProjectApi &project) {
  PlatformFileDialogOptions options;
  options.owner_window_handle = owner_window_handle;
  if (!project.root().empty()) {
    options.initial_directory = project.root().parent_path();
  }
  return options;
}

PlatformFileDialogOptions
scene_file_dialog_options(void *owner_window_handle,
                          const QuaderProjectApi &project,
                          const QuaderSceneDocumentInfo *scene = nullptr) {
  PlatformFileDialogOptions options;
  options.owner_window_handle = owner_window_handle;
  if (project.root().empty()) {
    return options;
  }
  options.initial_directory = project.root() / "Scenes";
  if (scene == nullptr) {
    return options;
  }
  if (scene->path.has_value()) {
    const std::filesystem::path relative = scene->path->native_path();
    options.initial_directory = project.root() / relative.parent_path();
    options.initial_file_name = relative.filename();
  } else {
    options.initial_file_name =
        std::filesystem::path(scene->name + ".qdr");
  }
  return options;
}

PlatformFileDialogOptions
obj_export_file_dialog_options(void *owner_window_handle,
                               const QuaderProjectApi &project) {
  PlatformFileDialogOptions options;
  options.owner_window_handle = owner_window_handle;
  if (!project.root().empty()) {
    options.initial_directory = project.root();
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
  project_session_.set_resource_root(executable_dir);
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
  project_material_resolver_ =
      std::make_unique<ProjectSessionRenderMaterialResolver>(project_session_);
  render_bridge_.set_project_material_resolver(
      project_material_resolver_.get());
  LOG_INFO("Scene render bridge initialized");

  LOG_INFO("Initializing Qt Widgets UI system");
  if (!ui_.initialize(render_system_, executable_dir)) {
    LOG_ERROR("Qt Widgets UI initialization failed");
    shutdown();
    return false;
  }

  ui_.set_session_settings_changed_callback(
      [this](const SessionPreferences &settings) {
        project_session_.set_history_max_steps(
            static_cast<std::size_t>(settings.undo_steps));
      });
  project_session_.set_history_max_steps(
      static_cast<std::size_t>(ui_.session_settings().undo_steps));
  editor_ = std::make_unique<QuaderEditorApi>(project_session_);
  QuaderEditorApiSessionAccess::set_scene_binding_changed_callback(
      *editor_, [this](SceneSession &scene) { bind_active_scene(scene); });
  bind_active_scene(project_session_.fallback_scene());
  editor_->project().set_runtime_hooks(QuaderProjectRuntimeHooks{
      .choose_create_root =
          [this]() {
            return platform_choose_project_folder_dialog(
                project_folder_dialog_options(ui_.native_window_handle(),
                                              editor_->project()));
          },
      .choose_open_manifest_path =
          [this]() {
            return platform_open_project_file_dialog(
                project_file_dialog_options(ui_.native_window_handle(),
                                            editor_->project()));
          },
      .choose_export_obj_path =
          [this]() {
            return platform_save_obj_file_dialog(
                obj_export_file_dialog_options(ui_.native_window_handle(),
                                               editor_->project()));
          },
      .confirm_dirty_project_replacement =
          [this](std::string_view action_id) {
            return ui_.confirm_dirty_project_replacement(action_id);
          },
      .prepare_material_document_close =
          [this](std::string_view action_id) {
            return ui_.prepare_standard_material_project_transition(
                action_id);
          },
  });
  editor_->scene_documents().set_runtime_hooks(
      QuaderSceneDocumentsRuntimeHooks{
          .choose_open_scene_path =
              [this]() {
                return platform_open_scene_file_dialog(
                    scene_file_dialog_options(ui_.native_window_handle(),
                                              editor_->project()));
              },
          .choose_save_scene_as_path =
              [this](SceneSessionId id) {
                std::optional<QuaderSceneDocumentInfo> selected;
                for (const QuaderSceneDocumentInfo &scene :
                     editor_->scene_documents().open_scenes()) {
                  if (scene.session_id == id) {
                    selected = scene;
                    break;
                  }
                }
                return platform_save_scene_file_dialog(
                    scene_file_dialog_options(
                        ui_.native_window_handle(), editor_->project(),
                        selected.has_value() ? &*selected : nullptr));
              },
          .confirm_dirty_scene =
              [this](const QuaderSceneDocumentInfo &scene,
                     std::string_view action_id) {
                return ui_.confirm_dirty_scene(scene, action_id);
              },
      });

  apply_window_settings();
  ui_.show();
  install_display_refresh_rate_sync();
  frame_timer_ = std::make_unique<QTimer>();
  frame_timer_->setTimerType(Qt::PreciseTimer);
  auto *frame_presentation_gate =
      new AppFramePresentationGate(application, frame_timer_.get());
  QObject::connect(frame_timer_.get(), &QTimer::timeout, application_,
                   [this, frame_presentation_gate]() {
                     if (running_ && !shutdown_complete_) {
                       if (frame_presentation_gate
                               ->blocks_frame_submission()) {
                         // QDialog::exec() and QMenu::exec() keep this timer
                         // alive in a nested event loop. Yield only until each
                         // newly shown transient completes its first paint;
                         // live modal previews continue rendering afterward.
                         frame_clock_.reset();
                         schedule_next_frame();
                         return;
                       }
                       if (!render_system_.presentation_is_synchronized() &&
                           ui_.needs_continuous_rendering() &&
                           !render_system_.ready_for_next_frame()) {
                         schedule_next_frame(false);
                         return;
                       }
                       const bool submitted =
                           render_frame(frame_clock_.tick());
                       schedule_next_frame(submitted);
                     }
                   });
  QObject::connect(application_, &QApplication::aboutToQuit, frame_timer_.get(),
                   [this]() { running_ = false; });
  autosave_timer_ = std::make_unique<QTimer>();
  autosave_timer_->setTimerType(Qt::CoarseTimer);
  autosave_timer_->setInterval(1000);
  QObject::connect(autosave_timer_.get(), &QTimer::timeout, application_,
                   [this]() { poll_autosave(); });
  running_ = true;
  return true;
}

void App::poll_autosave() {
  if (!running_ || shutdown_complete_) {
    return;
  }
  // Autosave has its own coarse timer. Never serialize in the render loop or
  // while a native gesture, popup, or modal operation owns the interaction.
  const bool external_edit_active =
      rendering_frame_ || QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr ||
      QApplication::mouseButtons() != Qt::NoButton;
  const ProjectAutosaveResult result = project_session_.autosave().poll(
      project_session_, ui_.session_settings(),
      std::chrono::steady_clock::now(), external_edit_active);
  for (const std::string &warning : result.warnings) {
    LOG_WARN("Autosave: {}", warning);
    if (editor_ != nullptr) {
      (void)editor_->ui().notify(
          {.level = QuaderNotificationLevel::Warning, .message = warning});
    }
  }
}

int App::run() {
  if (application_ == nullptr || !running_) {
    return 1;
  }
  LOG_INFO("Entering Qt application event loop");
  frame_clock_.reset();
  frame_timer_->start(0);
  autosave_timer_->start();
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
  if (autosave_timer_) {
    autosave_timer_->stop();
  }
  ui_.set_session_settings_changed_callback({});
  QObject::disconnect(window_screen_changed_connection_);
  QObject::disconnect(screen_refresh_rate_changed_connection_);
  display_refresh_screen_ = nullptr;
  save_window_settings();
  ui_.shutdown();
  frame_timer_.reset();
  autosave_timer_.reset();
  render_overlay_snapshot_cache_.reset();
  editor_.reset();
  render_bridge_.shutdown();
  project_material_resolver_.reset();
  render_system_.shutdown();
  application_ = nullptr;
}

void App::bind_active_scene(SceneSession &scene) {
  default_material_ = kInvalidAssetId;
  if (!project_session_.has_project()) {
    const ProjectDefaultAssets defaults =
        ProjectAssetBootstrapService{}.bootstrap_default_assets(scene);
    default_material_ = defaults.default_material;
  }
  scene.set_document_replacement_finalizer(
      [this](SceneSession &replacement) { bind_active_scene(replacement); });
  render_overlay_snapshot_cache_.reset();
  if (editor_ != nullptr) {
    ui_.install_runtime_hooks(
        *editor_, scene.scene(), scene.assets(), scene.modeling(),
        scene.selection(), scene.native_tools(), scene.point_placement(),
        default_material_);
  }
}

void App::install_display_refresh_rate_sync() {
  QMainWindow *window = ui_.main_window();
  QScreen *screen = active_display_screen(window);
  synchronize_display_refresh_rate(screen);
  bind_display_refresh_rate_screen(screen);
  if (window == nullptr) {
    return;
  }

  QObject::disconnect(window_screen_changed_connection_);
  if (QWindow *native_window = window->windowHandle()) {
    window_screen_changed_connection_ =
        QObject::connect(native_window, &QWindow::screenChanged, window,
                         [this](QScreen *screen) {
                           synchronize_display_refresh_rate(screen);
                           bind_display_refresh_rate_screen(screen);
                         });
  }
}

void App::bind_display_refresh_rate_screen(QScreen *screen) {
  if (display_refresh_screen_ == screen) {
    return;
  }

  QObject::disconnect(screen_refresh_rate_changed_connection_);
  screen_refresh_rate_changed_connection_ = {};
  display_refresh_screen_ = screen;

  QMainWindow *window = ui_.main_window();
  if (window == nullptr || screen == nullptr) {
    return;
  }
  screen_refresh_rate_changed_connection_ =
      QObject::connect(screen, &QScreen::refreshRateChanged, window,
                       [this, screen](qreal) {
                         if (display_refresh_screen_ == screen) {
                           synchronize_display_refresh_rate(screen);
                         }
                       });
}

void App::synchronize_display_refresh_rate(QScreen *screen) {
  render_system_.set_display_refresh_rate_hz(
      display_refresh_rate_hz(screen != nullptr ? screen
                                                : active_display_screen(
                                                      ui_.main_window())));
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
  const PlatformWindowChromeFrame preliminary =
      platform_window_recover_frame_to_display_work_area(
          placement->normal_frame, work_areas);
  const QRect preliminary_client_rect(
      preliminary.x, preliminary.y, preliminary.width, preliminary.height);
  if (QScreen *target_screen =
          QGuiApplication::screenAt(preliminary_client_rect.center())) {
    window->setScreen(target_screen);
  }
  window->setGeometry(preliminary_client_rect);
  (void)window->winId();
  QWindow *native_window = window->windowHandle();
  QMargins margins;
  if (native_window != nullptr) {
    // Hidden QWidget geometry is deferred. Synchronize the native window first
    // so its frame margins reflect the saved monitor and its DPI, without
    // mapping the shell before UISystem::show().
    native_window->setGeometry(window->geometry());
    margins = native_window->frameMargins();
  }
  const PlatformWindowChromeFrame frame =
      platform_window_recover_frame_to_display_work_area(
          preliminary, work_areas,
          {.left = margins.left(),
           .top = margins.top(),
           .right = margins.right(),
           .bottom = margins.bottom()});
  window->setGeometry(frame.x, frame.y,
                      std::max(kPlatformWindowMinimumWidth, frame.width),
                      std::max(kPlatformWindowMinimumHeight, frame.height));
  if (native_window != nullptr) {
    native_window->setGeometry(window->geometry());
  }
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

  {
    QDR_PROFILE_SCOPE("UI retained refresh");
    ui_.refresh(frame_timing, render_system_.frame_performance_stats());
  }
  const auto ui_refresh_end = std::chrono::steady_clock::now();
  if (project_session_.active_scene() != nullptr) {
    QDR_PROFILE_SCOPE("UI process viewport input");
    ui_.process_viewport_input();
  }
  const auto viewport_input_end = std::chrono::steady_clock::now();
  // Viewport commands execute synchronously and may close the active scene.
  // Do not retain an OpenSceneSession pointer across that dispatch boundary.
  if (project_session_.active_scene() != nullptr) {
    QDR_PROFILE_SCOPE("Render sync scene");
    (void)QuaderRenderApiRuntimeAccess::sync_scene(editor_->render(),
                                                   render_bridge_);
  }
  const auto scene_sync_end = std::chrono::steady_clock::now();
  if (const OpenSceneSession *active_scene = project_session_.active_scene();
      active_scene != nullptr && active_scene->session != nullptr) {
    const SceneSessionReadView session_view = active_scene->session->read_view();
    if (const EntityId root = session_view.scene.scene_root();
        root != kInvalidEntityId) {
      if (const SceneSettingsComponent *settings =
              session_view.scene.scene_settings(root)) {
        render_system_.apply_scene_settings(*settings);
      }
    }
  }
  const auto scene_settings_end = std::chrono::steady_clock::now();
  if (project_session_.active_scene() != nullptr) {
    ui_.prepare_viewports(render_bridge_, render_overlay_snapshot_cache_);
  }
  const auto viewport_prepare_end = std::chrono::steady_clock::now();
  const bool render_scene_viewports =
      project_session_.active_scene() != nullptr && ui_.can_render_viewports();
  const std::size_t submitted =
      ui_.render_surfaces(render_scene_viewports);
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

void App::schedule_next_frame(bool submitted) {
  if (!frame_timer_ || !running_) {
    return;
  }
  const bool continuous = ui_.needs_continuous_rendering();
  const bool synchronized = render_system_.presentation_is_synchronized();
  const int display_interval = continuous && synchronized
                                   ? presentation_frame_interval_ms(ui_.main_window())
                                   : 0;
  const int interval = frame_schedule_.next_interval_ms(
      continuous, synchronized, display_interval, submitted);
  if (frame_timer_->interval() != interval) {
    frame_timer_->setInterval(interval);
  }
}

} // namespace quader
