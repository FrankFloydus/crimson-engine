////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/ui_system.hpp"

#include "api/quader_editor_api.hpp"
#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/viewport/quader_viewport_api_runtime_access.hpp"
#include "quader_app_version.hpp"
#include "render/scene_render_extraction.hpp"
#include "ui/console_panel.hpp"
#include "ui/imgui_popup_utils.hpp"
#include "ui/inspector_panel.hpp"
#include "ui/outliner_panel.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_platform_adapter.hpp"
#include "ui/viewport_input_frame_builder.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float kWindowChromeHeight = 35.0f;
constexpr float kWindowChromeControlsWidth = 105.0f;
constexpr float kWindowChromeButtonWidth = 35.0f;
constexpr float kStaticBottomBarHeight = 28.0f;
constexpr float kStaticBottomBarAccentHeight = 3.0f;
constexpr float kChromeLogoSize = 18.0f;
constexpr float kChromeLogoLeft = 16.0f;
constexpr float kChromeMenuLeft = 48.0f;
constexpr float kChromeMenuSpacing = 10.0f;
constexpr float kCompressedSidebarMinWidth = 220.0f;
constexpr float kReservedViewportDockWidth = 300.0f;

ImVec4 ui_color(int red, int green, int blue, float alpha = 1.0f) {
  return {
      static_cast<float>(red) / 255.0f,
      static_cast<float>(green) / 255.0f,
      static_cast<float>(blue) / 255.0f,
      alpha,
  };
}

ImU32 ui_packed_color(int red, int green, int blue, int alpha = 255) {
  return IM_COL32(red, green, blue, alpha);
}

UIIconSet to_ui_icon_set(const RenderUiIconSet &icons) {
  return UIIconSet{
      .logo = static_cast<ImTextureID>(icons.logo),
      .mesh = static_cast<ImTextureID>(icons.mesh),
      .empty = static_cast<ImTextureID>(icons.empty),
      .lock = static_cast<ImTextureID>(icons.lock),
      .unlock = static_cast<ImTextureID>(icons.unlock),
      .hide = static_cast<ImTextureID>(icons.hide),
      .show = static_cast<ImTextureID>(icons.show),
  };
}

void clamp_dock_leaf_node_width(ImGuiDockNode *node, float minimum_width) {
  if (node == nullptr || node->ParentNode == nullptr ||
      node->ParentNode->SplitAxis != ImGuiAxis_X) {
    return;
  }

  ImGuiDockNode *parent = node->ParentNode;
  ImGuiDockNode *sibling = parent->ChildNodes[0] == node
                               ? parent->ChildNodes[1]
                               : parent->ChildNodes[0];
  if (sibling == nullptr || parent->Size.x <= minimum_width + 1.0f ||
      node->Size.x >= minimum_width) {
    return;
  }

  const bool node_is_left_child = parent->ChildNodes[0] == node;
  const float clamped_width = std::min(minimum_width, parent->Size.x - 1.0f);
  const float sibling_width = std::max(1.0f, parent->Size.x - clamped_width);

  node->Size.x = clamped_width;
  node->SizeRef.x = clamped_width;
  sibling->Size.x = sibling_width;
  sibling->SizeRef.x = sibling_width;

  if (node_is_left_child) {
    node->Pos.x = parent->Pos.x;
    sibling->Pos.x = parent->Pos.x + clamped_width;
  } else {
    sibling->Pos.x = parent->Pos.x;
    node->Pos.x = parent->Pos.x + sibling_width;
  }

  node->WantLockSizeOnce = true;
  sibling->WantLockSizeOnce = true;
}

void clamp_docked_window_width(const char *window_name, float minimum_width) {
  ImGuiWindow *window = ImGui::FindWindowByName(window_name);
  if (window == nullptr || window->DockNode == nullptr) {
    return;
  }
  clamp_dock_leaf_node_width(window->DockNode, minimum_width);
}

int visible_sidebar_count(const UIPanelManager &panels) {
  int count = 0;
  if (panels.is_visible(UIPanelId::Outliner)) {
    ++count;
  }
  if (panels.is_visible(UIPanelId::Inspector)) {
    ++count;
  }
  return count;
}

float effective_sidebar_minimum_width(float dockspace_width,
                                      const UIPanelManager &panels,
                                      float preferred_minimum_width) {
  const int sidebar_count = visible_sidebar_count(panels);
  if (sidebar_count <= 0) {
    return preferred_minimum_width;
  }

  const float available_for_sidebars =
      std::max(1.0f, dockspace_width - kReservedViewportDockWidth);
  const float width_per_sidebar =
      available_for_sidebars / static_cast<float>(sidebar_count);
  return std::min(preferred_minimum_width,
                  std::max(kCompressedSidebarMinWidth, width_per_sidebar));
}

std::optional<UIPanelId> panel_id_from_name(std::string_view name) {
  for (const UIPanelId id : {UIPanelId::Viewport, UIPanelId::Outliner,
                             UIPanelId::Inspector, UIPanelId::Console}) {
    if (name == ui_panel_stable_id(id) || name == ui_panel_key(id)) {
      return id;
    }
  }
  return std::nullopt;
}

