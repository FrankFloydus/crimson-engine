#pragma once
#include "physics/CollisionFilter.h"
#include <imgui.h>
#include <array>
#include <bit>
#include <string>

namespace crimson {
// Each slot has a persistent bit identity; project names change presentation.
inline bool drawCollisionMaskControl(const char* label,std::uint32_t& value,
    const std::array<std::string,32>& names,std::uint32_t editableBits=AllCollisionLayers,
    const char* lockedTooltip=nullptr) {
    const auto count=std::popcount(value);
    const auto preview=count==0?std::string("None"):count==32?std::string("All"):std::to_string(count)+" selected";
    bool changed=false;
    if(ImGui::BeginCombo(label,preview.c_str(),ImGuiComboFlags_HeightLarge)) {
        for(unsigned slot=0;slot<32;++slot) {
            const auto bit=std::uint32_t{1}<<slot;bool selected=(value&bit)!=0;
            ImGui::PushID(static_cast<int>(slot));ImGui::BeginDisabled((editableBits&bit)==0);
            if(ImGui::Checkbox("##layer",&selected)) {
                value=selected?value|bit:value&~bit;changed=true;
            }
            const bool hoveredCheckbox=ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
            ImGui::EndDisabled();ImGui::SameLine();
            const auto name=names[slot].empty()?"Layer "+std::to_string(slot+1):names[slot];
            ImGui::Text("%u: %s",slot+1,name.c_str());
            if((editableBits&bit)==0 && lockedTooltip &&
               (hoveredCheckbox || ImGui::IsItemHovered()))ImGui::SetTooltip(lockedTooltip,name.c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}
inline CollisionFilter collisionFilterForMode(CollisionFilter value,ColliderMode mode) {
    if(mode==ColliderMode::Solid) {value.layers|=WorldCollisionLayer;value.mask=AllCollisionLayers;}
    else {value.layers&=~WorldCollisionLayer;if(!value.layers)value.layers=TriggerCollisionLayer;value.mask=CharacterCollisionLayers;}
    return value;
}
}
