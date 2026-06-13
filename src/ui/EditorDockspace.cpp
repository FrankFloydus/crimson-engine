module;

#include <imgui.h>

module quader.ui.editor_dockspace;

import quader.ui.controls;

namespace quader {

void render_editor_dockspace()
{
    const ImGuiIO& io = ImGui::GetIO();
    if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
        return;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    constexpr ImGuiWindowFlags dockspace_window_flags =
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    const StyleVarScope rounding(ImGuiStyleVar_WindowRounding, 0.0f);
    const StyleVarScope border_size(ImGuiStyleVar_WindowBorderSize, 0.0f);
    const StyleVarScope padding(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const WindowScope window("Quader Editor Dockspace", nullptr, dockspace_window_flags);

    if (window) {
        const ImGuiID dockspace_id = ImGui::GetID("QuaderEditorDockspace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    }
}

} // namespace quader
