////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/MainMenuBar.h"

#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"
#include "ui/UIPanelManager.h"
#include "ui/ViewportPanel.h"
#include "viewport/ViewportLayout.h"
#include "viewport/modes/ViewportToolCatalog.h"

#include <imgui.h>

#include <string>

namespace quader {
namespace {

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
    stableId.append(uiPanelKey(panel.id()));
    return localization.imguiLabel(uiPanelLocalizationKey(panel.id()), stableId, panel.title());
}

} // namespace

void FileMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.file", "ui.menu.file", "File");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

    const std::string exitLabel = context.localization.imguiLabel("ui.menu.file.exit", "ui.menu.file.exit", "Exit");
    if (ImGui::MenuItem(exitLabel.c_str())) {
        context.quitRequested = true;
    }

    ImGui::EndMenu();
}

void EditMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.edit", "ui.menu.edit", "Edit");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

    const std::string settingsLabel = context.localization.imguiLabel("ui.menu.edit.settings", "ui.menu.edit.settings", "Settings");
    if (ImGui::MenuItem(settingsLabel.c_str())) {
        context.viewportSettingsPanelOpen = true;
    }
    const std::string toolSettingsLabel =
            context.localization.imguiLabel("ui.menu.edit.tool_settings", "ui.menu.edit.tool_settings", "Tool Settings");
    if (ImGui::MenuItem(toolSettingsLabel.c_str())) {
        context.toolSettingsPanelOpen = true;
    }

    ImGui::EndMenu();
}

void ToolsMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.tools", "ui.menu.tools", "Tools");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

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

    ImGui::EndMenu();
}

void ShowMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    const std::string label = context.localization.imguiLabel("ui.menu.show", "ui.menu.show", "Show");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

    bool navigationGizmoVisible =
            context.viewportPanel != nullptr && context.viewportPanel->navigationGizmoVisible();
    const std::string gizmoLabel =
            context.localization.imguiLabel("ui.menu.show.navigation_gizmo", "ui.menu.show.navigation_gizmo", "Navigation Gizmo");
    if (ImGui::MenuItem(gizmoLabel.c_str(), nullptr, &navigationGizmoVisible, context.viewportPanel != nullptr)) {
        (void)context.editor.preferences().setNavigationGizmoVisible(navigationGizmoVisible);
        result.settingsChanged = true;
    }

    ImGui::EndMenu();
}

void ViewMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    const std::string label = context.localization.imguiLabel("ui.menu.view", "ui.menu.view", "View");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

    const std::string toolbarsLabel = context.localization.imguiLabel("ui.menu.view.toolbars", "ui.menu.view.toolbars", "Toolbars");
    if (ImGui::BeginMenu(toolbarsLabel.c_str())) {
        for (const std::unique_ptr<UIPanel>& panel : context.panels.panels()) {
            if (!panel->canToggle()) {
                continue;
            }
            bool visible = context.panels.isVisible(panel->id());
            const std::string localizedLabel = panelMenuLabel(context.localization, *panel);
            if (ImGui::MenuItem(localizedLabel.c_str(), nullptr, &visible)) {
                if (visible) {
                    (void)context.editor.ui().showPanel(uiPanelKey(panel->id()));
                } else {
                    (void)context.editor.ui().hidePanel(uiPanelKey(panel->id()));
                }
                result.settingsChanged = true;
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void LayoutMenu::draw(MainMenuBarContext& context) const {
    const std::string label = context.localization.imguiLabel("ui.menu.layout", "ui.menu.layout", "Layout");
    if (!ImGui::BeginMenu(label.c_str())) {
        return;
    }

    for (const ViewportLayoutOption& option : viewportLayoutOptions()) {
        const bool selected = context.editor.viewports().layout() == option.id;
        const std::string localizedLabel = layoutLabel(context.localization, option);
        if (ImGui::MenuItem(localizedLabel.c_str(), nullptr, selected)) {
            (void)context.editor.viewports().setLayout(option.id);
        }
    }

    ImGui::EndMenu();
}

MainMenuBarResult MainMenuBar::draw(MainMenuBarContext context) const {
    MainMenuBarResult result;
    if (!ImGui::BeginMenuBar()) {
        return result;
    }

    fileMenu_.draw(context);
    editMenu_.draw(context);
    toolsMenu_.draw(context);
    showMenu_.draw(context, result);
    viewMenu_.draw(context, result);
    layoutMenu_.draw(context);

    ImGui::EndMenuBar();
    return result;
}

} // namespace quader
