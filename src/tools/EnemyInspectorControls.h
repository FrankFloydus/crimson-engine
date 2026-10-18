#pragma once
#include "scene/Scene.h"
#include <imgui.h>
#include <algorithm>
#include <functional>
#include <limits>

namespace crimson {
enum class EnemyInspectorControl : unsigned { Behaviour,Radius,MinWait,MaxWait };
// Widgets share the scene Inspector's existing property gesture owner. Hidden
// Random Move values are retained when switching to another behaviour.
inline void drawEnemyInspectorControls(SceneEnemy& value,
    const std::function<void(EnemyInspectorControl,const char*,bool)>& edit) {
    auto behaviour=static_cast<int>(value.behaviour);
    const bool changed=ImGui::Combo("Behaviour",&behaviour,"Idle\0Chase Player\0Random Move\0");
    if(changed)value.behaviour=static_cast<EnemyBehaviour>(behaviour);
    edit(EnemyInspectorControl::Behaviour,"Change enemy behaviour",changed);
    if(value.behaviour!=EnemyBehaviour::RandomMove)return;
    const bool radius=ImGui::DragFloat("Radius (m)",&value.radius,.1f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
    edit(EnemyInspectorControl::Radius,"Change enemy radius",radius);
    const bool minimum=ImGui::DragFloat("Min Wait (s)",&value.minWait,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
    if(minimum)value.maxWait=std::max(value.minWait,value.maxWait);
    edit(EnemyInspectorControl::MinWait,"Change enemy minimum wait",minimum);
    const bool maximum=ImGui::DragFloat("Max Wait (s)",&value.maxWait,.05f,0,std::numeric_limits<float>::max(),"%.2f",ImGuiSliderFlags_AlwaysClamp);
    if(maximum)value.minWait=std::min(value.minWait,value.maxWait);
    edit(EnemyInspectorControl::MaxWait,"Change enemy maximum wait",maximum);
}
}
