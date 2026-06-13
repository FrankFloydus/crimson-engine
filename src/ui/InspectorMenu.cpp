module;

#include <imgui.h>

#include <string_view>

module quader.ui.inspector_menu;

import quader.ui.controls;

namespace quader {
namespace {

[[nodiscard]] const char* c_string(std::string_view text) noexcept
{
    return text.empty() ? "" : text.data();
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

    ImGui::TextDisabled("Active: %s", c_string(input.active_render_driver_label));
    if (input.restart_required) {
        ImGui::TextDisabled("Restart required");
    }
    ImGui::Separator();

    for (const EditorMainMenuInput::RenderDriverMenuOption& option :
         input.render_driver_options) {
        if (ImGui::MenuItem(c_string(option.label), nullptr, option.selected, option.enabled) &&
            !option.selected) {
            result.requested_render_driver = option.kind;
        }
        if (!option.enabled) {
            render_item_tooltip(c_string(option.disabled_reason));
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
