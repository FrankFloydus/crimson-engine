module;

#include <imgui.h>

#include <string_view>

module quader.ui.material_panel;

import quader.ui.controls;
import quader.ui.style;

namespace quader {
namespace {

std::string_view filename_or_path_text(std::string_view path)
{
    const size_t separator = path.find_last_of("/\\");
    if (separator != std::string_view::npos && separator + 1 < path.size()) {
        return path.substr(separator + 1);
    }

    return path;
}

void render_texture_row(
    const char* label,
    PbrMaterialTextureSlot slot,
    const PbrMaterialSettings& settings,
    bool picker_busy,
    const char* row_tooltip,
    MaterialPanelResult& result)
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

} // namespace

MaterialPanelResult render_material_settings_panel(PbrMaterialSettings& settings, bool picker_busy)
{
    const PreviewPanelUiStyle style = preview_panel_ui_style();
    set_initial_window_bounds(style.material_window);

    MaterialPanelResult result;
    if (ImGui::Begin("Mesh Material")) {
        const PbrMaterialSettings defaults;
        result.settings_changed |= render_resettable_srgb_color_edit(
            "Albedo",
            settings.albedo,
            defaults.albedo,
            "Base surface color, authored in sRGB and converted to linear for rendering.");
        render_texture_row(
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
            "Controls microsurface scattering. Lower values are glossier; higher values are broader and matte.");
        render_texture_row(
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
            "Blends between dielectric and metallic shading. Metals use albedo as reflected color.");
        render_texture_row(
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
            "Dielectric reflectance at normal incidence. Use this for non-metal specular strength.");
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
            "Strength of an additional glossy coating layer above the base material.");
        result.settings_changed |= render_resettable_float_drag(
            "Clear Coat Roughness",
            settings.clear_coat_roughness,
            defaults.clear_coat_roughness,
            0.005f,
            0.0f,
            1.0f,
            "%.3f",
            "Roughness of the clear-coat layer. Lower values produce sharper coated highlights.");

        if (ImGui::Button("Reset PBR")) {
            settings = {};
            result.settings_changed = true;
        }
        render_item_tooltip("Restores all material scalar, color, and texture settings to their defaults.");
    }

    ImGui::End();

    if (result.settings_changed) {
        settings = clamp_pbr_material_settings(settings);
    }

    return result;
}

} // namespace quader
