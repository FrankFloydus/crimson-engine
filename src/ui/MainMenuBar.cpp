#include "ui/MainMenuBar.h"

#include "ui/UIPanelManager.h"
#include "ui/ViewportPanel.h"
#include "viewport/ViewportLayout.h"
#include "viewport/tools/ViewportToolCatalog.h"

#include <imgui.h>

namespace quader {

void FileMenu::draw(MainMenuBarContext& context) const {
    if (!ImGui::BeginMenu("File")) {
        return;
    }

    if (ImGui::MenuItem("Exit")) {
        context.quitRequested = true;
    }

    ImGui::EndMenu();
}

void EditMenu::draw(MainMenuBarContext& context) const {
    if (!ImGui::BeginMenu("Edit")) {
        return;
    }

    if (ImGui::MenuItem("Settings")) {
        context.viewportSettingsPanelOpen = true;
    }
    if (ImGui::MenuItem("Tool Settings")) {
        context.toolSettingsPanelOpen = true;
    }

    ImGui::EndMenu();
}

void ToolsMenu::draw(MainMenuBarContext& context) const {
    if (!ImGui::BeginMenu("Tools")) {
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
        if (ImGui::MenuItem(tool->label().data(), shortcut, selected, enabled)) {
            (void)context.viewportPanel->activateToolFromMenu(tool->viewportTool(), context.modeling);
        }
    }

    ImGui::EndMenu();
}

void ShowMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    if (!ImGui::BeginMenu("Show")) {
        return;
    }

    bool navigationGizmoVisible =
            context.viewportPanel != nullptr && context.viewportPanel->navigationGizmoVisible();
    if (ImGui::MenuItem("Navigation Gizmo", nullptr, &navigationGizmoVisible, context.viewportPanel != nullptr)) {
        context.viewportPanel->setNavigationGizmoVisible(navigationGizmoVisible);
        context.settings.setNavigationGizmoVisible(navigationGizmoVisible);
        result.settingsChanged = true;
    }

    ImGui::EndMenu();
}

void ViewMenu::draw(MainMenuBarContext& context, MainMenuBarResult& result) const {
    if (!ImGui::BeginMenu("View")) {
        return;
    }

    if (ImGui::BeginMenu("Toolbars")) {
        for (const std::unique_ptr<UIPanel>& panel : context.panels.panels()) {
            if (!panel->canToggle()) {
                continue;
            }
            bool visible = context.panels.isVisible(panel->id());
            if (ImGui::MenuItem(panel->title(), nullptr, &visible)) {
                context.panels.setVisible(panel->id(), visible);
                result.settingsChanged = true;
            }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMenu();
}

void LayoutMenu::draw(MainMenuBarContext& context) const {
    if (!ImGui::BeginMenu("Layout")) {
        return;
    }

    if (context.viewportPanel != nullptr) {
        for (const ViewportLayoutOption& option : viewportLayoutOptions()) {
            const bool selected = context.viewportPanel->viewportLayout() == option.id;
            if (ImGui::MenuItem(option.label.data(), nullptr, selected)) {
                context.viewportPanel->setViewportLayout(option.id);
            }
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
