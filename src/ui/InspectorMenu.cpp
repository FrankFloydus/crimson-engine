module;

#include <imgui.h>

module quader.ui.inspector_menu;

namespace quader {

InspectorCreateMenuResult draw_inspector_create_menu()
{
    InspectorCreateMenuResult result;

    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube")) {
                result.requested_create = CreateSceneItemKind::cube;
            }
            if (ImGui::MenuItem("Plane")) {
                result.requested_create = CreateSceneItemKind::plane;
            }
            if (ImGui::MenuItem("Directional Light")) {
                result.requested_create = CreateSceneItemKind::directional_light;
            }
            if (ImGui::MenuItem("Point Light")) {
                result.requested_create = CreateSceneItemKind::point_light;
            }

            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    return result;
}

} // namespace quader
