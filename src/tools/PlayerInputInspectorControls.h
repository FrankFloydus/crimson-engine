#pragma once
#include "scene/Scene.h"
#include <imgui.h>
#include <functional>

namespace crimson {
enum class PlayerInputInspectorControl : unsigned {
    Enabled,MouseSensitivity,InvertY,JumpHeight,GroundAcceleration,GroundFriction,AirAcceleration,AirControl,StrafeTilt,TiltResponse
};
// Each callback runs immediately after the real item so the scene panel owns
// activation, live updates, cancellation and one history command per gesture.
inline void drawPlayerInputInspectorControls(ScenePlayerInput& value,
    const std::function<void(PlayerInputInspectorControl,const char*,bool)>& edit) {
    edit(PlayerInputInspectorControl::Enabled,"Toggle player input",ImGui::Checkbox("Enabled##player-input",&value.enabled));
    edit(PlayerInputInspectorControl::MouseSensitivity,"Change mouse sensitivity",ImGui::DragFloat("Mouse Sensitivity",&value.mouseSensitivity,.005f,.001f,10.f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Degrees per logical mouse pixel.");
    edit(PlayerInputInspectorControl::InvertY,"Toggle inverted look",ImGui::Checkbox("Invert Y",&value.invertY));
    edit(PlayerInputInspectorControl::JumpHeight,"Change jump height",ImGui::DragFloat("Jump Height (m)",&value.jumpHeight,.05f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::GroundAcceleration,"Change ground acceleration",ImGui::DragFloat("Ground Acceleration",&value.groundAcceleration,.5f,0,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::GroundFriction,"Change ground friction",ImGui::DragFloat("Ground Friction",&value.groundFriction,.1f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::AirAcceleration,"Change air acceleration",ImGui::DragFloat("Air Acceleration",&value.airAcceleration,.25f,0,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::AirControl,"Change air control",ImGui::DragFloat("Air Control",&value.airControl,.01f,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::StrafeTilt,"Change strafe tilt",ImGui::DragFloat("Strafe Tilt (deg)",&value.strafeTilt,.1f,0,45.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
    edit(PlayerInputInspectorControl::TiltResponse,"Change tilt response",ImGui::DragFloat("Tilt Response",&value.tiltResponse,.25f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp));
}
}
