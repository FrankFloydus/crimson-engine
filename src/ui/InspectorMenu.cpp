module;

#include <imgui.h>

module quader.ui.inspector_menu;

import quader.ui.controls;

namespace quader {
namespace {

constexpr RenderDriverKind kRenderDriverMenuItems[] {
    RenderDriverKind::automatic,
    RenderDriverKind::vulkan,
    RenderDriverKind::opengl,
    RenderDriverKind::metal,
};

[[nodiscard]] const char* render_driver_label(RenderDriverKind driver) noexcept
{
    return render_driver_kind_display_name(driver).data();
}

[[nodiscard]] bool render_driver_can_be_selected(RenderDriverKind driver) noexcept
{
    return driver == RenderDriverKind::automatic || render_driver_is_implemented_in_this_build(driver);
}

void draw_create_menu(EditorMainMenuResult& result)
{
    const MenuScope create_menu("Create");
    if (!create_menu) {
        return;
    }

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

void draw_render_driver_menu(
    const EditorMainMenuInput& input,
    EditorMainMenuResult& result)
{
    const MenuScope render_driver_menu("Render Driver");
    if (!render_driver_menu) {
        return;
    }

    ImGui::TextDisabled("Active: %s", render_driver_label(input.active_driver));
    if (input.restart_required) {
        ImGui::TextDisabled("Restart required");
    }
    ImGui::Separator();

    for (const RenderDriverKind driver : kRenderDriverMenuItems) {
        const bool selected = input.saved_selection.requested_driver == driver;
        const bool enabled = render_driver_can_be_selected(driver);
        if (ImGui::MenuItem(render_driver_label(driver), nullptr, selected, enabled) && !selected) {
            result.requested_render_driver = driver;
        }
        if (!enabled) {
            render_item_tooltip("This render driver is not implemented in this build.");
        }
    }
}

void draw_settings_menu(
    const EditorMainMenuInput& input,
    EditorMainMenuResult& result)
{
    const MenuScope settings_menu("Settings");
    if (!settings_menu) {
        return;
    }

    draw_render_driver_menu(input, result);
}

} // namespace

EditorMainMenuResult draw_editor_main_menu(const EditorMainMenuInput& input)
{
    EditorMainMenuResult result;

    const MainMenuBarScope main_menu;
    if (main_menu) {
        draw_create_menu(result);
        draw_settings_menu(input, result);
    }

    return result;
}

} // namespace quader
