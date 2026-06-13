module;

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <vector>

module quader.ui.viewport_host_panel;

import quader.ui.controls;
import quader.ui.style;
import quader.ui.viewport_surface;

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

constexpr std::array<BloomBlendMode, 2> kBloomBlendModes {
    BloomBlendMode::add,
    BloomBlendMode::interpolate,
};

constexpr std::array<BloomQuality, 3> kBloomQualities {
    BloomQuality::low,
    BloomQuality::medium,
    BloomQuality::high,
};

constexpr std::array<AmbientOcclusionAlgorithm, 2> kAmbientOcclusionAlgorithms {
    AmbientOcclusionAlgorithm::scalable,
    AmbientOcclusionAlgorithm::ground_truth,
};

constexpr std::array<AmbientOcclusionQuality, 3> kAmbientOcclusionQualities {
    AmbientOcclusionQuality::low,
    AmbientOcclusionQuality::medium,
    AmbientOcclusionQuality::high,
};

[[nodiscard]] const char* bloom_blend_mode_label(BloomBlendMode mode)
{
    switch (mode) {
    case BloomBlendMode::add:
        return "Add";
    case BloomBlendMode::interpolate:
        return "Interpolate";
    }

    return "Add";
}

[[nodiscard]] const char* bloom_quality_label(BloomQuality quality)
{
    switch (quality) {
    case BloomQuality::low:
        return "Low";
    case BloomQuality::medium:
        return "Medium";
    case BloomQuality::high:
        return "High";
    }

    return "Low";
}

[[nodiscard]] const char* ambient_occlusion_algorithm_label(AmbientOcclusionAlgorithm algorithm)
{
    switch (algorithm) {
    case AmbientOcclusionAlgorithm::scalable:
        return "Scalable";
    case AmbientOcclusionAlgorithm::ground_truth:
        return "Ground Truth";
    }

    return "Scalable";
}

[[nodiscard]] const char* ambient_occlusion_quality_label(AmbientOcclusionQuality quality)
{
    switch (quality) {
    case AmbientOcclusionQuality::low:
        return "Low";
    case AmbientOcclusionQuality::medium:
        return "Medium";
    case AmbientOcclusionQuality::high:
        return "High";
    }

    return "Low";
}

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

[[nodiscard]] bool layout_states_match(const ViewportLayoutState& left, const ViewportLayoutState& right)
{
    return left.preset == right.preset &&
        left.maximized_viewport == right.maximized_viewport &&
        floats_match(left.root_split_ratio, right.root_split_ratio) &&
        floats_match(left.secondary_split_ratio, right.secondary_split_ratio);
}

[[nodiscard]] bool colors_match(LinearRgba left, LinearRgba right)
{
    return floats_match(left.r, right.r) &&
        floats_match(left.g, right.g) &&
        floats_match(left.b, right.b) &&
        floats_match(left.a, right.a);
}

[[nodiscard]] bool render_srgb_rgba_color_edit(const char* label, LinearRgba& color)
{
    const SrgbColor srgb = linear_to_srgb(LinearColor { color.r, color.g, color.b });
    float values[4] { srgb.r, srgb.g, srgb.b, color.a };
    if (!ImGui::ColorEdit4(label, values, ImGuiColorEditFlags_Float)) {
        return false;
    }

    const LinearColor linear = srgb_to_linear(SrgbColor { values[0], values[1], values[2] });
    color = LinearRgba {
        .r = linear.r,
        .g = linear.g,
        .b = linear.b,
        .a = values[3],
    };
    return true;
}