bool chrome_icon_button(const char *stable_id, WindowChromeAction action,
                        const ImVec2 &size) {
  ImGui::PushID(stable_id);
  const bool clicked = ImGui::InvisibleButton("##button", size);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  const ImVec2 center{std::floor((min.x + max.x) * 0.5f) + 0.5f,
                      std::floor((min.y + max.y) * 0.5f) + 0.5f};
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  if (hovered || active) {
    ImU32 background = ui_packed_color(46, 46, 46);
    if (active) {
      background = ui_packed_color(60, 60, 60);
    }
    if (action == WindowChromeAction::Close) {
      background =
          active ? ui_packed_color(155, 32, 32) : ui_packed_color(206, 59, 59);
    }
    draw_list->AddRectFilled(min, max, background);
  }

  const ImU32 icon_color = hovered || active ? ui_packed_color(247, 247, 247)
                                             : ui_packed_color(188, 188, 188);
  const float thickness = 1.25f;
  const float half = 4.5f;
  if (action == WindowChromeAction::Minimize) {
    draw_list->AddLine({center.x - half, center.y + 0.5f},
                       {center.x + half, center.y + 0.5f}, icon_color,
                       thickness);
  } else if (action == WindowChromeAction::ToggleMaximizeRestore) {
    draw_list->AddRect({center.x - half, center.y - half},
                       {center.x + half, center.y + half}, icon_color, 0.0f, 0,
                       thickness);
  } else {
    draw_list->AddLine({center.x - half, center.y - half},
                       {center.x + half, center.y + half}, icon_color,
                       thickness);
    draw_list->AddLine({center.x + half, center.y - half},
                       {center.x - half, center.y + half}, icon_color,
                       thickness);
  }

  ImGui::PopID();
  return clicked;
}

QuaderApiVoidResult runtime_status(bool changed, std::string message = {}) {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .mutation_kind =
          changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None,
  };
}

QuaderApiVoidResult runtime_failure(std::string message) {
  return {
      .ok = false,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::None,
  };
}

std::string
status_bar_message(const ModelingLiveStatus &live_status,
                   const LocalizationService &localization,
                   const std::optional<QuaderApiStatus> &command_status) {
  if (live_status.kind ==
      ModelingLiveStatusKind::MergeByDistanceMergedVertices) {
    std::string message =
        localization.text("ui.status.merge_by_distance.merged_vertices",
                          "Merged {count} vertices");
    const std::string count = std::to_string(live_status.count);
    constexpr std::string_view kCountToken = "{count}";
    if (const std::size_t token = message.find(kCountToken);
        token != std::string::npos) {
      message.replace(token, kCountToken.size(), count);
    }
    return message;
  }
  if (!command_status) {
    return {};
  }
  if (!command_status->ok && command_status->message.empty()) {
    return "Command failed.";
  }
  return command_status->message;
}

std::string ellipsized_text_to_width(std::string text, float max_width) {
  if (text.empty() || max_width <= 0.0f) {
    return {};
  }
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) {
    return text;
  }

  constexpr const char *kEllipsis = "...";
  const float ellipsis_width = ImGui::CalcTextSize(kEllipsis).x;
  if (ellipsis_width > max_width) {
    return {};
  }

  while (!text.empty()) {
    std::string candidate = text;
    candidate.append(kEllipsis);
    if (ImGui::CalcTextSize(candidate.c_str()).x <= max_width) {
      return candidate;
    }
    text.pop_back();
  }
  return {};
}

bool dispatch_focused_panel_command_shortcut(
    QuaderEditorApi &editor, const ViewportToolState &viewport_tool_state,
    UIPanelId focused_panel, bool command_shortcut_consumed,
    std::span<const QuaderInputChord> pressed_input_chords) {
  if (command_shortcut_consumed || focused_panel == UIPanelId::Viewport) {
    return false;
  }

  const ImGuiKeyboardInputChords keyboard =
      collect_im_gui_keyboard_input_chords();
  if (keyboard.text_input_active) {
    editor.commands().reset_pending_keybinding_chord();
    return false;
  }

  const std::span<const QuaderInputChord> pressed_chords =
      pressed_input_chords.empty()
          ? std::span<const QuaderInputChord>(keyboard.pressed)
          : pressed_input_chords;

  for (const QuaderInputChord &chord : pressed_chords) {
    const QuaderViewportKeybindingDispatch dispatch =
        editor.commands().dispatch_viewport_input_binding(
            chord, viewport_tool_state, false, false);
    if (dispatch.kind == QuaderViewportKeybindingDispatch::Kind::PendingChord) {
      return true;
    }
    if (dispatch.kind != QuaderViewportKeybindingDispatch::Kind::Command) {
      continue;
    }

    const QuaderApiVoidResult result =
        editor.commands().execute(dispatch.command_id);
    if (!result.ok) {
      LOG_ERROR("Global command shortcut '{}' failed: {}", dispatch.command_id,
                result.message.empty() ? "Command failed." : result.message);
    }
    return true;
  }

  return false;
}

