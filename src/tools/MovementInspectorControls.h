#pragma once
#include "scene/Scene.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <functional>

namespace crimson {
enum class MovementInspectorControl : unsigned {
    Enabled,JumpHeight,GroundAcceleration,GroundFriction,AirAcceleration,AirControl,StrafeTilt,TiltResponse,StairSmoothingTime
};
inline void drawMovementInspectorControls(MovementComponent& value,
    const std::function<void(MovementInspectorControl,const char*,bool)>& edit) {
    const MovementComponent defaults;
    edit(MovementInspectorControl::Enabled,"Toggle movement",resettableProperty(value.enabled,defaults.enabled,[&]{return ImGui::Checkbox("Enabled##movement",&value.enabled);}));
    edit(MovementInspectorControl::JumpHeight,"Change jump height",resettableProperty(value.jumpHeight,defaults.jumpHeight,[&]{return ImGui::DragFloat("Jump Height (m)",&value.jumpHeight,.05f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::GroundAcceleration,"Change ground acceleration",resettableProperty(value.groundAcceleration,defaults.groundAcceleration,[&]{return ImGui::DragFloat("Ground Acceleration",&value.groundAcceleration,.5f,0,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::GroundFriction,"Change ground friction",resettableProperty(value.groundFriction,defaults.groundFriction,[&]{return ImGui::DragFloat("Ground Friction",&value.groundFriction,.1f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::AirAcceleration,"Change air acceleration",resettableProperty(value.airAcceleration,defaults.airAcceleration,[&]{return ImGui::DragFloat("Air Acceleration",&value.airAcceleration,.25f,0,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::AirControl,"Change air control",resettableProperty(value.airControl,defaults.airControl,[&]{return ImGui::DragFloat("Air Control",&value.airControl,.01f,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::StrafeTilt,"Change strafe tilt",resettableProperty(value.strafeTilt,defaults.strafeTilt,[&]{return ImGui::DragFloat("Strafe Tilt (deg)",&value.strafeTilt,.1f,0,45.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::TiltResponse,"Change tilt response",resettableProperty(value.tiltResponse,defaults.tiltResponse,[&]{return ImGui::DragFloat("Tilt Response",&value.tiltResponse,.25f,0,100.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);}));
    edit(MovementInspectorControl::StairSmoothingTime,"Change stair smoothing",resettableProperty(value.stairSmoothingTime,defaults.stairSmoothingTime,[&]{return ImGui::DragFloat("Stair Smoothing (s)",&value.stairSmoothingTime,.005f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);}));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Lower is faster; higher is smoother. Zero disables stair smoothing.");
}
}
