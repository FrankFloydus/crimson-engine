#pragma once

#include "PropertyReset.h"
#include "scene/Scene.h"
#include <imgui.h>
#include <algorithm>

namespace crimson {
enum class SpotConeControl { InnerAngle, OuterAngle, Falloff };

// A cone edit publishes both angles together when a reset crosses the other
// angle. The document still owns one gesture and its exact undo baseline.
template<class Edit>
void drawSpotConeControls(SceneSpotLight& light,const Edit& edit) {
    const SceneSpotLight defaults;
    bool changed=resettableProperty(light.innerAngle,defaults.innerAngle,[&] {
        return ImGui::DragFloat("Inner Angle (\u00b0)",&light.innerAngle,.1f,0,light.outerAngle,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    });
    if(changed) {
        if(propertyResetClicked())light.outerAngle=std::max(light.outerAngle,light.innerAngle);
        light.innerAngle=std::clamp(light.innerAngle,0.f,light.outerAngle);
    }
    edit(SpotConeControl::InnerAngle,"Change spot inner angle",changed);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Half-angle of the fully lit cone, measured from the light's local -Z axis.");
    changed=resettableProperty(light.outerAngle,defaults.outerAngle,[&] {
        return ImGui::DragFloat("Outer Angle (\u00b0)",&light.outerAngle,.1f,.1f,89,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    });
    if(changed) {
        light.outerAngle=std::clamp(light.outerAngle,.1f,89.f);
        light.innerAngle=std::min(light.innerAngle,light.outerAngle);
    }
    edit(SpotConeControl::OuterAngle,"Change spot outer angle",changed);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Half-angle of the outer cone. Light fades from the inner angle to zero at this boundary.");
    changed=resettableProperty(light.coneFalloff,defaults.coneFalloff,[&] {
        return ImGui::DragFloat("Cone Falloff",&light.coneFalloff,.02f,.01f,16,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    });
    edit(SpotConeControl::Falloff,"Change spot cone falloff",changed);
}
}
