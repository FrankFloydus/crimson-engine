////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_registry.hpp"
#include "core/frame_timing.hpp"
#include "core/localization_service.hpp"
#include "core/types.hpp"
#include "render/imgui_filament_renderer.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_renderer.hpp"
#include "scene/scene_document.hpp"
#include "ui/main_menu_bar.hpp"
#include "ui/tool_settings_panel.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/ui_icon_set.hpp"
#include "ui/ui_settings.hpp"
#include "ui/ui_style_tokens.hpp"
#include "ui/viewport_settings_panel.hpp"
#include "ui/viewport_panel.hpp"
#include "viewport/viewport_settings.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace quader {

class QuaderEditorApi;
class EditorSelectionService;
class ModelingSessionService;
class UIPlatformAdapter;
struct UIPlatformEvent;

/**
 * Enumerates native window chrome actions requested by the ImGui shell.
 */
enum class WindowChromeAction {
    Minimize,
    ToggleMaximizeRestore,
    Close,
};

/**
 * Owns UI System behavior within the ImGui editor presentation layer.
 */
class UISystem {
public:
    ~UISystem();

    void initialize(UIPlatformAdapter &platform_adapter,
                    RenderSystem &render_system,
                    const std::filesystem::path &executable_dir,
                    bool custom_chrome);
    void install_runtime_hooks(QuaderEditorApi &editor);
    void shutdown();

    bool process_event(const UIPlatformEvent &event);
    void begin_frame(const FrameTiming &frame_timing,
                     std::uint32_t framebuffer_width,
                     std::uint32_t framebuffer_height);
    void end_frame_without_render();
    void draw(QuaderEditorApi &editor, SceneRenderBridge &render_bridge,
              SceneDocument &document, AssetRegistry &assets,
              ModelingSessionService &modeling,
              EditorSelectionService &selection, AssetId default_material,
              bool &quit_requested);
    void prepare_render_data(std::uint32_t framebuffer_width,
                             std::uint32_t framebuffer_height);
    void prepare_viewports(QuaderEditorApi &editor,
                           SceneRenderBridge &render_bridge,
                           const SceneDocument &document,
                           const AssetRegistry &assets);
    void render_viewports(RenderSystem &render_system);
    void render(RenderSystem &render_system);
    void set_render_performance_stats(RenderFramePerformanceStats stats);
    void set_viewport_render_resize_suspended(bool suspended);
    [[nodiscard]] bool needs_continuous_rendering() const;
    std::optional<WindowChromeAction> take_window_chrome_action();
    const ViewportToolState &viewport_tool_state() const {
      return viewport_panel_->tool_state();
    }

private:
  void register_panels(RenderSystem &render_system);
  MainMenuBarResult draw_window_chrome(QuaderEditorApi &editor,
                                       bool &quit_requested,
                                       SceneDocument &document,
                                       ModelingSessionService &modeling);
  void draw_dockspace(QuaderEditorApi &editor, bool &quit_requested,
                      SceneDocument &document, ModelingSessionService &modeling,
                      float sidebar_minimum_width);
  void build_default_dock_layout(ImGuiID dockspace_id, ImVec2 dockspace_size);
  void close_menu_popup_for_pass_through_click();
  void enforce_sidebar_dock_minimums(float sidebar_minimum_width);
  void set_viewport_cursor_captured(bool captured);
  void warp_captured_cursor(QVec2 point);
  void save_settings();
  void save_viewport_settings();

  UIPlatformAdapter *platform_adapter_ = nullptr;
  RenderSystem *render_system_ = nullptr;
  ImGuiFilamentRenderer renderer_;
  UIStyleTokens style_;
  UIPanelManager panels_;
  MainMenuBar main_menu_bar_;
  ViewportSettingsPanel viewport_settings_panel_;
  ToolSettingsPanel tool_settings_panel_;
  ViewportPanel *viewport_panel_ = nullptr;
  UIIconSet icons_;
  LocalizationService localization_;
  UISettings settings_;
  UISettingsPaths settings_paths_;
  ViewportSettings viewport_settings_;
  std::string imgui_ini_path_;
  bool settings_panel_open_ = false;
  bool tool_settings_panel_open_ = false;
  bool initialized_ = false;
  bool custom_chrome_ = false;
  bool dock_layout_built_ = false;
  bool has_saved_dock_layout_ = false;
  bool main_menu_popup_open_ = false;
  bool relative_mouse_captured_ = false;
  bool relative_mouse_capture_warning_issued_ = false;
  QVec2 relative_mouse_delta_ = {};
  FrameTiming current_frame_timing_ = {};
  RenderFramePerformanceStats render_performance_stats_;
  UIPanelId focused_panel_ = UIPanelId::Viewport;
  std::optional<WindowChromeAction> pending_chrome_action_;
  bool viewport_render_resize_suspended_ = false;
};

} // namespace quader
