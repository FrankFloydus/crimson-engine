////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_query_api.hpp"
#include "app/app_frame_clock.hpp"
#include "app/app_frame_schedule.hpp"
#include "app/app_window_settings.hpp"
#include "core/types.hpp"
#include "editor/session/project_session.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "ui/ui_system.hpp"

#include <QMetaObject>
#include <QPointer>

#include <filesystem>
#include <memory>
#include <optional>

class QApplication;
class QScreen;
class QTimer;

namespace quader {

class QuaderEditorApi;
class ProjectSessionRenderMaterialResolver;

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
  void apply_home_window_placement();
  void install_display_refresh_rate_sync();
  void bind_display_refresh_rate_screen(QScreen *screen);
  void synchronize_display_refresh_rate(QScreen *screen);
  void save_window_settings();
  void bind_active_scene(SceneSession &scene);
  bool render_frame(const FrameTiming &frame_timing);
  void prepare_composed_frame();
  void request_viewport_presentation();
  void schedule_next_frame(bool submitted = false);
  void poll_autosave();

  QApplication *application_ = nullptr;
  std::unique_ptr<QTimer> frame_timer_;
  std::unique_ptr<QTimer> autosave_timer_;
  QMetaObject::Connection window_screen_changed_connection_;
  QMetaObject::Connection screen_refresh_rate_changed_connection_;
  QPointer<QScreen> display_refresh_screen_;
  AppFrameClock frame_clock_;
  AppFrameSchedule frame_schedule_;
  AppFramePresentationRequest viewport_presentation_request_;
  RenderSystem render_system_;
  SceneRenderBridge render_bridge_;
  std::unique_ptr<ProjectSessionRenderMaterialResolver> project_material_resolver_;
  UISystem ui_;
  ProjectSession project_session_;
  RenderOverlaySnapshotCache render_overlay_snapshot_cache_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::filesystem::path executable_dir_;
  std::filesystem::path window_settings_path_;
  AppWindowSettings window_settings_;
  AssetId default_material_ = kInvalidAssetId;
  std::optional<QuaderSceneDocumentBinding> scene_settings_binding_;
  std::uint64_t scene_settings_revision_ = 0u;
  bool running_ = false;
  bool shutdown_complete_ = false;
  bool rendering_frame_ = false;
  bool preparing_composition_ = false;
};

} // namespace quader