void draw_static_bottom_bar(
    const ImGuiViewport &viewport, const ModelingLiveStatus &live_status,
    const LocalizationService &localization,
    const std::optional<QuaderApiStatus> &command_status) {
  ImGui::SetNextWindowPos(
      {viewport.Pos.x,
       viewport.Pos.y +
           std::max(0.0f, viewport.Size.y - kStaticBottomBarHeight)});
  ImGui::SetNextWindowSize(
      {std::max(1.0f, viewport.Size.x), kStaticBottomBarHeight});
  ImGui::SetNextWindowViewport(viewport.ID);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ui_color(18, 19, 22));
  ImGui::Begin("Quader Static Bottom Bar", nullptr, flags);
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(3);

  const ImVec2 min = ImGui::GetWindowPos();
  const ImVec2 max{min.x + ImGui::GetWindowWidth(),
                   min.y + kStaticBottomBarHeight};
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  draw_list->AddRectFilled(min, max, ui_packed_color(18, 19, 22));
  draw_list->AddLine(min, {max.x, min.y}, ui_packed_color(72, 74, 82));
  draw_list->AddRectFilled({min.x, max.y - kStaticBottomBarAccentHeight}, max,
                           ui_packed_color(118, 92, 168));

  const float bottom_bar_text_height =
      kStaticBottomBarHeight - kStaticBottomBarAccentHeight;
  const float text_y =
      min.y +
      std::floor((bottom_bar_text_height - ImGui::GetTextLineHeight()) * 0.5f);

  const char *right_label = kQuaderAppVersionDisplay;
  const ImVec2 right_label_size = ImGui::CalcTextSize(right_label);
  const float right_label_x =
      std::max(min.x + 72.0f, max.x - right_label_size.x - 12.0f);
  const float message_max_width = std::max(0.0f, right_label_x - min.x - 24.0f);
  const std::string message = ellipsized_text_to_width(
      status_bar_message(live_status, localization, command_status),
      message_max_width);
  if (!message.empty()) {
    const bool live_message_active =
        live_status.kind != ModelingLiveStatusKind::None;
    const ImU32 message_color =
        !live_message_active && command_status && !command_status->ok
            ? ui_packed_color(238, 117, 117)
            : ui_packed_color(216, 216, 216);
    draw_list->AddText({min.x + 12.0f, text_y}, message_color, message.c_str());
  }

  draw_list->AddText({right_label_x, text_y}, ui_packed_color(150, 154, 163),
                     right_label);

  ImGui::End();
}

} // namespace

UISystem::~UISystem() { shutdown(); }

void UISystem::initialize(UIPlatformAdapter &platform_adapter,
                          RenderSystem &render_system,
                          const std::filesystem::path &executable_dir,
                          bool custom_chrome) {
  platform_adapter_ = &platform_adapter;
  render_system_ = &render_system;
  custom_chrome_ = custom_chrome;
  settings_paths_ = UISettings::resolve_paths(executable_dir);
  std::error_code error;
  std::filesystem::create_directories(settings_paths_.directory, error);
  settings_ = UISettings::load(settings_paths_.settings_file);
  localization_.load(executable_dir / "locales", settings_.locale());
  if (settings_.locale() != localization_.current_locale()) {
    settings_.set_locale(localization_.current_locale());
  }
  viewport_settings_ =
      ViewportSettings::load(settings_paths_.viewport_settings_file);
  imgui_ini_path_ = settings_paths_.imgui_ini_file.string();
  const bool ini_exists =
      std::filesystem::exists(settings_paths_.imgui_ini_file, error);
  const std::uintmax_t ini_size =
      ini_exists
          ? std::filesystem::file_size(settings_paths_.imgui_ini_file, error)
          : 0u;
  has_saved_dock_layout_ = ini_exists && !error && ini_size > 0u;

  register_panels(render_system);
  panels_.apply_settings(settings_);
  if (viewport_panel_ != nullptr) {
    viewport_panel_->set_navigation_gizmo_visible(
        settings_.navigation_gizmo_visible());
  }

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.IniFilename = imgui_ini_path_.c_str();

  load_ui_default_font(executable_dir, style_);
  apply_ui_style(style_);

  LOG_INFO("Initializing ImGui platform backend");
  platform_adapter_->initialize_imgui_backend();
  LOG_INFO("Initializing ImGui Filament renderer");
  render_system.initialize_imgui_renderer(renderer_);
  icons_ = to_ui_icon_set(render_system.load_ui_icon_textures(executable_dir));
  initialized_ = true;
  LOG_INFO("ImGui docking UI initialized");
}

