////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/frame_timing.hpp"
#include "core/localization_service.hpp"
#include "core/types.hpp"
#include "render/render_performance_stats.hpp"
#include "ui/qt_auxiliary_window_state.hpp"
#include "ui/qt_main_menu.hpp"
#include "ui/qt_startup_presentation.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings.hpp"

#include <chrono>
#include <filesystem>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

class QDockWidget;
class QLabel;
class QMainWindow;
class QWidget;

namespace quader {

class AssetRegistry;
class ConsolePanel;
class EditorSelectionService;
class InspectorPanel;
class LinearArrayToolService;
class ModelingSessionService;
class OutlinerPanel;
class PivotToolService;
class QtSettingsDialog;
class QtCommandEventFilter;
class QtToolSettingsWindow;
class QtViewportPanel;
enum class QtSettingsChangeKind;
class QuaderEditorApi;
class RenderOverlaySnapshotCache;
class RenderSystem;
class SceneDocument;
class SceneRenderBridge;

/**
 * Owns Quader's retained Qt Widgets shell and all top-level presentation
 * surfaces. Domain state remains owned by editor, scene, viewport and render
 * services.
 */
class UISystem final {
public:
  UISystem();
  ~UISystem();

  UISystem(const UISystem &) = delete;
  UISystem &operator=(const UISystem &) = delete;

  bool initialize(RenderSystem &render_system,
                  const std::filesystem::path &executable_dir);
  void install_runtime_hooks(QuaderEditorApi &editor,
                             SceneDocument &document, AssetRegistry &assets,
                             ModelingSessionService &modeling,
                             EditorSelectionService &selection,
                             PivotToolService &pivot,
                             LinearArrayToolService &linear_array,
                             AssetId default_material);
  void show();
  void shutdown();

  void refresh(const FrameTiming &frame_timing,
               RenderFramePerformanceStats render_performance);
  void process_viewport_input();
  void prepare_viewports(SceneRenderBridge &render_bridge,
                         RenderOverlaySnapshotCache &snapshot_cache);
  [[nodiscard]] std::size_t render_viewports();
  [[nodiscard]] bool needs_continuous_rendering() const;

  [[nodiscard]] QMainWindow *main_window() const { return main_window_.get(); }
  [[nodiscard]] void *native_window_handle() const;
  [[nodiscard]] bool can_render_viewports() const;

  [[nodiscard]] bool save_settings();
  [[nodiscard]] bool save_viewport_settings();
  void save_window_state();

private:
  void create_docks();
  QDockWidget &create_dock(UIPanelId id, QWidget &content,
                           int default_area);
  void restore_window_state();
  void prepare_startup_presentation();
  void advance_startup_presentation();
  void finish_startup_presentation(bool forced_fallback);
  [[nodiscard]] bool startup_layout_ready() const;
  [[nodiscard]] std::uint64_t startup_layout_geometry_signature(
      int native_client_width_pixels,
      int native_client_height_pixels) const;
  void save_auxiliary_window_state();
  void synchronize_auxiliary_window_visibility();
  void refresh_status_bar();
  void refresh_localized_chrome();
  void handle_settings_dialog_change(QtSettingsChangeKind kind);
  void synchronize_settings_surfaces(bool refresh_dialog);
  void show_settings_dialog();
  [[nodiscard]] bool panel_visible(UIPanelId id) const;
  void set_panel_visible(UIPanelId id, bool visible);

  std::unique_ptr<QMainWindow> main_window_;
  std::unique_ptr<QtMainMenu> main_menu_;
  std::unique_ptr<QtCommandEventFilter> command_event_filter_;
  QtViewportPanel *viewport_panel_ = nullptr;
  OutlinerPanel *outliner_panel_ = nullptr;
  InspectorPanel *inspector_panel_ = nullptr;
  ConsolePanel *console_panel_ = nullptr;
  QtSettingsDialog *settings_dialog_ = nullptr;
  QtToolSettingsWindow *tool_settings_window_ = nullptr;
  QLabel *status_message_ = nullptr;
  QLabel *status_version_ = nullptr;
  UIPanelManager panels_;
  LocalizationService localization_;
  UISettings settings_;
  UISettingsPaths settings_paths_;
  QtAuxiliaryWindowState auxiliary_window_state_;
  ViewportSettings viewport_settings_;
  RenderSystem *render_system_ = nullptr;
  QuaderEditorApi *editor_ = nullptr;
  SceneDocument *document_ = nullptr;
  AssetRegistry *assets_ = nullptr;
  ModelingSessionService *modeling_ = nullptr;
  EditorSelectionService *selection_ = nullptr;
  PivotToolService *pivot_ = nullptr;
  LinearArrayToolService *linear_array_ = nullptr;
  AssetId default_material_ = kInvalidAssetId;
  FrameTiming frame_timing_{};
  RenderFramePerformanceStats render_performance_{};
  QtStartupPresentationState startup_presentation_;
  std::chrono::steady_clock::time_point startup_presentation_started_{};
  bool startup_expected_maximized_ = false;
  bool initialized_ = false;
  bool ui_settings_changed_ = false;
  std::string localized_locale_;
  std::uint64_t outliner_scene_revision_ = 0;
  std::uint64_t inspector_scene_revision_ = 0;
  std::uint64_t inspector_modeling_content_revision_ = 0;
  std::uint64_t inspector_modeling_selection_revision_ = 0;
  std::string outliner_locale_;
  std::string inspector_locale_;
  bool outliner_refresh_valid_ = false;
  bool inspector_refresh_valid_ = false;
};

} // namespace quader