[[nodiscard]] bool render_resettable_srgb_rgba_edit(
    const char* label,
    LinearRgba& value,
    LinearRgba default_value,
    const char* tooltip)
{
    const bool changed_from_default = !colors_match(value, default_value);
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = render_srgb_rgba_color_edit("##Value", value);
    render_item_tooltip(tooltip);
    if (end_resettable_property(layout, "Reset this color to its default.")) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] bool draw_layout_combo(ViewportLayoutState& layout_state, const ViewportHostUiStyle& style)
{
    bool changed = false;
    const ViewportLayoutState default_layout_state;
    const char* current = viewport_layout_preset_name(layout_state.preset);
    ImGui::SetNextItemWidth(style.layout_combo_width);
    {
        const ComboScope combo("##ViewportLayoutPreset", current);
        render_item_tooltip("Chooses the viewport arrangement. Reset restores the single-view layout and centered splitters.");
        if (combo) {
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
        }
    }
    if (render_changed_reset_control(
            "Viewport Layout",
            !layout_states_match(layout_state, default_layout_state),
            "Reset the viewport layout to the default single view.")) {
        layout_state = default_layout_state;
        changed = true;
    }
    return changed;
}

[[nodiscard]] bool draw_environment_popup(
    EnvironmentLightingSettings& settings,
    const ViewportHostUiStyle& style)
{
    bool changed = false;
    if (ImGui::Button("Environment", ImVec2 { style.environment_button_width, 0.0f })) {
        ImGui::OpenPopup("EnvironmentSettingsPopup");
    }
    render_item_tooltip("Edits the scene environment lighting used by every viewport.");

    // Popups auto-fit their contents. The resettable controls size from the available width,
    // so clamp X to avoid a feedback loop where changed rows make the popup grow every frame.
    ImGui::SetNextWindowSizeConstraints(
        ImVec2 { style.environment_popup_width, 0.0f },
        ImVec2 { style.environment_popup_width, std::numeric_limits<float>::max() });
    ImGui::SetNextWindowSize(ImVec2 { style.environment_popup_width, 0.0f }, ImGuiCond_Appearing);
    const PopupScope popup("EnvironmentSettingsPopup");
    if (popup) {
        const EnvironmentLightingSettings defaults;

        ImGui::TextUnformatted(environment_lighting_source_name(settings.source));
        render_item_tooltip("Current environment source.");

        changed |= render_resettable_checkbox(
            "Enabled",
            settings.enabled,
            defaults.enabled,
            "Enables the scene's indirect environment light.");

        {
            DisabledScope disabled(!settings.enabled);
            changed |= render_resettable_float_drag(
                "Intensity",
                settings.intensity,
                defaults.intensity,
                100.0f,
                0.0f,
                200000.0f,
                "%.0f",
                "Scales the indirect light in lux.");
            changed |= render_resettable_float_drag(
                "Rotation",
                settings.rotation_degrees,
                defaults.rotation_degrees,
                0.25f,
                -180.0f,
                180.0f,
                "%.1f deg",
                "Rotates the environment around the vertical axis.");
            changed |= render_resettable_checkbox(
                "Skybox",
                settings.show_skybox,
                defaults.show_skybox,
                "Shows the environment as the viewport background.");
        }

        changed |= render_resettable_srgb_rgba_edit(
            "Fallback",
            settings.fallback_color,
            defaults.fallback_color,
            "Viewport background color used when the environment skybox is hidden or unavailable.");

        if (changed) {
            settings = clamp_environment_lighting_settings(settings);
        }
    }

    return changed;
}

[[nodiscard]] bool draw_uint32_drag_property(
    const char* label,
    uint32_t& value,
    uint32_t default_value,
    int min_value,
    int max_value,
    const char* tooltip)
{
    int editable_value = static_cast<int>(value);
    bool changed = render_resettable_int_drag(
        label,
        editable_value,
        static_cast<int>(default_value),
        1.0f,
        min_value,
        max_value,
        tooltip);
    if (changed) {
        value = static_cast<uint32_t>(editable_value);
    }
    return changed;
}

[[nodiscard]] bool draw_uint8_drag_property(
    const char* label,
    uint8_t& value,
    uint8_t default_value,
    int min_value,
    int max_value,
    const char* tooltip)
{
    int editable_value = static_cast<int>(value);
    bool changed = render_resettable_int_drag(
        label,
        editable_value,
        static_cast<int>(default_value),
        0.1f,
        min_value,
        max_value,
        tooltip);
    if (changed) {
        value = static_cast<uint8_t>(editable_value);
    }
    return changed;
}

