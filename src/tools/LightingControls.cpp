#include "LightingControls.h"
#include "crimson-renderer/RenderTypes.h"

#include <imgui.h>
#include <array>
#include <limits>

namespace crimson {
namespace {
void tooltip(const char* text) {
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",text);
}
} // namespace

void drawAmbientLightControls(AmbientLightSettings& value,
    const std::function<void(AmbientLightControl,bool)>& edit) {
    ImGui::PushID("Ambient Light");
    int source=static_cast<int>(value.source);
    const char* sources[]{"Disabled","Color","Sky"};
    const bool sourceChanged=ImGui::Combo("Source",&source,sources,3);
    if(sourceChanged)value.source=static_cast<AmbientLightSource>(source);
    edit(AmbientLightControl::Source,sourceChanged);
    tooltip("Sky blends the selected HDRI or procedural sky with the ambient color. Color uses a uniform ambient color. Reflections are independent.");
    if(value.source!=AmbientLightSource::Disabled) {
        edit(AmbientLightControl::Color,ImGui::ColorEdit3("Color",value.color.data(),ImGuiColorEditFlags_Float));
        if(value.source!=AmbientLightSource::Color) {
            edit(AmbientLightControl::SkyContribution,
                ImGui::SliderFloat("Sky Contribution",&value.skyContribution,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            tooltip("Blend from the chosen ambient color (0) to sky diffuse lighting (1).");
        }
        edit(AmbientLightControl::Energy,
            ImGui::SliderFloat("Energy",&value.energy,0,16,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        tooltip("Energy of the custom ambient color. At Sky Contribution 1, use environment Intensity to change sky lighting.");
    }
    ImGui::PopID();
}

bool drawLightingMaskControl(const char* label,std::uint32_t& mask) {
    ImGui::PushID(label);ImGui::BeginGroup();
    ImGui::TextUnformatted(label);
    tooltip("Character includes models under a Visual Root. Other models belong to World.");
    bool world=(mask&lighting_scope::World)!=0,character=(mask&lighting_scope::Character)!=0;
    bool changed=ImGui::Checkbox("World",&world);
    ImGui::SameLine();changed|=ImGui::Checkbox("Character",&character);
    if(changed)mask=(world?lighting_scope::World:0u)|(character?lighting_scope::Character:0u);
    ImGui::EndGroup();ImGui::PopID();return changed;
}

bool drawIndirectEnergyControl(float& energy) {
    const bool changed=ImGui::DragFloat("GI Energy",&energy,.05f,0,16,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    tooltip("Diffuse light contributed to SSGI. 0 disables bounce from this light; 1 preserves its normal energy.");
    return changed;
}
} // namespace crimson
