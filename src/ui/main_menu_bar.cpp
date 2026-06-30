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
#include <imgui_internal.h>

#include <string>
#include <string_view>
#include <vector>

namespace quader {
namespace {

constexpr float kMenuPopupPaddingX = 12.0f;
constexpr float kMenuPopupPaddingY = 8.0f;
constexpr float kMenuPopupFramePaddingY = 5.0f;
constexpr float kMenuPopupItemSpacingY = 4.0f;

bool beginMenuWithPopupSpacing(const std::string& label) {
    const ImGuiID menuId = ImGui::GetID(label.c_str());
    const bool wasOpen = ImGui::IsPopupOpen(menuId, ImGuiPopupFlags_None);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{kMenuPopupPaddingX, kMenuPopupPaddingY});
    if (!ImGui::BeginMenu(label.c_str())) {
        ImGui::PopStyleVar();
        return false;
    }
    if (wasOpen && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ImGui::CloseCurrentPopup();
        ImGui::EndMenu();
        ImGui::PopStyleVar();
        return false;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{style.FramePadding.x, kMenuPopupFramePaddingY});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{style.ItemSpacing.x, kMenuPopupItemSpacingY});
    return true;
}

void endMenuWithPopupSpacing() {
    ImGui::PopStyleVar(2);
    ImGui::EndMenu();
    ImGui::PopStyleVar();
}

std::string toolLabel(const LocalizationService& localization, const Tool& tool) {
    std::string key = "viewport.tool.";
    key.append(tool.id());
    key.append(".label");
    std::string stableId = "viewport.tool.";
    stableId.append(tool.id());
    return localization.imguiLabel(key, stableId, tool.label());
}

std::string layoutLabel(const LocalizationService& localization, const ViewportLayoutOption& option) {
    std::string key = "viewport.layout.";
    key.append(viewportLayoutStableId(option.id));
    key.append(".label");
    std::string stableId = "viewport.layout.";
    stableId.append(viewportLayoutStableId(option.id));
    return localization.imguiLabel(key, stableId, option.label);
}

std::string panelMenuLabel(const LocalizationService& localization, const UIPanel& panel) {
    std::string stableId = "ui.panel.";
    stableId.append(uiPanelStableId(panel.id()));
    return localization.imguiLabel(uiPanelLocalizationKey(panel.id()), stableId, panel.title());
}

const QuaderCommandDescriptor* findCommandDescriptor(
        const std::vector<QuaderCommandDescriptor>& commands,
        QuaderCommandId commandId) {
    const std::string_view targetId = commandId.string();
    for (const QuaderCommandDescriptor& command : commands) {
        if (std::string_view(command.id) == targetId) {
            return &command;
        }
    }
    return nullptr;
}

bool drawCommandMenuItem(
        MainMenuBarContext& context,
        const std::vector<QuaderCommandDescriptor>& commands,
        QuaderCommandId commandId,
        const char* localizationKey,
        const char* fallbackLabel) {
    const QuaderCommandDescriptor* command = findCommandDescriptor(commands, commandId);
    const std::string label = context.localization.imguiLabel(localizationKey, localizationKey, fallbackLabel);
    const std::string shortcut = command != nullptr ? command->shortcut : std::string{};
    const char* shortcutText = shortcut.empty() ? nullptr : shortcut.c_str();
    const bool enabled = command != nullptr && command->enabled;
    if (ImGui::MenuItem(label.c_str(), shortcutText, false, enabled)) {
        (void)context.editor.commands().execute(commandId);
        return true;
    }
    return false;
}

bool drawViewportBoolSettingMenuItem(
        MainMenuBarContext& context,
        ViewportBoolSettingId settingId,
        const char* localizationKey,
        const char* fallbackLabel) {
    bool value = context.viewportSettings.boolValue(settingId);
    const std::string label = context.localization.imguiLabel(localizationKey, localizationKey, fallbackLabel);
    if (ImGui::MenuItem(label.c_str(), nullptr, &value)) {
        return context.viewportSettings.setBoolValue(settingId, value);
    }
    return false;
}

void drawFileMenuContents(MainMenuBarContext& context) {
    const std::string exitLabel = context.localization.imguiLabel("ui.menu.file.exit", "ui.menu.file.exit", "Exit");
    if (ImGui::MenuItem(exitLabel.c_str())) {
        context.quitRequested = true;
    }
}

