module;

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <vector>

module quader.ui.viewport_host_panel;

import quader.ui.style;

namespace quader {
namespace {

constexpr size_t kInlineViewportIdCapacity = 8;

constexpr std::array<ViewportLayoutPreset, 8> kLayoutPresets {
    ViewportLayoutPreset::single_view,
    ViewportLayoutPreset::two_view_horizontal,
    ViewportLayoutPreset::two_view_vertical,
    ViewportLayoutPreset::three_view_primary_right,
    ViewportLayoutPreset::three_view_primary_left,
    ViewportLayoutPreset::three_view_primary_bottom,
    ViewportLayoutPreset::three_view_primary_top,
    ViewportLayoutPreset::four_view_quad,
};

constexpr std::array<ViewportCameraPreset, 4> kCameraPresets {
    ViewportCameraPreset::perspective,
    ViewportCameraPreset::top,
    ViewportCameraPreset::side,
    ViewportCameraPreset::front,
};

[[nodiscard]] std::span<const ViewportId> viewport_ids_from(
    std::span<const ViewportPaneInput> panes,
    std::array<ViewportId, kInlineViewportIdCapacity>& inline_ids,
    std::vector<ViewportId>& overflow_ids)
{
    if (panes.size() <= inline_ids.size()) {
        for (size_t index = 0; index < panes.size(); ++index) {
            inline_ids[index] = panes[index].id;
        }
        return std::span<const ViewportId> { inline_ids.data(), panes.size() };
    }

    overflow_ids.clear();
    overflow_ids.reserve(panes.size());
    for (const ViewportPaneInput& pane : panes) {
        overflow_ids.push_back(pane.id);
    }
    return overflow_ids;
}

[[nodiscard]] const ViewportPaneInput* find_pane(std::span<const ViewportPaneInput> panes, ViewportId viewport_id)
{
    for (const ViewportPaneInput& pane : panes) {
        if (pane.id == viewport_id) {
            return &pane;
        }
    }
    return nullptr;
}

[[nodiscard]] bool draw_layout_combo(ViewportLayoutState& layout_state, const ViewportHostUiStyle& style)
{
    bool changed = false;
    const char* current = viewport_layout_preset_name(layout_state.preset);
    ImGui::SetNextItemWidth(style.layout_combo_width);
    if (ImGui::BeginCombo("##ViewportLayoutPreset", current)) {
        for (ViewportLayoutPreset preset : kLayoutPresets) {
            const bool selected = layout_state.preset == preset;
            if (ImGui::Selectable(viewport_layout_preset_name(preset), selected)) {
                layout_state.preset = preset;
                layout_state.maximized_viewport.reset();
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

[[nodiscard]] std::optional<ViewportCameraPreset> draw_camera_combo(
    ViewportCameraPreset current,
    const ViewportHostUiStyle& style)
{
    std::optional<ViewportCameraPreset> requested;
    ImGui::SetNextItemWidth(style.camera_combo_width);
    if (ImGui::BeginCombo("##CameraPreset", viewport_camera_preset_name(current))) {
        for (ViewportCameraPreset preset : kCameraPresets) {
            const bool selected = current == preset;
            if (ImGui::Selectable(viewport_camera_preset_name(preset), selected)) {
                requested = preset;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return requested;
}

[[nodiscard]] ImVec2 rect_min(ImVec2 origin, ImVec2 size, ViewportRect rect)
{
    return ImVec2 {
        origin.x + rect.x * size.x,
        origin.y + rect.y * size.y,
    };
}

struct PaneScreenRect {
    ImVec2 min;
    ImVec2 size;
};

[[nodiscard]] PaneScreenRect pane_screen_rect(
    ImVec2 origin,
    ImVec2 size,
    ViewportRect rect,
    const ViewportHostUiStyle& style)
{
    ImVec2 min = rect_min(origin, size, rect);
    ImVec2 max {
        min.x + rect.width * size.x,
        min.y + rect.height * size.y,
    };

    if (rect.x > 0.0f) {
        min.x += style.splitter_half_thickness;
    }
    if (rect.y > 0.0f) {
        min.y += style.splitter_half_thickness;
    }
    if (rect.x + rect.width < 1.0f) {
        max.x -= style.splitter_half_thickness;
    }
    if (rect.y + rect.height < 1.0f) {
        max.y -= style.splitter_half_thickness;
    }

    return PaneScreenRect {
        .min = min,
        .size = ImVec2 {
            std::max(1.0f, max.x - min.x),
            std::max(1.0f, max.y - min.y),
        },
    };
}

bool drag_vertical_splitter(
    const char* id,
    float& ratio,
    float x,
    float y,
    float height,
    float available_width,
    const ViewportHostUiStyle& style)
{
    ImGui::SetCursorScreenPos(ImVec2 { x - style.splitter_half_thickness, y });
    ImGui::InvisibleButton(id, ImVec2 { style.splitter_thickness, height });
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag to resize");
    }
    if (ImGui::IsItemActive() && available_width > 0.0f) {
        ratio += ImGui::GetIO().MouseDelta.x / available_width;
        ratio = clamp_viewport_split_ratio(ratio, available_width, style.minimum_pane_pixels);
        return true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2 { x - style.splitter_visible_half_thickness, y },
        ImVec2 { x + style.splitter_visible_half_thickness, y + height },
        ImGui::GetColorU32(style.splitter_color));
    return false;
}

bool drag_horizontal_splitter(
    const char* id,
    float& ratio,
    float x,
    float y,
    float width,
    float available_height,
    const ViewportHostUiStyle& style)
{
    ImGui::SetCursorScreenPos(ImVec2 { x, y - style.splitter_half_thickness });
    ImGui::InvisibleButton(id, ImVec2 { width, style.splitter_thickness });
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Drag to resize");
    }
    if (ImGui::IsItemActive() && available_height > 0.0f) {
        ratio += ImGui::GetIO().MouseDelta.y / available_height;
        ratio = clamp_viewport_split_ratio(ratio, available_height, style.minimum_pane_pixels);
        return true;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    }
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2 { x, y - style.splitter_visible_half_thickness },
        ImVec2 { x + width, y + style.splitter_visible_half_thickness },
        ImGui::GetColorU32(style.splitter_color));
    return false;
}

bool draw_splitters(ViewportLayoutState& state, ImVec2 origin, ImVec2 size, const ViewportHostUiStyle& style)
{
    if (state.maximized_viewport.has_value()) {
        return false;
    }

    bool changed = false;
    switch (state.preset) {
    case ViewportLayoutPreset::two_view_vertical:
        changed |= drag_vertical_splitter(
            "##RootVerticalSplitter",
            state.root_split_ratio,
            origin.x + state.root_split_ratio * size.x,
            origin.y,
            size.y,
            size.x,
            style);
        break;
    case ViewportLayoutPreset::three_view_primary_left:
        changed |= drag_vertical_splitter(
            "##RootVerticalSplitter",
            state.root_split_ratio,
            origin.x + state.root_split_ratio * size.x,
            origin.y,
            size.y,
            size.x,
            style);
        changed |= drag_horizontal_splitter(
            "##SecondaryHorizontalSplitter",
            state.secondary_split_ratio,
            origin.x + state.root_split_ratio * size.x,
            origin.y + state.secondary_split_ratio * size.y,
            (1.0f - state.root_split_ratio) * size.x,
            size.y,
            style);
        break;
    case ViewportLayoutPreset::three_view_primary_right:
        changed |= drag_vertical_splitter(
            "##RootVerticalSplitter",
            state.root_split_ratio,
            origin.x + (1.0f - state.root_split_ratio) * size.x,
            origin.y,
            size.y,
            size.x,
            style);
        changed |= drag_horizontal_splitter(
            "##SecondaryHorizontalSplitter",
            state.secondary_split_ratio,
            origin.x,
            origin.y + state.secondary_split_ratio * size.y,
            (1.0f - state.root_split_ratio) * size.x,
            size.y,
            style);
        break;
    case ViewportLayoutPreset::two_view_horizontal:
        changed |= drag_horizontal_splitter(
            "##RootHorizontalSplitter",
            state.root_split_ratio,
            origin.x,
            origin.y + state.root_split_ratio * size.y,
            size.x,
            size.y,
            style);
        break;
    case ViewportLayoutPreset::three_view_primary_top:
        changed |= drag_horizontal_splitter(
            "##RootHorizontalSplitter",
            state.root_split_ratio,
            origin.x,
            origin.y + state.root_split_ratio * size.y,
            size.x,
            size.y,
            style);
        changed |= drag_vertical_splitter(
            "##SecondaryVerticalSplitter",
            state.secondary_split_ratio,
            origin.x + state.secondary_split_ratio * size.x,
            origin.y + state.root_split_ratio * size.y,
            (1.0f - state.root_split_ratio) * size.y,
            size.x,
            style);
        break;
    case ViewportLayoutPreset::three_view_primary_bottom:
        changed |= drag_horizontal_splitter(
            "##RootHorizontalSplitter",
            state.root_split_ratio,
            origin.x,
            origin.y + (1.0f - state.root_split_ratio) * size.y,
            size.x,
            size.y,
            style);
        changed |= drag_vertical_splitter(
            "##SecondaryVerticalSplitter",
            state.secondary_split_ratio,
            origin.x + state.secondary_split_ratio * size.x,
            origin.y,
            (1.0f - state.root_split_ratio) * size.y,
            size.x,
            style);
        break;
    case ViewportLayoutPreset::four_view_quad:
        changed |= drag_vertical_splitter(
            "##RootVerticalSplitter",
            state.root_split_ratio,
            origin.x + state.root_split_ratio * size.x,
            origin.y,
            size.y,
            size.x,
            style);
        changed |= drag_horizontal_splitter(
            "##SecondaryHorizontalSplitter",
            state.secondary_split_ratio,
            origin.x,
            origin.y + state.secondary_split_ratio * size.y,
            size.x,
            size.y,
            style);
        break;
    case ViewportLayoutPreset::single_view:
        break;
    }

    return changed;
}

void draw_pane_toolbar(
    const ViewportPaneInput& pane,
    ViewportLayoutState& layout_state,
    ViewportPaneResult& result,
    const ViewportHostUiStyle& style)
{
    const bool is_maximized = layout_state.maximized_viewport == pane.id;
    const float controls_width = style.maximize_button_width +
        (pane.camera_dropdown_enabled ? style.camera_combo_width + style.toolbar_control_gap : 0.0f);

    if (!ImGui::BeginTable(
            "ViewportPaneToolbar",
            2,
            ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_NoPadOuterX |
                ImGuiTableFlags_NoPadInnerX)) {
        return;
    }

    ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Controls", ImGuiTableColumnFlags_WidthFixed, controls_width);
    ImGui::TableNextRow(ImGuiTableRowFlags_None, style.pane_toolbar_height);

    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(pane.title.data(), pane.title.data() + pane.title.size());

    ImGui::TableSetColumnIndex(1);
    if (pane.camera_dropdown_enabled) {
        if (std::optional<ViewportCameraPreset> preset = draw_camera_combo(pane.camera_preset, style)) {
            result.requested_camera_preset = *preset;
        }
        ImGui::SameLine(0.0f, style.toolbar_control_gap);
    }

    if (ImGui::Button(is_maximized ? "Restore" : "Max", ImVec2 { style.maximize_button_width, 0.0f })) {
        result.requested_maximize_toggle = true;
    }

    ImGui::EndTable();
}

ViewportPaneResult draw_pane(
    const ViewportPaneInput& pane,
    ViewportLayoutState& layout_state,
    ImVec2 min,
    ImVec2 size,
    const ViewportHostUiStyle& style)
{
    ViewportPaneResult result;
    result.id = pane.id;

    ImGui::SetCursorScreenPos(min);
    char pane_id[32] {};
    std::snprintf(pane_id, sizeof(pane_id), "ViewportPane-%llu", static_cast<unsigned long long>(pane.id.value));
    ImGui::PushID(pane_id);
    if (ImGui::BeginChild("ViewportPaneChild", size, true, ImGuiWindowFlags_NoScrollbar)) {
        draw_pane_toolbar(pane, layout_state, result, style);
        ImGui::Separator();
        result.surface = draw_viewport_surface(ViewportSurfaceInput {
            .id = pane.id,
            .texture = pane.texture,
            .performance = pane.performance,
            .empty_text = "Waiting for viewport pixels.",
            .active = pane.active,
        });
    }
    ImGui::EndChild();
    ImGui::PopID();

    return result;
}

} // namespace

ViewportHostPanelResult draw_viewport_host_panel(
    ViewportLayoutState& layout_state,
    std::span<const ViewportPaneInput> panes)
{
    const ViewportHostUiStyle style = viewport_host_ui_style();
    set_initial_window_bounds(style.window);

    ViewportHostPanelResult result;
    if (ImGui::Begin("Viewport")) {
        result.layout_changed |= draw_layout_combo(layout_state, style);
        ImGui::Separator();

        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        if (content_size.x < style.minimum_pane_pixels || content_size.y < style.minimum_pane_pixels) {
            ImGui::End();
            return result;
        }

        std::array<ViewportId, kInlineViewportIdCapacity> inline_viewport_ids {};
        std::vector<ViewportId> overflow_viewport_ids;
        const std::span<const ViewportId> viewport_ids =
            viewport_ids_from(panes, inline_viewport_ids, overflow_viewport_ids);
        const std::vector<ViewportLayoutSlot> slots = build_viewport_layout(layout_state, viewport_ids);

        result.panes.reserve(slots.size());
        for (const ViewportLayoutSlot& slot : slots) {
            if (const ViewportPaneInput* pane = find_pane(panes, slot.viewport_id)) {
                const PaneScreenRect pane_rect = pane_screen_rect(content_origin, content_size, slot.rect, style);
                ViewportPaneResult pane_result = draw_pane(
                    *pane,
                    layout_state,
                    pane_rect.min,
                    pane_rect.size,
                    style);
                if (pane_result.surface.hovered || pane_result.surface.clicked || pane_result.requested_maximize_toggle) {
                    result.active_viewport = pane_result.id;
                }
                if (pane_result.requested_maximize_toggle) {
                    if (layout_state.maximized_viewport == pane_result.id) {
                        layout_state.maximized_viewport.reset();
                    } else {
                        layout_state.maximized_viewport = pane_result.id;
                    }
                    result.layout_changed = true;
                }
                result.panes.push_back(std::move(pane_result));
            }
        }

        result.layout_changed |= draw_splitters(layout_state, content_origin, content_size, style);
    }

    ImGui::End();
    return result;
}

} // namespace quader
