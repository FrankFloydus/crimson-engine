#pragma once
#include "scene/Scene.h"
#include "CollisionFilterControls.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <functional>

namespace crimson {
enum class TraceTestInspectorControl : unsigned { Duration,Length,CollisionQuadSize,CollisionLayers,HitImpulse,UpwardImpulse };
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
    edit(TraceTestInspectorControl::HitImpulse,"Change trace hit impulse",resettableProperty(value.hitImpulse,defaults.hitImpulse,[&]{
        return ImGui::DragFloat("Hit Impulse (N s)",&value.hitImpulse,.5f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    }));
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Impulse along the trace at the struck ragdoll body. Zero omits the directional kick.");
    edit(TraceTestInspectorControl::UpwardImpulse,"Change trace upward impulse",resettableProperty(value.upwardImpulse,defaults.upwardImpulse,[&]{
        return ImGui::DragFloat("Upward Impulse (N s)",&value.upwardImpulse,.5f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    }));
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Additional world-up impulse at the struck ragdoll body, independent of the trace direction.");
    edit(TraceTestInspectorControl::CollisionLayers,"Change trace collision layers",drawCollisionMaskControl("Collision Layers",value.collisionMask,
        collisionNames,AllCollisionLayers,nullptr,defaults.collisionMask));
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Detect colliders belonging to any selected layer.");
    ImGui::PopID();
}
}
