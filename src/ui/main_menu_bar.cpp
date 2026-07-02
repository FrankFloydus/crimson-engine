////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/main_menu_bar.hpp"

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/viewport_panel.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"
#include "viewport/viewport_settings.hpp"

#include <imgui.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

constexpr float kMenuPopupPaddingX = 12.0f;
constexpr float kMenuPopupPaddingY = 8.0f;
constexpr float kMenuPopupFramePaddingY = 5.0f;
constexpr float kMenuPopupItemSpacingY = 4.0f;

bool begin_menu_with_popup_spacing(const std::string &label) {
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2{kMenuPopupPaddingX, kMenuPopupPaddingY});
  if (!ImGui::BeginMenu(label.c_str())) {
    ImGui::PopStyleVar();
    return false;
  }

  const ImGuiStyle &style = ImGui::GetStyle();
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2{style.FramePadding.x, kMenuPopupFramePaddingY});
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2{style.ItemSpacing.x, kMenuPopupItemSpacingY});
  return true;
}

void end_menu_with_popup_spacing() {
  ImGui::PopStyleVar(2);
  ImGui::EndMenu();
  ImGui::PopStyleVar();
}

std::string tool_label(const LocalizationService &localization,
                       const Tool &tool) {
  std::string key = "viewport.tool.";
  key.append(tool.id());
  key.append(".label");
  std::string stable_id = "viewport.tool.";
  stable_id.append(tool.id());
  return localization.imgui_label(key, stable_id, tool.label());
}

std::string layout_label(const LocalizationService &localization,
                         const ViewportLayoutOption &option) {
  std::string key = "viewport.layout.";
  key.append(viewport_layout_stable_id(option.id));
  key.append(".label");
  std::string stable_id = "viewport.layout.";
  stable_id.append(viewport_layout_stable_id(option.id));
  return localization.imgui_label(key, stable_id, option.label);
}

std::string panel_menu_label(const LocalizationService &localization,
                             const UIPanel &panel) {
  std::string stable_id = "ui.panel.";
  stable_id.append(ui_panel_stable_id(panel.id()));
  return localization.imgui_label(ui_panel_localization_key(panel.id()),
                                  stable_id, panel.title());
}

const QuaderCommandDescriptor *
find_command_descriptor(const std::vector<QuaderCommandDescriptor> &commands,
                        QuaderCommandId command_id) {
  const std::string_view target_id = command_id.string();
  for (const QuaderCommandDescriptor &command : commands) {
    if (std::string_view(command.id) == target_id) {
      return &command;
    }
  }
  return nullptr;
}

bool draw_command_menu_item(
    MainMenuBarContext &context,
    const std::vector<QuaderCommandDescriptor> &commands,
    QuaderCommandId command_id, const char *localization_key,
    const char *fallback_label) {
  const QuaderCommandDescriptor *command =
      find_command_descriptor(commands, command_id);
  const std::string label = context.localization.imgui_label(
      localization_key, localization_key, fallback_label);
  const std::string shortcut =
      command != nullptr ? command->shortcut : std::string{};
  const char *shortcut_text = shortcut.empty() ? nullptr : shortcut.c_str();
  const bool enabled = command != nullptr && command->enabled;
  if (ImGui::MenuItem(label.c_str(), shortcut_text, false, enabled)) {
    (void)context.editor.commands().execute(command_id);
    return true;
  }
  return false;
}

bool draw_viewport_bool_setting_menu_item(MainMenuBarContext &context,
                                          ViewportBoolSettingId setting_id,
                                          const char *localization_key,
                                          const char *fallback_label) {
  bool value = context.viewport_settings.bool_value(setting_id);
  const std::string label = context.localization.imgui_label(
      localization_key, localization_key, fallback_label);
  if (ImGui::MenuItem(label.c_str(), nullptr, &value)) {
    return context.viewport_settings.set_bool_value(setting_id, value);
  }
  return false;
}

