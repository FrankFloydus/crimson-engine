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
    MaterialPanelResult& result)
{
    const TextureAssetReference& texture = material_texture_reference(settings, slot);

    ImGui::PushID(label);
    ImGui::Spacing();
    ImGui::TextUnformatted(label);

    {
        const DisabledScope disabled(picker_busy);
        if (ImGui::Button("Browse")) {
            result.requested_texture_pick = slot;
        }
    }

    ImGui::SameLine();
    {
        const DisabledScope disabled(!texture.has_source());
        if (ImGui::Button("Clear")) {
            result.requested_texture_clear = slot;
        }
    }

    if (texture.has_source()) {
        const std::string_view filename = filename_or_path_text(texture.source_path);
        ImGui::TextUnformatted(filename.data(), filename.data() + filename.size());
    } else {
        ImGui::TextDisabled("No texture selected");
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
        result.settings_changed |= render_srgb_color_edit("Albedo", settings.albedo);
        render_texture_row("Albedo Texture", PbrMaterialTextureSlot::albedo, settings, picker_busy, result);

        result.settings_changed |= ImGui::SliderFloat("Roughness", &settings.roughness, 0.0f, 1.0f);
        render_texture_row("Roughness Texture", PbrMaterialTextureSlot::roughness, settings, picker_busy, result);

        result.settings_changed |= ImGui::SliderFloat("Metallic", &settings.metallic, 0.0f, 1.0f);
        render_texture_row("Metallic Texture", PbrMaterialTextureSlot::metallic, settings, picker_busy, result);

        result.settings_changed |= ImGui::SliderFloat("Reflectance", &settings.reflectance, 0.0f, 1.0f);
        result.settings_changed |= ImGui::SliderFloat("Specular Factor", &settings.specular_factor, 0.0f, 1.0f);
        result.settings_changed |= render_srgb_color_edit("Specular Color Factor", settings.specular_color_factor);
        result.settings_changed |= ImGui::SliderFloat("Clear Coat", &settings.clear_coat, 0.0f, 1.0f);
        result.settings_changed |= ImGui::SliderFloat(
            "Clear Coat Roughness",
            &settings.clear_coat_roughness,
            0.0f,
            1.0f);

        if (ImGui::Button("Reset PBR")) {
            settings = {};
            result.settings_changed = true;
        }
    }

    ImGui::End();

    if (result.settings_changed) {
        settings = clamp_pbr_material_settings(settings);
    }

    return result;
}

} // namespace quader
