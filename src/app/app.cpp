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
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "editor/render/standard_material_render_snapshot_adapter.hpp"
#include "api/quader_project_assets_api_internal_access.hpp"
#include "platform/project_asset_recycle.hpp"
#include "editor/session/project_asset_bootstrap_service.hpp"
#include "editor/session/scene_session.hpp"
#include "render/scene_render_extraction.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/session/quader_editor_api_session_access.hpp"
#include "platform/platform_file_dialog.hpp"
#include "assets/material_document.hpp"
#if defined(CRIMSON_ENGINE_APPLICATION)
#include "integration/LevelSession.h"
#include "integration/LevelPanels.h"
#include "integration/project_asset_mutations.hpp"
#include "integration/native_asset_retention.hpp"
#include "assets/AssetLibrary.h"
#include "integration/profiling/LevelProfiling.h"
#include "profiling/EngineProfile.h"
#include "profiling/FrameProfile.h"
#include "resources/ResourceManager.h"
#include "crimson-renderer/Renderer.h"
#include "editor/EditorPreferences.h"
#include "core/user_config_paths.hpp"
#include <QMessageBox>
#endif

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
#include <stdexcept>
#include <system_error>
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

/// Bounds renderer command producers, including nested Qt paint turns.
class SharedRenderCommandBatch final {
public:
  explicit SharedRenderCommandBatch(RenderHost &render_system)
      : render_system_(render_system) {
    render_system_.begin_shared_command_batch();
  }
  SharedRenderCommandBatch(const SharedRenderCommandBatch &) = delete;
  SharedRenderCommandBatch &operator=(const SharedRenderCommandBatch &) = delete;
  ~SharedRenderCommandBatch() {
    render_system_.end_shared_command_batch();
    (void)render_system_.finish_external_queue_submission();
  }

private:
  RenderHost &render_system_;
};

} // namespace

App::App() = default;

App::~App() { shutdown(); }

bool App::initialize(QApplication &application,
                     const std::filesystem::path &executable_path,
                     AppOptions options) {
  shutdown_complete_ = false;
  running_ = false;
  viewport_presentation_request_ = {};
  application_ = &application;
  const std::filesystem::path executable_dir = executable_path.parent_path();
  executable_dir_ = executable_dir;
  project_session_.set_resource_root(executable_dir);
  window_settings_path_.clear();
  window_settings_ = AppWindowSettings::defaults();
  placement_screen_.clear();

  options_ = options;
#if defined(CRIMSON_ENGINE_APPLICATION)
  rejected_project_root_.clear();workspace_project_generation_=UINT64_MAX;
  if(!engine_profile_){crimson::profiling::EngineProfileOptions profile;profile.toolsDirectory=executable_dir_/"profiling";profile.outputDirectory=options.profile_output;profile.mode=options.profile_mode;profile.durationSeconds=options.profile_duration;engine_profile_=std::make_unique<crimson::profiling::EngineProfile>(profile);}
  if(!options.frame_profile.empty())frame_profile_=std::make_unique<crimson::profiling::FrameProfile>(options.frame_profile);
  profile_frame_=0;profile_backend_ready_=false;
#endif
  if (!render_system_) {
    render_system_ = create_app_render_host();
  }
  if (!render_system_) {
    LOG_ERROR("The executable did not provide a renderer host");
    shutdown();
    return false;
  }
  shell_replacement_pending_ = false;
#if defined(CRIMSON_ENGINE_APPLICATION)
  const crimson::EditorPreferences startup_preferences;
  const auto native_api=options.composition_api.value_or(startup_preferences.values().graphicsBackend==crimson::GraphicsBackend::Direct3D11?RenderCompositionApi::Direct3D11:RenderCompositionApi::Direct3D12);
  if(!render_system_->set_composition_api(native_api)){LOG_ERROR("The renderer host does not support the selected native Qt backend");shutdown();return false;}
#endif
  shell_replacement_started_ = false;
  if (!initialize_shell()) {
    shutdown();
    return false;
  }

  editor_ = std::make_unique<QuaderEditorApi>(project_session_);
#if defined(CRIMSON_ENGINE_APPLICATION)
  QuaderViewportApiRuntimeAccess::set_scene_rendering_available(editor_->viewports(),true);
#endif
  QuaderProjectAssetsApiInternalAccess::set_recycle_port(
      editor_->project_assets(), recycle_project_asset_paths);
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
#if defined(CRIMSON_ENGINE_APPLICATION)
            return platform_choose_project_folder_dialog(
                project_folder_dialog_options(ui_.native_window_handle(),
                                              editor_->project()));
#else
            return platform_open_project_file_dialog(
                project_file_dialog_options(ui_.native_window_handle(),
                                            editor_->project()));
#endif
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
#if defined(CRIMSON_ENGINE_APPLICATION)
            if(engine_panels_&&!engine_panels_->confirmClose())return false;
#endif
            return ui_.prepare_standard_material_project_transition(
                action_id);
          },
      .begin_loading = [this]() { ui_.begin_loading_presentation(); },
      .end_loading =
          [this](bool succeeded) { ui_.end_loading_presentation(succeeded); },
