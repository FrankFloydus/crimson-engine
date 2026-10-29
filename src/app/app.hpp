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
#include "render/render_host.hpp"
#include "render/render_scene_sink.hpp"
#include "render/render_source_stamp.hpp"
#include "ui/ui_system.hpp"

#include <QMetaObject>
#include <QPointer>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

class QApplication;
class QScreen;
class QTimer;
namespace crimson::integration {class LevelSession;class LevelPanels;}
namespace crimson::integration {class NativeAssetRetention;}
namespace crimson::integration {class LevelProfiling;}
namespace crimson::profiling {class EngineProfile;class FrameProfile;}

namespace quader {

class QuaderEditorApi;
class ProjectSessionRenderMaterialResolver;

/// Options owned by the native Qt application shell.
struct AppOptions {
  RendererBackend render_backend = RendererBackend::Vulkan;
  std::optional<RenderCompositionApi> composition_api;
  std::string profile_mode;
  std::uint32_t profile_duration{15};
  std::filesystem::path profile_output,frame_profile;
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
  bool prepare_composed_shell();
  bool initialize_shell();
  void retire_shell();
  void replace_shell();
  void apply_window_settings();
  void apply_home_window_placement();
  void install_display_refresh_rate_sync();
  void bind_display_refresh_rate_screen(QScreen *screen);
  void synchronize_display_refresh_rate(QScreen *screen);
  void save_window_settings();
  void bind_current_scene();
  void bind_active_scene(SceneSession &scene);
  bool render_frame(const FrameTiming &frame_timing);
  void prepare_composed_frame();
  void request_viewport_presentation();
  void schedule_next_frame(bool submitted = false);
  void poll_autosave();
#if defined(CRIMSON_ENGINE_APPLICATION)
  void synchronize_engine_session();
  void synchronize_modeling_world();
  bool bind_engine_project(const std::filesystem::path&);
  std::unique_ptr<crimson::integration::LevelSession> engine_level_;
  std::unique_ptr<crimson::integration::LevelPanels> engine_panels_;
  std::shared_ptr<crimson::integration::NativeAssetRetention> native_asset_retention_;
  std::uint64_t retention_project_generation_{UINT64_MAX};
  std::filesystem::path bound_project_root_,rejected_project_root_;
  std::uint64_t workspace_project_generation_{UINT64_MAX};
  std::uint64_t engine_asset_catalog_revision_{UINT64_MAX};
  std::shared_ptr<const RenderSceneSnapshot> modeling_authoring_source_;
  std::optional<RenderSourceStamp> modeling_authoring_stamp_;
  std::unique_ptr<crimson::profiling::EngineProfile> engine_profile_;
  std::unique_ptr<crimson::profiling::FrameProfile> frame_profile_;
  std::unique_ptr<crimson::integration::LevelProfiling> profiling_controls_;
  std::uint32_t profile_frame_{};
  bool profile_backend_ready_{};
#endif

  QApplication *application_ = nullptr;
  std::unique_ptr<QTimer> frame_timer_;
  std::unique_ptr<QTimer> autosave_timer_;
  QMetaObject::Connection window_screen_changed_connection_;
  QMetaObject::Connection screen_refresh_rate_changed_connection_;
  QPointer<QScreen> display_refresh_screen_;
  QPointer<QScreen> placement_screen_;
  AppFrameClock frame_clock_;
  AppFrameSchedule frame_schedule_;
  AppFramePresentationRequest viewport_presentation_request_;
  std::unique_ptr<RenderHost> render_system_;
  std::unique_ptr<RenderSceneSink> render_bridge_;
  std::unique_ptr<ProjectSessionRenderMaterialResolver> project_material_resolver_;
  UISystem ui_;
  ProjectSession project_session_;
  RenderOverlaySnapshotCache render_overlay_snapshot_cache_;
  std::unique_ptr<QuaderEditorApi> editor_;
  std::filesystem::path executable_dir_;
  std::filesystem::path window_settings_path_;
  AppWindowSettings window_settings_;
  AppOptions options_;
  AssetId default_material_ = kInvalidAssetId;
  std::optional<QuaderSceneDocumentBinding> scene_settings_binding_;
  std::uint64_t scene_settings_revision_ = 0u;
  bool running_ = false;
  bool shutdown_complete_ = false;
  bool rendering_frame_ = false;
  bool preparing_composition_ = false;
  bool shell_replacement_pending_ = false;
  bool shell_replacement_started_ = false;
};

} // namespace quader
