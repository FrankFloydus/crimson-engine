module;

#include <imgui.h>

#include <algorithm>
#include <cmath>

module quader.ui.viewport_panels;

namespace quader {
namespace {

bool render_srgb_color_edit(const char* label, LinearColor& linear_color)
{
    SrgbColor srgb = linear_to_srgb(linear_color);
    float values[3] = { srgb.r, srgb.g, srgb.b };
    if (!ImGui::ColorEdit3(label, values, ImGuiColorEditFlags_Float)) {
        return false;
    }

    linear_color = srgb_to_linear(SrgbColor { values[0], values[1], values[2] });
    return true;
}

RenderExtent render_extent_from_size(ImVec2 size)
{
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return {};
    }

    return RenderExtent {
        static_cast<uint32_t>(std::max(1.0f, std::floor(size.x))),
        static_cast<uint32_t>(std::max(1.0f, std::floor(size.y))),
    };
}

ViewportImageBounds image_bounds_from_rect(ImVec2 min, ImVec2 max)
{
    return ViewportImageBounds {
        min.x,
        min.y,
        std::max(0.0f, max.x - min.x),
        std::max(0.0f, max.y - min.y),
    };
}

bool panel_mouse_input_started()
{
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
        ImGui::GetIO().MouseWheel != 0.0f;
}

bool viewport_navigation_key_is_down()
{
    return ImGui::IsKeyDown(ImGuiKey_W) ||
        ImGui::IsKeyDown(ImGuiKey_A) ||
        ImGui::IsKeyDown(ImGuiKey_S) ||
        ImGui::IsKeyDown(ImGuiKey_D) ||
        ImGui::IsKeyDown(ImGuiKey_Q) ||
        ImGui::IsKeyDown(ImGuiKey_E);
}

} // namespace

ViewportPanelResult render_scene_viewport_panel(ImTextureID texture_id, RenderExtent extent)
{
    ImGui::SetNextWindowPos(ImVec2(32.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_FirstUseEver);

    ViewportPanelResult result;
    if (ImGui::Begin("Viewport")) {
        result.viewport_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
        result.viewport_focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        result.viewport_clicked = result.viewport_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        result.viewport_active = result.viewport_hovered && ImGui::IsMouseDown(ImGuiMouseButton_Left);

        if (result.viewport_hovered && panel_mouse_input_started()) {
            ImGui::SetWindowFocus();
            result.viewport_focused = true;
        }

        const ImVec2 available_size = ImGui::GetContentRegionAvail();
        result.requested_extent = render_extent_from_size(available_size);

        if (texture_id != ImTextureID {} && extent.is_valid()) {
            const ImVec2 image_size(
                std::max(1.0f, available_size.x),
                std::max(1.0f, available_size.y));

            ImGui::InvisibleButton(
                "SceneViewportImage",
                image_size,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);
            result.image_visible = true;
            result.image_hovered = ImGui::IsItemHovered();
            result.image_focused = ImGui::IsItemFocused();
            result.image_active = ImGui::IsItemActive();
            result.image_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            result.image_dragging = result.image_active && ImGui::IsMouseDragging(ImGuiMouseButton_Left);

            const ImVec2 image_min = ImGui::GetItemRectMin();
            const ImVec2 image_max = ImGui::GetItemRectMax();
            result.image_bounds = image_bounds_from_rect(image_min, image_max);
            ImGui::GetWindowDrawList()->AddImage(texture_id, image_min, image_max);

            if (result.image_hovered && viewport_navigation_key_is_down()) {
                ImGui::SetWindowFocus();
                result.viewport_focused = true;
                result.image_focused = true;
            }
        } else {
            ImGui::TextUnformatted("Waiting for scene viewport pixels.");
        }
    }

    ImGui::End();
    return result;
}

bool render_directional_light_panel(DirectionalLightSettings& settings)
{
    ImGui::SetNextWindowPos(ImVec2(888.0f, 472.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 260.0f), ImGuiCond_FirstUseEver);

    bool changed = false;
    if (ImGui::Begin("Directional Light")) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && panel_mouse_input_started()) {
            ImGui::SetWindowFocus();
        }

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