[[nodiscard]] bool draw_vec3_drag_property(
    const char* label,
    Vec3& value,
    Vec3 default_value,
    float speed,
    const char* format,
    const char* tooltip)
{
    float values[3] { value.x, value.y, value.z };
    const bool changed_from_default =
        !floats_match(value.x, default_value.x) ||
        !floats_match(value.y, default_value.y) ||
        !floats_match(value.z, default_value.z);
    const ResettablePropertyLayout layout = begin_resettable_property(label, tooltip, changed_from_default);
    bool changed = ImGui::DragFloat3("##Value", values, speed, 0.0f, 0.0f, format);
    render_item_tooltip(tooltip);
    if (changed) {
        value = Vec3 { values[0], values[1], values[2] };
    }
    if (end_resettable_property(layout)) {
        value = default_value;
        changed = true;
    }
    return changed;
}

[[nodiscard]] bool draw_post_processing_popup(
    PostProcessingSettings& settings,
    const ViewportHostUiStyle& style)
{
    bool changed = false;
    if (ImGui::Button("Post FX", ImVec2 { style.post_processing_button_width, 0.0f })) {
        ImGui::OpenPopup("PostProcessingSettingsPopup");
    }
    render_item_tooltip("Edits post-processing effects applied to rendered viewports.");

    ImGui::SetNextWindowSizeConstraints(
        ImVec2 { style.post_processing_popup_width, 0.0f },
        ImVec2 { style.post_processing_popup_width, std::numeric_limits<float>::max() });
    ImGui::SetNextWindowSize(ImVec2 { style.post_processing_popup_width, 0.0f }, ImGuiCond_Appearing);
    const PopupScope popup("PostProcessingSettingsPopup");
    if (popup) {
        const PostProcessingSettings defaults;

        if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen)) {
            BloomSettings& bloom = settings.bloom;
            const BloomSettings& default_bloom = defaults.bloom;

            changed |= render_resettable_checkbox(
                "Enabled",
                bloom.enabled,
                default_bloom.enabled,
                "Adds a glow around high-luminance pixels, including emissive materials.");

            {
                DisabledScope disabled(!bloom.enabled);
                changed |= render_resettable_float_drag(
                    "Strength",
                    bloom.strength,
                    default_bloom.strength,
                    0.005f,
                    0.0f,
                    1.0f,
                    "%.3f",
                    "How much bloom is added back into the scene.");
                changed |= draw_uint32_drag_property(
                    "Resolution",
                    bloom.resolution,
                    default_bloom.resolution,
                    8,
                    4096,
                    "Minor-axis resolution of the bloom buffer.");
                changed |= draw_uint8_drag_property(
                    "Levels",
                    bloom.levels,
                    default_bloom.levels,
                    1,
                    11,
                    "Number of blur levels used to spread the glow.");
                changed |= render_resettable_combo(
                    "Blend",
                    bloom.blend_mode,
                    default_bloom.blend_mode,
                    std::span<const BloomBlendMode> { kBloomBlendModes },
                    bloom_blend_mode_label,
                    "Chooses whether bloom is added to or interpolated with the scene.");
                changed |= render_resettable_combo(
                    "Quality",
                    bloom.quality,
                    default_bloom.quality,
                    std::span<const BloomQuality> { kBloomQualities },
                    bloom_quality_label,
                    "Controls the filter quality used by the bloom pass.");
                changed |= render_resettable_checkbox(
                    "Threshold",
                    bloom.threshold,
                    default_bloom.threshold,
                    "Only blooms HDR values above Filament's bloom threshold.");
                changed |= render_resettable_float_drag(
                    "Highlight Limit",
                    bloom.highlight,
                    default_bloom.highlight,
                    10.0f,
                    10.0f,
                    1000000.0f,
                    "%.0f",
                    "Caps very bright highlights before bloom.");
            }
        }

        if (ImGui::CollapsingHeader("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
            AmbientOcclusionSettings& ambient_occlusion = settings.ambient_occlusion;
            const AmbientOcclusionSettings& default_ambient_occlusion = defaults.ambient_occlusion;

            changed |= render_resettable_checkbox(
                "Enabled##AmbientOcclusion",
                ambient_occlusion.enabled,
                default_ambient_occlusion.enabled,
                "Adds screen-space occlusion to indirect lighting.");

            {
                DisabledScope disabled(!ambient_occlusion.enabled);
                changed |= render_resettable_combo(
                    "Algorithm",
                    ambient_occlusion.algorithm,
                    default_ambient_occlusion.algorithm,
                    std::span<const AmbientOcclusionAlgorithm> { kAmbientOcclusionAlgorithms },
                    ambient_occlusion_algorithm_label,
                    "Chooses the screen-space occlusion algorithm.");
                changed |= render_resettable_float_drag(
                    "Radius",
                    ambient_occlusion.radius,
                    default_ambient_occlusion.radius,
                    0.01f,
                    0.0f,
                    10.0f,
                    "%.3f",
                    "World-space occlusion radius in meters.");
                changed |= render_resettable_float_drag(
                    "Intensity",
                    ambient_occlusion.intensity,
                    default_ambient_occlusion.intensity,
                    0.01f,
                    0.0f,
                    10.0f,
                    "%.3f",
                    "Strength of the ambient occlusion pass.");
                changed |= render_resettable_float_drag(
                    "Power",
                    ambient_occlusion.power,
                    default_ambient_occlusion.power,
                    0.01f,
                    0.001f,
                    16.0f,
                    "%.3f",
                    "Contrast curve applied to the occlusion term.");
                changed |= render_resettable_float_drag(
                    "Bias",
                    ambient_occlusion.bias,
                    default_ambient_occlusion.bias,
                    0.0001f,
                    0.0f,
                    0.1f,
                    "%.4f",
                    "World-space self-occlusion bias.");
                changed |= render_resettable_float_drag(
                    "Resolution",
                    ambient_occlusion.resolution_scale,
                    default_ambient_occlusion.resolution_scale,
                    0.5f,
                    0.5f,
                    1.0f,
                    "%.1f",
                    "AO buffer scale; Filament accepts half or full resolution.");
                changed |= render_resettable_float_drag(
                    "Bilateral Threshold",
                    ambient_occlusion.bilateral_threshold,
                    default_ambient_occlusion.bilateral_threshold,
                    0.005f,
                    0.0f,
                    1.0f,
                    "%.3f",
                    "Depth threshold used by the bilateral filter.");
                changed |= render_resettable_combo(
                    "Quality##AmbientOcclusion",
                    ambient_occlusion.quality,
                    default_ambient_occlusion.quality,
                    std::span<const AmbientOcclusionQuality> { kAmbientOcclusionQualities },
                    ambient_occlusion_quality_label,
                    "Controls sampling and filtering quality.");
                changed |= render_resettable_combo(
                    "Low Pass",
                    ambient_occlusion.low_pass_filter,
                    default_ambient_occlusion.low_pass_filter,
                    std::span<const AmbientOcclusionQuality> { kAmbientOcclusionQualities },
                    ambient_occlusion_quality_label,
                    "Controls AO smoothness.");
                changed |= render_resettable_combo(
                    "Upsampling",
                    ambient_occlusion.upsampling,
                    default_ambient_occlusion.upsampling,
                    std::span<const AmbientOcclusionQuality> { kAmbientOcclusionQualities },
                    ambient_occlusion_quality_label,
                    "Controls AO buffer upsampling quality.");
                changed |= render_resettable_checkbox(
                    "Bent Normals",
                    ambient_occlusion.bent_normals,
                    default_ambient_occlusion.bent_normals,
                    "Computes bent normals for more accurate specular occlusion.");
                changed |= render_resettable_float_drag(
                    "Min Horizon",
                    ambient_occlusion.min_horizon_angle_radians,
                    default_ambient_occlusion.min_horizon_angle_radians,
                    0.01f,
                    0.0f,
                    1.5708f,
                    "%.3f",
                    "Minimum horizon angle in radians for scalable AO.");

                if (ImGui::TreeNodeEx("SSCT", ImGuiTreeNodeFlags_DefaultOpen)) {
                    AmbientOcclusionSsctSettings& ssct = ambient_occlusion.ssct;
                    const AmbientOcclusionSsctSettings& default_ssct = default_ambient_occlusion.ssct;
                    changed |= render_resettable_checkbox(
                        "Enabled",
                        ssct.enabled,
                        default_ssct.enabled,
                        "Adds screen-space cone-traced ambient shadows from a dominant light direction.");
                    {
                        DisabledScope ssct_disabled(!ssct.enabled);
                        changed |= render_resettable_float_drag(
                            "Light Cone",
                            ssct.light_cone_radians,
                            default_ssct.light_cone_radians,
                            0.01f,
                            0.0f,
                            1.5708f,
                            "%.3f",
                            "Full cone angle in radians.");
                        changed |= render_resettable_float_drag(
                            "Shadow Distance",
                            ssct.shadow_distance,
                            default_ssct.shadow_distance,
                            0.01f,
                            0.0f,
                            100.0f,
                            "%.3f",
                            "Maximum ambient shadow distance.");
                        changed |= render_resettable_float_drag(
                            "Contact Max",
                            ssct.contact_distance_max,
                            default_ssct.contact_distance_max,
                            0.01f,
                            0.0f,
                            100.0f,
                            "%.3f",
                            "Maximum contact distance.");
                        changed |= render_resettable_float_drag(
                            "Intensity",
                            ssct.intensity,
                            default_ssct.intensity,
                            0.01f,
                            0.0f,
                            10.0f,
                            "%.3f",
                            "SSCT shadow strength.");
                        changed |= draw_vec3_drag_property(
                            "Light Direction",
                            ssct.light_direction,
                            default_ssct.light_direction,
                            0.01f,
                            "%.3f",
                            "Dominant light direction for cone tracing.");
                        changed |= render_resettable_float_drag(
                            "Depth Bias",
                            ssct.depth_bias,
                            default_ssct.depth_bias,
                            0.001f,
                            0.0f,
                            1.0f,
                            "%.3f",
                            "World-space depth bias to reduce self-shadowing.");
                        changed |= render_resettable_float_drag(
                            "Slope Bias",
                            ssct.depth_slope_bias,
                            default_ssct.depth_slope_bias,
                            0.001f,
                            0.0f,
                            1.0f,
                            "%.3f",
                            "Depth slope bias to reduce self-shadowing.");
                        changed |= draw_uint8_drag_property(
                            "Samples",
                            ssct.sample_count,
                            default_ssct.sample_count,
                            1,
                            255,
                            "Number of cone-tracing samples.");
                        changed |= draw_uint8_drag_property(
                            "Rays",
                            ssct.ray_count,
                            default_ssct.ray_count,
                            1,
                            255,
                            "Number of cone-tracing rays.");
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx("GTAO", ImGuiTreeNodeFlags_DefaultOpen)) {
                    AmbientOcclusionGtaoSettings& gtao = ambient_occlusion.gtao;
                    const AmbientOcclusionGtaoSettings& default_gtao = default_ambient_occlusion.gtao;
                    changed |= draw_uint8_drag_property(
                        "Slices",
                        gtao.sample_slice_count,
                        default_gtao.sample_slice_count,
                        1,
                        255,
                        "Number of angular slices used by ground-truth AO.");
                    changed |= draw_uint8_drag_property(
                        "Steps",
                        gtao.sample_steps_per_slice,
                        default_gtao.sample_steps_per_slice,
                        1,
                        255,
                        "Number of integration steps per slice.");
                    changed |= render_resettable_float_drag(
                        "Thickness",
                        gtao.thickness_heuristic,
                        default_gtao.thickness_heuristic,
                        0.001f,
                        0.0f,
                        1.0f,
                        "%.3f",
                        "Thickness heuristic used by GTAO without visibility bitmasks.");
                    changed |= render_resettable_checkbox(
                        "Visibility Bitmasks",
                        gtao.use_visibility_bitmasks,
                        default_gtao.use_visibility_bitmasks,
                        "Enables GTAO visibility bitmasks; changing this can trigger shader recompilation.");
                    changed |= render_resettable_float_drag(
                        "Const Thickness",
                        gtao.const_thickness,
                        default_gtao.const_thickness,
                        0.01f,
                        0.0f,
                        100.0f,
                        "%.3f",
                        "Constant screen object thickness in world units.");
                    changed |= render_resettable_checkbox(
                        "Linear Thickness",
                        gtao.linear_thickness,
                        default_gtao.linear_thickness,
                        "Increases GTAO thickness with distance; changing this can trigger shader recompilation.");
                    ImGui::TreePop();
                }
            }
        }

        if (changed) {
            settings = clamp_post_processing_settings(settings);
        }
    }

    return changed;
}

