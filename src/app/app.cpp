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
#include "platform/sdl_event_utils.hpp"
#include "ui/ui_platform_adapter.hpp"

#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

namespace quader {
namespace {

constexpr Sint32 kActiveFrameWaitMs = 1;
constexpr Sint32 kIdleFrameWaitMs = 100;
constexpr Sint32 kSuspendedWindowWaitMs = 250;

std::filesystem::path
default_project_save_file_name(const QuaderProjectApi &project) {
  std::string name = project.metadata().name.empty() ? "Untitled Project"
                                                     : project.metadata().name;
  name.append(".qdr");
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

bool chrome_action_invalidates_current_frame(WindowChromeAction action) {
  return action == WindowChromeAction::Minimize ||
         action == WindowChromeAction::ToggleMaximizeRestore;
}

/**
 * Bridges SDL window and event calls into the SDL-free UI platform contract.
 */
class SdlUIPlatformAdapter final : public UIPlatformAdapter {
public:
  explicit SdlUIPlatformAdapter(SDL_Window *window) : window_(window) {}

  void initialize_imgui_backend() override {
    ImGui_ImplSDL3_InitForOther(window_);
  }

  void shutdown_imgui_backend() override { ImGui_ImplSDL3_Shutdown(); }

  bool process_event(const UIPlatformEvent &event) override {
    const SDL_Event *sdl_event =
        static_cast<const SDL_Event *>(event.native_event);
    return sdl_event != nullptr && ImGui_ImplSDL3_ProcessEvent(sdl_event);
  }

  void begin_frame() override { ImGui_ImplSDL3_NewFrame(); }

  QVec2 relative_mouse_delta() override {
    float delta_x = 0.0f;
    float delta_y = 0.0f;
    (void)SDL_GetRelativeMouseState(&delta_x, &delta_y);
    return {delta_x, delta_y};
  }

  bool set_relative_mouse_captured(bool captured,
                                   std::string &error_message) override {
    if (SDL_SetWindowRelativeMouseMode(window_, captured)) {
      error_message.clear();
      return true;
    }
    error_message = SDL_GetError();
    return false;
  }

  void discard_relative_mouse_delta() override {
    float discard_x = 0.0f;
    float discard_y = 0.0f;
    (void)SDL_GetRelativeMouseState(&discard_x, &discard_y);
  }

  void warp_mouse_in_window(QVec2 point) override {
    SDL_WarpMouseInWindow(window_, point.x, point.y);
  }

private:
  SDL_Window *window_ = nullptr;
};

} // namespace

App::App() = default;
App::~App() {
    shutdown();
}

bool App::initialize(const std::filesystem::path &executable_path,
                     AppOptions options) {
  shutdown_complete_ = false;
  running_ = false;
  const std::filesystem::path executable_dir = executable_path.parent_path();
  window_settings_path_ = AppWindowSettings::resolve_path(executable_dir);
  window_settings_ = AppWindowSettings::load(window_settings_path_);
  const std::optional<PlatformWindowPlacement> saved_placement =
      window_settings_.placement();
  const PlatformWindowChromeFrame initial_frame =
      saved_placement ? saved_placement->normal_frame
                      : PlatformWindowChromeFrame{0, 0, 1600, 960};

  if (!window_.create(
          "Quader", std::max(kPlatformWindowMinimumWidth, initial_frame.width),
          std::max(kPlatformWindowMinimumHeight, initial_frame.height),
          {.borderless = options.borderless_window})) {
    return false;
  }
  if (saved_placement) {
    window_.apply_placement(*saved_placement);
  }

  if (!render_system_.initialize(
          window_.native_window_handle(), executable_dir,
          RenderConfig{.backend = options.render_backend})) {
    LOG_ERROR("Renderer initialization failed");
    shutdown();
    return false;
  }

  LOG_INFO("Initializing scene render bridge");
  render_system_.initialize_scene_render_bridge(render_bridge_,
                                                executable_dir);
  LOG_INFO("Scene render bridge initialized");

  LOG_INFO("Initializing UI system");
  ui_platform_adapter_ =
      std::make_unique<SdlUIPlatformAdapter>(window_.sdl_window());
  ui_.initialize(*ui_platform_adapter_, render_system_, executable_dir,
                 options.borderless_window);
  LOG_INFO("UI system initialized");

  const ProjectDefaultAssets default_assets =
      ProjectAssetBootstrapService{}.bootstrap_default_assets(project_session_);
  default_material_ = default_assets.default_material;
  editor_ = std::make_unique<QuaderEditorApi>(
      QuaderApiSessionInternalAccess::from_project_session(project_session_));
  ui_.install_runtime_hooks(*editor_);
  editor_->project().set_runtime_hooks(QuaderProjectRuntimeHooks{
      .choose_open_path =
          [this]() {
            return platform_open_project_file_dialog(
                project_file_dialog_options(window_.native_window_handle(),
                                            editor_->project()));
          },
      .choose_save_as_path =
          [this]() {
            return platform_save_project_file_dialog(
                project_file_dialog_options(window_.native_window_handle(),
                                            editor_->project()));
          },
  });
  window_.set_live_resize_frame_callback([this]() {
    if (!running_ || shutdown_complete_ || rendering_frame_) {
      return false;
    }
    const bool frame_submitted = render_frame(frame_clock_.tick());
    if (frame_submitted) {
      live_resize_frame_rendered_ = true;
    }
    return frame_submitted;
  });

  if (SDL_AddEventWatch(&App::live_resize_event_watch, this)) {
    event_watch_installed_ = true;
  } else {
    LOG_WARN("SDL_AddEventWatch failed for live resize redraw: {}",
             SDL_GetError());
  }

  running_ = true;
  return true;
}

int App::run() {
    LOG_INFO("Entering main loop");
    frame_clock_.reset();
    const SDL_WindowID main_window_id = SDL_GetWindowID(window_.sdl_window());

    while (running_) {
      const FrameTiming frame_timing = frame_clock_.tick();
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        const bool chrome_consumed = window_.process_chrome_event(event);
        if (!chrome_consumed) {
          ui_.process_event(UIPlatformEvent{&event});
          if (event.type == SDL_EVENT_MOUSE_MOTION) {
            (void)window_.process_chrome_event(event);
          }
        }
        if (event.type == SDL_EVENT_QUIT ||
            (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
             sdl_event_targets_window(event, main_window_id))) {
          running_ = false;
        }
        if ((event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
             event.type == SDL_EVENT_WINDOW_RESIZED ||
             event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
             event.type == SDL_EVENT_WINDOW_RESTORED ||
             event.type == SDL_EVENT_WINDOW_SHOWN ||
             event.type == SDL_EVENT_WINDOW_EXPOSED) &&
            sdl_event_targets_window(event, main_window_id)) {
          window_.refresh_pixel_size();
        }
        if ((event.type == SDL_EVENT_WINDOW_FOCUS_GAINED ||
             event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
             event.type == SDL_EVENT_WINDOW_MAXIMIZED ||
             event.type == SDL_EVENT_WINDOW_RESTORED ||
             event.type == SDL_EVENT_WINDOW_SHOWN ||
             event.type == SDL_EVENT_WINDOW_HIDDEN ||
             event.type == SDL_EVENT_WINDOW_MINIMIZED ||
             event.type == SDL_EVENT_WINDOW_OCCLUDED) &&
            sdl_event_targets_window(event, main_window_id)) {
          window_.refresh_native_chrome();
        }
        }
        if (!running_) {
            break;
        }

        if (live_resize_frame_rendered_) {
          live_resize_frame_rendered_ = false;
        } else {
          if (window_.can_render_frames()) {
            render_frame(frame_timing);
          }
        }

        wait_until_next_frame();
    }

