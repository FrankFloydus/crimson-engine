#include "ParticleAppearanceEditor.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>

namespace crimson {
void ParticleAppearanceEdit::include(bool itemChanged) {
    changed|=itemChanged;activated|=ImGui::IsItemActivated();
    active|=ImGui::IsItemActive();deactivated|=ImGui::IsItemDeactivated();
}
void ParticleAppearanceEdit::merge(const ParticleAppearanceEdit& other) {
    changed|=other.changed;activated|=other.activated;active|=other.active;deactivated|=other.deactivated;
}
namespace {
uint64_t newKeyId(const std::vector<ParticleCurveKey>& keys) {
    uint64_t id=static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    while(!id || std::any_of(keys.begin(),keys.end(),[&](const auto& key){return key.id==id;}))++id;
    return id;
}
size_t selectedIndex(const ParticleScalarCurve& curve,uint64_t& id) {
    const auto found=std::find_if(curve.keys.begin(),curve.keys.end(),[&](const auto& key){return key.id==id;});
    if(found!=curve.keys.end())return static_cast<size_t>(found-curve.keys.begin());
    id=curve.keys.front().id;return 0;
}
float clampedAge(const ParticleScalarCurve& curve,size_t index,float age) {
    if(!std::isfinite(age))return curve.keys[index].age;
    float lower=0,upper=1;
    if(index>0) {
        lower=curve.keys[index-1].age+MinimumParticleKeySpacing;
        if(lower-curve.keys[index-1].age<MinimumParticleKeySpacing)
            lower=std::nextafter(lower,std::numeric_limits<float>::infinity());
    }
    if(index+1<curve.keys.size()) {
        upper=curve.keys[index+1].age-MinimumParticleKeySpacing;
        if(curve.keys[index+1].age-upper<MinimumParticleKeySpacing)
            upper=std::nextafter(upper,-std::numeric_limits<float>::infinity());
    }
    return lower<=upper?std::clamp(age,lower,upper):curve.keys[index].age;
}
float scalarValue(const ParticleScalarCurve& curve,float age) {
    if(age<=curve.keys.front().age)return curve.keys.front().value;
    if(age>=curve.keys.back().age)return curve.keys.back().value;
    const auto next=std::upper_bound(curve.keys.begin(),curve.keys.end(),age,[](float time,const auto& key){return time<key.age;});
    const auto& previous=*(next-1);
    if(curve.interpolation==ParticleInterpolation::Constant)return previous.value;
    const float fraction=(age-previous.age)/(next->age-previous.age);
    return previous.value+(next->value-previous.value)*fraction;
}
bool addKey(ParticleScalarCurve& curve,ParticleCurveEditorState& state,float age,float value,float minimum,float maximum) {
    if(curve.keys.size()>=MaximumParticleAppearanceKeys || !std::isfinite(age) || !std::isfinite(value))return false;
    age=std::clamp(age,0.f,1.f);value=std::clamp(value,minimum,maximum);
    const auto next=std::lower_bound(curve.keys.begin(),curve.keys.end(),age,[](const auto& key,float time){return key.age<time;});
    if((next!=curve.keys.end() && next->age-age<MinimumParticleKeySpacing) ||
       (next!=curve.keys.begin() && age-(next-1)->age<MinimumParticleKeySpacing))return false;
    const ParticleCurveKey key{newKeyId(curve.keys),age,value};
    curve.keys.insert(next,key);state.selected=key.id;return true;
}
bool addMidpoint(ParticleScalarCurve& curve,ParticleCurveEditorState& state,float minimum,float maximum) {
    float start=0,end=curve.keys.front().age,largest=end;
    for(size_t i=0;i+1<curve.keys.size();++i)if(curve.keys[i+1].age-curve.keys[i].age>largest) {
        start=curve.keys[i].age;end=curve.keys[i+1].age;largest=end-start;
    }
    if(1-curve.keys.back().age>largest){start=curve.keys.back().age;end=1;}
    const float age=(start+end)*.5f;
    return addKey(curve,state,age,scalarValue(curve,age),minimum,maximum);
}
void removeSelected(ParticleScalarCurve& curve,ParticleCurveEditorState& state) {
    if(curve.keys.size()<=2)return;
    const auto index=selectedIndex(curve,state.selected);
    curve.keys.erase(curve.keys.begin()+static_cast<std::ptrdiff_t>(index));
    state.selected=curve.keys[std::min(index,curve.keys.size()-1)].id;state.dragging=0;
}
void resetCurve(ParticleScalarCurve& curve,ParticleCurveEditorState& state,float value) {
    const auto first=curve.keys.front().id,last=curve.keys.back().id;
    curve.keys={{first,0,value},{last,1,value}};curve.interpolation=ParticleInterpolation::Linear;
    state.selected=first;state.dragging=0;state.fitNext=true;
}
struct PlotInputScope {
    ImPlotInputMap saved{ImPlot::GetInputMap()};
    PlotInputScope() {
        auto& input=ImPlot::GetInputMap();input.Pan=ImGuiMouseButton_Middle;input.Fit=ImGuiMouseButton_Middle;
    }
    ~PlotInputScope() { ImPlot::GetInputMap()=saved; }
};
}

ParticleAppearanceEdit drawParticleCurveEditor(ParticleScalarCurve& curve,ParticleCurveEditorState& state,
    float minimum,float maximum,float resetValue,const char* valueLabel) {
    ParticleAppearanceEdit edit;
    if(curve.keys.size()<2)return edit;
    ImGui::PushID("Curve editor");
    selectedIndex(curve,state.selected);
    ImGui::SetNextItemWidth(ImGui::GetFontSize()*7.5f);
    int interpolation=static_cast<int>(curve.interpolation);
    const bool interpolationChanged=ImGui::Combo("##Interpolation",&interpolation,"Linear\0Constant\0");
    edit.include(interpolationChanged);
    if(interpolationChanged)curve.interpolation=static_cast<ParticleInterpolation>(interpolation);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("Interpolation: Linear blends between keys; Constant holds the previous value.");
    ImGui::SameLine();
    if(ImGui::SmallButton("Frame"))state.fitNext=true;
    ImGui::SameLine();
    if(ImGui::SmallButton("..."))ImGui::OpenPopup("Curve actions");
    if(ImGui::BeginPopup("Curve actions")) {
        if(ImGui::MenuItem("Add key",nullptr,false,curve.keys.size()<MaximumParticleAppearanceKeys))edit.changed|=addMidpoint(curve,state,minimum,maximum);
        if(ImGui::MenuItem("Remove key","Delete",false,curve.keys.size()>2)){removeSelected(curve,state);edit.changed=true;}
        if(ImGui::MenuItem("Reset curve")){resetCurve(curve,state,resetValue);edit.changed=true;}
        ImGui::EndPopup();
    }
    bool plotHovered=false,anyHeld=false,anyHovered=false,openContext=false;
    const auto previousDrag=state.dragging;
    const bool fit=state.fitNext;
    double low=curve.keys.front().value,high=low;
    for(const auto& key:curve.keys){low=std::min(low,double(key.value));high=std::max(high,double(key.value));}
    const double usefulSpan=minimum<0?90:maximum>=100000?1:.01;
    const double span=std::max({high-low,std::abs(high)*.5,std::abs(low)*.5,usefulSpan});
    low=std::max(double(minimum),low-span*.25);high=std::min(double(maximum),high+span*.25);
    {
        PlotInputScope inputs;
        const float font=ImGui::GetFontSize();
        ImPlot::PushStyleVar(ImPlotStyleVar_PlotMinSize,ImVec2(font*11,font*10));
        constexpr auto flags=ImPlotFlags_NoTitle|ImPlotFlags_NoLegend|ImPlotFlags_NoMenus|ImPlotFlags_NoBoxSelect;
        const ImGuiID plotId=ImGui::GetID("##Curve");
        if(ImPlot::BeginPlot("##Curve",{-1,font*14},flags)) {
            ImPlot::SetupAxes("Normalized age",valueLabel,ImPlotAxisFlags_NoMenus,ImPlotAxisFlags_NoMenus);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_X1,-.05,1.05);
            ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1,minimum,maximum);
            ImPlot::SetupAxesLimits(-.025,1.025,low,high,fit?ImPlotCond_Always:ImPlotCond_Once);
            ImPlot::SetupAxisFormat(ImAxis_X1,"%.2f");ImPlot::SetupAxisFormat(ImAxis_Y1,"%.3g");
            ImPlot::SetupFinish();state.fitNext=false;
            // ImPlot 1.0's plot chrome captures all mouse buttons even when
            // panning uses MMB. With ImGui 1.92 ownership this can block its
            // own DragPoint tools after hovering empty plot space. Release
            // only this plot's unused LMB capture so handles/insertion own it.
            auto& context=*ImGui::GetCurrentContext();
            if(context.ActiveId==plotId && context.ActiveIdMouseButton==ImGuiMouseButton_Left) {
                ImGui::ClearActiveID();
                ImGui::SetKeyOwner(ImGuiKey_MouseLeft,ImGuiKeyOwner_NoOwner);
                if(context.HoveredId==plotId)ImGui::SetHoveredID(0);
            }
            const auto limits=ImPlot::GetPlotLimits();state.lower=static_cast<float>(limits.Y.Min);state.upper=static_cast<float>(limits.Y.Max);
            const auto zero=ImPlot::PlotToPixels(0,limits.Y.Max),one=ImPlot::PlotToPixels(1,limits.Y.Min);
            state.plotMinimum={zero.x,zero.y};state.plotMaximum={one.x,one.y};
            std::array<double,MaximumParticleAppearanceKeys+2> ages{},values{};size_t count=0;
            ages[count]=0;values[count++]=curve.keys.front().value;
            for(const auto& key:curve.keys){ages[count]=key.age;values[count++]=key.value;}
            ages[count]=1;values[count++]=curve.keys.back().value;
            ImPlotSpec style;style.LineColor=ImGui::GetStyleColorVec4(ImGuiCol_PlotLines);style.LineWeight=2;
            if(curve.interpolation==ParticleInterpolation::Constant)ImPlot::PlotStairs("##Values",ages.data(),values.data(),static_cast<int>(count),style);
            else ImPlot::PlotLine("##Values",ages.data(),values.data(),static_cast<int>(count),style);
            uint64_t hoveredKey=0;
            for(size_t index=0;index<curve.keys.size();++index) {
                auto& key=curve.keys[index];double age=key.age,value=key.value;bool clicked=false,hovered=false,held=false;
                // Pointer IDs are hashed by value, never dereferenced. This
                // preserves all 64 authored bits on Crimson's x64 platform.
                ImGui::PushID(reinterpret_cast<const void*>(static_cast<uintptr_t>(key.id)));
                const auto color=ImGui::GetStyleColorVec4(key.id==state.selected?ImGuiCol_PlotLinesHovered:ImGuiCol_Text);
                const bool moved=ImPlot::DragPoint(0,&age,&value,color,key.id==state.selected?5.f:4.f,
                    ImPlotDragToolFlags_NoFit|ImPlotDragToolFlags_Delayed,&clicked,&hovered,&held);
                ImGui::PopID();
                anyHeld|=held;anyHovered|=hovered;
                if(hovered)hoveredKey=key.id;
                if(held) {
                    state.selected=key.id;state.dragging=key.id;
                    if(previousDrag!=key.id)edit.activated=true;
                    edit.active=true;
                } else if(clicked)state.selected=key.id;
                if(moved && std::isfinite(age) && std::isfinite(value)) {
                    const float at=clampedAge(curve,index,static_cast<float>(age));
                    const float next=static_cast<float>(std::clamp(value,double(minimum),double(maximum)));
                    if(key.age!=at || key.value!=next){key.age=at;key.value=next;edit.changed=true;}
                }
            }
            plotHovered=ImPlot::IsPlotHovered() || anyHovered;
            if(plotHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))state.panning=true;
            if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle))state.panning=false;
            if(plotHovered && !anyHeld && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if(hoveredKey)state.selected=hoveredKey;
                const auto mouse=ImPlot::GetPlotMousePos();state.contextAge=static_cast<float>(mouse.x);state.contextValue=static_cast<float>(mouse.y);
                openContext=true;
            }
            if(plotHovered && !anyHovered && !anyHeld && !ImGui::IsAnyItemActive() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const auto mouse=ImPlot::GetPlotMousePos();
                edit.changed|=addKey(curve,state,static_cast<float>(mouse.x),static_cast<float>(mouse.y),minimum,maximum);
            }
            ImPlot::EndPlot();
        }
        ImPlot::PopStyleVar();
    }
    if(previousDrag && !anyHeld){state.dragging=0;edit.deactivated=true;}
    if(!anyHeld && !ImGui::IsMouseDown(ImGuiMouseButton_Left))state.dragging=0;
    if(openContext)ImGui::OpenPopup("Curve key actions");
    if(ImGui::BeginPopup("Curve key actions")) {
        if(ImGui::MenuItem("Add key",nullptr,false,curve.keys.size()<MaximumParticleAppearanceKeys))
            edit.changed|=addKey(curve,state,state.contextAge,state.contextValue,minimum,maximum);
        if(ImGui::MenuItem("Remove key","Delete",false,curve.keys.size()>2)){removeSelected(curve,state);edit.changed=true;}
        ImGui::EndPopup();
    }
    if(plotHovered && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !anyHeld && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup) && curve.keys.size()>2 && ImGui::IsKeyPressed(ImGuiKey_Delete,false)) {
        removeSelected(curve,state);edit.changed=true;
    }
    if(plotHovered && !anyHeld && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Drag keys. Double-click empty space to add. Right-click or Delete to remove.\nMMB pan, wheel zoom, Frame to fit. Key ages range from 0 to 1.");
    const auto index=selectedIndex(curve,state.selected);
    ImGui::PushItemWidth(std::max(70.f,ImGui::GetContentRegionAvail().x*.5f-ImGui::GetStyle().ItemSpacing.x));
    float age=curve.keys[index].age;
    const bool ageChanged=ImGui::DragFloat("Age##Selected key",&age,.002f,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);edit.include(ageChanged);
    if(ageChanged)curve.keys[index].age=clampedAge(curve,index,age);
    ImGui::PopItemWidth();
    edit.include(ImGui::DragFloat(valueLabel,&curve.keys[index].value,.01f,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp));
    ImGui::PopID();return edit;
}
} // namespace crimson
