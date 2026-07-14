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
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/ui_system.hpp"

#include <filesystem>
#include <memory>

class QApplication;
class QTimer;

namespace quader {

class QuaderEditorApi;

/// Options owned by the native Qt application shell.
struct AppOptions {
  RendererBackend render_backend = RendererBackend::Vulkan;
};

/// Coordinates Qt lifecycle, editor services and render submission.
class App final {
public:
  App();
  ~App();

  bool initialize(QApplication &application,
                  const std::filesystem::path &executable_path,
                  AppOptions options = {});
  int run();
  void shutdown();

private:
  void apply_window_settings();
  void save_window_settings();
  void bind_active_scene(SceneSession &scene);
  bool render_frame(const FrameTiming &frame_timing);
  void schedule_next_frame();

  QApplication *application_ = nullptr;
  std::unique_ptr<QTimer> frame_timer_;
  AppFrameClock frame_clock_;
  RenderSystem render_system_;
  SceneRenderBridge render_bridge_;
  UISystem ui_;
  ProjectSession project_session_;
  RenderOverlaySnapshotCache render_overlay_snapshot_cache_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::filesystem::path window_settings_path_;
  AppWindowSettings window_settings_;
  AssetId default_material_ = kInvalidAssetId;
  bool running_ = false;
  bool shutdown_complete_ = false;
  bool rendering_frame_ = false;
};

} // namespace quader
