#pragma once
#include "scene/Scene.h"
#include "CollisionFilterControls.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <functional>

namespace crimson {
enum class TraceTestInspectorControl : unsigned { Duration,Length,CollisionQuadSize,CollisionLayers };
inline void drawTraceTestInspectorControls(SceneTraceTestSettings& value,const std::array<std::string,32>& collisionNames,
    const std::function<void(TraceTestInspectorControl,const char*,bool)>& edit) {
    const SceneTraceTestSettings defaults;
    ImGui::PushID("trace-test");
    edit(TraceTestInspectorControl::Duration,"Change trace duration",resettableProperty(value.duration,defaults.duration,[&]{
        return ImGui::DragFloat("Duration (s)",&value.duration,.01f,0,3600,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    }));
    edit(TraceTestInspectorControl::Length,"Change trace length",resettableProperty(value.length,defaults.length,[&]{
        return ImGui::DragFloat("Length (m)",&value.length,.1f,.001f,100000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    }));
    edit(TraceTestInspectorControl::CollisionQuadSize,"Change trace collision quad size",resettableProperty(value.collisionQuadSize,defaults.collisionQuadSize,[&]{
        return ImGui::DragFloat("Collision Quad Size (m)",&value.collisionQuadSize,.005f,.001f,1000,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    }));
    edit(TraceTestInspectorControl::CollisionLayers,"Change trace collision layers",drawCollisionMaskControl("Collision Layers",value.collisionMask,
        collisionNames,AllCollisionLayers,nullptr,defaults.collisionMask));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Detect colliders belonging to any selected layer.");
    ImGui::PopID();
}
}
