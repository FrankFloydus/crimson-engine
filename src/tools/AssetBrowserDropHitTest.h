#pragma once
#include <imgui_internal.h>

namespace crimson {
// SDL drops arrive before NewFrame and may target a different platform window
// from the last mouse event. ImGui's query still filters by MouseViewport even
// in its "any viewport" mode. Scope that input to this query only; ordinary
// pointer, popup and gesture ownership remains unchanged after the call.
inline bool assetBrowserDropHitTest(ImGuiWindow* browser,const ImGuiViewport* viewport,const ImVec2& point) {
    auto* context=ImGui::GetCurrentContext();
    if(!context || !browser || !viewport || browser->Viewport!=viewport)return false;
    struct RestoreViewport {
        ImGuiContext& context;
        ImGuiViewportP* previous;
        ~RestoreViewport(){context.MouseViewport=previous;}
    } restore{*context,context->MouseViewport};
    context->MouseViewport=static_cast<ImGuiViewportP*>(const_cast<ImGuiViewport*>(viewport));
    ImGuiWindow* hit{};ImGui::FindHoveredWindowEx(point,true,&hit,nullptr);
    return hit && (hit==browser || ImGui::IsWindowChildOf(hit,browser,false,false));
}
} // namespace crimson
