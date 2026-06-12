module;

#include <imgui.h>

#include <string>
#include <string_view>

module quader.ui.material_panel;

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

std::string filename_or_path_text(std::string_view path)
{
    const size_t separator = path.find_last_of("/\\");
    if (separator != std::string_view::npos && separator + 1 < path.size()) {
        return std::string(path.substr(separator + 1));
    }

    return std::string(path);
}

class DisabledScope final {
public:
    explicit DisabledScope(bool disabled)
        : disabled_(disabled)
    {
        if (disabled_) {
            ImGui::BeginDisabled();
        }
    }

    DisabledScope(const DisabledScope&) = delete;
    DisabledScope& operator=(const DisabledScope&) = delete;

    ~DisabledScope()
    {
        if (disabled_) {
            ImGui::EndDisabled();
        }
    }

private:
    bool disabled_ = false;
};

bool panel_mouse_input_started()
{
    return ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
        ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
        ImGui::GetIO().MouseWheel != 0.0f;
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
        const std::string filename = filename_or_path_text(texture.source_path);
        ImGui::TextUnformatted(filename.c_str());
    } else {
        ImGui::TextDisabled("No texture selected");
    }

    ImGui::PopID();
}

} // namespace

MaterialPanelResult render_material_settings_panel(PbrMaterialSettings& settings, bool picker_busy)
{
    ImGui::SetNextWindowPos(ImVec2(888.0f, 56.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(380.0f, 390.0f), ImGuiCond_FirstUseEver);

    MaterialPanelResult result;
    if (ImGui::Begin("Mesh Material")) {
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && panel_mouse_input_started()) {
            ImGui::SetWindowFocus();
        }

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

        if (ImGui::Button("Reset Red PBR")) {
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
