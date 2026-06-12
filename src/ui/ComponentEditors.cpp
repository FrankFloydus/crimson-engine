module;

#include <imgui.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

module quader.ui.component_editors;

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

[[nodiscard]] std::string_view filename_or_path_text(std::string_view path)
{
    const size_t separator = path.find_last_of("/\\");
    if (separator != std::string_view::npos && separator + 1 < path.size()) {
        return path.substr(separator + 1);
    }

    return path;
}

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

[[nodiscard]] bool draw_vec3_drag(
    const char* label,
    Vec3& value,
    Vec3 default_value,
    float speed,
    const char* format,
    const char* tooltip)
{
    float values[3] = { value.x, value.y, value.z };
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

void draw_texture_row(
    const char* label,
    PbrMaterialTextureSlot slot,
    const PbrMaterialSettings& settings,
    bool picker_busy,
    const char* row_tooltip,
    MaterialComponentEditorResult& result)
{
    const TextureAssetReference& texture = material_texture_reference(settings, slot);

    ImGui::PushID(label);
    ImGui::Spacing();
    ImGui::TextUnformatted(label);
    render_item_tooltip(row_tooltip);

    {
        const DisabledScope disabled(picker_busy);
        if (ImGui::Button("Browse")) {
            result.requested_texture_pick = slot;
        }
        render_item_tooltip(
            picker_busy ? "Texture picker is already open." : "Choose an image file for this material texture slot.");
    }

    ImGui::SameLine();
    if (texture.has_source()) {
        const std::string_view filename = filename_or_path_text(texture.source_path);
        ImGui::SameLine();
        ImGui::TextUnformatted(filename.data(), filename.data() + filename.size());
        render_item_tooltip("Currently assigned texture source.");
        if (render_changed_reset_control(label, true, "Reset this texture slot to use no texture.")) {
            result.requested_texture_clear = slot;
        }
    } else {
        ImGui::TextDisabled("No texture selected");
        render_item_tooltip("This material slot will use the scalar or color property value only.");
    }

    ImGui::PopID();
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
        "How far from the camera shadows are kept, in meters.");
    changed |= draw_uint8_drag_property(
        "Cascades",
        shadow.cascades,
        defaults.cascades,
        1,
        4,
        "Splits the shadow range into 1 to 4 bands.");
    changed |= render_resettable_checkbox(
        "Stable Shadows",
        shadow.stable,
        defaults.stable,
        "Reduces swimming and shimmer while the camera moves.");
    changed |= render_resettable_checkbox(
        "Contact Shadows",
        shadow.contact_shadows,
        defaults.contact_shadows,
        "Adds a small dark touch where objects meet surfaces.");
    changed |= render_resettable_float_drag(
        "Contact Distance",
        shadow.contact_distance,
        defaults.contact_distance,
        0.01f,
        0.0f,
        100.0f,
        "%.2f m",
        "How far contact shadows can reach, in meters.");
    changed |= draw_uint8_drag_property(
        "Contact Steps",
        shadow.contact_steps,
        defaults.contact_steps,
        1,
        64,
        "How carefully contact shadows are traced.");

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
            "Moves shadows slightly off the surface to hide speckles.");
        changed |= render_resettable_float_drag(
            "Normal Bias",
            shadow.normal_bias,
            defaults.normal_bias,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Pushes shadow tests along the surface shape.");
        changed |= render_resettable_float_drag(
            "Polygon Offset Constant",
            shadow.polygon_offset_constant,
            defaults.polygon_offset_constant,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Fine-tunes depth offset for shadow casters.");
        changed |= render_resettable_float_drag(
            "Polygon Offset Slope",
            shadow.polygon_offset_slope,
            defaults.polygon_offset_slope,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Adds more offset on steep angles.");
        changed |= render_resettable_float_drag(
            "Near Hint",
            shadow.near_hint,
            defaults.near_hint,
            0.1f,
            0.0f,
            10000.0f,
            "%.2f m",
            "Hints where nearby shadow detail should begin, in meters.");
        changed |= render_resettable_float_drag(
            "Far Hint",
            shadow.far_hint,
            defaults.far_hint,
            0.1f,
            0.0f,
            10000.0f,
            "%.2f m",
            "Hints where useful shadow detail ends, in meters.");
        changed |= render_resettable_checkbox(
            "LiSPSM",
            shadow.lispsm,
            defaults.lispsm,
            "Biases shadow detail toward the visible part of the scene.");
        changed |= render_resettable_float_drag(
            "VSM Blur Width",
            shadow.vsm_blur_width,
            defaults.vsm_blur_width,
            0.1f,
            0.0f,
            125.0f,
            "%.2f",
            "Softens VSM shadows by blurring them.");
        changed |= render_resettable_checkbox(
            "VSM ELVSM",
            shadow.vsm_elvsm,
            defaults.vsm_elvsm,
            "Makes VSM shadows resist light leaking better.");
        changed |= render_resettable_checkbox(
            "VSM High Precision",
            shadow.vsm_high_precision,
            defaults.vsm_high_precision,
            "Gives VSM more room to represent subtle shadow gradients.");
        changed |= render_resettable_float_drag(
            "VSM Light Bleed Reduction",
            shadow.vsm_light_bleed_reduction,
            defaults.vsm_light_bleed_reduction,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Reduces bright leaks in VSM shadows.");
        changed |= render_resettable_float_drag(
            "Soft Shadow Bulb Radius",
            shadow.soft_shadow_bulb_radius,
            defaults.soft_shadow_bulb_radius,
            0.001f,
            0.0f,
            100.0f,
            "%.3f m",
            "Virtual size of the sun for soft shadows, in meters.");
        changed |= render_resettable_float_drag(
            "Penumbra Scale",
            shadow.penumbra_scale,
            defaults.penumbra_scale,
            0.01f,
            0.001f,
            100.0f,
            "%.3f",
            "Overall strength of the soft shadow edge.");
        changed |= render_resettable_float_drag(
            "Penumbra Ratio Scale",
            shadow.penumbra_ratio_scale,
            defaults.penumbra_ratio_scale,
            0.01f,
            1.0f,
            100.0f,
            "%.3f",
            "Controls how quickly shadows soften as they move away from the caster.");
    }
    render_item_tooltip("Fine controls for cleaning up shadow speckles, floating edges, shimmering, leaks, and softness.");

    return changed;
}

} // namespace

TransformComponentEditorResult draw_transform_component(SceneTransform& transform)
{
    const InspectorUiStyle style = inspector_ui_style();
    const SceneTransform defaults;

    TransformComponentEditorResult result;
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        result.transform_changed |= draw_vec3_drag(
            "Position",
            transform.position,
            defaults.position,
            style.position_drag_speed,
            "%.3f m",
            "Entity position in scene units.");
        Vec3 rotation_degrees = transform.rotation_degrees();
        const bool rotation_changed = draw_vec3_drag(
            "Rotation",
            rotation_degrees,
            defaults.rotation_degrees(),
            style.rotation_drag_speed,
            "%.2f deg",
            "Entity Euler rotation in degrees.");
        if (rotation_changed) {
            transform.set_rotation_degrees(rotation_degrees);
        }
        result.transform_changed |= rotation_changed;
        result.transform_changed |= draw_vec3_drag(
            "Scale",
            transform.scale,
            defaults.scale,
            style.scale_drag_speed,
            "%.3f",
            "Entity local scale.");
    }

    return result;
}

