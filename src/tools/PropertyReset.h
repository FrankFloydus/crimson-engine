#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

namespace crimson {
namespace property_reset_detail {
inline thread_local bool clicked{};
template<class T> std::string default_text(const T& value) {
    if constexpr(std::is_same_v<T,bool>)return value?"On":"Off";
    else if constexpr(std::is_arithmetic_v<T>) {
        char text[48];std::snprintf(text,sizeof(text),"%.6g",static_cast<double>(value));return text;
    } else if constexpr(std::is_enum_v<T>)return default_text(static_cast<std::underlying_type_t<T>>(value));
    else if constexpr(requires {std::begin(value);std::end(value);}) {
        std::string text;for(const auto& component:value){if(!text.empty())text+=", ";text+=default_text(component);}return text;
    } else return {};
}
}

// Read immediately after a resettable property when a custom gesture owner
// needs to distinguish an atomic reset from an ordinary live edit.
inline bool propertyResetClicked() {return property_reset_detail::clicked;}

// The ordinary item's identity, bounds and status remain available to caller
// hover/activation/history code. Reset is an atomic changed, inactive edit.
template<class T,class Draw>
bool resettableProperty(T& value,const T& defaults,Draw&& draw,const char* defaultDescription=nullptr) {
    property_reset_detail::clicked=false;
    if(ImGui::GetCurrentWindow()->SkipItems)return std::forward<Draw>(draw)();
    const float extent=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=ImGui::CalcItemWidth();
    ImGui::SetNextItemWidth(std::max(extent,width-extent-gap));
    const bool changed=std::forward<Draw>(draw)();
    auto& context=*ImGui::GetCurrentContext();
    auto item=context.LastItemData;
    ImGui::SameLine(0,gap);
    ImGui::PushID(static_cast<int>(item.ID));
    if(!item.ID)ImGui::PushID(static_cast<const void*>(&value));
    ImGui::BeginDisabled(value==defaults);
    const bool reset=ImGui::Button("##reset-default",{extent,extent});
    const auto a=ImGui::GetItemRectMin();const ImVec2 center{a.x+extent*.5f,a.y+extent*.5f};
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);auto* list=ImGui::GetWindowDrawList();
    const float radius=extent*.23f;
    list->PathArcTo(center,radius,-.6f,4.25f,18);list->PathStroke(color,0,std::max(1.f,extent*.065f));
    const ImVec2 tip{center.x+std::cos(4.25f)*radius,center.y+std::sin(4.25f)*radius};
    list->AddTriangleFilled(tip,{tip.x+extent*.19f,tip.y-extent*.015f},{tip.x+extent*.04f,tip.y+extent*.16f},color);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled)) {
        const auto text=defaultDescription?std::string(defaultDescription):property_reset_detail::default_text(defaults);
        if(text.empty())ImGui::SetTooltip("Reset to default");else ImGui::SetTooltip("Reset to default: %s",text.c_str());
    }
    ImGui::EndDisabled();
    if(!item.ID)ImGui::PopID();ImGui::PopID();
    if(reset) {
        value=defaults;property_reset_detail::clicked=true;ImGui::ClearActiveID();
        item.StatusFlags|=ImGuiItemStatusFlags_Edited|ImGuiItemStatusFlags_HasDeactivated|ImGuiItemStatusFlags_Deactivated;
        if(item.ID)context.DeactivatedItemData={item.ID,context.FrameCount,true,true};
    }
    context.LastItemData=item;
    return changed||reset;
}
} // namespace crimson