void drawEditMenuContents(MainMenuBarContext& context, MainMenuBarResult& result) {
    const std::vector<QuaderCommandDescriptor> commands = context.editor.commands().available();

    (void)drawCommandMenuItem(context, commands, QuaderCommand::EditUndo, "ui.menu.edit.undo", "Undo");
    (void)drawCommandMenuItem(context, commands, QuaderCommand::EditRedo, "ui.menu.edit.redo", "Redo");
    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::EditRepeatLastAction,
            "ui.menu.edit.repeat_last_action",
            "Repeat Last Action");
    ImGui::Separator();

    (void)drawCommandMenuItem(context, commands, QuaderCommand::CopySelection, "ui.menu.edit.copy", "Copy");
    (void)drawCommandMenuItem(context, commands, QuaderCommand::PasteSelection, "ui.menu.edit.paste", "Paste");
    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::DuplicateSelection,
            "ui.menu.edit.duplicate",
            "Duplicate");
    ImGui::Separator();

    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::ModelingFlipHorizontal,
            "ui.menu.edit.flip_horizontal",
            "Flip Horizontal");
    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::ModelingFlipVertical,
            "ui.menu.edit.flip_vertical",
            "Flip Vertical");
    ImGui::Separator();

    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::EditSelectAll,
            "ui.menu.edit.select_all",
            "Select All");
    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::EditClearSelection,
            "ui.menu.edit.clear_selection",
            "Clear Selection");
    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::EditInvertSelection,
            "ui.menu.edit.invert_selection",
            "Invert Selection");
    ImGui::Separator();

    (void)drawCommandMenuItem(context, commands, QuaderCommand::DeleteSelection, "ui.menu.edit.delete", "Delete");
    ImGui::Separator();

    (void)drawCommandMenuItem(context, commands, QuaderCommand::GroupSelection, "ui.menu.edit.group", "Group");
    (void)drawCommandMenuItem(context, commands, QuaderCommand::UngroupSelection, "ui.menu.edit.ungroup", "Ungroup");
    ImGui::Separator();

    const std::string snapLabel = context.localization.imguiLabel("ui.menu.edit.snap", "ui.menu.edit.snap", "Snap");
    if (beginMenuWithPopupSpacing(snapLabel)) {
        result.viewportSettingsChanged =
                drawViewportBoolSettingMenuItem(
                        context,
                        ViewportBoolSettingId::GridSnap,
                        "ui.menu.edit.snap.grid",
                        "Grid") ||
                result.viewportSettingsChanged;
        result.viewportSettingsChanged =
                drawViewportBoolSettingMenuItem(
                        context,
                        ViewportBoolSettingId::VertexSnap,
                        "ui.menu.edit.snap.vertex",
                        "Vertex") ||
                result.viewportSettingsChanged;
        endMenuWithPopupSpacing();
    }
    ImGui::Separator();

    const std::string settingsLabel = context.localization.imguiLabel("ui.menu.edit.settings", "ui.menu.edit.settings", "Settings");
    if (ImGui::MenuItem(settingsLabel.c_str())) {
        context.viewportSettingsPanelOpen = true;
    }
}

void drawToolsMenuContents(MainMenuBarContext& context) {
    ToolMenuGroup currentGroup = ToolMenuGroup::Navigation;
    bool firstTool = true;
    for (const Tool* tool : viewportToolCatalog()) {
        if (tool == nullptr) {
            continue;
        }
        if (!firstTool && tool->menuGroup() != currentGroup) {
            ImGui::Separator();
        }
        currentGroup = tool->menuGroup();
        firstTool = false;

        const bool selected =
                context.viewportPanel != nullptr &&
                context.viewportPanel->toolState().activeTool == tool->viewportTool();
        const char* shortcut = tool->shortcut().empty() ? nullptr : tool->shortcut().data();
        const bool enabled =
                context.viewportPanel != nullptr &&
                viewportToolAvailable(tool->viewportTool());
        const std::string localizedLabel = toolLabel(context.localization, *tool);
        if (ImGui::MenuItem(localizedLabel.c_str(), shortcut, selected, enabled)) {
            (void)context.viewportPanel->activateToolFromMenu(
                    tool->viewportTool(),
                    context.editor,
                    context.modeling);
        }
    }
}

void drawCreateMenuContents(MainMenuBarContext& context) {
    const std::string pointLightLabel =
            context.localization.imguiLabel("ui.menu.create.point_light", "ui.menu.create.point_light", "Point Light");
    if (ImGui::MenuItem(pointLightLabel.c_str())) {
        (void)context.editor.commands().execute(QuaderCommand::SceneCreatePointLight);
    }
    const std::string directionalLightLabel = context.localization.imguiLabel(
            "ui.menu.create.directional_light",
            "ui.menu.create.directional_light",
            "Directional Light");
    if (ImGui::MenuItem(directionalLightLabel.c_str())) {
        (void)context.editor.commands().execute(QuaderCommand::SceneCreateDirectionalLight);
    }
}

