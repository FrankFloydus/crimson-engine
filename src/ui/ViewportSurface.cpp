module;

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstdio>

module quader.ui.viewport_surface;

import quader.ui.style;

namespace quader {
namespace {

[[nodiscard]] RenderExtent render_extent_from_size(ImVec2 size, const ViewportSurfaceUiStyle& style)
{
    if (size.x <= 0.0f || size.y <= 0.0f) {
        return {};
    }

    return RenderExtent {
        static_cast<uint32_t>(std::max(style.minimum_surface_pixels, std::floor(size.x))),
        static_cast<uint32_t>(std::max(style.minimum_surface_pixels, std::floor(size.y))),
    };
}

[[nodiscard]] ViewportImageBounds image_bounds_from_rect(ImVec2 min, ImVec2 max)
{
    return ViewportImageBounds {
        min.x,
        min.y,
        std::max(0.0f, max.x - min.x),
        std::max(0.0f, max.y - min.y),
    };
}

void draw_centered_text(
    ImDrawList* draw_list,
    ImVec2 min,
    ImVec2 max,
    const char* text,
    const ViewportSurfaceUiStyle& style)
{
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 position {
        min.x + std::max(0.0f, (max.x - min.x - text_size.x) * 0.5f),
        min.y + std::max(0.0f, (max.y - min.y - text_size.y) * 0.5f),
    };
    draw_list->AddText(position, ImGui::GetColorU32(style.empty_text_color), text);
}

void draw_performance_overlay(
    ImDrawList* draw_list,
    ImVec2 min,
    ImVec2 max,
    const ViewportSurfaceInput& input,
    RenderExtent requested_extent,
    const ViewportSurfaceUiStyle& style)
{
    if (!input.performance.enabled) {
        return;
    }

    const float delta_seconds =
        input.performance.frame_delta_seconds > 0.0f ? input.performance.frame_delta_seconds : ImGui::GetIO().DeltaTime;
    const float frame_ms = delta_seconds > 0.0f ? delta_seconds * 1000.0f : 0.0f;
    const float fps = delta_seconds > 0.0f ? 1.0f / delta_seconds : 0.0f;
    const char* render_state =
        input.performance.render_request_pending ? "pending" : (input.performance.dirty ? "dirty" : "ready");

    char frame_line[96] {};
    char surface_line[96] {};
    char texture_line[96] {};
    char render_line[96] {};
    std::snprintf(frame_line, sizeof(frame_line), "FPS %.1f | Frame %.2f ms", fps, frame_ms);
    std::snprintf(
        surface_line,
        sizeof(surface_line),
        "Surface %ux%u",
        requested_extent.width,
        requested_extent.height);
    std::snprintf(
        texture_line,
        sizeof(texture_line),
        "Texture %ux%u",
        input.texture.extent.width,
        input.texture.extent.height);
    std::snprintf(render_line, sizeof(render_line), "Render %s", render_state);

    const char* lines[] = { frame_line, surface_line, texture_line, render_line };
    const float line_height = ImGui::GetTextLineHeight();

    ImVec2 text_size {};
    for (const char* line : lines) {
        const ImVec2 line_size = ImGui::CalcTextSize(line);
        text_size.x = std::max(text_size.x, line_size.x);
        text_size.y += line_height;
    }
    text_size.y += style.overlay_line_gap * 3.0f;

    const ImVec2 box_min {
        min.x + style.overlay_margin,
        std::max(
            min.y + style.overlay_margin,
            max.y - text_size.y - style.overlay_padding * 2.0f - style.overlay_margin),
    };
    const ImVec2 box_max {
        box_min.x + text_size.x + style.overlay_padding * 2.0f,
        box_min.y + text_size.y + style.overlay_padding * 2.0f,
    };

    draw_list->AddRectFilled(box_min, box_max, style.overlay_background_color, style.overlay_rounding);
    draw_list->AddRect(box_min, box_max, style.overlay_border_color, style.overlay_rounding);

    ImVec2 text_position { box_min.x + style.overlay_padding, box_min.y + style.overlay_padding };
    for (const char* line : lines) {
        draw_list->AddText(text_position, style.overlay_text_color, line);
        text_position.y += line_height + style.overlay_line_gap;
    }
}

} // namespace

ViewportSurfaceResult draw_viewport_surface(const ViewportSurfaceInput& input)
{
    const ViewportSurfaceUiStyle style = viewport_surface_ui_style();
    ImGui::PushID(static_cast<int>(input.id.value));

    const ImVec2 available_size = ImGui::GetContentRegionAvail();
    const ImVec2 surface_size {
        std::max(style.minimum_surface_pixels, available_size.x),
        std::max(style.minimum_surface_pixels, available_size.y),
    };

    ViewportSurfaceResult result;
    result.id = input.id;
    result.requested_extent = render_extent_from_size(surface_size, style);

    ImGui::InvisibleButton(
        "ViewportSurface",
        surface_size,
        ImGuiButtonFlags_MouseButtonLeft |
            ImGuiButtonFlags_MouseButtonRight |
            ImGuiButtonFlags_MouseButtonMiddle);

    result.visible = ImGui::IsItemVisible();
    result.hovered = ImGui::IsItemHovered();
    result.focused = ImGui::IsItemFocused();
    result.active = ImGui::IsItemActive();
    result.clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
        ImGui::IsItemClicked(ImGuiMouseButton_Right) ||
        ImGui::IsItemClicked(ImGuiMouseButton_Middle);
    result.left_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    result.left_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);
    result.left_released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    result.dragging = result.active &&
        (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Right) ||
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle));

    const ImVec2 image_min = ImGui::GetItemRectMin();
    const ImVec2 image_max = ImGui::GetItemRectMax();
    result.image_bounds = image_bounds_from_rect(image_min, image_max);
    if (result.hovered || result.active) {
        const ImVec2 mouse = ImGui::GetMousePos();
        result.pointer = ViewportSurfacePointer {
            .local_x = std::clamp(mouse.x - image_min.x, 0.0f, result.image_bounds.width),
            .local_y = std::clamp(mouse.y - image_min.y, 0.0f, result.image_bounds.height),
        };
    }
    if (result.left_clicked) {
        const ImVec2 mouse = ImGui::GetMousePos();
        result.click = ViewportSurfaceClick {
            .local_x = std::clamp(mouse.x - image_min.x, 0.0f, result.image_bounds.width),
            .local_y = std::clamp(mouse.y - image_min.y, 0.0f, result.image_bounds.height),
        };
    }

    if (!result.visible) {
        ImGui::PopID();
        return result;
    }

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImU32 background = ImGui::GetColorU32(style.surface_background_color);
    draw_list->AddRectFilled(image_min, image_max, background);

    if (input.texture.has_pixels && input.texture.texture_id != ImTextureID {} && input.texture.extent.is_valid()) {
        draw_list->AddImage(input.texture.texture_id, image_min, image_max);
    } else {
        draw_centered_text(draw_list, image_min, image_max, input.empty_text, style);
    }

    draw_performance_overlay(draw_list, image_min, image_max, input, result.requested_extent, style);

    const ImU32 border =
        ImGui::GetColorU32(input.active ? style.active_border_color : style.inactive_border_color);
    draw_list->AddRect(
        image_min,
        image_max,
        border,
        0.0f,
        0,
        input.active ? style.active_border_thickness : style.inactive_border_thickness);

    ImGui::PopID();
    return result;
}

} // namespace quader
