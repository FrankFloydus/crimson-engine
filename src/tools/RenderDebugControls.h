#pragma once
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/RenderPassPlan.h"
#include <imgui.h>
#include <algorithm>
#include <array>

namespace crimson {
// A separate toolbar row keeps diagnostic controls outside the viewport image's
// selection/navigation/gameplay input item. The Renderer retains each view's
// transient selection independently of scene documents and editor layout.
inline void drawRenderDebugControls(Renderer& renderer,RenderViewPurpose purpose) {
    constexpr std::array labels{"Lit","Albedo","Roughness","Metal","Normal","Direct Lighting","Indirect Lighting"};
    const auto cursor=ImGui::GetCursorPos();const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float width=std::min(available,ImGui::CalcTextSize("Indirect Lighting").x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetFrameHeight());
    ImGui::SetCursorPosX(cursor.x+std::max(0.f,available-width));ImGui::SetNextItemWidth(width);
    int selected=std::clamp(static_cast<int>(renderer.debugView(purpose)),0,static_cast<int>(labels.size())-1);
    if(ImGui::Combo("##RenderDebugView",&selected,labels.data(),static_cast<int>(labels.size())))
        renderer.setDebugView(purpose,static_cast<RenderDebugView>(selected));
}
} // namespace crimson
