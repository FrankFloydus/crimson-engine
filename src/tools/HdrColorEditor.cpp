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
    const float divisor=1.f+std::max({positive(color[0]),positive(color[1]),positive(color[2])});
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
    HdrColorEditorState& state,bool fullPicker,float maximum) {
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
    bool colorChanged=false;
    if(fullPicker)
        colorChanged=ImGui::ColorPicker3("Hue",state.decomposition.hueSrgb.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoAlpha);
    else {
        fieldWidth("Hue");
        colorChanged=ImGui::ColorEdit3("Hue",state.decomposition.hueSrgb.data(),ImGuiColorEditFlags_Float|ImGuiColorEditFlags_NoAlpha);
    }
    include(colorChanged);
    const float maximumEv=std::log2(std::max(1.f,maximum));
    fieldWidth("Intensity (EV)");
    const bool intensityChanged=ImGui::DragFloat("Intensity (EV)",&state.decomposition.intensityEv,.025f,-32.f,maximumEv,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    include(intensityChanged);
    const auto intensityMinimum=ImGui::GetItemRectMin(),intensityMaximum=ImGui::GetItemRectMax();
    state.intensityMinimum={intensityMinimum.x,intensityMinimum.y};state.intensityMaximum={intensityMaximum.x,intensityMaximum.y};
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Linear brightness multiplier: %.4g\n+1 EV doubles brightness; opacity is independent.",std::exp2(state.decomposition.intensityEv));
    ImGui::TextDisabled("Multiplier: x%.4g",std::exp2(state.decomposition.intensityEv));
    if(opacity) {
        fieldWidth("Opacity");
        include(ImGui::SliderFloat("Opacity",opacity,0.f,1.f,"%.3f",ImGuiSliderFlags_AlwaysClamp));
        const auto opacityMinimum=ImGui::GetItemRectMin(),opacityMaximum=ImGui::GetItemRectMax();
        state.opacityMinimum={opacityMinimum.x,opacityMinimum.y};state.opacityMaximum={opacityMaximum.x,opacityMaximum.y};
    }
    if(colorChanged || intensityChanged) {
        linearRgb=composeHdrColor(state.decomposition,maximum);state.synchronized=linearRgb;
    }
    // A compound color picker may expose its active child only through the
    // enclosing ColorEdit item. Track it before subsequent numeric fields.
    if(!edit.active)state.activeItem=0;
    edit.activated|=!wasActive && edit.active;
    edit.deactivated|=wasActive && !edit.active;
    ImGui::PopID();
    return edit;
}
} // namespace crimson