void drawViewMenuContents(MainMenuBarContext& context, MainMenuBarResult& result) {
    const std::vector<QuaderCommandDescriptor> commands = context.editor.commands().available();

    result.viewportSettingsChanged =
            drawViewportBoolSettingMenuItem(
                    context,
                    ViewportBoolSettingId::ShowGrid,
                    "ui.menu.view.grid",
                    "Grid") ||
            result.viewportSettingsChanged;
    result.viewportSettingsChanged =
            drawViewportBoolSettingMenuItem(
                    context,
                    ViewportBoolSettingId::ShowGizmos,
                    "ui.menu.view.gizmos",
                    "Gizmos") ||
            result.viewportSettingsChanged;
    result.viewportSettingsChanged =
            drawViewportBoolSettingMenuItem(
                    context,
                    ViewportBoolSettingId::ShowOverlays,
                    "ui.menu.view.overlays",
                    "Overlays") ||
            result.viewportSettingsChanged;
    ImGui::Separator();

    (void)drawCommandMenuItem(
            context,
            commands,
            QuaderCommand::ViewportFrameSelection,
            "ui.menu.view.focus_selection",
            "Focus Selection");
    ImGui::Separator();

    bool navigationGizmoVisible =
            context.viewportPanel != nullptr && context.viewportPanel->navigationGizmoVisible();
    const std::string gizmoLabel =
            context.localization.imguiLabel("ui.menu.show.navigation_gizmo", "ui.menu.show.navigation_gizmo", "Navigation Gizmo");
    if (ImGui::MenuItem(gizmoLabel.c_str(), nullptr, &navigationGizmoVisible, context.viewportPanel != nullptr)) {
        (void)context.editor.preferences().setNavigationGizmoVisible(navigationGizmoVisible);
        result.settingsChanged = true;
    }
    ImGui::Separator();

    const std::string toolbarsLabel = context.localization.imguiLabel("ui.menu.view.toolbars", "ui.menu.view.toolbars", "Toolbars");
    if (beginMenuWithPopupSpacing(toolbarsLabel)) {
        for (const std::unique_ptr<UIPanel>& panel : context.panels.panels()) {
            if (!panel->canToggle()) {
                continue;
            }
            bool visible = context.panels.isVisible(panel->id());
            const std::string localizedLabel = panelMenuLabel(context.localization, *panel);
            if (ImGui::MenuItem(localizedLabel.c_str(), nullptr, &visible)) {
                if (visible) {
                    (void)context.editor.ui().showPanel(uiPanelStableId(panel->id()));
                } else {
                    (void)context.editor.ui().hidePanel(uiPanelStableId(panel->id()));
                }
                result.settingsChanged = true;
            }
        }
        endMenuWithPopupSpacing();
    }
}

void drawLayoutMenuContents(MainMenuBarContext& context) {
    for (const ViewportLayoutOption& option : viewportLayoutOptions()) {
        const bool selected = context.editor.viewports().layout() == option.id;
        const std::string localizedLabel = layoutLabel(context.localization, option);
        if (ImGui::MenuItem(localizedLabel.c_str(), nullptr, selected)) {
            (void)context.editor.viewports().setLayout(option.id);
        }
    }
}

} // namespace

bool FileMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.file", "ui.menu.file", "File");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawFileMenuContents(context);
    endMenuWithPopupSpacing();
    return true;
}

bool EditMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    const std::string label = context.localization.imguiLabel("ui.menu.edit", "ui.menu.edit", "Edit");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawEditMenuContents(context, result);
    endMenuWithPopupSpacing();
    return true;
}

bool ToolsMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.tools", "ui.menu.tools", "Tools");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawToolsMenuContents(context);
    endMenuWithPopupSpacing();
    return true;
}

bool CreateMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.create", "ui.menu.create", "Create");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawCreateMenuContents(context);
    endMenuWithPopupSpacing();
    return true;
}

bool ViewMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    const std::string label = context.localization.imguiLabel("ui.menu.view", "ui.menu.view", "View");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawViewMenuContents(context, result);
    endMenuWithPopupSpacing();
    return true;
}

bool LayoutMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.layout", "ui.menu.layout", "Layout");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawLayoutMenuContents(context);
    endMenuWithPopupSpacing();
    return true;
}

MainMenuBarResult MainMenuBar::draw(MainMenuBarContext context) const {
    MainMenuBarResult result;
    if (!ImGui::BeginMenuBar()) {
        return result;
    }

    result = drawMenus(context);
    ImGui::EndMenuBar();
    return result;
}

MainMenuBarResult MainMenuBar::drawMenus(MainMenuBarContext context) const {
    MainMenuBarResult result;
    result.popupOpen = fileMenu_.draw(context) || result.popupOpen;
    result.popupOpen = editMenu_.draw(context, result) || result.popupOpen;
    result.popupOpen = toolsMenu_.draw(context) || result.popupOpen;
    result.popupOpen = createMenu_.draw(context) || result.popupOpen;
    result.popupOpen = viewMenu_.draw(context, result) || result.popupOpen;
    result.popupOpen = layoutMenu_.draw(context) || result.popupOpen;
    return result;
}

} // namespace quader
