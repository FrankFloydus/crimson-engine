module;

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

export module quader.ui_shell.editor_viewport_layout;

export import quader.render.viewport_types;

export namespace quader {

enum class ViewportLayoutPreset {
    single_view,
    two_view_horizontal,
    two_view_vertical,
    three_view_primary_right,
    three_view_primary_left,
    three_view_primary_bottom,
    three_view_primary_top,
    four_view_quad,
};

struct ViewportRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ViewportLayoutSlot {
    ViewportId viewport_id;
    ViewportRect rect;
};

struct ViewportLayoutState {
    ViewportLayoutPreset preset = ViewportLayoutPreset::single_view;
    std::optional<ViewportId> maximized_viewport;
    float root_split_ratio = 0.5f;
    float secondary_split_ratio = 0.5f;
};

struct ViewportLayoutPresetInfo {
    ViewportLayoutPreset preset = ViewportLayoutPreset::single_view;
    const char* name = "";
    std::size_t required_viewport_count = 1;
};

[[nodiscard]] std::span<const ViewportLayoutPresetInfo> viewport_layout_presets() noexcept;
[[nodiscard]] const char* viewport_layout_preset_name(ViewportLayoutPreset preset) noexcept;
[[nodiscard]] std::size_t viewport_layout_required_viewport_count(ViewportLayoutPreset preset) noexcept;
[[nodiscard]] float clamp_viewport_split_ratio(float ratio, float available_pixels, float minimum_pixels) noexcept;
[[nodiscard]] std::vector<ViewportLayoutSlot> build_viewport_layout(
    const ViewportLayoutState& state,
    std::span<const ViewportId> viewport_ids);

} // namespace quader