void UISystem::shutdown() {
  if (!initialized_) {
    return;
  }

  set_viewport_cursor_captured(false);
  save_settings();
  save_viewport_settings();
  if (!imgui_ini_path_.empty()) {
    ImGui::SaveIniSettingsToDisk(imgui_ini_path_.c_str());
  }

  if (viewport_panel_ != nullptr) {
    viewport_panel_->shutdown();
  }
  renderer_.shutdown();
  if (render_system_ != nullptr) {
    render_system_->clear_ui_icon_textures();
  }
  icons_ = UIIconSet{};
  if (platform_adapter_ != nullptr) {
    platform_adapter_->shutdown_imgui_backend();
  }
  ImGui::DestroyContext();
  initialized_ = false;
  platform_adapter_ = nullptr;
  render_system_ = nullptr;
  custom_chrome_ = false;
  pending_chrome_action_.reset();
  viewport_panel_ = nullptr;
}

bool UISystem::process_event(const UIPlatformEvent &event) {
  if (event.pressed_input_chord) {
    pending_pressed_input_chords_.push_back(*event.pressed_input_chord);
  }
  return platform_adapter_ != nullptr &&
         platform_adapter_->process_event(event);
}

void UISystem::begin_frame(const FrameTiming &frame_timing,
                           std::uint32_t framebuffer_width,
                           std::uint32_t framebuffer_height) {
  QDR_PROFILE_FUNCTION();
  current_frame_timing_ = frame_timing;
  renderer_.release_frame_resources();
  relative_mouse_delta_ = {};
  if (relative_mouse_captured_) {
    relative_mouse_delta_ = platform_adapter_->relative_mouse_delta();
  }
  platform_adapter_->begin_frame();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = {
      static_cast<float>(std::max(1u, framebuffer_width)),
      static_cast<float>(std::max(1u, framebuffer_height)),
  };
  io.DisplayFramebufferScale = {1.0f, 1.0f};
  ImGui::GetIO().DeltaTime =
      static_cast<float>(std::max(current_frame_timing_.delta_seconds, 0.0));
  ImGui::NewFrame();
}

void UISystem::end_frame_without_render() {
  if (ImGui::GetCurrentContext() != nullptr) {
    ImGui::EndFrame();
  }
  pending_pressed_input_chords_.clear();
}

void UISystem::install_runtime_hooks(QuaderEditorApi &editor,
                                     SceneDocument &document,
                                     AssetRegistry &assets,
                                     ModelingSessionService &modeling) {
  editor.viewports().set_runtime_hooks(QuaderViewportRuntimeHooks{
      .frame_all =
          [this, &editor, &document, &assets](ViewportPaneId pane) {
            if (viewport_panel_ == nullptr) {
              return runtime_failure(
                  localization_.text("ui.runtime.viewport_panel_unavailable",
                                     "Viewport panel is not available."));
            }
            return viewport_panel_->frame_all(
                QuaderViewportApiRuntimeAccess::workspace(editor.viewports()),
                document, assets, pane);
          },
      .frame_selection =
          [this, &editor, &document, &assets, &modeling](ViewportPaneId pane) {
            if (viewport_panel_ == nullptr) {
              return runtime_failure(
                  localization_.text("ui.runtime.viewport_panel_unavailable",
                                     "Viewport panel is not available."));
            }
            return viewport_panel_->frame_selection(
                QuaderViewportApiRuntimeAccess::workspace(editor.viewports()),
                document, assets, modeling, pane);
          },
  });
#ifndef NDEBUG
  editor.dev_tools().set_runtime_hooks(QuaderDevToolsRuntimeHooks{
      .set_clipboard_text =
          [this](std::string_view text, std::string &error_message) {
            if (platform_adapter_ == nullptr) {
              error_message =
                  localization_.text("ui.runtime.platform_unavailable",
                                     "UI platform is not available.");
              return false;
            }
            return platform_adapter_->set_clipboard_text(text, error_message);
          },
  });
#endif
  editor.ui().set_runtime_hooks(QuaderUIRuntimeHooks{
      .is_panel_visible =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            return id.has_value() && panels_.is_visible(*id);
          },
      .set_panel_visible =
          [this](std::string_view panel, bool visible) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            const bool before = panels_.is_visible(*id);
            panels_.set_visible(*id, visible);
            if (before != visible) {
              save_settings();
            }
            return runtime_status(before != visible);
          },
      .focus_panel =
          [this](std::string_view panel) {
            const std::optional<UIPanelId> id = panel_id_from_name(panel);
            if (!id.has_value()) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_panel", "Unknown UI panel."));
            }
            focused_panel_ = *id;
            return runtime_status(true);
          },
  });
  editor.preferences().set_runtime_hooks(QuaderPreferencesRuntimeHooks{
      .locale = [this]() { return settings_.locale(); },
      .set_locale =
          [this](std::string locale) {
            if (!localization_.select_locale(locale)) {
              return runtime_failure(localization_.text(
                  "ui.runtime.unknown_locale", "Unknown locale."));
            }
            const bool changed =
                settings_.locale() != localization_.current_locale();
            settings_.set_locale(localization_.current_locale());
            if (changed) {
              save_settings();
            }
            return runtime_status(changed);
          },
      .save =
          [this]() {
            save_settings();
            save_viewport_settings();
            return runtime_status(
                false, localization_.text("ui.runtime.preferences_saved",
                                          "Preferences saved."));
          },
      .navigation_gizmo_visible =
          [this]() {
            return viewport_panel_ != nullptr &&
                   viewport_panel_->navigation_gizmo_visible();
          },
      .set_navigation_gizmo_visible =
          [this](bool visible) {
            if (viewport_panel_ == nullptr) {
              return runtime_failure(
                  localization_.text("ui.runtime.viewport_panel_unavailable",
                                     "Viewport panel is not available."));
            }
            const bool before = viewport_panel_->navigation_gizmo_visible();
            viewport_panel_->set_navigation_gizmo_visible(visible);
            settings_.set_navigation_gizmo_visible(visible);
            save_settings();
            return runtime_status(before != visible);
          },
  });
}