void draw_file_menu_contents(MainMenuBarContext &context) {
  const std::vector<QuaderCommandDescriptor> commands =
      context.editor.commands().available();

  (void)draw_command_menu_item(context, commands, quader_command::kProjectOpen,
                               "ui.menu.file.open", "Open...");
  (void)draw_command_menu_item(context, commands, quader_command::kProjectSave,
                               "ui.menu.file.save", "Save");
  (void)draw_command_menu_item(context, commands,
                               quader_command::kProjectSaveAs,
                               "ui.menu.file.save_as", "Save As...");
  ImGui::Separator();

  const std::string exit_label = context.localization.imgui_label(
      "ui.menu.file.exit", "ui.menu.file.exit", "Exit");
  if (ImGui::MenuItem(exit_label.c_str())) {
    context.quit_requested = true;
  }
}

void draw_edit_menu_contents(MainMenuBarContext &context,
                             MainMenuBarResult &result) {
  const std::vector<QuaderCommandDescriptor> commands =
      context.editor.commands().available();

  (void)draw_command_menu_item(context, commands, quader_command::kEditUndo,
                               "ui.menu.edit.undo", "Undo");
  (void)draw_command_menu_item(context, commands, quader_command::kEditRedo,
                               "ui.menu.edit.redo", "Redo");
  (void)draw_command_menu_item(
      context, commands, quader_command::kEditRepeatLastAction,
      "ui.menu.edit.repeat_last_action", "Repeat Last Action");
  ImGui::Separator();

  (void)draw_command_menu_item(context, commands,
                               quader_command::kCopySelection,
                               "ui.menu.edit.copy", "Copy");
  (void)draw_command_menu_item(context, commands,
                               quader_command::kPasteSelection,
                               "ui.menu.edit.paste", "Paste");
  (void)draw_command_menu_item(context, commands,
                               quader_command::kDuplicateSelection,
                               "ui.menu.edit.duplicate", "Duplicate");
  ImGui::Separator();

  (void)draw_command_menu_item(
      context, commands, quader_command::kModelingFlipHorizontal,
      "ui.menu.edit.flip_horizontal", "Flip Horizontal");
  (void)draw_command_menu_item(context, commands,
                               quader_command::kModelingFlipVertical,
                               "ui.menu.edit.flip_vertical", "Flip Vertical");
  ImGui::Separator();

  (void)draw_command_menu_item(context, commands,
                               quader_command::kEditSelectAll,
                               "ui.menu.edit.select_all", "Select All");
  (void)draw_command_menu_item(
      context, commands, quader_command::kEditClearSelection,
      "ui.menu.edit.clear_selection", "Clear Selection");
  (void)draw_command_menu_item(
      context, commands, quader_command::kEditInvertSelection,
      "ui.menu.edit.invert_selection", "Invert Selection");
  ImGui::Separator();

  (void)draw_command_menu_item(context, commands,
                               quader_command::kDeleteSelection,
                               "ui.menu.edit.delete", "Delete");
  ImGui::Separator();

  (void)draw_command_menu_item(context, commands,
                               quader_command::kGroupSelection,
                               "ui.menu.edit.group", "Group");
  (void)draw_command_menu_item(context, commands,
                               quader_command::kUngroupSelection,
                               "ui.menu.edit.ungroup", "Ungroup");
  ImGui::Separator();

  const std::string snap_label = context.localization.imgui_label(
      "ui.menu.edit.snap", "ui.menu.edit.snap", "Snap");
  if (begin_menu_with_popup_spacing(snap_label)) {
    result.viewport_settings_changed =
        draw_viewport_bool_setting_menu_item(
            context, ViewportBoolSettingId::GridSnap, "ui.menu.edit.snap.grid",
            "Grid") ||
        result.viewport_settings_changed;
    result.viewport_settings_changed =
        draw_viewport_bool_setting_menu_item(
            context, ViewportBoolSettingId::VertexSnap,
            "ui.menu.edit.snap.vertex", "Vertex") ||
        result.viewport_settings_changed;
    end_menu_with_popup_spacing();
  }
  ImGui::Separator();

  const std::string settings_label = context.localization.imgui_label(
      "ui.menu.edit.settings", "ui.menu.edit.settings", "Settings");
  if (ImGui::MenuItem(settings_label.c_str())) {
    context.viewport_settings_panel_open = true;
  }
}

