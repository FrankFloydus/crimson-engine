module;

#include <imgui.h>

#include <array>
#include <cstdint>
#include <span>

module quader.ui.light_panel;

import quader.ui.controls;
import quader.ui.style;

namespace quader {
namespace {

constexpr std::array<DirectionalShadowMode, 3> kShadowModes {
    DirectionalShadowMode::crisp,
    DirectionalShadowMode::soft,
    DirectionalShadowMode::vsm_transparent,
};

constexpr std::array<DirectionalShadowQuality, 4> kShadowQualities {
    DirectionalShadowQuality::low,
    DirectionalShadowQuality::medium,
    DirectionalShadowQuality::high,
    DirectionalShadowQuality::ultra,
};

[[nodiscard]] const char* shadow_mode_label(DirectionalShadowMode mode)
{
    switch (mode) {
    case DirectionalShadowMode::crisp:
        return "Crisp";
    case DirectionalShadowMode::soft:
        return "Soft";
    case DirectionalShadowMode::vsm_transparent:
        return "VSM / Transparent";
    }

    return "Crisp";
}

[[nodiscard]] const char* shadow_quality_label(DirectionalShadowQuality quality)
{
    switch (quality) {
    case DirectionalShadowQuality::low:
        return "Low";
    case DirectionalShadowQuality::medium:
        return "Medium";
    case DirectionalShadowQuality::high:
        return "High";
    case DirectionalShadowQuality::ultra:
        return "Ultra";
    }

    return "High";
}

[[nodiscard]] bool draw_shadow_mode_combo(DirectionalShadowMode& mode, DirectionalShadowMode default_mode)
{
    return render_resettable_combo(
        "Shadow Mode",
        mode,
        default_mode,
        std::span<const DirectionalShadowMode> { kShadowModes },
        shadow_mode_label,
        "Changes the character of the shadow edge. Crisp feels like hard sunlight, Soft gives a broader falloff, and VSM / Transparent is for experiments with translucent-looking shadowing.");
}

[[nodiscard]] bool draw_shadow_quality_combo(
    DirectionalShadowQuality& quality,
    DirectionalShadowQuality default_quality)
{
    return render_resettable_combo(
        "Shadow Quality",
        quality,
        default_quality,
        std::span<const DirectionalShadowQuality> { kShadowQualities },
        shadow_quality_label,
        "Controls how much detail the shadow can hold. Use higher quality for sharper contact and cleaner silhouettes; lower quality is faster but blurrier.");
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

[[nodiscard]] bool draw_shadow_basics(DirectionalShadowSettings& shadow)
{
    const DirectionalShadowSettings defaults;
    bool changed = false;

    changed |= render_resettable_checkbox(
        "Shadows Enabled",
        shadow.enabled,
        defaults.enabled,
        "Turns the light's shadow on or off without changing the light color or brightness.");

    changed |= draw_shadow_mode_combo(shadow.mode, defaults.mode);
    changed |= draw_shadow_quality_combo(shadow.quality, defaults.quality);

    changed |= render_resettable_float_drag(
        "Shadow Distance",
        shadow.distance,
        defaults.distance,
        1.0f,
        0.0f,
        10000.0f,
        "%.1f m",
        "How far from the camera shadows are kept, in meters. Raise it for large outdoor views; lower it to spend detail near the subject.");

    changed |= draw_uint8_drag_property(
        "Cascades",
        shadow.cascades,
        defaults.cascades,
        1,
        4,
        "Splits the shadow range into 1 to 4 bands. More cascades keep distant shadows readable, but each band costs extra work.");

    changed |= render_resettable_checkbox(
        "Stable Shadows",
        shadow.stable,
        defaults.stable,
        "Reduces swimming and shimmer while the camera moves. Use it when motion feels distracting, knowing it can make shadows a bit less crisp.");

    changed |= render_resettable_checkbox(
        "Contact Shadows",
        shadow.contact_shadows,
        defaults.contact_shadows,
        "Adds a small dark touch where objects meet surfaces, helping the cube feel planted instead of floating.");

    changed |= render_resettable_float_drag(
        "Contact Distance",
        shadow.contact_distance,
        defaults.contact_distance,
        0.01f,
        0.0f,
        100.0f,
        "%.2f m",
        "How far contact shadows can reach, in meters. Small values create tight grounding; larger values can look heavier.");

    changed |= draw_uint8_drag_property(
        "Contact Steps",
        shadow.contact_steps,
        defaults.contact_steps,
        1,
        64,
        "How carefully contact shadows are traced. More steps can look cleaner; too many are usually wasted on a subtle effect.");

    return changed;
}

[[nodiscard]] bool draw_advanced_shadows(DirectionalShadowSettings& shadow)
{
    const DirectionalShadowSettings defaults;
    bool changed = false;
    if (ImGui::CollapsingHeader("Advanced Shadows")) {
        changed |= render_resettable_float_drag(
            "Constant Bias",
            shadow.constant_bias,
            defaults.constant_bias,
            0.0001f,
            0.0f,
            1.0f,
            "%.5f",
            "Moves shadows slightly off the surface to hide speckles. Too much makes shadows look detached.");

        changed |= render_resettable_float_drag(
            "Normal Bias",
            shadow.normal_bias,
            defaults.normal_bias,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Pushes shadow tests along the surface shape. Increase it to fight acne; decrease it if shadows float away.");

        changed |= render_resettable_float_drag(
            "Polygon Offset Constant",
            shadow.polygon_offset_constant,
            defaults.polygon_offset_constant,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Fine-tunes depth offset for shadow casters. Useful when flat surfaces show flickering or peppered shadow noise.");

        changed |= render_resettable_float_drag(
            "Polygon Offset Slope",
            shadow.polygon_offset_slope,
            defaults.polygon_offset_slope,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Adds more offset on steep angles. Adjust when angled faces show acne or when edges start to separate.");

        changed |= render_resettable_float_drag(
            "Near Hint",
            shadow.near_hint,
            defaults.near_hint,
            0.1f,
            0.0f,
            10000.0f,
            "%.2f m",
            "Hints where nearby shadow detail should begin, in meters. Raise only if close-up shadow detail is being wasted.");

        changed |= render_resettable_float_drag(
            "Far Hint",
            shadow.far_hint,
            defaults.far_hint,
            0.1f,
            0.0f,
            10000.0f,
            "%.2f m",
            "Hints where useful shadow detail ends, in meters. Lower it when the important action is close to the camera.");

        changed |= render_resettable_checkbox(
            "LiSPSM",
            shadow.lispsm,
            defaults.lispsm,
            "Biases shadow detail toward the visible part of the scene. It can make sunlight shadows sharper, but may interact with very soft blur.");

        changed |= render_resettable_float_drag(
            "VSM Blur Width",
            shadow.vsm_blur_width,
            defaults.vsm_blur_width,
            0.1f,
            0.0f,
            125.0f,
            "%.2f",
            "Softens VSM shadows by blurring them. Keep it low for definition; raise it for a hazier look.");

        changed |= render_resettable_checkbox(
            "VSM ELVSM",
            shadow.vsm_elvsm,
            defaults.vsm_elvsm,
            "Makes VSM shadows resist light leaking better, especially when blur is high.");

        changed |= render_resettable_checkbox(
            "VSM High Precision",
            shadow.vsm_high_precision,
            defaults.vsm_high_precision,
            "Gives VSM more room to represent subtle shadow gradients. Use it when VSM looks washed out or unstable.");

        changed |= render_resettable_float_drag(
            "VSM Light Bleed Reduction",
            shadow.vsm_light_bleed_reduction,
            defaults.vsm_light_bleed_reduction,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Reduces bright leaks in VSM shadows. Higher values make leaks less obvious but can make soft shadows feel dense.");

        changed |= render_resettable_float_drag(
            "Soft Shadow Bulb Radius",
            shadow.soft_shadow_bulb_radius,
            defaults.soft_shadow_bulb_radius,
            0.001f,
            0.0f,
            100.0f,
            "%.3f m",
            "Virtual size of the sun for soft shadows, in meters. Larger values widen the soft edge.");

        changed |= render_resettable_float_drag(
            "Penumbra Scale",
            shadow.penumbra_scale,
            defaults.penumbra_scale,
            0.01f,
            0.001f,
            100.0f,
            "%.3f",
            "Overall strength of the soft shadow edge. Raise it for gentler shadows; lower it for tighter ones.");

        changed |= render_resettable_float_drag(
            "Penumbra Ratio Scale",
            shadow.penumbra_ratio_scale,
            defaults.penumbra_ratio_scale,
            0.01f,
            1.0f,
            100.0f,
            "%.3f",
            "Controls how quickly shadows soften as they move away from the caster. Higher values exaggerate the spread.");
    }
    render_item_tooltip("Fine controls for cleaning up shadow speckles, floating edges, shimmering, leaks, and softness.");

    return changed;
}

} // namespace

bool render_directional_light_panel(DirectionalLightSettings& settings)
{
    const PreviewPanelUiStyle style = preview_panel_ui_style();
    set_initial_window_bounds(style.light_window);

    bool changed = false;
    if (ImGui::Begin("Directional Light")) {
        const DirectionalLightSettings defaults;
        changed |= render_resettable_srgb_color_edit(
            "Color",
            settings.color,
            defaults.color,
            "Tint of the directional light before intensity is applied.");

        changed |= render_resettable_float_drag(
            "Intensity",
            settings.intensity,
            defaults.intensity,
            500.0f,
            0.0f,
            200000.0f,
            "%.0f lx",
            "Light strength in lux. Sunlight is typically tens of thousands of lux.");

        changed |= render_resettable_float_drag(
            "Yaw",
            settings.yaw_degrees,
            defaults.yaw_degrees,
            0.25f,
            -180.0f,
            180.0f,
            "%.1f deg",
            "Horizontal rotation of the directional light in degrees.");

        changed |= render_resettable_float_drag(
            "Pitch",
            settings.pitch_degrees,
            defaults.pitch_degrees,
            0.25f,
            -89.0f,
            89.0f,
            "%.1f deg",
            "Vertical angle of the directional light in degrees.");

        ImGui::Separator();
        changed |= draw_shadow_basics(settings.shadow);
        changed |= draw_advanced_shadows(settings.shadow);

        if (ImGui::Button("Reset Light")) {
            settings = {};
            changed = true;
        }
        render_item_tooltip("Restores the directional light and all shadow controls to their defaults.");
    }

    ImGui::End();
    if (changed) {
        settings = clamp_directional_light_settings(settings);
    }

    return changed;
}

} // namespace quader