void UISystem::draw(QuaderEditorApi &editor, SceneRenderBridge &render_bridge,
                    SceneDocument &document, AssetRegistry &assets,
                    ModelingSessionService &modeling,
                    EditorSelectionService &selection, PivotToolService &pivot,
                    AssetId default_material, bool &quit_requested) {
  QDR_PROFILE_FUNCTION();
  {
    QDR_PROFILE_SCOPE("UI menus and chrome");
    close_menu_popup_for_pass_through_click();
  }
  const MainMenuBarResult chrome_menu_result = [&]() {
    QDR_PROFILE_SCOPE("UI draw window chrome");
    return draw_window_chrome(editor, quit_requested, document, modeling);
  }();
  main_menu_popup_open_ = chrome_menu_result.popup_open;
  if (chrome_menu_result.settings_changed) {
    save_settings();
  }
  if (chrome_menu_result.viewport_settings_changed) {
    save_viewport_settings();
  }
  {
    QDR_PROFILE_SCOPE("UI draw dockspace");
    const float sidebar_minimum_width = effective_sidebar_minimum_width(
        ImGui::GetMainViewport()->Size.x, panels_, style_.sidebar_min_width);
    draw_dockspace(editor, quit_requested, document, modeling,
                   sidebar_minimum_width);
  }

  bool viewport_cursor_capture_requested = false;
  QVec2 viewport_cursor_capture_point = {};
  const UIPanelId previous_focused_panel = focused_panel_;
  bool panel_focus_changed = false;
  bool command_shortcut_consumed = false;
  bool settings_changed = false;
  UIStyleTokens frame_style = style_;
  frame_style.sidebar_min_width = effective_sidebar_minimum_width(
      ImGui::GetMainViewport()->Size.x, panels_, style_.sidebar_min_width);
  UIInspectorPreviewContext inspector_preview_context{
      .document = document,
      .assets = assets,
  };
  UIPreferencesContext preferences_context{
      .ui = settings_,
      .viewport = viewport_settings_,
      .ui_settings_changed = &settings_changed,
  };
  UIViewportSurfaceContext viewport_context{
      .render_bridge = render_bridge,
      .document = document,
      .assets = assets,
      .modeling = modeling,
      .selection = selection,
      .pivot = &pivot,
      .default_material = default_material,
      .frame_timing = current_frame_timing_,
      .render_performance = render_performance_stats_,
      .relative_mouse_delta = relative_mouse_delta_,
      .pressed_input_chords = pending_pressed_input_chords_,
      .render_resize_suspended = viewport_render_resize_suspended_,
      .cursor_capture_requested = &viewport_cursor_capture_requested,
      .cursor_capture_point = &viewport_cursor_capture_point,
  };
  UIContext context{
      .intent = {.editor = editor},
      .scene =
          {
              .document = document,
              .assets = assets,
              .modeling = modeling,
              .selection = selection,
              .default_material = default_material,
          },
      .inspector_preview = &inspector_preview_context,
      .viewport = &viewport_context,
      .preferences = preferences_context,
      .quit_requested = quit_requested,
      .style = frame_style,
      .icons = icons_,
      .localization = localization_,
      .focused_panel = focused_panel_,
      .previous_focused_panel = previous_focused_panel,
      .focused_panel_storage = &focused_panel_,
      .panel_focus_changed = &panel_focus_changed,
      .command_shortcut_consumed = &command_shortcut_consumed,
  };
  {
    QDR_PROFILE_SCOPE("UI draw panels");
    panels_.draw(context);
  }
  if (viewport_settings_panel_.draw(context, settings_panel_open_)) {
    save_viewport_settings();
  }
  if (settings_changed) {
    save_settings();
  }
  tool_settings_panel_.draw(context, tool_settings_panel_open_);
  if (viewport_panel_ != nullptr) {
    (void)dispatch_focused_panel_command_shortcut(
        editor, viewport_panel_->tool_state(), focused_panel_,
        command_shortcut_consumed, pending_pressed_input_chords_);
  }
  draw_static_bottom_bar(*ImGui::GetMainViewport(), modeling.live_status(),
                         localization_, editor.commands().last_status());
  {
    QDR_PROFILE_SCOPE("UI sync tool preview meshes");
    const ModelingToolStatus tool_status = modeling.tool_status();
    const bool has_tool_preview_meshes =
        tool_status.active_tool == ModelingViewportTool::Box ||
        (tool_status.mirror_active && tool_status.mirror_can_preview &&
         tool_status.mirror_preview_valid);
    std::vector<ModelingToolPreviewMesh> modeling_preview_meshes =
        has_tool_preview_meshes ? modeling.tool_preview_meshes()
                                : std::vector<ModelingToolPreviewMesh>{};
    std::vector<ToolPreviewMesh> render_preview_meshes;
    render_preview_meshes.reserve(modeling_preview_meshes.size());
    for (ModelingToolPreviewMesh &preview_mesh : modeling_preview_meshes) {
      const AssetId material = preview_mesh.material != kInvalidAssetId
                                   ? preview_mesh.material
                                   : default_material;
      render_preview_meshes.push_back(ToolPreviewMesh{
          .source_entity = preview_mesh.source_entity,
          .material = material,
          .mesh = std::move(preview_mesh.mesh),
          .transform = {},
      });
    }
    render_bridge.set_tool_preview_meshes(
        build_render_tool_preview_snapshot(render_preview_meshes, assets));
  }
  enforce_sidebar_dock_minimums(frame_style.sidebar_min_width);
  set_viewport_cursor_captured(viewport_cursor_capture_requested);
  if (viewport_cursor_capture_requested) {
    warp_captured_cursor(viewport_cursor_capture_point);
  }
  pending_pressed_input_chords_.clear();
}