void draw_tools_menu_contents(MainMenuBarContext &context) {
  ToolMenuGroup current_group = ToolMenuGroup::Navigation;
  bool first_tool = true;
  for (const Tool *tool : viewport_tool_catalog()) {
    if (tool == nullptr) {
      continue;
    }
    if (!first_tool && tool->menu_group() != current_group) {
      ImGui::Separator();
    }
    current_group = tool->menu_group();
    first_tool = false;

    const bool selected = context.viewport_panel != nullptr &&
                          context.viewport_panel->tool_state().active_tool ==
                              tool->viewport_tool();
    const char *shortcut =
        tool->shortcut().empty() ? nullptr : tool->shortcut().data();
    const bool enabled = context.viewport_panel != nullptr &&
                         viewport_tool_available(tool->viewport_tool());
    const std::string localized_label = tool_label(context.localization, *tool);
    if (ImGui::MenuItem(localized_label.c_str(), shortcut, selected, enabled)) {
      (void)context.viewport_panel->activate_tool_from_menu(
          tool->viewport_tool(), context.editor, context.modeling);
    }
  }
}

void draw_create_menu_contents(MainMenuBarContext &context) {
  const std::string point_light_label = context.localization.imgui_label(
      "ui.menu.create.point_light", "ui.menu.create.point_light",
      "Point Light");
  if (ImGui::MenuItem(point_light_label.c_str())) {
    (void)context.editor.commands().execute(
        quader_command::kSceneCreatePointLight);
  }
  const std::string directional_light_label = context.localization.imgui_label(
      "ui.menu.create.directional_light", "ui.menu.create.directional_light",
      "Directional Light");
  if (ImGui::MenuItem(directional_light_label.c_str())) {
    (void)context.editor.commands().execute(
        quader_command::kSceneCreateDirectionalLight);
  }
}

void draw_view_menu_contents(MainMenuBarContext &context,
                             MainMenuBarResult &result) {
  const std::vector<QuaderCommandDescriptor> commands =
      context.editor.commands().available();

  result.viewport_settings_changed =
      draw_viewport_bool_setting_menu_item(context,
                                           ViewportBoolSettingId::ShowGrid,
                                           "ui.menu.view.grid", "Grid") ||
      result.viewport_settings_changed;
  result.viewport_settings_changed =
      draw_viewport_bool_setting_menu_item(context,
                                           ViewportBoolSettingId::ShowGizmos,
                                           "ui.menu.view.gizmos", "Gizmos") ||
      result.viewport_settings_changed;
  result.viewport_settings_changed =
      draw_viewport_bool_setting_menu_item(
          context, ViewportBoolSettingId::ShowOverlays, "ui.menu.view.overlays",
          "Overlays") ||
      result.viewport_settings_changed;
  result.viewport_settings_changed =
      draw_viewport_bool_setting_menu_item(
          context, ViewportBoolSettingId::ShowInfo, "ui.menu.view.show_info",
          "Show Info") ||
      result.viewport_settings_changed;
  ImGui::Separator();

  (void)draw_command_menu_item(
      context, commands, quader_command::kViewportFrameSelection,
      "ui.menu.view.focus_selection", "Focus Selection");
  ImGui::Separator();

  bool navigation_gizmo_visible =
      context.viewport_panel != nullptr &&
      context.viewport_panel->navigation_gizmo_visible();
  const std::string gizmo_label = context.localization.imgui_label(
      "ui.menu.show.navigation_gizmo", "ui.menu.show.navigation_gizmo",
      "Navigation Gizmo");
  if (ImGui::MenuItem(gizmo_label.c_str(), nullptr, &navigation_gizmo_visible,
                      context.viewport_panel != nullptr)) {
    (void)context.editor.preferences().set_navigation_gizmo_visible(
        navigation_gizmo_visible);
    result.settings_changed = true;
  }
  ImGui::Separator();

  const std::string toolbars_label = context.localization.imgui_label(
      "ui.menu.view.toolbars", "ui.menu.view.toolbars", "Toolbars");
  if (begin_menu_with_popup_spacing(toolbars_label)) {
    for (const std::unique_ptr<UIPanel> &panel : context.panels.panels()) {
      if (!panel->can_toggle()) {
        continue;
      }
      bool visible = context.panels.is_visible(panel->id());
      const std::string localized_label =
          panel_menu_label(context.localization, *panel);
      if (ImGui::MenuItem(localized_label.c_str(), nullptr, &visible)) {
        if (visible) {
          (void)context.editor.ui().show_panel(ui_panel_stable_id(panel->id()));
        } else {
          (void)context.editor.ui().hide_panel(ui_panel_stable_id(panel->id()));
        }
        result.settings_changed = true;
      }
    }
    end_menu_with_popup_spacing();
  }
}

