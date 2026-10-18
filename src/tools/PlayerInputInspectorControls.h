#pragma once
#include "scene/Scene.h"
#include <imgui.h>
#include <functional>

namespace crimson {
enum class PlayerInputInspectorControl : unsigned {
    Enabled,MouseSensitivity,InvertY
};
// Each callback runs immediately after the real item so the scene panel owns
// activation, live updates, cancellation and one history command per gesture.
inline void drawPlayerInputInspectorControls(ScenePlayerInput& value,
    const std::function<void(PlayerInputInspectorControl,const char*,bool)>& edit) {
    edit(PlayerInputInspectorControl::Enabled,"Toggle player input",ImGui::Checkbox("Enabled##player-input",&value.enabled));
    edit(PlayerInputInspectorControl::MouseSensitivity,"Change mouse sensitivity",ImGui::DragFloat("Mouse Sensitivity",&value.mouseSensitivity,.005f,.001f,10.f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Degrees per logical mouse pixel.");
    edit(PlayerInputInspectorControl::InvertY,"Toggle inverted look",ImGui::Checkbox("Invert Y",&value.invertY));
}
}