void UISystem::prepare_render_data(std::uint32_t framebuffer_width,
                                   std::uint32_t framebuffer_height) {
  QDR_PROFILE_FUNCTION();
  ImGui::Render();
  renderer_.prepare(ImGui::GetDrawData(), framebuffer_width,
                    framebuffer_height);
}

void UISystem::prepare_viewports(QuaderEditorApi &editor,
                                 SceneRenderBridge &render_bridge,
                                 const SceneDocument &document,
                                 const AssetRegistry &assets,
                                 const PivotToolService &pivot) {
  QDR_PROFILE_FUNCTION();
  if (viewport_panel_ != nullptr) {
    viewport_panel_->prepare_viewports(
        QuaderViewportApiRuntimeAccess::workspace(editor.viewports()), editor,
        render_bridge, document, assets, &pivot);
  }
}

void UISystem::render_viewports(RenderSystem &render_system) {
  QDR_PROFILE_FUNCTION();
  if (viewport_panel_ != nullptr) {
    viewport_panel_->render_viewports(render_system);
  }
}

void UISystem::render(RenderSystem &render_system) {
  QDR_PROFILE_FUNCTION();
  render_system.render_imgui(renderer_);
}

void UISystem::set_render_performance_stats(RenderFramePerformanceStats stats) {
  render_performance_stats_ = stats;
}

void UISystem::set_viewport_render_resize_suspended(bool suspended) {
  viewport_render_resize_suspended_ = suspended;
}

bool UISystem::needs_continuous_rendering() const {
  return relative_mouse_captured_ ||
         (viewport_panel_ != nullptr &&
          viewport_panel_->needs_continuous_rendering());
}

std::optional<WindowChromeAction> UISystem::take_window_chrome_action() {
  std::optional<WindowChromeAction> action = pending_chrome_action_;
  pending_chrome_action_.reset();
  return action;
}

void UISystem::register_panels(RenderSystem &render_system) {
  panels_.add(std::make_unique<OutlinerPanel>());

  auto viewport = std::make_unique<ViewportPanel>();
  viewport->initialize(render_system);
  viewport_panel_ = viewport.get();
  panels_.add(std::move(viewport));

  panels_.add(std::make_unique<InspectorPanel>());
  panels_.add(std::make_unique<ConsolePanel>());
}

