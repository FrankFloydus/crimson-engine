module;

#include <imgui.h>

module quader.ui.inspector_menu;

import quader.ui.controls;

namespace quader {

InspectorCreateMenuResult draw_inspector_create_menu()
{
    InspectorCreateMenuResult result;

    const MainMenuBarScope main_menu;
    if (main_menu) {
        const MenuScope create_menu("Create");
        if (create_menu) {
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
        }
    }

    return result;
}

} // namespace quader
