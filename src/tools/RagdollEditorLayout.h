#pragma once
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <utility>

namespace crimson {
// Controls are recorded before the preview so edits and simulation actions
// affect the pose evaluated in the same frame. Each side pane owns scrolling.
// False means the preview callback was not invoked (including clipped tables).
template<class BonesCallback,class SimulationCallback,class PropertiesCallback,class PreviewCallback>
bool drawRagdollWorkspace(float& simulationHeight,BonesCallback&& bones,SimulationCallback&& simulation,
    PropertiesCallback&& properties,PreviewCallback&& preview) {
    const auto available=ImGui::GetContentRegionAvail();
    constexpr auto tableFlags=ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV|
        ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoHostExtendY;
    if(!ImGui::BeginTable("Ragdoll workspace",3,tableFlags,available))return false;
    ImGui::TableSetupColumn("Bones",ImGuiTableColumnFlags_WidthFixed,260);
    ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Simulation and properties",ImGuiTableColumnFlags_WidthFixed,350);
    ImGui::TableNextRow();
    const float paneHeight=std::max(1.f,available.y-2*ImGui::GetStyle().CellPadding.y);
    ImGui::TableSetColumnIndex(0);
    if(ImGui::BeginChild("Ragdoll bones pane",{0,paneHeight}))std::forward<BonesCallback>(bones)();
    ImGui::EndChild();
    ImGui::TableSetColumnIndex(2);
    constexpr auto noScroll=ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse;
    if(ImGui::BeginChild("Ragdoll controls pane",{0,paneHeight},0,noScroll)) {
        const auto space=ImGui::GetContentRegionAvail();
        if(space.x>0&&space.y>=16) {
            const float divider=std::min(6.f,space.y/3);
            const float gap=std::min(ImGui::GetStyle().ItemSpacing.y,std::max(0.f,space.y-divider)*.1f);
            const float usable=std::max(0.f,space.y-divider-2*gap);
            const float minimumTop=std::min(100.f,usable*(100.f/220.f));
            const float minimumBottom=std::min(120.f,usable*(120.f/220.f));
            const float maximumTop=std::max(minimumTop,usable-minimumBottom);
            if(!std::isfinite(simulationHeight))simulationHeight=300;
            simulationHeight=std::clamp(simulationHeight,minimumTop,maximumTop);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,{ImGui::GetStyle().ItemSpacing.x,gap});
            if(ImGui::BeginChild("Ragdoll simulation pane",{0,simulationHeight}))std::forward<SimulationCallback>(simulation)();
            ImGui::EndChild();
            ImGui::InvisibleButton("Resize ragdoll simulation",{space.x,divider});
            const bool hovered=ImGui::IsItemHovered(),active=ImGui::IsItemActive();
            if(hovered||active)ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if(active)simulationHeight=std::clamp(simulationHeight+ImGui::GetIO().MouseDelta.y,minimumTop,maximumTop);
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            const auto color=ImGui::GetColorU32(active?ImGuiCol_SeparatorActive:hovered?ImGuiCol_SeparatorHovered:ImGuiCol_Separator);
            ImGui::GetWindowDrawList()->AddLine({a.x,(a.y+b.y)*.5f},{b.x,(a.y+b.y)*.5f},color,2);
            if(ImGui::BeginChild("Ragdoll properties pane",{0,0}))std::forward<PropertiesCallback>(properties)();
            ImGui::EndChild();ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
    ImGui::TableSetColumnIndex(1);
    const bool previewVisible=ImGui::BeginChild("Ragdoll preview pane",{0,paneHeight},0,noScroll);
    if(previewVisible)std::forward<PreviewCallback>(preview)();
    ImGui::EndChild();ImGui::EndTable();return previewVisible;
}
}
