#include "PlayerLightRigOverlay.h"

#include "CollisionWire.h"
#include "runtime/PlayerLightRig.h"
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace crimson {
void drawPlayerLightRigOverlay(ImDrawList& draw,const PlayerLightRigSettings& settings,const CharacterRevealSettings& reveal,
    const Vec3& characterPosition,float characterHeight,const Mat4& viewProjection,const Vec4& viewport,ClipDepth depth) {
    if((!settings.enabled&&!reveal.enabled) || !math::finite(viewport) || viewport[2]<=0 || viewport[3]<=0)return;
    const auto rig=evaluatePlayerLightRig(settings,characterPosition,{},{});
    if(!rig.valid())return;
    draw.PushClipRect({viewport[0],viewport[1]},{viewport[0]+viewport[2],viewport[1]+viewport[3]},true);
    const auto wire=[&](const Vec3& start,const Vec3& end,ImU32 color,float width) {
        if(const auto segment=projectClippedSegment(start,end,viewProjection,viewport,depth))
            draw.AddLine({(*segment)[0][0],(*segment)[0][1]},{(*segment)[1][0],(*segment)[1][1]},color,width);
    };
    const auto sphere=[&](const Vec3& center,float radius,ImU32 color) {
        constexpr unsigned segments=64;
        for(unsigned plane=0;plane<3;++plane)for(unsigned step=0;step<segments;++step) {
            const auto point=[&](unsigned segment) {
                const auto angle=2*std::numbers::pi_v<float>*segment/segments;
                Vec3 offset{};offset[(plane+1)%3]=std::cos(angle)*radius;offset[(plane+2)%3]=std::sin(angle)*radius;
                return center+offset;
            };
            wire(point(step),point(step+1),color,1.f);
        }
    };
    const auto label=[&](Vec3 position,ImU32 color,const char* text) {
        if(const auto point=math::project(position,viewProjection,viewport,depth);
            point&&(*point)[2]>=0&&(*point)[2]<=1)
            draw.AddText({(*point)[0]+8,(*point)[1]-7},color,text);
    };
    if(reveal.enabled&&validCharacterRevealSettings(reveal)) {
        const auto center=characterPosition+Vec3{0,characterLightingCenterHeight(characterHeight),0};
        const auto color=ImGui::ColorConvertFloat4ToU32({.22f,.69f,.87f,.75f});
        sphere(center,reveal.spread,color);
        label(center+Vec3{reveal.spread,0,0},color,"50% Reveal");
    }
    const auto arrow=[&](Vec3 anchor,Vec3 direction,ImU32 color,const char* text) {
        const auto start=anchor-direction*2.f;
        const auto side=math::normalized(math::cross(direction,Vec3{0,1,0}),{1,0,0})*.2f;
        wire(start,anchor,color,2.f);
        wire(anchor,anchor-direction*.35f+side,color,2.f);
        wire(anchor,anchor-direction*.35f-side,color,2.f);
        label(start,color,text);
    };
    const auto head=characterPosition+Vec3{0,characterHeight*.7f,0};
    for(std::size_t index=0;index<rig.characterLights.size();++index) {
        const auto& light=rig.characterLights[index];if(!light.enabled)continue;
        const auto color=ImGui::ColorConvertFloat4ToU32(
            {std::max(light.color[0],.25f),std::max(light.color[1],.25f),std::max(light.color[2],.25f),1});
        arrow(head,light.direction,color,index==0?"Character Key":"Character Fill");
    }
    if(rig.localWorld) {
        const auto& light=*rig.localWorld;
        const auto color=ImGui::ColorConvertFloat4ToU32(
            {std::max(light.color[0],.25f),std::max(light.color[1],.25f),std::max(light.color[2],.25f),.75f});
        wire(characterPosition,light.position,color,1.5f);
        sphere(light.position,light.radius,color);
        for(unsigned axis=0;axis<3;++axis) {
            Vec3 offset{};offset[axis]=.2f;
            wire(light.position-offset,light.position+offset,color,1.5f);
        }
        label(light.position,color,"Local World Light");
    }
    if(rig.characterShadow.enabled)
        arrow(characterPosition+Vec3{0,.1f,0},rig.characterShadow.direction,
            ImGui::ColorConvertFloat4ToU32({.8f,.4f,1,1}),"Key Shadow");
    draw.PopClipRect();
}
} // namespace crimson