void draw_layout_menu_contents(MainMenuBarContext &context) {
  for (const ViewportLayoutOption &option : viewport_layout_options()) {
    const bool selected = context.editor.viewports().layout() == option.id;
    const std::string localized_label =
        layout_label(context.localization, option);
    if (ImGui::MenuItem(localized_label.c_str(), nullptr, selected)) {
      (void)context.editor.viewports().set_layout(option.id);
    }
  }
}

} // namespace

bool FileMenu::draw(MainMenuBarContext& context) const {
  const std::string label =
      context.localization.imgui_label("ui.menu.file", "ui.menu.file", "File");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_file_menu_contents(context);
  end_menu_with_popup_spacing();
  return true;
}

bool EditMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
  const std::string label =
      context.localization.imgui_label("ui.menu.edit", "ui.menu.edit", "Edit");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_edit_menu_contents(context, result);
  end_menu_with_popup_spacing();
  return true;
}

bool ToolsMenu::draw(MainMenuBarContext& context) const {
  const std::string label = context.localization.imgui_label(
      "ui.menu.tools", "ui.menu.tools", "Tools");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_tools_menu_contents(context);
  end_menu_with_popup_spacing();
  return true;
}

bool CreateMenu::draw(MainMenuBarContext& context) const {
  const std::string label = context.localization.imgui_label(
      "ui.menu.create", "ui.menu.create", "Create");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_create_menu_contents(context);
  end_menu_with_popup_spacing();
  return true;
}

bool ViewMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
  const std::string label =
      context.localization.imgui_label("ui.menu.view", "ui.menu.view", "View");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_view_menu_contents(context, result);
  end_menu_with_popup_spacing();
  return true;
}

bool LayoutMenu::draw(MainMenuBarContext& context) const {
  const std::string label = context.localization.imgui_label(
      "ui.menu.layout", "ui.menu.layout", "Layout");
  if (!begin_menu_with_popup_spacing(label)) {
    return false;
  }

  draw_layout_menu_contents(context);
  end_menu_with_popup_spacing();
  return true;
}

MainMenuBarResult MainMenuBar::draw(MainMenuBarContext context) const {
    MainMenuBarResult result;
    if (!ImGui::BeginMenuBar()) {
        return result;
    }

    result = draw_menus(context);
    ImGui::EndMenuBar();
    return result;
}

MainMenuBarResult MainMenuBar::draw_menus(MainMenuBarContext context) const {
  MainMenuBarResult result;
  result.popup_open = file_menu_.draw(context) || result.popup_open;
  result.popup_open = edit_menu_.draw(context, result) || result.popup_open;
  result.popup_open = tools_menu_.draw(context) || result.popup_open;
  result.popup_open = create_menu_.draw(context) || result.popup_open;
  result.popup_open = view_menu_.draw(context, result) || result.popup_open;
  result.popup_open = layout_menu_.draw(context) || result.popup_open;
  return result;
}

} // namespace quader