MainMenuBarResult
UISystem::draw_window_chrome(QuaderEditorApi &editor, bool &quit_requested,
                             SceneDocument &document,
                             ModelingSessionService &modeling) {
  MainMenuBarResult menu_result;
  if (!custom_chrome_) {
    return menu_result;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->Pos);
  ImGui::SetNextWindowSize({viewport->Size.x, kWindowChromeHeight});
  ImGui::SetNextWindowViewport(viewport->ID);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_MenuBar |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      {6.0f, std::max(0.0f, std::floor((kWindowChromeHeight -
                                                        ImGui::GetFontSize()) *
                                                       0.5f) -
                                                1.0f)});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {2.0f, 0.0f});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ui_color(25, 25, 25));
  ImGui::PushStyleColor(ImGuiCol_MenuBarBg, ui_color(25, 25, 25));
  ImGui::Begin("Quader Window Chrome", nullptr, flags);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImVec2 min = ImGui::GetWindowPos();
  const ImVec2 max{min.x + ImGui::GetWindowWidth(),
                   min.y + kWindowChromeHeight};
  draw_list->AddLine({min.x, max.y - 1.0f}, {max.x, max.y - 1.0f},
                     ui_packed_color(46, 46, 46));
  const bool draw_logo = icons_.logo != ImTextureID_Invalid;
  const float logo_y =
      std::floor((kWindowChromeHeight - kChromeLogoSize) * 0.5f);
  const ImVec2 logo_min{min.x + kChromeLogoLeft, min.y + logo_y};
  const ImVec2 logo_max{logo_min.x + kChromeLogoSize,
                        logo_min.y + kChromeLogoSize};

  const float controls_left =
      std::max(0.0f, ImGui::GetWindowWidth() - kWindowChromeControlsWidth);
  if (ImGui::BeginMenuBar()) {
    ImGui::SetCursorPos({kChromeMenuLeft, 0.0f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {kChromeMenuSpacing, 0.0f});
    menu_result = main_menu_bar_.draw_menus({
        .quit_requested = quit_requested,
        .editor = editor,
        .document = document,
        .modeling = modeling,
        .viewport_panel = viewport_panel_,
        .viewport_settings = viewport_settings_,
        .panels = panels_,
        .localization = localization_,
        .viewport_settings_panel_open = settings_panel_open_,
    });
    ImGui::PopStyleVar();

    ImGui::SetCursorPos({controls_left, 0.0f});
    if (chrome_icon_button("minimize", WindowChromeAction::Minimize,
                           {kWindowChromeButtonWidth, kWindowChromeHeight})) {
      pending_chrome_action_ = WindowChromeAction::Minimize;
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (chrome_icon_button("restore", WindowChromeAction::ToggleMaximizeRestore,
                           {kWindowChromeButtonWidth, kWindowChromeHeight})) {
      pending_chrome_action_ = WindowChromeAction::ToggleMaximizeRestore;
    }
    ImGui::SameLine(0.0f, 0.0f);
    if (chrome_icon_button("close", WindowChromeAction::Close,
                           {kWindowChromeButtonWidth, kWindowChromeHeight})) {
      pending_chrome_action_ = WindowChromeAction::Close;
    }
    ImGui::EndMenuBar();
  }

  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(5);
  ImGui::End();
  if (draw_logo) {
    ImGui::GetForegroundDrawList()->AddImage(icons_.logo, logo_min, logo_max);
  }
  return menu_result;
}

void UISystem::draw_dockspace(QuaderEditorApi &editor, bool &quit_requested,
                              SceneDocument &document,
                              ModelingSessionService &modeling,
                              float sidebar_minimum_width) {
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float chrome_height = custom_chrome_ ? kWindowChromeHeight : 0.0f;
  ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y + chrome_height});
  ImGui::SetNextWindowSize(
      {viewport->Size.x, std::max(1.0f, viewport->Size.y - chrome_height -
                                            kStaticBottomBarHeight)});
  ImGui::SetNextWindowViewport(viewport->ID);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoScrollWithMouse |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  if (!custom_chrome_) {
    flags |= ImGuiWindowFlags_MenuBar;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::Begin("Main Dockspace", nullptr, flags);
  ImGui::PopStyleVar(3);

  if (!custom_chrome_) {
    const MainMenuBarResult menu_result = main_menu_bar_.draw({
        .quit_requested = quit_requested,
        .editor = editor,
        .document = document,
        .modeling = modeling,
        .viewport_panel = viewport_panel_,
        .viewport_settings = viewport_settings_,
        .panels = panels_,
        .localization = localization_,
        .viewport_settings_panel_open = settings_panel_open_,
    });
    if (menu_result.settings_changed) {
      save_settings();
    }
    if (menu_result.viewport_settings_changed) {
      save_viewport_settings();
    }
  }

  const ImGuiID dockspace_id = ImGui::GetID("QuaderDockspace");
  const ImVec2 dockspace_size = ImGui::GetContentRegionAvail();
  if (!has_saved_dock_layout_ && !dock_layout_built_ &&
      dockspace_size.x > 1.0f && dockspace_size.y > 1.0f) {
    build_default_dock_layout(dockspace_id, dockspace_size);
    dock_layout_built_ = true;
  }

  enforce_sidebar_dock_minimums(sidebar_minimum_width);
  const ImVec2 saved_window_min_size = ImGui::GetStyle().WindowMinSize;
  ImGui::PushStyleVar(
      ImGuiStyleVar_WindowMinSize,
      ImVec2(std::max(saved_window_min_size.x, sidebar_minimum_width),
             saved_window_min_size.y));
  ImGui::DockSpace(dockspace_id, dockspace_size,
                   ImGuiDockNodeFlags_AutoHideTabBar);
  ImGui::PopStyleVar();
  enforce_sidebar_dock_minimums(sidebar_minimum_width);

  ImGui::End();
}

