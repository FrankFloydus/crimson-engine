module;

#include <imgui.h>

module quader.ui.light_panel;

import quader.ui.controls;
import quader.ui.style;

namespace quader {

bool render_directional_light_panel(DirectionalLightSettings& settings)
{
    const PreviewPanelUiStyle style = preview_panel_ui_style();
    set_initial_window_bounds(style.light_window);

    bool changed = false;
    if (ImGui::Begin("Directional Light")) {
        changed |= render_srgb_color_edit("Color", settings.color);
        changed |= ImGui::DragFloat("Intensity", &settings.intensity, 500.0f, 0.0f, 200000.0f, "%.0f lx");
        changed |= ImGui::SliderFloat("Yaw", &settings.yaw_degrees, -180.0f, 180.0f);
        changed |= ImGui::SliderFloat("Pitch", &settings.pitch_degrees, -89.0f, 89.0f);

        if (ImGui::Button("Reset Light")) {
            settings = {};
            changed = true;
        }
    }

    ImGui::End();
    if (changed) {
        settings = clamp_directional_light_settings(settings);
    }

    return changed;
}

} // namespace quader