#if defined(CRIMSON_ENGINE_APPLICATION)
      .allow_nonempty_project_folder = true,
#endif
  });
  editor_->scene_documents().set_runtime_hooks(
      QuaderSceneDocumentsRuntimeHooks{
          .choose_open_scene_path =
              [this]() {
                return ui_.choose_project_scene_path(false).path;
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
                return ui_.choose_project_scene_path(
                    true, selected.has_value() ? &*selected : nullptr);
              },
          .confirm_dirty_scene =
              [this](const QuaderSceneDocumentInfo &scene,
                     std::string_view action_id) {
                return ui_.confirm_dirty_scene(scene, action_id);
              },
          .begin_loading = [this]() { ui_.begin_loading_presentation(); },
          .end_loading =
              [this](bool succeeded) { ui_.end_loading_presentation(succeeded); },
      });

  frame_timer_ = std::make_unique<QTimer>();
  frame_timer_->setTimerType(Qt::PreciseTimer);
  auto *frame_presentation_gate =
      new AppFramePresentationGate(application, frame_timer_.get());
  QObject::connect(frame_timer_.get(), &QTimer::timeout, application_,
                   [this, frame_presentation_gate]() {
                     if (running_ && !shutdown_complete_ &&
                         !shell_replacement_pending_) {
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
                       if (!render_system_->presentation_is_synchronized() &&
                           ui_.needs_continuous_rendering() &&
                           !render_system_->ready_for_next_frame()) {
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

bool App::prepare_composed_shell() {
  if (!ui_.initialize(*render_system_, executable_dir_, true)) {
    LOG_ERROR("Qt composition shell initialization failed");
    return false;
  }
  if (editor_ != nullptr) {
    // The new root may receive Close while Qt delivers its initial exposure.
    // Bind the current project now so its ordinary dirty preflight is ready.
    bind_current_scene();
  }
  apply_window_settings();
  ui_.show();
  return true;
}

bool App::initialize_shell() {
  const bool composed_viewports = options_.render_backend == RendererBackend::Vulkan;
  if (composed_viewports) {
    if (ui_.main_window() == nullptr && !prepare_composed_shell()) {
      return false;
    }
    if (ui_.composition_context() == nullptr) {
      LOG_ERROR("Qt did not initialize the viewport composition device");
      return false;
    }
    if (!ui_.loading_logo_presented()) {
      LOG_ERROR("Qt did not present the loading logo at the native client geometry");
      return false;
    }
  }

  if (!render_system_->initialize(
          nullptr, executable_dir_,
          RenderConfig{.backend = options_.render_backend},
          ui_.composition_context())) {
    LOG_ERROR("Renderer initialization failed");
    return false;
  }

  render_bridge_ = render_system_->create_scene_sink(executable_dir_);
  if (!render_bridge_) {
    LOG_ERROR("Unable to initialize scene render publication");
    return false;
  }
  project_material_resolver_ =
      std::make_unique<ProjectSessionRenderMaterialResolver>(project_session_);
  render_bridge_->set_project_material_resolver(
      project_material_resolver_.get());

  if ((!composed_viewports &&
       !ui_.initialize(*render_system_, executable_dir_)) ||
      (composed_viewports && !ui_.initialize_render_resources())) {
    LOG_ERROR("Qt Widgets UI initialization failed");
    return false;
  }

  ui_.set_session_settings_changed_callback(
      [this](const SessionPreferences &settings) {
        project_session_.set_history_max_steps(
            static_cast<std::size_t>(settings.undo_steps));
      });
  project_material_resolver_->set_default_material_texture(
      ui_.default_material_texture());
  ui_.set_default_material_texture_changed_callback(
      [this](const std::filesystem::path &path) {
        project_material_resolver_->set_default_material_texture(path);
      });
  project_session_.set_history_max_steps(
      static_cast<std::size_t>(ui_.session_settings().undo_steps));
  if (editor_ != nullptr) {
    if (composed_viewports) {
      ui_.refresh_project_workspace();
    } else {
      bind_current_scene();
    }
  }
  if (!composed_viewports) {
    apply_window_settings();
  }
  ui_.show();
  ui_.set_composed_frame_prepare_callback([this]() { prepare_composed_frame(); });
  ui_.set_viewport_presentation_requested_callback(
      [this]() { request_viewport_presentation(); });
  install_display_refresh_rate_sync();
  return true;
}

void App::poll_autosave() {
  if (!running_ || shutdown_complete_ || shell_replacement_pending_) {
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
  if (!shell_replacement_pending_) {
    save_window_settings();
  }
  frame_timer_.reset();
  autosave_timer_.reset();
  retire_shell();
#if defined(CRIMSON_ENGINE_APPLICATION)
  native_asset_retention_.reset();
  frame_profile_.reset();engine_profile_.reset();
#endif
  editor_.reset();
  shell_replacement_pending_ = false;
  shell_replacement_started_ = false;
  application_ = nullptr;
}

void App::retire_shell() {
#if defined(CRIMSON_ENGINE_APPLICATION)
  ui_.set_external_close_guard({});
  ui_.set_external_command_input_owner({});
  ui_.set_asset_browser_extension({});
  ui_.set_scene_component_extension(nullptr);
  project_session_.project_assets().set_mutation_prepare({});
  profiling_controls_.reset();
  if(engine_profile_)engine_profile_->backendRetired();profile_backend_ready_=false;
  engine_panels_.reset();
  if(render_system_)render_system_->publish_modeling_authoring_source({});
  modeling_authoring_source_.reset();modeling_authoring_stamp_.reset();
  if(engine_level_)engine_level_->detachRenderer();
  engine_level_.reset();bound_project_root_.clear();
#endif
  ui_.set_viewport_presentation_requested_callback({});
  viewport_presentation_request_ = {};
  ui_.set_session_settings_changed_callback({});
  ui_.set_default_material_texture_changed_callback({});
  QObject::disconnect(window_screen_changed_connection_);
  QObject::disconnect(screen_refresh_rate_changed_connection_);
  display_refresh_screen_ = nullptr;
  ui_.detach_render_resources();
  render_overlay_snapshot_cache_.reset();
  scene_settings_binding_.reset();
  if (render_bridge_) {
    render_bridge_->shutdown();
    render_bridge_.reset();
  }
  project_material_resolver_.reset();
  if (render_system_) {
    render_system_->shutdown();
  }
  ui_.shutdown();
}

void App::replace_shell() {
  if (!shell_replacement_pending_ || !running_ || shutdown_complete_ ||
      application_ == nullptr || frame_timer_ == nullptr) {
    return;
  }
  // A queued delivery may run inside an initiating action's modal loop. Keep
  // that action's widgets and command batch alive until their stacks unwind.
  if (rendering_frame_ || preparing_composition_ ||
      ui_.loading_operation_active() ||
      QApplication::activeModalWidget() != nullptr ||
      QApplication::activePopupWidget() != nullptr) {
    QTimer::singleShot(16, frame_timer_.get(), [this]() { replace_shell(); });
    return;
  }
  const bool composed_viewports = options_.render_backend == RendererBackend::Vulkan;
  if (!shell_replacement_started_) {
    shell_replacement_started_ = true;
    const bool quit_on_last_window_closed = application_->quitOnLastWindowClosed();
    application_->setQuitOnLastWindowClosed(false);
    retire_shell();
    const bool prepared = !composed_viewports || prepare_composed_shell();
    application_->setQuitOnLastWindowClosed(quit_on_last_window_closed);
    if (!prepared) {
      LOG_ERROR("Unable to prepare the replacement application window");
      shutdown();
      QCoreApplication::exit(1);
      return;
    }
  }
  if (!shell_replacement_pending_ || !running_ || shutdown_complete_) {
    return;
  }
  if (!shell_replacement_started_) {
    QTimer::singleShot(0, frame_timer_.get(), [this]() { replace_shell(); });
    return;
  }
  if (composed_viewports &&
      (ui_.main_window() == nullptr || !ui_.main_window()->isVisible())) {
    return;
  }
  if (composed_viewports && (ui_.composition_context() == nullptr ||
                            !ui_.loading_logo_presented())) {
    // A newly shown HWND can have its final client geometry before Qt handles
    // the queued native exposure. Let that ordinary event paint and submit the
    // logo before beginning synchronous renderer work; never pump input here.
    QTimer::singleShot(16, frame_timer_.get(), [this]() { replace_shell(); });
    return;
  }
  const bool initialized = initialize_shell();
  if (!initialized) {
    LOG_ERROR("Unable to initialize the replacement application window");
    shutdown();
    QCoreApplication::exit(1);
    return;
  }
  shell_replacement_pending_ = false;
  shell_replacement_started_ = false;
  frame_clock_.reset();
  frame_schedule_ = {};
  frame_timer_->start(0);
}

void App::bind_current_scene() {
  const OpenSceneSession *active_scene = project_session_.active_scene();
  SceneSession &scene = active_scene != nullptr && active_scene->session != nullptr
                            ? *active_scene->session
                            : project_session_.fallback_scene();
  bind_active_scene(scene);
}

void App::bind_active_scene(SceneSession &scene) {
#if defined(CRIMSON_ENGINE_APPLICATION)
  if(editor_&&project_session_.has_project()&&workspace_project_generation_!=project_session_.project_generation()){
    (void)editor_->mode().set_workspace_mode(QuaderWorkspaceMode::Default);
    workspace_project_generation_=project_session_.project_generation();
  }
#endif
  const auto next_window_settings_path = AppWindowSettings::resolve_project_path(
      executable_dir_, project_session_.manifest_path());
  const bool project_changed = next_window_settings_path != window_settings_path_;
  bool queue_shell_replacement = false;
  if (project_changed) {
    const bool closing_window = ui_.main_window_close_in_progress();
    if (!shell_replacement_pending_) {
      // The visible shell still owns the outgoing placement. Coalesced commits
      // must never write it into a project that has not received its own shell.
      save_window_settings();
      placement_screen_ = active_display_screen(ui_.main_window());
    }
    queue_shell_replacement = !closing_window && !shell_replacement_pending_;
    shell_replacement_pending_ = !closing_window;
    shell_replacement_started_ = false;
    if (shell_replacement_pending_) {
      ui_.prepare_shell_replacement();
    }
    window_settings_path_ = next_window_settings_path;
    window_settings_ = AppWindowSettings::defaults();
    if (!window_settings_path_.empty()) {
      std::error_code error;
      const bool project_settings_exist =
          std::filesystem::exists(window_settings_path_, error);
      window_settings_ = AppWindowSettings::load(
          project_settings_exist || error
              ? window_settings_path_
              : AppWindowSettings::resolve_path(executable_dir_));
    }
  }
  scene_settings_binding_.reset();
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
  if (queue_shell_replacement && frame_timer_ != nullptr) {
    // Keep domain/UI binding publication synchronous, then retire the old
    // widgets only after the project action that owns their stack returns.
    frame_timer_->stop();
    QTimer::singleShot(0, frame_timer_.get(), [this]() { replace_shell(); });
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
  render_system_->set_display_refresh_rate_hz(
      display_refresh_rate_hz(screen != nullptr ? screen
                                                : active_display_screen(
                                                      ui_.main_window())));
}

void App::apply_window_settings() {
  QMainWindow *window = ui_.main_window();
  if (window == nullptr) {
    return;
  }
  if (window_settings_path_.empty()) {
    apply_home_window_placement();
    return;
  }
  window->setWindowState(Qt::WindowNoState);
  window->setMinimumSize(kPlatformWindowMinimumWidth, kPlatformWindowMinimumHeight);
  const std::optional<PlatformWindowPlacement> placement =
      window_settings_.placement();
  if (!placement.has_value()) {
    // A project without saved or legacy placement starts from the current
    // display's centered Home bounds, then owns its subsequent placement.
    apply_home_window_placement();
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
  if (placement->maximized) {
    window->setWindowState(Qt::WindowMaximized);
  }
  (void)window->winId();
  QWindow *native_window = window->windowHandle();
  QMargins margins;
  if (native_window != nullptr) {
    // Hidden QWidget geometry is deferred. Synchronize the native window first
    // so its frame margins reflect the saved monitor and its DPI, without
    // mapping the shell before UISystem::show().
    if (!placement->maximized) {
      native_window->setGeometry(window->geometry());
    }
    margins = native_window->frameMargins();
  }
  const PlatformWindowChromeFrame frame =
      platform_window_recover_frame_to_display_work_area(
          preliminary, work_areas,
          {.left = margins.left(),
           .top = margins.top(),
           .right = margins.right(),
           .bottom = margins.bottom()});
  if (placement->maximized) {
    window->setWindowState(Qt::WindowNoState);
  }
  window->setGeometry(frame.x, frame.y,
                      std::max(kPlatformWindowMinimumWidth, frame.width),
                      std::max(kPlatformWindowMinimumHeight, frame.height));
  if (native_window != nullptr) {
    native_window->setGeometry(window->geometry());
  }
  if (placement->maximized) {
    // Configure placement while hidden so UISystem remains the sole owner of
    // mapping the composed shell. Its loading cover admits the logo only once
    // retained geometry matches this real native client and maximized state.
    window->setWindowState(window->windowState() | Qt::WindowMaximized);
  }
}

void App::apply_home_window_placement() {
  QMainWindow *window = ui_.main_window();
  if (window == nullptr) {
    return;
  }
  window->setWindowState(Qt::WindowNoState);
  QScreen *screen = placement_screen_ != nullptr
                        ? placement_screen_.data()
                        : active_display_screen(window);
  if (screen == nullptr) {
    window->resize(1200, 740);
    return;
  }
  window->setScreen(screen);
  const QRect available = screen->availableGeometry();
  const QSize preliminary_size(std::min(1200, available.width()),
                               std::min(740, available.height()));
  window->setGeometry(QRect(available.center() -
                               QPoint(preliminary_size.width() / 2,
                                      preliminary_size.height() / 2),
                           preliminary_size));
  (void)window->winId();
  QWindow *host = window->windowHandle();
  const QMargins margins = host != nullptr ? host->frameMargins() : QMargins{};
  const int frame_width = std::max(0, margins.left()) + std::max(0, margins.right());
  const int frame_height = std::max(0, margins.top()) + std::max(0, margins.bottom());
  const int maximum_width = std::max(1, available.width() - frame_width);
  const int maximum_height = std::max(1, available.height() - frame_height);
  // Home dimensions are Qt logical pixels, independent of interface scale.
  const int width = std::min(1200, maximum_width);
  const int height = std::min(740, maximum_height);
  window->setMinimumSize(std::min(kPlatformWindowMinimumWidth, maximum_width),
                         std::min(kPlatformWindowMinimumHeight, maximum_height));
  const int x = available.x() + (available.width() - width - frame_width) / 2 +
                std::max(0, margins.left());
  const int y = available.y() + (available.height() - height - frame_height) / 2 +
                std::max(0, margins.top());
  window->setGeometry(x, y, width, height);
  if (host != nullptr) {
    host->setGeometry(window->geometry());
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

void App::prepare_composed_frame() {
  if (shutdown_complete_ || shell_replacement_pending_ || preparing_composition_ ||
      editor_ == nullptr ||
      project_session_.active_scene() == nullptr) {
    return;
  }
  // A dock layout can paint before the next application timer. Publish the
  // current scene/camera to that same composed frame, without consuming input
  // again or recursively refreshing the retained UI.
  preparing_composition_ = true;
  const SharedRenderCommandBatch command_batch(*render_system_);
#if defined(CRIMSON_ENGINE_APPLICATION)
  if(engine_panels_)(void)engine_panels_->prepareFrame();
#endif
  (void)QuaderRenderApiRuntimeAccess::sync_scene(editor_->render(), *render_bridge_);
  ui_.prepare_viewports(*render_bridge_, render_overlay_snapshot_cache_);
  preparing_composition_ = false;
}

bool App::render_frame(const FrameTiming &frame_timing) {
  QDR_PROFILE_FUNCTION();
  render_system_->begin_application_frame();
  if (rendering_frame_ || shell_replacement_pending_ || !editor_) {
    return false;
  }
  const auto frame_begin = std::chrono::steady_clock::now();
  rendering_frame_ = true;
  const SharedRenderCommandBatch command_batch(*render_system_);
#if defined(CRIMSON_ENGINE_APPLICATION)
  synchronize_engine_session();
#endif

  {
    QDR_PROFILE_SCOPE("UI retained refresh");
    ui_.refresh(frame_timing, render_system_->frame_performance_stats());
  }
  const auto ui_refresh_end = std::chrono::steady_clock::now();
  if (project_session_.active_scene() != nullptr) {
    QDR_PROFILE_SCOPE("UI process viewport input");
    ui_.process_viewport_input();
  }
  const auto viewport_input_end = std::chrono::steady_clock::now();
#if defined(CRIMSON_ENGINE_APPLICATION)
  synchronize_modeling_world();
  if(engine_level_&&engine_panels_) {
    const bool active=QGuiApplication::applicationState()==Qt::ApplicationActive;
    engine_panels_->update(frame_timing.delta_seconds,active);
    engine_level_->update(frame_timing.delta_seconds,active,render_system_->completed_frame());
    engine_panels_->syncNativeSelection();
    const auto catalog_revision=engine_level_->assets().catalogRevision();
    if(catalog_revision!=engine_asset_catalog_revision_){
      (void)editor_->project_assets().assets(true);
      engine_asset_catalog_revision_=catalog_revision;
    }
    (void)engine_panels_->prepareFrame();
  }
#endif
  if (shell_replacement_pending_) {
    rendering_frame_ = false;
    return false;
  }
  // Viewport commands execute synchronously and may close the active scene.
  // Do not retain an OpenSceneSession pointer across that dispatch boundary.
  if (project_session_.active_scene() != nullptr) {
    QDR_PROFILE_SCOPE("Render sync scene");
    (void)QuaderRenderApiRuntimeAccess::sync_scene(editor_->render(),
                                                   *render_bridge_);
  }
  const auto scene_sync_end = std::chrono::steady_clock::now();
  if (const OpenSceneSession *active_scene = project_session_.active_scene();
      active_scene != nullptr && active_scene->session != nullptr) {
    const SceneSessionReadView session_view = active_scene->session->read_view();
    const QuaderSceneDocumentBinding binding =
        editor_->query().document_binding();
    const std::uint64_t revision = session_view.scene.scene_settings_revision();
    if (scene_settings_binding_ != binding ||
        scene_settings_revision_ != revision) {
      render_system_->apply_scene_settings(session_view.scene.scene_settings());
      scene_settings_binding_ = binding;
      scene_settings_revision_ = revision;
    }
  }
  const auto scene_settings_end = std::chrono::steady_clock::now();
  if (project_session_.active_scene() != nullptr) {
    ui_.prepare_viewports(*render_bridge_, render_overlay_snapshot_cache_);
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
      double maximum_interval_ms = 0.0;
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
    diagnostics.maximum_interval_ms = std::max(
        diagnostics.maximum_interval_ms, frame_timing.raw_delta_seconds * 1000.0);
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
          render_system_->frame_performance_stats();
      LOG_INFO(
          "Frame diagnostics: {:.1f} app fps, {:.2f} ms average, {:.2f} ms "
          "maximum, {:.2f} ms maximum interval, {} submissions, "
          "stages ui={:.2f} input={:.2f} sync={:.2f} "
          "settings={:.2f} prepare={:.2f} render={:.2f} ms, renderer {:.1f} "
          "fps, CPU {:.2f} ms, GPU {:.2f} ms",
          static_cast<double>(diagnostics.frames) / sample_seconds,
          diagnostics.total_ms / static_cast<double>(diagnostics.frames),
          diagnostics.maximum_ms, diagnostics.maximum_interval_ms,
          diagnostics.submitted,
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
#if defined(CRIMSON_ENGINE_APPLICATION)
  if(engine_profile_){
    const bool initialized=render_system_->initialized();
    if(initialized&&!profile_backend_ready_){if(const auto* graphics=render_system_->crimson_renderer()){engine_profile_->backendInitialized(graphics->graphicsBackend());profile_backend_ready_=true;}}
    crimson::profiling::CaptureFrame capture;capture.appFrame=frame_timing.frame_index;capture.bgfxFrame=initialized?render_system_->completed_frame():0;capture.playing=engine_level_&&engine_level_->playing();
    const crimson::SceneSettings* settings{};std::optional<crimson::ResourceStats> resources;
    if(engine_level_){resources=engine_level_->resources().stats();capture.materialsReady=engine_level_->materialPreparationReady()&&resources->queued==0&&resources->running==0;const auto& prepared=engine_level_->lastPreparedFrame();if(prepared)for(const auto& view:prepared->views){if(view.identity.purpose==crimson::RenderViewPurpose::Scene){capture.sceneWidth=view.width;capture.sceneHeight=view.height;settings=&view.settings;}else if(view.identity.purpose==crimson::RenderViewPurpose::Game){capture.gameWidth=view.width;capture.gameHeight=view.height;}}}
    engine_profile_->frame(capture,settings,resources?&*resources:nullptr,engine_level_?&engine_level_->document():nullptr);
    if(profiling_controls_)profiling_controls_->update(capture.playing);
    if(frame_profile_&&capture.bgfxFrame&&capture.bgfxFrame!=profile_frame_){profile_frame_=static_cast<std::uint32_t>(capture.bgfxFrame);const auto measured=render_system_->frame_performance_stats();const auto milliseconds=measured.frames_per_second_available&&measured.frames_per_second>0?1000./measured.frames_per_second:frame_timing.raw_delta_seconds*1000.;const auto work=measured.cpu_frame_ms_available?measured.cpu_frame_ms:std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-frame_begin).count();frame_profile_->record({milliseconds,work},engine_panels_&&engine_panels_->navigating(),capture.sceneWidth,capture.sceneHeight);}
  }
#endif
  QDR_PROFILE_FRAME_MARK();
  // Composed panes submit during Qt painting. Keep scheduling their requested
  // work here; their accepted-frame accounting happens after root composition.
  return submitted > 0u ||
         (render_scene_viewports && render_system_->uses_composited_vulkan());
}

void App::request_viewport_presentation() {
  if (!frame_timer_ || !running_ || shutdown_complete_ ||
      shell_replacement_pending_) {
    return;
  }
  if (!viewport_presentation_request_.request()) {
    return;
  }
  // Coalesce native resize/expose events after their command/layout stack.
  // The timer retains ownership of first-paint gates, scene sync and submission.
  QMetaObject::invokeMethod(
      frame_timer_.get(),
      [this]() {
        viewport_presentation_request_.deliver();
        if (!frame_timer_ || !running_ || shutdown_complete_ ||
            shell_replacement_pending_) {
          return;
        }
        if (viewport_presentation_request_.take(rendering_frame_)) {
          frame_timer_->start(0);
        }
      },
      Qt::QueuedConnection);
}

void App::schedule_next_frame(bool submitted) {
  if (!frame_timer_ || !running_ || shell_replacement_pending_) {
    return;
  }
  // A nested Qt loop may deliver the queued request during render_frame().
  // Keep that request across the ordinary cadence update at the frame's end.
  if (viewport_presentation_request_.take(rendering_frame_)) {
    frame_timer_->start(0);
    return;
  }
  const bool continuous = ui_.needs_continuous_rendering()
#if defined(CRIMSON_ENGINE_APPLICATION)
      || (engine_panels_&&engine_panels_->continuous())
#endif
      ;
  const bool synchronized = render_system_->presentation_is_synchronized();
  const int display_interval = continuous && synchronized
                                   ? presentation_frame_interval_ms(ui_.main_window())
                                   : 0;
  const int interval = frame_schedule_.next_interval_ms(
      continuous, synchronized, display_interval, submitted,
      render_system_->uses_composited_vulkan());
  if (frame_timer_->interval() != interval) {
    frame_timer_->setInterval(interval);
  }
}

#if defined(CRIMSON_ENGINE_APPLICATION)
bool App::bind_engine_project(const std::filesystem::path& selected) {
  try {
    const auto root=std::filesystem::absolute(selected).lexically_normal();
    if(!std::filesystem::is_directory(root))throw std::runtime_error("The selected project folder is unavailable.");
    if(root==bound_project_root_)return true;
    if(engine_level_&&engine_panels_&&!engine_panels_->confirmClose())return false;
    auto candidate=std::make_unique<crimson::integration::LevelSession>(root,executable_dir_);
    candidate->attachRenderer(*render_system_->crimson_renderer());
    candidate->setViewportPickRequester([this](const crimson::PickRequest& request){return ui_.request_native_pick(request);});
    project_session_.project_assets().set_mutation_prepare({});
    if(engine_panels_)engine_panels_->bind(nullptr);
    render_system_->publish_engine_frame({});
    render_system_->publish_native_scene_bindings({});
    if(engine_level_)engine_level_->detachRenderer();
    engine_level_=std::move(candidate);bound_project_root_=root;
    if(!native_asset_retention_||retention_project_generation_!=project_session_.project_generation()){
      native_asset_retention_=std::make_shared<crimson::integration::NativeAssetRetention>(project_session_);
      retention_project_generation_=project_session_.project_generation();
    }
    const std::weak_ptr<crimson::integration::NativeAssetRetention> retention=native_asset_retention_;
    engine_level_->assets().setExternalAssetRetentionProbe([retention]{const auto source=retention.lock();if(!source)throw std::runtime_error("The project reference owner is unavailable.");return source->snapshot();},
        [retention]{const auto source=retention.lock();return source?source->revision():0;});
    engine_asset_catalog_revision_=UINT64_MAX;
    modeling_authoring_source_.reset();modeling_authoring_stamp_.reset();
    render_system_->publish_modeling_authoring_source({});
    project_session_.project_assets().set_mutation_prepare([this](const ProjectAssetMutation& mutation){
      return crimson::integration::prepare_project_asset_mutation(engine_level_->assets(),&engine_level_->document(),mutation);
    });
    if(engine_profile_)engine_profile_->projectChanged(root);
    if(engine_panels_){
      engine_panels_->setNativeAssetObserver([retention](quader::SceneDocument& scene){if(const auto source=retention.lock())source->observe(scene);});
      engine_panels_->bind(engine_level_.get());
    }
    return true;
  }catch(const std::exception& error){LOG_ERROR("Project runtime initialization failed: {}",error.what());if(ui_.main_window())QMessageBox::warning(ui_.main_window(),"Open Project",QString::fromUtf8(error.what()));return false;}
}
void App::synchronize_engine_session() {
  // Home belongs to the native project lifecycle. Runtime controls and docks
  // only exist inside a chosen project workspace.
  if(!project_session_.has_project()){
    if(engine_level_||engine_panels_){
      ui_.set_asset_browser_extension({});
      ui_.set_scene_component_extension(nullptr);
      project_session_.project_assets().set_mutation_prepare({});
      profiling_controls_.reset();engine_panels_.reset();
      render_system_->publish_engine_frame({});
    render_system_->publish_native_scene_bindings({});
      render_system_->publish_modeling_authoring_source({});
      modeling_authoring_source_.reset();modeling_authoring_stamp_.reset();
      if(engine_level_)engine_level_->detachRenderer();
      engine_level_.reset();bound_project_root_.clear();rejected_project_root_.clear();
      native_asset_retention_.reset();
    }
    return;
  }
  if(!render_system_->crimson_renderer()||!ui_.main_window())return;
  const auto& root=project_session_.root();
  if(root!=bound_project_root_){
    if(root==rejected_project_root_)return;
    if(!bind_engine_project(root)){rejected_project_root_=root;return;}
    rejected_project_root_.clear();
  }
  if(!profiling_controls_&&engine_profile_)profiling_controls_=std::make_unique<crimson::integration::LevelProfiling>(*ui_.main_window(),engine_profile_->captures());
  if(!engine_panels_&&ui_.main_window()) {
    engine_panels_=std::make_unique<crimson::integration::LevelPanels>(*ui_.main_window(),*render_system_,*editor_,project_session_,[this]{request_viewport_presentation();});
    engine_panels_->bind(engine_level_.get());
    const std::weak_ptr<crimson::integration::NativeAssetRetention> retention=native_asset_retention_;
    engine_panels_->setNativeAssetObserver([retention](quader::SceneDocument& scene){if(const auto source=retention.lock())source->observe(scene);});
    ui_.set_asset_browser_extension(engine_panels_->assetBrowserExtension());
    ui_.set_scene_component_extension(engine_panels_->componentExtension());
    (void)editor_->ui().show_panel("asset_browser");
    engine_panels_->setBeforePlay([this]{synchronize_modeling_world();});
    ui_.set_external_close_guard([this]{return !engine_panels_||engine_panels_->confirmClose();});
    ui_.set_external_command_input_owner([this]{return engine_panels_&&engine_panels_->ownsInput();});
  }
  const auto* active=project_session_.active_scene();
  if(!engine_level_)return;
  engine_level_->attachRenderer(*render_system_->crimson_renderer());
  if(!active||!active->session)return;
  const std::string scene(active->scene_id.str());
  engine_panels_->bindScene(active->session.get(),scene);
  synchronize_modeling_world();
}
void App::synchronize_modeling_world() {
  if(!engine_level_||!project_session_.has_project())return;
  const auto* active=project_session_.active_scene();
  if(!active||!active->session){render_system_->publish_modeling_authoring_source({});engine_level_->setModelingWorld({});return;}
  if(engine_panels_)engine_panels_->bindScene(active->session.get(),std::string(active->scene_id.str()));
  const auto view=active->session->read_view();
  const RenderSceneBindingKey binding{active->session_id.packed(),project_session_.activation_generation(),active->session->document_binding_generation()};
  const auto material_revision=project_material_resolver_?project_material_resolver_->revision():0;
  auto stamp=capture_render_source_stamp(view.scene,view.assets,{.scene_binding=binding,.project_material_revision=material_revision});
  // Runtime geometry is independent of editor selection and local isolation.
  stamp.selection_revision=0;stamp.hierarchy_presentation_revision=0;
  if(!modeling_authoring_stamp_||*modeling_authoring_stamp_!=stamp){
    auto source=build_render_scene_snapshot(view.scene,view.assets,{.previous=modeling_authoring_source_.get(),.scene_binding=binding,
        .project_material_resolver=project_material_resolver_.get(),.project_material_revision=material_revision});
    modeling_authoring_source_=std::make_shared<const RenderSceneSnapshot>(std::move(source));modeling_authoring_stamp_=stamp;
    render_system_->publish_modeling_authoring_source(modeling_authoring_source_);
  }
  engine_level_->setModelingWorld(render_system_->modeling_world_snapshot(),render_system_->modeling_world_ready());
}
#endif

} // namespace quader
