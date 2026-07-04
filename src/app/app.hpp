////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "app/app_frame_clock.hpp"
#include "app/app_window_settings.hpp"
#include "core/types.hpp"
#include "editor/session/project_session.hpp"
#include "platform/platform_event.hpp"
#include "platform/platform_window.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/ui_system.hpp"

#include <filesystem>
#include <memory>

namespace quader {

class QuaderEditorApi;
class UIPlatformAdapter;

/**
 * Stores the App Options data contract used by the native application shell.
 */
struct AppOptions {
  bool borderless_window = true;
  RendererBackend render_backend = RendererBackend::Vulkan;
};

/**
 * Owns App behavior within the native application shell.
 */
class App {
public:
    App();
    ~App();

    bool initialize(const std::filesystem::path &executable_path,
                    AppOptions options = {});
    int run();
    void shutdown();

private:
  void handle_window_chrome_action(WindowChromeAction action);
  void save_window_settings();
  bool render_frame(const FrameTiming &frame_timing);
  void wait_until_next_frame();
  [[nodiscard]] bool
  should_render_live_resize_event(const PlatformEvent &event) const;
  static bool live_resize_event_watch(void *userdata,
                                      const PlatformEvent &event);

  AppFrameClock frame_clock_;
  PlatformWindow window_;
  RenderSystem render_system_;
  SceneRenderBridge render_bridge_;
  UISystem ui_;
  std::unique_ptr<UIPlatformAdapter> ui_platform_adapter_;
  ProjectSession project_session_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::filesystem::path window_settings_path_;
  AppWindowSettings window_settings_;
  AssetId default_material_ = kInvalidAssetId;
  bool running_ = false;
  bool shutdown_complete_ = false;
  bool rendering_frame_ = false;
  bool live_resize_frame_rendered_ = false;
  PlatformEventWatch live_resize_event_watch_;
};

} // namespace quader