    return 0;
}

void App::shutdown() {
  if (shutdown_complete_) {
    return;
  }

  shutdown_complete_ = true;
  running_ = false;
  if (event_watch_installed_) {
    SDL_RemoveEventWatch(&App::live_resize_event_watch, this);
    event_watch_installed_ = false;
  }
  window_.set_live_resize_frame_callback({});
  save_window_settings();
  ui_.shutdown();
  ui_platform_adapter_.reset();
  editor_.reset();
  render_bridge_.shutdown();
  render_system_.shutdown();
  window_.destroy();
}

void App::handle_window_chrome_action(WindowChromeAction action) {
  switch (action) {
  case WindowChromeAction::Minimize:
    window_.minimize();
    break;
  case WindowChromeAction::ToggleMaximizeRestore:
    window_.toggle_maximize_restore();
    window_.refresh_native_chrome();
    break;
  case WindowChromeAction::Close:
    running_ = false;
    break;
  }
}

void App::save_window_settings() {
  if (window_settings_path_.empty()) {
    return;
  }

  const std::optional<PlatformWindowPlacement> placement = window_.placement();
  if (!placement) {
    return;
  }

  window_settings_.set_placement(*placement);
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
  if (window_.native_chrome_message_active() &&
      !window_.native_live_resize_frame_active()) {
    return false;
  }

  rendering_frame_ = true;

  window_.refresh_pixel_size();
  const std::uint32_t framebuffer_width = window_.width_pixels();
  const std::uint32_t framebuffer_height = window_.height_pixels();
  if (!window_.can_render_frames()) {
    rendering_frame_ = false;
    return false;
  }
  ui_.set_viewport_render_resize_suspended(
      window_.chrome_resize_active() ||
      window_.native_chrome_interaction_active());
  ui_.set_render_performance_stats(render_system_.frame_performance_stats());
  {
    QDR_PROFILE_SCOPE("UI begin frame");
    ui_.begin_frame(frame_timing, framebuffer_width, framebuffer_height);
  }
  bool quit_requested = false;
  {
    QDR_PROFILE_SCOPE("UI draw");
    ui_.draw(*editor_, render_bridge_, project_session_.scene(),
             project_session_.assets(), project_session_.modeling(),
             project_session_.selection(), default_material_, quit_requested);
  }
  if (const std::optional<WindowChromeAction> chrome_action =
          ui_.take_window_chrome_action()) {
    handle_window_chrome_action(*chrome_action);
    if (running_ && chrome_action_invalidates_current_frame(*chrome_action)) {
      ui_.end_frame_without_render();
      rendering_frame_ = false;
      return false;
    }
  }
  if (quit_requested) {
    running_ = false;
  }
  if (!running_) {
    ui_.end_frame_without_render();
    rendering_frame_ = false;
    return false;
  }
  if (!window_.can_render_frames()) {
    ui_.end_frame_without_render();
    rendering_frame_ = false;
    return false;
  }
  {
    QDR_PROFILE_SCOPE("Render sync scene");
    (void)QuaderRenderApiRuntimeAccess::sync_scene(editor_->render(),
                                                   render_bridge_);
  }
  const ProjectSessionReadView session_view = project_session_.read_view();
  if (const EntityId root = session_view.scene.scene_root();
      root != kInvalidEntityId) {
    if (const SceneSettingsComponent *settings =
            session_view.scene.scene_settings(root)) {
      render_system_.apply_scene_settings(*settings);
    }
  }

  {
    QDR_PROFILE_SCOPE("UI prepare viewports");
    ui_.prepare_viewports(*editor_, render_bridge_, session_view.scene,
                          session_view.assets);
  }
  {
    QDR_PROFILE_SCOPE("UI prepare render data");
    ui_.prepare_render_data(framebuffer_width, framebuffer_height);
  }

  {
    QDR_PROFILE_SCOPE("Render frame submit");
    if (render_system_.begin_frame()) {
      {
        QDR_PROFILE_SCOPE("Render viewport panes");
        ui_.render_viewports(render_system_);
      }
      {
        QDR_PROFILE_SCOPE("Render ImGui");
        ui_.render(render_system_);
      }
      render_system_.end_frame();
      window_.note_frame_presented(framebuffer_width, framebuffer_height);
      rendering_frame_ = false;
      QDR_PROFILE_FRAME_MARK();
      return true;
    }
  }

  rendering_frame_ = false;
  QDR_PROFILE_FRAME_MARK();
  return false;
}

void App::wait_until_next_frame() {
  const bool drawable = window_.can_render_frames();
  const Sint32 wait_ms =
      !drawable ? kSuspendedWindowWaitMs
                : (ui_.needs_continuous_rendering() ||
                           window_.chrome_resize_active() ||
                           window_.native_chrome_interaction_active()
                       ? kActiveFrameWaitMs
                       : kIdleFrameWaitMs);
  (void)SDL_WaitEventTimeout(nullptr, wait_ms);
}

bool App::should_render_live_resize_event(const SDL_Event &event) const {
  if (!running_ || shutdown_complete_ || rendering_frame_ ||
      event.type != SDL_EVENT_WINDOW_EXPOSED || event.window.data1 != 1 ||
      window_.sdl_window() == nullptr || !window_.can_render_frames()) {
    return false;
  }
  if (window_.native_chrome_interaction_active()) {
    return false;
  }
  if (!window_.chrome_resize_active()) {
    return false;
  }

  return sdl_event_targets_window(event, SDL_GetWindowID(window_.sdl_window()));
}

bool App::live_resize_event_watch(void *userdata, SDL_Event *event) {
  if (userdata == nullptr || event == nullptr ||
      event->type != SDL_EVENT_WINDOW_EXPOSED) {
    return true;
  }

  App *app = static_cast<App *>(userdata);
  if (!app->should_render_live_resize_event(*event)) {
    return true;
  }

  if (app->render_frame(app->frame_clock_.tick())) {
    app->live_resize_frame_rendered_ = true;
  }
  return true;
}

} // namespace quader
