module;

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

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

constexpr std::array<PointLightFalloffMode, 2> kPointLightFalloffModes {
    PointLightFalloffMode::linear,
    PointLightFalloffMode::inverse_squared,
};

constexpr std::array<PbrMaterialBlendMode, 7> kMaterialBlendModes {
    PbrMaterialBlendMode::opaque,
    PbrMaterialBlendMode::transparent,
    PbrMaterialBlendMode::fade,
    PbrMaterialBlendMode::masked,
    PbrMaterialBlendMode::add,
    PbrMaterialBlendMode::multiply,
    PbrMaterialBlendMode::screen,
};

constexpr std::array<PbrMaterialTransparencyMode, 3> kMaterialTransparencyModes {
    PbrMaterialTransparencyMode::default_sorting,
    PbrMaterialTransparencyMode::two_passes_one_side,
    PbrMaterialTransparencyMode::two_passes_two_sides,
};

constexpr std::array<PbrMaterialCullingMode, 4> kMaterialCullingModes {
    PbrMaterialCullingMode::none,
    PbrMaterialCullingMode::front,
    PbrMaterialCullingMode::back,
    PbrMaterialCullingMode::front_and_back,
};

constexpr std::array<PbrMaterialRefractionMode, 3> kMaterialRefractionModes {
    PbrMaterialRefractionMode::none,
    PbrMaterialRefractionMode::cubemap,
    PbrMaterialRefractionMode::screen_space,
};

constexpr std::array<PbrMaterialRefractionType, 2> kMaterialRefractionTypes {
    PbrMaterialRefractionType::solid,
    PbrMaterialRefractionType::thin,
};

