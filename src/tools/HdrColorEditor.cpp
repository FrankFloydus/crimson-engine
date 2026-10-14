#include "HdrColorEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

namespace crimson {
namespace {
float srgbToLinear(float value) {
    return value<=.04045f?value/12.92f:std::pow((value+.055f)/1.055f,2.4f);
}
float linearToSrgb(float value) {
    return value<=.0031308f?12.92f*value:1.055f*std::pow(value,1.f/2.4f)-.055f;
}
float positive(float value) { return std::isfinite(value)?std::max(0.f,value):0.f; }
}
HdrColorDecomposition decomposeHdrColor(const Vec3& linearRgb) {
    HdrColorDecomposition result;
    const float multiplier=std::max({1.f,positive(linearRgb[0]),positive(linearRgb[1]),positive(linearRgb[2])});
    result.intensityEv=std::log2(multiplier);
    for(size_t component=0;component<3;++component)
        result.hueSrgb[component]=linearToSrgb(positive(linearRgb[component])/multiplier);
    return result;
}
Vec3 composeHdrColor(const HdrColorDecomposition& color,float maximum) {
    const double multiplier=std::exp2(static_cast<double>(std::isfinite(color.intensityEv)?color.intensityEv:0.f));
    Vec3 result;
    for(size_t component=0;component<3;++component) {
        const auto hue=std::clamp(positive(color.hueSrgb[component]),0.f,1.f);
        result[component]=static_cast<float>(std::clamp(srgbToLinear(hue)*multiplier,0.0,static_cast<double>(maximum)));
    }
    return result;
}
Vec4 hdrColorDisplay(const Vec4& color) {
    const float divisor=std::max({1.f,positive(color[0]),positive(color[1]),positive(color[2])});
    return {linearToSrgb(positive(color[0])/divisor),linearToSrgb(positive(color[1])/divisor),
        linearToSrgb(positive(color[2])/divisor),std::clamp(color[3],0.f,1.f)};
}
void HdrColorEdit::include(bool itemChanged) {
    changed|=itemChanged;activated|=ImGui::IsItemActivated();
    active|=ImGui::IsItemActive();deactivated|=ImGui::IsItemDeactivated();
}
void HdrColorEditorState::cancel() {
    if(activeItem && ImGui::GetCurrentContext() && ImGui::GetActiveID()==activeItem)ImGui::ClearActiveID();
    initialized=false;activeItem=0;
}
HdrColorEdit drawHdrColorControl(const char* label,Vec3& linearRgb,float* opacity,
    HdrColorEditorState& state,bool fullPicker,float maximum,const char* alphaLabel) {
    if(!state.initialized || state.synchronized!=linearRgb) {
        state.decomposition=decomposeHdrColor(linearRgb);state.synchronized=linearRgb;state.initialized=true;
    }
    HdrColorEdit edit;
    const bool wasActive=state.activeItem!=0;
    ImGui::PushID(label);
    const auto include=[&](bool changed) {
        edit.include(changed);
        if(ImGui::IsItemActive())state.activeItem=ImGui::GetActiveID();
    };
    const auto fieldWidth=[](const char* title) {
        ImGui::SetNextItemWidth(std::max(ImGui::GetFontSize()*4,
            ImGui::GetContentRegionAvail().x-ImGui::CalcTextSize(title).x-ImGui::GetStyle().ItemInnerSpacing.x));
    };
    state.channelMinimum={};state.channelMaximum={};state.opacityMinimum={};state.opacityMaximum={};
    bool channelsChanged=false;
    const auto channels=[&] {
        static constexpr const char* labels[]{"R","G","B"};
        for(size_t component=0;component<3;++component) {
            fieldWidth(labels[component]);
            const bool changed=ImGui::DragFloat(labels[component],&linearRgb[component],.01f,0.f,maximum,"%.4g",ImGuiSliderFlags_AlwaysClamp);
            channelsChanged|=changed;
            include(changed);
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            state.channelMinimum[component]={a.x,a.y};state.channelMaximum[component]={b.x,b.y};
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("Linear %s; values above 1 retain HDR radiance.",labels[component]);
        }
        if(opacity) {
            fieldWidth(alphaLabel);
            include(ImGui::SliderFloat(alphaLabel,opacity,0.f,1.f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
            const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
            state.opacityMinimum={a.x,a.y};state.opacityMaximum={b.x,b.y};
        }
    };
    const auto picker=[&] {
        // ImGui's hue control is display-referred. Keep its normalized editing
        // value separate from the linear numeric rows and preserve HDR peaks.
        const bool changed=ImGui::ColorPicker3("##picker",state.decomposition.hueSrgb.data(),
            ImGuiColorEditFlags_NoInputs|ImGuiColorEditFlags_NoSidePreview|ImGuiColorEditFlags_NoSmallPreview|ImGuiColorEditFlags_NoAlpha);
        include(changed);
        if(changed)linearRgb=composeHdrColor(state.decomposition,maximum);
        channels();
    };
    if(fullPicker) {
        ImGui::PushItemWidth(ImGui::GetFontSize()*16);picker();ImGui::PopItemWidth();
        if((ImGui::GetCurrentWindow()->Flags&ImGuiWindowFlags_Popup) && ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();
    }
    else {
        ImGui::SetNextItemOpen(state.expanded,ImGuiCond_Always);
        state.expanded=ImGui::TreeNodeEx("##details",ImGuiTreeNodeFlags_NoTreePushOnOpen,"%s",label);
        auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        state.disclosureMinimum={a.x,a.y};state.disclosureMaximum={b.x,b.y};
        ImGui::SameLine();
        const auto display=hdrColorDisplay({linearRgb[0],linearRgb[1],linearRgb[2],opacity?*opacity:1.f});
        const bool open=ImGui::ColorButton("##swatch",{display[0],display[1],display[2],display[3]},
            ImGuiColorEditFlags_NoTooltip|ImGuiColorEditFlags_AlphaPreviewHalf,
            {std::max(ImGui::GetFrameHeight(),ImGui::GetContentRegionAvail().x),ImGui::GetFrameHeight()});
        a=ImGui::GetItemRectMin();b=ImGui::GetItemRectMax();
        state.swatchMinimum={a.x,a.y};state.swatchMaximum={b.x,b.y};
        if(std::max({linearRgb[0],linearRgb[1],linearRgb[2]})>1.f) {
            const auto size=ImGui::CalcTextSize("HDR");
            ImGui::GetWindowDrawList()->AddText({b.x-size.x-4,a.y+(b.y-a.y-size.y)*.5f},ImGui::GetColorU32(ImGuiCol_Text),"HDR");
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Linear RGB: %.4g, %.4g, %.4g%s",linearRgb[0],linearRgb[1],linearRgb[2],
                std::max({linearRgb[0],linearRgb[1],linearRgb[2]})>1.f?"\nHDR swatch brightness is normalized.":"");
        if(open)ImGui::OpenPopup("Color picker");
        if(state.expanded){ImGui::Indent();channels();ImGui::Unindent();}
        if(ImGui::BeginPopup("Color picker")) {
            ImGui::PushItemWidth(ImGui::GetFontSize()*16);picker();ImGui::PopItemWidth();
            if(ImGui::IsKeyPressed(ImGuiKey_Escape,false))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
    }
    // Retain the picker coordinate throughout a hue gesture. Re-decomposing
    // its intermediate RGB would renormalize the value slider every frame.
    if(edit.changed) { if(channelsChanged)state.decomposition=decomposeHdrColor(linearRgb);state.synchronized=linearRgb; }
    // A compound color picker may expose its active child only through the
    // enclosing ColorEdit item. Track it before subsequent numeric fields.
    if(!edit.active)state.activeItem=0;
    edit.activated|=!wasActive && edit.active;
    edit.deactivated|=wasActive && !edit.active;
    ImGui::PopID();
    return edit;
}
} // namespace crimson