[[nodiscard]] std::optional<ViewportCameraPreset> draw_camera_combo(
    ViewportCameraPreset current,
    const ViewportHostUiStyle& style)
{
    std::optional<ViewportCameraPreset> requested;

    ImGui::SetNextItemWidth(style.camera_combo_width);
    {
        const ComboScope combo("##CameraPreset", viewport_camera_preset_name(current));
        render_item_tooltip("Chooses how this viewport looks at the scene.");
        if (combo) {
            for (const ViewportCameraPreset preset : kCameraPresets) {
                const bool selected = current == preset;
                if (ImGui::Selectable(viewport_camera_preset_name(preset), selected) && !selected) {
                    requested = preset;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
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
        (pane.camera_dropdown_enabled ?
                style.camera_combo_width + style.toolbar_control_gap :
                0.0f);

    const TableScope toolbar_table(
            "ViewportPaneToolbar",
            2,
            ImGuiTableFlags_SizingStretchProp |
                ImGuiTableFlags_NoPadOuterX |
                ImGuiTableFlags_NoPadInnerX);
    if (!toolbar_table) {
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
        if (std::optional<ViewportCameraPreset> preset =
                draw_camera_combo(pane.camera_preset, style)) {
            result.requested_camera_preset = *preset;
        }
        ImGui::SameLine(0.0f, style.toolbar_control_gap);
    }

    if (ImGui::Button(is_maximized ? "Restore" : "Max", ImVec2 { style.maximize_button_width, 0.0f })) {
        result.requested_maximize_toggle = true;
    }
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
    const IdScope pane_scope(pane_id);
    const ChildScope child("ViewportPaneChild", size, true, ImGuiWindowFlags_NoScrollbar);
    if (child) {
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

    return result;
}

} // namespace

ViewportHostPanelResult draw_viewport_host_panel(const ViewportHostPanelInput& input)
{
    const ViewportHostUiStyle style = viewport_host_ui_style();
    set_initial_window_bounds(style.window);

    ViewportHostPanelResult result;
    ViewportLayoutState layout_state = input.layout;
    EnvironmentLightingSettings environment_settings = input.environment;
    PostProcessingSettings post_processing_settings = input.post_processing;
    const std::span<const ViewportPaneInput> panes = input.panes;
    auto capture_result_state = [&result, &layout_state, &environment_settings, &post_processing_settings] {
        result.layout = layout_state;
        result.environment = environment_settings;
        result.post_processing = post_processing_settings;
    };

    const WindowScope window("Viewport");
    if (window) {
        result.layout_changed |= draw_layout_combo(layout_state, style);
        ImGui::SameLine(0.0f, style.toolbar_control_gap);
        result.environment_changed |= draw_environment_popup(environment_settings, style);
        ImGui::SameLine(0.0f, style.toolbar_control_gap);
        result.post_processing_changed |= draw_post_processing_popup(post_processing_settings, style);
        ImGui::Separator();

        const ImVec2 content_origin = ImGui::GetCursorScreenPos();
        const ImVec2 content_size = ImGui::GetContentRegionAvail();
        if (content_size.x < style.minimum_pane_pixels || content_size.y < style.minimum_pane_pixels) {
            capture_result_state();
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

    capture_result_state();
    return result;
}

} // namespace quader
