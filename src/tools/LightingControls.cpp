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
    tooltip("Character is the Player Character's subtree. Other models belong to World.");
    bool world=(mask&lighting_scope::World)!=0,character=(mask&lighting_scope::Character)!=0;
    bool changed=ImGui::Checkbox("World",&world);
    ImGui::SameLine();changed|=ImGui::Checkbox("Character",&character);
    if(changed)mask=(world?lighting_scope::World:0u)|(character?lighting_scope::Character:0u);
    ImGui::EndGroup();ImGui::PopID();return changed;
}

void drawAmbientVisibilityControls(AmbientVisibilitySettings& value,
    const std::function<void(AmbientVisibilityControl,bool)>& edit) {
    using Control=AmbientVisibilityControl;
    edit(Control::Enabled,ImGui::Checkbox("Enabled##ambient-darkness",&value.enabled));
    tooltip("Darken direct and environment lighting outside the player range and illuminated scene areas.");
    ImGui::BeginDisabled(!value.enabled);
    edit(Control::DarknessFloor,ImGui::SliderFloat("Darkness Floor",&value.darknessFloor,0,1,"%.3f"));
    tooltip("Remaining direct and environment lighting outside revealed areas. 0 is fully dark; 1 keeps the original light.");
    edit(Control::Transition,ImGui::SliderFloat("Softness",&value.transition,.001f,1,"%.3f"));
    tooltip("Scene-light energy transition from darkness. Character Darkness Reveal uses its own Spread profile.");
    if(ImGui::TreeNode("Advanced##ambient-darkness")) {
        edit(Control::Strength,ImGui::SliderFloat("Strength",&value.strength,0,1,"%.3f"));
        edit(Control::Threshold,ImGui::DragFloat("Threshold",&value.threshold,.005f,0,10000,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        tooltip("Scene-light energy required to reveal global lighting. The player's distance range is independent of this threshold.");
        edit(Control::ShadowAware,ImGui::Checkbox("Respect Shadows",&value.shadowAware));
        tooltip("Use available shadow maps for scene-light reveal. The player's range uses distance only.");
        edit(Control::Emission,ImGui::Checkbox("Affect Emission",&value.attenuateEmission));
        tooltip("Also darken lit material emission outside illuminated areas.");
        ImGui::TreePop();
    }
    ImGui::EndDisabled();
}

void drawPlayerLightRigControls(PlayerLightRigSettings& rig,
    const std::function<void(PlayerLightRigControl,const char*,bool)>& edit) {
    using Control=PlayerLightRigControl;
    ImGui::PushID("Character light rig");
    if(ImGui::CollapsingHeader("Light Rig",ImGuiTreeNodeFlags_DefaultOpen)) {
        edit(Control::Enabled,"Toggle character light rig",ImGui::Checkbox("Enabled",&rig.enabled));
        edit(Control::ShowRig,"Toggle character light rig display",ImGui::Checkbox("Show Rig",&rig.showGizmos));
        tooltip("Show character light directions, Local World range and Darkness Reveal. Selecting the player shows them in Scene automatically.");
        ImGui::BeginDisabled(!rig.enabled);
        if(ImGui::TreeNode("Character Lighting")) {
            const auto controls=[&](const char* label,CharacterLight& light,Control base) {
                ImGui::PushID(label);
                if(ImGui::TreeNode(label)) {
                    const auto field=[&](unsigned offset,const char* undoLabel,bool changed) {
                        edit(static_cast<Control>(static_cast<unsigned>(base)+offset),undoLabel,changed);
                    };
                    field(0,"Toggle character light",ImGui::Checkbox("Enabled",&light.enabled));
                    ImGui::BeginDisabled(!light.enabled);
                    field(1,"Change character light direction",ImGui::DragFloat3("Direction",light.direction.data(),.01f,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp));
                    tooltip("World-space direction in which light travels. Affects only the character; geometry does not block it.");
                    field(2,"Change character light color",ImGui::ColorEdit3("Color",light.color.data(),ImGuiColorEditFlags_Float));
                    field(3,"Change character light intensity",ImGui::DragFloat("Intensity",&light.intensity,.05f,0,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
                    if(base==Control::KeyEnabled) {
                        auto& shadow=rig.characterShadow;
                        edit(Control::ShadowEnabled,"Toggle Key character shadow",ImGui::Checkbox("Cast Shadow",&shadow.enabled));
                        tooltip("Cast the player's silhouette onto World lighting in the Key direction, without illuminating World surfaces.");
                        ImGui::BeginDisabled(!shadow.enabled);
                        edit(Control::ShadowStrength,"Change Key shadow strength",ImGui::SliderFloat("Shadow Strength",&shadow.strength,0,1,"%.3f"));
                        edit(Control::ShadowDistance,"Change Key shadow range",ImGui::DragFloat("Shadow Range (m)",&shadow.distance,.1f,.01f,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
                        ImGui::EndDisabled();
                    }
                    if(ImGui::TreeNode("Advanced")) {
                        field(4,"Change character diffuse lighting",ImGui::SliderFloat("Diffuse",&light.diffuse,0,1,"%.3f"));
                        field(5,"Change character specular lighting",ImGui::SliderFloat("Specular",&light.specular,0,1,"%.3f"));
                        if(base==Control::KeyEnabled) {
                            auto& shadow=rig.characterShadow;
                            ImGui::BeginDisabled(!shadow.enabled);
                            edit(Control::ShadowDepthBias,"Change Key shadow depth bias",ImGui::DragFloat("Shadow Depth Bias (m)",&shadow.shadowDepthBias,.001f,0,std::numeric_limits<float>::max(),"%.4f",ImGuiSliderFlags_AlwaysClamp));
                            edit(Control::ShadowNormalBias,"Change Key shadow normal bias",ImGui::DragFloat("Shadow Normal Bias (texels)",&shadow.shadowNormalBias,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp));
                            ImGui::EndDisabled();
                        }
                        ImGui::TreePop();
                    }
                    ImGui::EndDisabled();ImGui::TreePop();
                }
                ImGui::PopID();
            };
            controls("Key",rig.key,Control::KeyEnabled);controls("Fill",rig.fill,Control::FillEnabled);
            ImGui::TreePop();
        }
        if(ImGui::TreeNode("Local World Light")) {
            auto& light=rig.localWorld;
            edit(Control::WorldEnabled,"Toggle Local World Light",ImGui::Checkbox("Enabled",&light.enabled));
            tooltip("An omni light that follows the player and lights World geometry. Darkness Reveal is independent.");
            ImGui::BeginDisabled(!light.enabled);
            edit(Control::WorldHeight,"Change Local World Light height",ImGui::DragFloat("Height (m)",&light.height,.05f,.01f,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            edit(Control::WorldColor,"Change Local World Light color",ImGui::ColorEdit3("Color",light.color.data(),ImGuiColorEditFlags_Float));
            edit(Control::WorldIntensity,"Change Local World Light intensity",ImGui::DragFloat("Intensity",&light.intensity,.05f,0,1000000,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            edit(Control::WorldRadius,"Change Local World Light radius",ImGui::DragFloat("Radius (m)",&light.radius,.1f,.01f,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            tooltip("Maximum distance from the source. Illumination reaches zero at this radius.");
            edit(Control::WorldAttenuation,"Change Local World Light attenuation",ImGui::SliderFloat("Attenuation",&light.attenuation,-10,10,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            tooltip("Distance decay: 0 has no distance decay, 1 is inverse distance, and 2 is inverse square. Range Falloff still fades the range boundary.");
            edit(Control::WorldNearFalloff,"Change Local World Light near falloff",ImGui::DragFloat("Near Falloff Radius (m)",&light.nearFalloffRadius,.05f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            tooltip("Smooth the close-range brightness peak. This is separate from the source's apparent size.");
            edit(Control::WorldFalloffExponent,"Change Local World Light falloff",ImGui::SliderFloat("Range Falloff",&light.rangeFalloffExponent,.25f,8,"%.2f"));
            tooltip("Lower values fade sooner within the range; higher values retain more light toward its outer edge.");
            edit(Control::WorldEdgeSoftness,"Change Local World Light edge softness",ImGui::SliderFloat("Edge Softness",&light.edgeSoftness,.05f,1,"%.2f"));
            tooltip("Fraction of the radius used for the range fade. One fades across the full radius.");
            edit(Control::WorldSourceRadius,"Change Local World Light source radius",ImGui::DragFloat("Source Radius (m)",&light.sourceRadius,.05f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            tooltip("Larger source sizes soften shadows and broaden specular highlights. Zero retains a punctual source.");
            edit(Control::WorldSpecular,"Change Local World Light specular scale",ImGui::SliderFloat("Specular Scale",&light.specularStrength,0,1,"%.2f"));
            tooltip("Scale this light's shiny highlights without changing diffuse illumination.");
            edit(Control::WorldShadows,"Toggle Local World Light shadows",ImGui::Checkbox("Cast Shadows",&light.castShadows));
            if(ImGui::TreeNode("Advanced")) {
                ImGui::BeginDisabled(!light.castShadows);
                edit(Control::WorldDepthBias,"Change Local World Light shadow depth bias",ImGui::DragFloat("Depth Bias (m)",&light.shadowDepthBias,.001f,0,std::numeric_limits<float>::max(),"%.4f",ImGuiSliderFlags_AlwaysClamp));
                edit(Control::WorldNormalBias,"Change Local World Light shadow normal bias",ImGui::DragFloat("Normal Bias (texels)",&light.shadowNormalBias,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp));
                ImGui::EndDisabled();ImGui::TreePop();
            }
            ImGui::EndDisabled();ImGui::TreePop();
        }
        ImGui::EndDisabled();
    }
    ImGui::PopID();
}
} // namespace crimson