MaterialComponentEditorResult draw_material_component(PbrMaterialSettings& settings, bool picker_busy)
{
    MaterialComponentEditorResult result;
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        const PbrMaterialSettings defaults;
        result.settings_changed |= render_resettable_srgb_color_edit(
            "Albedo",
            settings.albedo,
            defaults.albedo,
            "Base surface color, authored in sRGB and converted to linear for rendering.");
        draw_texture_row(
            "Albedo Texture",
            PbrMaterialTextureSlot::albedo,
            settings,
            picker_busy,
            "Texture multiplied with the albedo color to define the visible base color.",
            result);

        result.settings_changed |= render_resettable_float_drag(
            "Roughness",
            settings.roughness,
            defaults.roughness,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Controls microsurface scattering.");
        draw_texture_row(
            "Roughness Texture",
            PbrMaterialTextureSlot::roughness,
            settings,
            picker_busy,
            "Grayscale texture that varies roughness across the surface.",
            result);

        result.settings_changed |= render_resettable_float_drag(
            "Metallic",
            settings.metallic,
            defaults.metallic,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Blends between dielectric and metallic shading.");
        draw_texture_row(
            "Metallic Texture",
            PbrMaterialTextureSlot::metallic,
            settings,
            picker_busy,
            "Grayscale texture that varies metallic response across the surface.",
            result);

        result.settings_changed |= render_resettable_float_drag(
            "Reflectance",
            settings.reflectance,
            defaults.reflectance,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Dielectric reflectance at normal incidence.");
        result.settings_changed |= render_resettable_float_drag(
            "Specular Factor",
            settings.specular_factor,
            defaults.specular_factor,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Overall multiplier for the specular lobe.");
        result.settings_changed |= render_resettable_srgb_color_edit(
            "Specular Color Factor",
            settings.specular_color_factor,
            defaults.specular_color_factor,
            "Tint applied to specular reflection before rendering.");
        result.settings_changed |= render_resettable_float_drag(
            "Clear Coat",
            settings.clear_coat,
            defaults.clear_coat,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Strength of an additional glossy coating layer.");
        result.settings_changed |= render_resettable_float_drag(
            "Clear Coat Roughness",
            settings.clear_coat_roughness,
            defaults.clear_coat_roughness,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Roughness of the clear-coat layer.");

        if (ImGui::Button("Reset PBR")) {
            settings = {};
            result.settings_changed = true;
        }
        render_item_tooltip("Restores all material scalar, color, and texture settings to their defaults.");
    }

    if (result.settings_changed) {
        settings = clamp_pbr_material_settings(settings);
    }

    return result;
}

DirectionalLightComponentEditorResult draw_directional_light_component(DirectionalLightSettings& settings)
{
    DirectionalLightComponentEditorResult result;
    if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        const DirectionalLightSettings defaults;
        result.settings_changed |= render_resettable_srgb_color_edit(
            "Color",
            settings.color,
            defaults.color,
            "Tint of the directional light before intensity is applied.");
        result.settings_changed |= render_resettable_float_drag(
            "Intensity",
            settings.intensity,
            defaults.intensity,
            500.0f,
            0.0f,
            200000.0f,
            "%.0f lx",
            "Light strength in lux.");

        ImGui::Separator();
        result.settings_changed |= draw_shadow_basics(settings.shadow);
        result.settings_changed |= draw_advanced_shadows(settings.shadow);

        if (ImGui::Button("Reset Light")) {
            settings = {};
            result.settings_changed = true;
        }
        render_item_tooltip("Restores the directional light and all shadow controls to their defaults.");
    }

    if (result.settings_changed) {
        settings = clamp_directional_light_settings(settings);
    }

    return result;
}

PointLightComponentEditorResult draw_point_light_component(PointLightComponent& light)
{
    PointLightComponentEditorResult result;
    if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        const PointLightComponent defaults;
        result.settings_changed |= render_resettable_srgb_color_edit(
            "Color",
            light.settings.color,
            defaults.settings.color,
            "Tint of the point light before intensity is applied.");
        result.settings_changed |= render_resettable_float_drag(
            "Intensity",
            light.settings.intensity,
            defaults.settings.intensity,
            100.0f,
            0.0f,
            200000.0f,
            "%.0f lm",
            "Point light strength.");
        result.settings_changed |= render_resettable_float_drag(
            "Range",
            light.settings.range,
            defaults.settings.range,
            0.05f,
            0.0f,
            10000.0f,
            "%.2f m",
            "Maximum point light influence distance.");
        result.settings_changed |= render_resettable_float_drag(
            "Falloff",
            light.settings.falloff,
            defaults.settings.falloff,
            0.01f,
            0.001f,
            10.0f,
            "%.3f",
            "Multiplier applied to the point light influence radius before it is passed to Filament falloff.");
        result.settings_changed |= render_resettable_checkbox(
            "Shadows Enabled",
            light.settings.shadow.enabled,
            defaults.settings.shadow.enabled,
            "Allows this point light to cast shadows.");
        result.settings_changed |= draw_shadow_quality_combo(
            light.settings.shadow.quality,
            defaults.settings.shadow.quality);
        result.settings_changed |= render_resettable_float_drag(
            "Constant Bias",
            light.settings.shadow.constant_bias,
            defaults.settings.shadow.constant_bias,
            0.0001f,
            0.0f,
            1.0f,
            "%.5f",
            "Moves shadows slightly off the surface to hide speckles.");
        result.settings_changed |= render_resettable_float_drag(
            "Normal Bias",
            light.settings.shadow.normal_bias,
            defaults.settings.shadow.normal_bias,
            0.01f,
            0.0f,
            100.0f,
            "%.3f",
            "Pushes shadow tests along the surface normal.");

        if (ImGui::Button("Reset Point Light")) {
            light = {};
            result.settings_changed = true;
        }
        render_item_tooltip("Restores the point light controls to their defaults.");
    }

    if (result.settings_changed) {
        light.settings = clamp_point_light_settings(light.settings);
    }

    return result;
}

} // namespace quader
