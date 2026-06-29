////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/main_menu_bar.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "ui/ui_panel_manager.hpp"
#include "ui/viewport_panel.hpp"
#include "api/quader_viewport_layout_types.hpp"
#include "editor/tools/viewport_tool_catalog.hpp"

#include <imgui.h>

#include <string>

namespace quader {
namespace {

constexpr float kMenuPopupPaddingX = 12.0f;
constexpr float kMenuPopupPaddingY = 8.0f;
constexpr float kMenuPopupFramePaddingY = 5.0f;
constexpr float kMenuPopupItemSpacingY = 4.0f;

bool beginMenuWithPopupSpacing(const std::string& label) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{kMenuPopupPaddingX, kMenuPopupPaddingY});
    if (!ImGui::BeginMenu(label.c_str())) {
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

void drawFileMenuContents(MainMenuBarContext& context) {
    const std::string exitLabel = context.localization.imguiLabel("ui.menu.file.exit", "ui.menu.file.exit", "Exit");
    if (ImGui::MenuItem(exitLabel.c_str())) {
        context.quitRequested = true;
    }
}

void drawEditMenuContents(MainMenuBarContext& context) {
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
                    context.document,
                    context.modeling);
        }
    }
}

void drawViewMenuContents(MainMenuBarContext& context, MainMenuBarResult& result) {
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

bool EditMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.edit", "ui.menu.edit", "Edit");
    if (!beginMenuWithPopupSpacing(label)) {
        return false;
    }

    drawEditMenuContents(context);
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
    result.popupOpen = editMenu_.draw(context) || result.popupOpen;
    result.popupOpen = toolsMenu_.draw(context) || result.popupOpen;
    result.popupOpen = viewMenu_.draw(context, result) || result.popupOpen;
    result.popupOpen = layoutMenu_.draw(context) || result.popupOpen;
    const std::string helpLabel = context.localization.imguiLabel("ui.menu.help", "ui.menu.help", "Help");
    if (beginMenuWithPopupSpacing(helpLabel)) {
        result.popupOpen = true;
        endMenuWithPopupSpacing();
    }
    return result;
}

} // namespace quader
