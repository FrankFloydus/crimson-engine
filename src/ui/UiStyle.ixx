module;

#include <imgui.h>

export module quader.ui.style;

export namespace quader {

struct InitialWindowBounds {
    ImVec2 position;
    ImVec2 size;
};

struct ViewportHostUiStyle {
    InitialWindowBounds window { ImVec2 { 32.0f, 56.0f }, ImVec2 { 820.0f, 560.0f } };
    float layout_combo_width = 220.0f;
    float camera_combo_width = 116.0f;
    float maximize_button_width = 70.0f;
    float pane_toolbar_height = 26.0f;
    float toolbar_control_gap = 6.0f;
    float splitter_thickness = 6.0f;
    float splitter_half_thickness = 3.0f;
    float splitter_visible_half_thickness = 1.0f;
    float minimum_pane_pixels = 120.0f;
    ImGuiCol splitter_color = ImGuiCol_Separator;
};

struct ViewportSurfaceUiStyle {
    float minimum_surface_pixels = 1.0f;
    float overlay_padding = 6.0f;
    float overlay_line_gap = 2.0f;
    float overlay_margin = 8.0f;
    float overlay_rounding = 4.0f;
    float inactive_border_thickness = 1.0f;
    float active_border_thickness = 2.0f;
    ImGuiCol surface_background_color = ImGuiCol_WindowBg;
    ImGuiCol empty_text_color = ImGuiCol_TextDisabled;
    ImGuiCol inactive_border_color = ImGuiCol_Border;
    ImGuiCol active_border_color = ImGuiCol_PlotHistogram;
    ImU32 overlay_background_color = IM_COL32(0, 0, 0, 170);
    ImU32 overlay_border_color = IM_COL32(255, 255, 255, 45);
    ImU32 overlay_text_color = IM_COL32(230, 235, 240, 230);
};

struct PreviewPanelUiStyle {
    InitialWindowBounds material_window { ImVec2 { 888.0f, 56.0f }, ImVec2 { 380.0f, 390.0f } };
    InitialWindowBounds light_window { ImVec2 { 888.0f, 472.0f }, ImVec2 { 340.0f, 260.0f } };
};

[[nodiscard]] inline ViewportHostUiStyle viewport_host_ui_style()
{
    return {};
}

[[nodiscard]] inline ViewportSurfaceUiStyle viewport_surface_ui_style()
{
    return {};
}

[[nodiscard]] inline PreviewPanelUiStyle preview_panel_ui_style()
{
    return {};
}

inline void set_initial_window_bounds(const InitialWindowBounds& bounds)
{
    ImGui::SetNextWindowPos(bounds.position, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(bounds.size, ImGuiCond_FirstUseEver);
}

} // namespace quader