void UISystem::build_default_dock_layout(ImGuiID dockspace_id,
                                         ImVec2 dockspace_size) {
  ImGui::DockBuilderRemoveNode(dockspace_id);
  ImGui::DockBuilderAddNode(dockspace_id,
                            ImGuiDockNodeFlags_DockSpace |
                                ImGuiDockNodeFlags_AutoHideTabBar);
  ImGui::DockBuilderSetNodeSize(dockspace_id, dockspace_size);

  ImGuiID main_id = dockspace_id;
  ImGuiID console_id = 0;
  if (panels_.is_visible(UIPanelId::Console)) {
    console_id = ImGui::DockBuilderSplitNode(main_id, ImGuiDir_Down, 0.24f,
                                             nullptr, &main_id);
  }
  const float sidebar_minimum_ratio =
      style_.sidebar_min_width / std::max(dockspace_size.x, 1.0f);
  const float outliner_ratio =
      std::clamp(std::max(0.18f, sidebar_minimum_ratio), 0.18f, 0.35f);
  const float inspector_ratio =
      std::clamp(std::max(0.24f, sidebar_minimum_ratio), 0.24f, 0.35f);
  const ImGuiID outliner_id = ImGui::DockBuilderSplitNode(
      main_id, ImGuiDir_Left, outliner_ratio, nullptr, &main_id);
  const ImGuiID inspector_id = ImGui::DockBuilderSplitNode(
      main_id, ImGuiDir_Right, inspector_ratio, nullptr, &main_id);

  ImGui::DockBuilderDockWindow(ui_panel_key(UIPanelId::Outliner), outliner_id);
  ImGui::DockBuilderDockWindow(ui_panel_key(UIPanelId::Viewport), main_id);
  ImGui::DockBuilderDockWindow(ui_panel_key(UIPanelId::Inspector),
                               inspector_id);
  if (console_id != 0) {
    ImGui::DockBuilderDockWindow(ui_panel_key(UIPanelId::Console), console_id);
  }
  ImGui::DockBuilderFinish(dockspace_id);
}

void UISystem::close_menu_popup_for_pass_through_click() {
  if (!main_menu_popup_open_) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 mouse = ImGui::GetMousePos();
  const float menu_click_exclusion_height =
      custom_chrome_ ? kWindowChromeHeight : ImGui::GetFrameHeight();
  if (mouse.y <= viewport->Pos.y + menu_click_exclusion_height) {
    return;
  }

  const ImVec2 pass_through_min{viewport->Pos.x,
                                viewport->Pos.y + menu_click_exclusion_height};
  const ImVec2 pass_through_max{viewport->Pos.x + viewport->Size.x,
                                viewport->Pos.y + viewport->Size.y};
  if (imgui_close_popup_for_pass_through_click(pass_through_min,
                                               pass_through_max)) {
    main_menu_popup_open_ = false;
  }
}

void UISystem::enforce_sidebar_dock_minimums(float sidebar_minimum_width) {
  clamp_docked_window_width(ui_panel_key(UIPanelId::Outliner),
                            sidebar_minimum_width);
  clamp_docked_window_width(ui_panel_key(UIPanelId::Inspector),
                            sidebar_minimum_width);
}

void UISystem::set_viewport_cursor_captured(bool captured) {
  if (platform_adapter_ == nullptr || relative_mouse_captured_ == captured) {
    return;
  }

  std::string error_message;
  if (!platform_adapter_->set_relative_mouse_captured(captured,
                                                      error_message)) {
    if (!relative_mouse_capture_warning_issued_) {
      LOG_WARN("Unable to {} viewport relative mouse mode: {}",
               captured ? "enable" : "disable", error_message);
      relative_mouse_capture_warning_issued_ = true;
    }
    return;
  }

  relative_mouse_captured_ = captured;
  relative_mouse_capture_warning_issued_ = false;
  if (captured) {
    platform_adapter_->discard_relative_mouse_delta();
    relative_mouse_delta_ = {};
  }
}

void UISystem::warp_captured_cursor(QVec2 point) {
  if (platform_adapter_ == nullptr || !relative_mouse_captured_) {
    return;
  }
  platform_adapter_->warp_mouse_in_window(point);
  platform_adapter_->discard_relative_mouse_delta();
  relative_mouse_delta_ = {};
}

void UISystem::save_settings() {
  panels_.write_settings(settings_);
  if (viewport_panel_ != nullptr) {
    settings_.set_navigation_gizmo_visible(
        viewport_panel_->navigation_gizmo_visible());
  }
  if (!settings_.save(settings_paths_.settings_file)) {
    LOG_WARN("Unable to save UI settings to {}",
             settings_paths_.settings_file.string());
  }
}

void UISystem::save_viewport_settings() {
  if (!viewport_settings_.save(settings_paths_.viewport_settings_file)) {
    LOG_WARN("Unable to save viewport settings to {}",
             settings_paths_.viewport_settings_file.string());
  }
}

} // namespace quader
