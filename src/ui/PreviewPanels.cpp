module;

#include <imgui.h>

module quader.ui.preview_panels;

namespace quader {
namespace {

bool render_srgb_color_edit(const char* label, LinearColor& linear_color)
{
    SrgbColor srgb = linear_to_srgb(linear_color);
    float values[3] = { srgb.r, srgb.g, srgb.b };
    if (!ImGui::ColorEdit3(label, values, ImGuiColorEditFlags_Float)) {
        return false;
    }

    linear_color = srgb_to_linear(SrgbColor { values[0], values[1], values[2] });
    return true;
}

} // namespace

void render_debug_preview_panel(ImTextureID texture_id, RenderExtent extent)
{
    ImGui::SetNextWindowPos(ImVec2(32.0f, 56.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(820.0f, 560.0f), ImGuiCond_Always);

    if (ImGui::Begin("Filament Cube Preview")) {
        if (texture_id != ImTextureID {} && extent.is_valid()) {
            const float available_width = ImGui::GetContentRegionAvail().x;
            const float aspect = static_cast<float>(extent.height) / static_cast<float>(extent.width);
            const ImVec2 size(available_width, available_width * aspect);
            ImGui::Image(texture_id, size);
        } else {
            ImGui::TextUnformatted("Waiting for Filament preview pixels.");
        }
    }

    ImGui::End();
}

bool render_material_settings_panel(PbrMaterialSettings& settings)
{
    ImGui::SetNextWindowPos(ImVec2(888.0f, 56.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 390.0f), ImGuiCond_Always);

    bool changed = false;
    if (ImGui::Begin("Material Settings")) {
        changed |= render_srgb_color_edit("Albedo", settings.albedo);
        changed |= ImGui::SliderFloat("Roughness", &settings.roughness, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Metallic", &settings.metallic, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Reflectance", &settings.reflectance, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Specular Factor", &settings.specular_factor, 0.0f, 1.0f);
        changed |= render_srgb_color_edit("Specular Color Factor", settings.specular_color_factor);
        changed |= ImGui::SliderFloat("Clear Coat", &settings.clear_coat, 0.0f, 1.0f);
        changed |= ImGui::SliderFloat("Clear Coat Roughness", &settings.clear_coat_roughness, 0.0f, 1.0f);

        if (ImGui::Button("Reset Red PBR")) {
            settings = {};
            changed = true;
        }
    }

    ImGui::End();

    if (changed) {
        settings = clamp_pbr_material_settings(settings);
    }

    return changed;
}

bool render_directional_light_panel(DirectionalLightSettings& settings)
{
    ImGui::SetNextWindowPos(ImVec2(888.0f, 472.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(340.0f, 260.0f), ImGuiCond_Always);

    bool changed = false;
    if (ImGui::Begin("Directional Light")) {
        changed |= render_srgb_color_edit("Color", settings.color);
        changed |= ImGui::DragFloat("Intensity", &settings.intensity, 500.0f, 0.0f, 200000.0f, "%.0f lx");
        changed |= ImGui::SliderFloat("Yaw", &settings.yaw_degrees, -180.0f, 180.0f);
        changed |= ImGui::SliderFloat("Pitch", &settings.pitch_degrees, -89.0f, 89.0f);

        if (ImGui::Button("Reset Light")) {
            settings = {};
            changed = true;
        }
    }

    ImGui::End();
    if (changed) {
        settings = clamp_directional_light_settings(settings);
    }

    return changed;
}

} // namespace quader
