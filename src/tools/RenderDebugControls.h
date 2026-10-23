#pragma once
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/RenderPassPlan.h"
#include "PropertyReset.h"
#include "editor/ViewportRotationSnap.h"
#include <imgui.h>
#include <algorithm>
#include <array>

namespace crimson {
// A separate toolbar row keeps diagnostic controls outside the viewport image's
// selection/navigation/gameplay input item. The Renderer retains each view's
// transient selection independently of scene documents and editor layout.
inline void drawRenderDebugControls(Renderer& renderer,RenderViewPurpose purpose,float* rotationSnapDegrees=nullptr) {
    constexpr std::array labels{"Lit","Albedo","Roughness","Metal","Normal","Direct Lighting","Indirect Lighting"};
    const auto cursor=ImGui::GetCursorPos();const float available=std::max(1.f,ImGui::GetContentRegionAvail().x);
    const float width=std::min(available,ImGui::CalcTextSize("Indirect Lighting").x+ImGui::GetStyle().FramePadding.x*2+ImGui::GetFrameHeight()*2+ImGui::GetStyle().ItemInnerSpacing.x);
    const float spacing=ImGui::GetStyle().ItemSpacing.x;
    const bool sameRow=rotationSnapDegrees&&available>=width+spacing+80.f;
    const float snapWidth=rotationSnapDegrees?std::min(160.f,sameRow?available-width-spacing:available):0.f;
    ImGui::SetCursorPosX(cursor.x+std::max(0.f,available-(rotationSnapDegrees?(sameRow?width+snapWidth+spacing:snapWidth):width)));
    if(rotationSnapDegrees) {
        ImGui::SetNextItemWidth(snapWidth);
        resettableProperty(*rotationSnapDegrees,DefaultRotationSnapDegrees,[&] {
            return ImGui::SliderFloat("##RotationSnapDegrees",rotationSnapDegrees,1.f,180.f,"Snap %.0f deg",ImGuiSliderFlags_AlwaysClamp);
        },"15 degrees");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Hold Shift while rotating to snap by this angle.");
        if(sameRow)ImGui::SameLine(0,spacing);
        else ImGui::SetCursorPosX(cursor.x+std::max(0.f,available-width));
    }
    ImGui::SetNextItemWidth(width);
    int selected=std::clamp(static_cast<int>(renderer.debugView(purpose)),0,static_cast<int>(labels.size())-1);
    if(resettableProperty(selected,static_cast<int>(RenderDebugView::Lit),[&]{return ImGui::Combo("##RenderDebugView",&selected,labels.data(),static_cast<int>(labels.size()));},"Lit"))
        renderer.setDebugView(purpose,static_cast<RenderDebugView>(selected));
}
} // namespace crimson