constexpr std::array<PbrMaterialAbsorptionMode, 2> kMaterialAbsorptionModes {
    PbrMaterialAbsorptionMode::coefficients,
    PbrMaterialAbsorptionMode::transmittance_color,
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

[[nodiscard]] const char* material_blend_mode_label(PbrMaterialBlendMode mode)
{
    switch (mode) {
    case PbrMaterialBlendMode::opaque:
        return "Opaque";
    case PbrMaterialBlendMode::transparent:
        return "Transparent";
    case PbrMaterialBlendMode::fade:
        return "Fade";
    case PbrMaterialBlendMode::masked:
        return "Masked";
    case PbrMaterialBlendMode::add:
        return "Add";
    case PbrMaterialBlendMode::multiply:
        return "Multiply";
    case PbrMaterialBlendMode::screen:
        return "Screen";
    }

    return "Opaque";
}

[[nodiscard]] const char* material_transparency_mode_label(PbrMaterialTransparencyMode mode)
{
    switch (mode) {
    case PbrMaterialTransparencyMode::default_sorting:
        return "Default";
    case PbrMaterialTransparencyMode::two_passes_one_side:
        return "Two Passes One Side";
    case PbrMaterialTransparencyMode::two_passes_two_sides:
        return "Two Passes Two Sides";
    }

    return "Default";
}

[[nodiscard]] const char* material_culling_mode_label(PbrMaterialCullingMode mode)
{
    switch (mode) {
    case PbrMaterialCullingMode::none:
        return "None";
    case PbrMaterialCullingMode::front:
        return "Front";
    case PbrMaterialCullingMode::back:
        return "Back";
    case PbrMaterialCullingMode::front_and_back:
        return "Front And Back";
    }

    return "Back";
}

[[nodiscard]] const char* material_refraction_mode_label(PbrMaterialRefractionMode mode)
{
    switch (mode) {
    case PbrMaterialRefractionMode::none:
        return "None";
    case PbrMaterialRefractionMode::cubemap:
        return "Cubemap";
    case PbrMaterialRefractionMode::screen_space:
        return "Screen Space";
    }

    return "None";
}

[[nodiscard]] const char* material_refraction_type_label(PbrMaterialRefractionType type)
{
    switch (type) {
    case PbrMaterialRefractionType::solid:
        return "Solid";
    case PbrMaterialRefractionType::thin:
        return "Thin";
    }

    return "Solid";
}

[[nodiscard]] const char* material_absorption_mode_label(PbrMaterialAbsorptionMode mode)
{
    switch (mode) {
    case PbrMaterialAbsorptionMode::coefficients:
        return "Coefficients";
    case PbrMaterialAbsorptionMode::transmittance_color:
        return "Transmittance Color";
    }

    return "Coefficients";
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

    const IdScope texture_row_id(label);
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

[[nodiscard]] bool draw_transparent_material_controls(
    PbrMaterialSettings& settings,
    const PbrMaterialSettings& defaults,
    bool picker_busy,
    MaterialComponentEditorResult& result)
{
    bool changed = false;
    if (!ImGui::CollapsingHeader("Transparent")) {
        return false;
    }

    changed |= render_resettable_combo(
        "Blend Mode",
        settings.blend_mode,
        defaults.blend_mode,
        std::span<const PbrMaterialBlendMode> { kMaterialBlendModes },
        material_blend_mode_label,
        "Selects how this material combines with pixels behind it.");
    changed |= render_resettable_float_drag(
        "Opacity",
        settings.opacity,
        defaults.opacity,
        0.005f,
        0.0f,
        1.0f,
        "%.3f",
        "Alpha value multiplied into the material base color.");
    draw_texture_row(
        "Opacity Texture",
        PbrMaterialTextureSlot::opacity,
        settings,
        picker_busy,
        "Grayscale texture that varies opacity across the surface.",
        result);
    changed |= render_resettable_float_drag(
        "Mask Threshold",
        settings.mask_threshold,
        defaults.mask_threshold,
        0.005f,
        0.0f,
        1.0f,
        "%.3f",
        "Alpha cutoff used by masked materials.");
    changed |= render_resettable_combo(
        "Transparency Pass",
        settings.transparency_mode,
        defaults.transparency_mode,
        std::span<const PbrMaterialTransparencyMode> { kMaterialTransparencyModes },
        material_transparency_mode_label,
        "Chooses the transparent rendering pass strategy.");
    ImGui::Separator();
    changed |= render_resettable_checkbox(
        "Transparent Shadows",
        settings.transparent_shadow,
        defaults.transparent_shadow,
        "Lets material alpha affect shadow opacity.");
    changed |= render_resettable_checkbox(
        "Double Sided",
        settings.double_sided,
        defaults.double_sided,
        "Renders and lights both sides of the material.");
    changed |= render_resettable_combo(
        "Culling",
        settings.culling,
        defaults.culling,
        std::span<const PbrMaterialCullingMode> { kMaterialCullingModes },
        material_culling_mode_label,
        "Controls which face orientation is skipped by rasterization.");
    changed |= render_resettable_checkbox(
        "Depth Write",
        settings.depth_write,
        defaults.depth_write,
        "Allows the material to write depth.");
    changed |= render_resettable_checkbox(
        "Depth Culling",
        settings.depth_culling,
        defaults.depth_culling,
        "Allows the material to depth-test against earlier surfaces.");
    changed |= render_resettable_checkbox(
        "Color Write",
        settings.color_write,
        defaults.color_write,
        "Allows the material to write color.");
    changed |= render_resettable_checkbox(
        "Alpha To Coverage",
        settings.alpha_to_coverage,
        defaults.alpha_to_coverage,
        "Uses alpha to coverage for masked edges when MSAA is available.");

    ImGui::Separator();
    changed |= render_resettable_combo(
        "Refraction Mode",
        settings.refraction_mode,
        defaults.refraction_mode,
        std::span<const PbrMaterialRefractionMode> { kMaterialRefractionModes },
        material_refraction_mode_label,
        "Chooses the source used for refracted light.");
    changed |= render_resettable_combo(
        "Refraction Type",
        settings.refraction_type,
        defaults.refraction_type,
        std::span<const PbrMaterialRefractionType> { kMaterialRefractionTypes },
        material_refraction_type_label,
        "Chooses whether refraction behaves like a solid volume or a thin surface.");
    changed |= render_resettable_float_drag(
        "Transmission",
        settings.transmission,
        defaults.transmission,
        0.005f,
        0.0f,
        1.0f,
        "%.3f",
        "How much diffuse light passes through the material.");
    changed |= render_resettable_float_drag(
        "IOR",
        settings.ior,
        defaults.ior,
        0.005f,
        1.0f,
        4.0f,
        "%.3f",
        "Index of refraction.");
    changed |= render_resettable_combo(
        "Absorption Mode",
        settings.absorption_mode,
        defaults.absorption_mode,
        std::span<const PbrMaterialAbsorptionMode> { kMaterialAbsorptionModes },
        material_absorption_mode_label,
        "Chooses direct absorption coefficients or artist-facing transmittance color.");
    changed |= draw_vec3_drag(
        "Absorption",
        settings.absorption,
        defaults.absorption,
        0.005f,
        "%.3f",
        "Beer-Lambert absorption coefficients.");
    changed |= render_resettable_srgb_color_edit(
        "Transmittance Color",
        settings.transmittance_color,
        defaults.transmittance_color,
        "Color white light becomes after traveling the transmittance distance.");
    changed |= render_resettable_float_drag(
        "Transmittance Distance",
        settings.transmittance_distance,
        defaults.transmittance_distance,
        0.01f,
        0.001f,
        1000.0f,
        "%.3f m",
        "Distance used to derive absorption from transmittance color.");
    changed |= render_resettable_float_drag(
        "Thickness",
        settings.thickness,
        defaults.thickness,
        0.005f,
        0.0f,
        1000.0f,
        "%.3f m",
        "Approximate optical depth for solid refractive objects.");
    changed |= render_resettable_float_drag(
        "Micro Thickness",
        settings.micro_thickness,
        defaults.micro_thickness,
        0.0001f,
        0.0f,
        10.0f,
        "%.4f m",
        "Approximate optical depth for thin refractive objects.");
    changed |= render_resettable_float_drag(
        "Dispersion",
        settings.dispersion,
        defaults.dispersion,
        0.001f,
        0.0f,
        1.0f,
        "%.3f",
        "Separates transmitted color by wavelength.");

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

        result.settings_changed |= draw_transparent_material_controls(settings, defaults, picker_busy, result);

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
            5000.0f,
            0.0f,
            100000000.0f,
            "%.0f lm",
            "Point light luminous power in lumens. High values are expected under the editor's outdoor-style exposure.");
        result.settings_changed |= render_resettable_float_drag(
            "Range",
            light.settings.range,
            defaults.settings.range,
            0.05f,
            0.0f,
            1000.0f,
            "%.2f m",
            "Maximum point light influence distance passed to Filament falloff.");
        result.settings_changed |= render_resettable_combo(
            "Falloff",
            light.settings.falloff_mode,
            defaults.settings.falloff_mode,
            std::span<const PointLightFalloffMode> { kPointLightFalloffModes },
            point_light_falloff_mode_name,
            "Linear keeps the authored radius visually useful for editor lighting. Inverse Squared uses Filament's physically based point-light attenuation.");
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
