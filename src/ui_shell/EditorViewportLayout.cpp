module;

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

module quader.ui_shell.editor_viewport_layout;

namespace quader {
namespace {

constexpr std::array<ViewportLayoutPresetInfo, 8> kViewportLayoutPresets {
    ViewportLayoutPresetInfo { ViewportLayoutPreset::single_view, "Single View", 1 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::two_view_horizontal, "Two-View Horizontal", 2 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::two_view_vertical, "Two-View Vertical", 2 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::three_view_primary_right, "Three-View Primary Right", 3 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::three_view_primary_left, "Three-View Primary Left", 3 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::three_view_primary_bottom, "Three-View Primary Bottom", 3 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::three_view_primary_top, "Three-View Primary Top", 3 },
    ViewportLayoutPresetInfo { ViewportLayoutPreset::four_view_quad, "Four-View Quad", 4 },
};

[[nodiscard]] const ViewportLayoutPresetInfo& fallback_preset_info() noexcept
{
    return kViewportLayoutPresets.front();
}

[[nodiscard]] const ViewportLayoutPresetInfo& preset_info_for(ViewportLayoutPreset preset) noexcept
{
    for (const ViewportLayoutPresetInfo& info : kViewportLayoutPresets) {
        if (info.preset == preset) {
            return info;
        }
    }

    return fallback_preset_info();
}

[[nodiscard]] ViewportLayoutSlot slot(ViewportId viewport_id, float x, float y, float width, float height)
{
    return ViewportLayoutSlot {
        .viewport_id = viewport_id,
        .rect = ViewportRect { x, y, width, height },
    };
}

[[nodiscard]] float normalized_split_ratio(float value)
{
    if (!std::isfinite(value)) {
        return 0.5f;
    }

    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] std::vector<ViewportLayoutSlot> maximized_layout(
    std::optional<ViewportId> maximized_viewport,
    std::span<const ViewportId> viewport_ids)
{
    if (!maximized_viewport.has_value()) {
        return {};
    }

    if (!maximized_viewport->is_valid()) {
        return {};
    }

    for (ViewportId viewport_id : viewport_ids) {
        if (viewport_id == *maximized_viewport) {
            return { slot(viewport_id, 0.0f, 0.0f, 1.0f, 1.0f) };
        }
    }

    return {};
}

} // namespace

std::span<const ViewportLayoutPresetInfo> viewport_layout_presets() noexcept
{
    return std::span<const ViewportLayoutPresetInfo> { kViewportLayoutPresets };
}

const char* viewport_layout_preset_name(ViewportLayoutPreset preset) noexcept
{
    return preset_info_for(preset).name;
}

std::size_t viewport_layout_required_viewport_count(ViewportLayoutPreset preset) noexcept
{
    return preset_info_for(preset).required_viewport_count;
}

float clamp_viewport_split_ratio(float ratio, float available_pixels, float minimum_pixels) noexcept
{
    const float normalized_ratio = normalized_split_ratio(ratio);
    if (!std::isfinite(available_pixels) || available_pixels <= 0.0f) {
        return normalized_ratio;
    }

    const float minimum = std::isfinite(minimum_pixels) ? std::max(0.0f, minimum_pixels) : 0.0f;
    if (minimum <= 0.0f) {
        return normalized_ratio;
    }

    if (available_pixels <= minimum * 2.0f) {
        return 0.5f;
    }

    const float minimum_ratio = minimum / available_pixels;
    return std::clamp(normalized_ratio, minimum_ratio, 1.0f - minimum_ratio);
}

std::vector<ViewportLayoutSlot> build_viewport_layout(
    const ViewportLayoutState& state,
    std::span<const ViewportId> viewport_ids)
{
    if (viewport_ids.empty()) {
        return {};
    }

    if (std::vector<ViewportLayoutSlot> maximized = maximized_layout(state.maximized_viewport, viewport_ids);
        !maximized.empty()) {
        return maximized;
    }

    const float root = normalized_split_ratio(state.root_split_ratio);
    const float secondary = normalized_split_ratio(state.secondary_split_ratio);
    std::vector<ViewportLayoutSlot> slots;
    slots.reserve(std::min(viewport_ids.size(), viewport_layout_required_viewport_count(state.preset)));

    switch (state.preset) {
    case ViewportLayoutPreset::single_view:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, 1.0f, 1.0f));
        break;
    case ViewportLayoutPreset::two_view_horizontal:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, 1.0f, root));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], 0.0f, root, 1.0f, 1.0f - root));
        }
        break;
    case ViewportLayoutPreset::two_view_vertical:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, root, 1.0f));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], root, 0.0f, 1.0f - root, 1.0f));
        }
        break;
    case ViewportLayoutPreset::three_view_primary_right:
        slots.push_back(slot(viewport_ids[0], 1.0f - root, 0.0f, root, 1.0f));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], 0.0f, 0.0f, 1.0f - root, secondary));
        }
        if (viewport_ids.size() > 2) {
            slots.push_back(slot(viewport_ids[2], 0.0f, secondary, 1.0f - root, 1.0f - secondary));
        }
        break;
    case ViewportLayoutPreset::three_view_primary_left:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, root, 1.0f));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], root, 0.0f, 1.0f - root, secondary));
        }
        if (viewport_ids.size() > 2) {
            slots.push_back(slot(viewport_ids[2], root, secondary, 1.0f - root, 1.0f - secondary));
        }
        break;
    case ViewportLayoutPreset::three_view_primary_bottom:
        slots.push_back(slot(viewport_ids[0], 0.0f, 1.0f - root, 1.0f, root));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], 0.0f, 0.0f, secondary, 1.0f - root));
        }
        if (viewport_ids.size() > 2) {
            slots.push_back(slot(viewport_ids[2], secondary, 0.0f, 1.0f - secondary, 1.0f - root));
        }
        break;
    case ViewportLayoutPreset::three_view_primary_top:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, 1.0f, root));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], 0.0f, root, secondary, 1.0f - root));
        }
        if (viewport_ids.size() > 2) {
            slots.push_back(slot(viewport_ids[2], secondary, root, 1.0f - secondary, 1.0f - root));
        }
        break;
    case ViewportLayoutPreset::four_view_quad:
        slots.push_back(slot(viewport_ids[0], 0.0f, 0.0f, root, secondary));
        if (viewport_ids.size() > 1) {
            slots.push_back(slot(viewport_ids[1], root, 0.0f, 1.0f - root, secondary));
        }
        if (viewport_ids.size() > 2) {
            slots.push_back(slot(viewport_ids[2], 0.0f, secondary, root, 1.0f - secondary));
        }
        if (viewport_ids.size() > 3) {
            slots.push_back(slot(viewport_ids[3], root, secondary, 1.0f - root, 1.0f - secondary));
        }
        break;
    }

    return slots;
}

} // namespace quader
