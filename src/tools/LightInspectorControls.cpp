#include "LightInspectorControls.h"
#include "PropertyReset.h"
#include "runtime/SceneLights.h"
#include <algorithm>
#include <cstdint>
#include <string>

namespace crimson {
namespace {
using Control=LightInspectorControl;
// Combo item identity is restored so field activation, deactivation and reset
// belong to the selector instead of the last row in its popup.
template<class Select> bool selector(const char* label,const char* preview,Select select) {
    const bool open=ImGui::BeginCombo(label,preview);
    const auto item=ImGui::GetCurrentContext()->LastItemData;
    bool changed=false;
    if(open){changed=select();ImGui::EndCombo();}
    ImGui::GetCurrentContext()->LastItemData=item;
    return changed;
}
bool colorEdit(const char* label,Vec3& color) {
    const auto id=ImGui::GetID(label);
    const bool changed=ImGui::ColorEdit3(label,color.data(),ImGuiColorEditFlags_Float);
    // ColorEdit's EndGroup emits ID zero while idle. Give that group its
    // authored field identity for resets, retaining its native active child ID
    // while a component or the popup picker owns the gesture.
    auto& item=ImGui::GetCurrentContext()->LastItemData;
    if(!item.ID)item.ID=id;
    return changed;
}
bool fixturePreset(LightInspectorValue& value,ImGuiTextFilter& filter) {
    return selector("Light Preset","Choose preset",[&] {
        if(ImGui::IsWindowAppearing()){filter.Clear();ImGui::SetKeyboardFocusHere();}
        filter.Draw("Search##fixture");
        ImGui::BeginChild("##fixtures",{0,ImGui::GetTextLineHeightWithSpacing()*16},ImGuiChildFlags_None);
        bool changed=false;std::string_view category;
        for(const auto& preset:lightFixturePresets()) {
            if(preset.directional!=value.directional)continue;
            const auto search=std::string(preset.category)+" "+std::string(preset.name);
            if(!filter.PassFilter(search.c_str()))continue;
            if(category!=preset.category){category=preset.category;ImGui::SeparatorText(std::string(category).c_str());}
            ImGui::PushID(preset.id.data(),preset.id.data()+preset.id.size());
            if(ImGui::Selectable(std::string(preset.name).c_str())) {
                value.colorMode=preset.colorMode;
                if(preset.colorMode==LightColorMode::Temperature)value.temperatureKelvin=preset.temperatureKelvin;else value.color=preset.color;
                value.intensity=preset.intensity;if(!value.directional)value.radius=preset.radius;
                changed=true;ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();return changed;
    });
}
bool flickerPreset(LightFlickerSettings& value,ImGuiTextFilter& filter) {
    const char* name="Choose preset";
    for(const auto& preset:lightFlickerPresets())if(preset.settings.behavior==value.behavior){name=preset.name.data();break;}
    return selector("Flicker Preset",name,[&] {
        if(ImGui::IsWindowAppearing()){filter.Clear();ImGui::SetKeyboardFocusHere();}
        filter.Draw("Search##flicker");
        ImGui::BeginChild("##behaviors",{0,ImGui::GetTextLineHeightWithSpacing()*16},ImGuiChildFlags_None);
        bool changed=false;std::string_view category;
        for(const auto& preset:lightFlickerPresets()) {
            const auto search=std::string(preset.category)+" "+std::string(preset.name);
            if(!filter.PassFilter(search.c_str()))continue;
            if(category!=preset.category){category=preset.category;ImGui::SeparatorText(std::string(category).c_str());}
            ImGui::PushID(preset.id.data(),preset.id.data()+preset.id.size());
            if(ImGui::Selectable(std::string(preset.name).c_str(),preset.settings.behavior==value.behavior)) {
                changed=applyLightFlickerPreset(value,preset.id);ImGui::CloseCurrentPopup();
            }
            ImGui::PopID();
        }
        ImGui::EndChild();return changed;
    });
}
struct Fields {
    const LightInspectorEdit& edit;
    void number(Control control,const char* label,float& value,float defaults,float low,float high,float speed=.01f)const {
        const auto history=std::string("Change light ")+label;
        edit(control,history.c_str(),resettableProperty(value,defaults,[&]{return ImGui::DragFloat(label,&value,speed,low,high,"%.3f",ImGuiSliderFlags_AlwaysClamp);}));
    }
    void toggle(Control control,const char* label,bool& value,bool defaults)const {
        const auto history=std::string("Toggle light ")+label;
        edit(control,history.c_str(),resettableProperty(value,defaults,[&]{return ImGui::Checkbox(label,&value);}));
    }
    void integer(Control control,const char* label,std::uint32_t& value,std::uint32_t defaults,std::uint32_t low,std::uint32_t high)const {
        const auto history=std::string("Change light ")+label;
        edit(control,history.c_str(),resettableProperty(value,defaults,[&]{return ImGui::DragScalar(label,ImGuiDataType_U32,&value,.1f,&low,&high,"%u",ImGuiSliderFlags_AlwaysClamp);}));
    }
    void range(Control first,Control last,const char* minLabel,const char* maxLabel,float& low,float& high,float defaultLow,float defaultHigh,float lower,float upper)const {
        // A reset must keep the paired value valid even when the current range
        // lies wholly above or below the defaults.
        const auto rangeField=[&](Control control,const char* label,float& target,float defaults,bool minimum) {
            const auto history=std::string("Change light ")+label;
            const bool changed=resettableProperty(target,defaults,[&]{return ImGui::DragFloat(label,&target,.01f,lower,upper,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            if(changed){if(minimum)high=std::max(high,low);else low=std::min(low,high);}
            edit(control,history.c_str(),changed);
        };
        rangeField(first,minLabel,low,defaultLow,true);rangeField(last,maxLabel,high,defaultHigh,false);
    }
};
void channel(const char* name,LightFlickerChannel& value,const LightFlickerChannel& defaults,Control first,const Fields& fields) {
    ImGui::PushID(name);
    const auto control=[&](unsigned offset){return static_cast<Control>(static_cast<unsigned>(first)+offset);};
    if(ImGui::TreeNodeEx(name,ImGuiTreeNodeFlags_DefaultOpen)) {
        fields.toggle(control(0),"Enabled",value.enabled,defaults.enabled);
        ImGui::BeginDisabled(!value.enabled);
        fields.range(control(1),control(2),"Minimum Multiplier","Maximum Multiplier",value.minimum,value.maximum,defaults.minimum,defaults.maximum,0,10);
        fields.toggle(control(3),"Invert",value.invert,defaults.invert);
        fields.number(control(4),"Response",value.response,defaults.response,.05f,10);
        ImGui::EndDisabled();ImGui::TreePop();
    }
    ImGui::PopID();
}
}
void drawLightAppearanceControls(LightInspectorValue& value,LightInspectorState& state,const LightInspectorEdit& edit) {
    edit(Control::FixturePreset,"Apply light preset",fixturePreset(value,state.fixtureFilter));
    edit(Control::ColorMode,"Change light color mode",resettableProperty(value.colorMode,LightColorMode::Color,[&] {
        return selector("Color Mode",value.colorMode==LightColorMode::Temperature?"Temperature":"Color",[&] {
            bool changed=false;
            if(ImGui::Selectable("Color",value.colorMode==LightColorMode::Color)){changed=value.colorMode!=LightColorMode::Color;value.colorMode=LightColorMode::Color;}
            if(ImGui::Selectable("Temperature",value.colorMode==LightColorMode::Temperature)){changed=value.colorMode!=LightColorMode::Temperature;value.colorMode=LightColorMode::Temperature;}
            return changed;
        });
    },"Color"));
    auto displayedColor=resolveLightColorSrgb(value.colorMode,value.color,value.temperatureKelvin);
    ImGui::BeginDisabled(value.colorMode==LightColorMode::Temperature);
    const bool colorChanged=resettableProperty(displayedColor,value.directional?Vec3{1,.96f,.9f}:Vec3{1,1,1},[&]{return colorEdit("Color",displayedColor);});
    if(colorChanged)value.color=displayedColor;
    edit(Control::Color,"Change light color",colorChanged);
    ImGui::EndDisabled();
    if(value.colorMode==LightColorMode::Temperature) {
        Fields{edit}.number(Control::Temperature,"Temperature (K)",value.temperatureKelvin,6500,MinimumLightTemperature,MaximumLightTemperature,10);
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Lower Kelvin is warmer; higher Kelvin is cooler.");
    }
}
void drawLightFlickerControls(LightInspectorValue& light,LightInspectorState& state,const LightInspectorEdit& edit,const LightPreviewContext& preview) {
    if(!ImGui::CollapsingHeader("Flickering"))return;
    if(preview.enabled && preview.paused && preview.restart) {
        ImGui::Checkbox("Preview",preview.enabled);ImGui::SameLine();
        ImGui::BeginDisabled(!*preview.enabled);
        if(ImGui::Button(*preview.paused?"Resume##lightPreview":"Pause##lightPreview"))*preview.paused=!*preview.paused;
        ImGui::SameLine();if(ImGui::Button("Restart##lightPreview"))*preview.restart=true;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Preview controls affect all lights in the scene.");
    }
    auto& value=light.flicker;const LightFlickerSettings defaults;const Fields fields{edit};
    fields.toggle(Control::FlickerEnabled,"Enabled##flicker",value.enabled,defaults.enabled);
    ImGui::BeginDisabled(!value.enabled);
    edit(Control::FlickerPreset,"Apply light flicker preset",flickerPreset(value,state.flickerFilter));
    fields.number(Control::Speed,"Speed",value.speed,defaults.speed,.01f,100);
    edit(Control::Playback,"Change light flicker playback",resettableProperty(value.playback,defaults.playback,[&] {
        auto playback=static_cast<int>(value.playback);const bool changed=ImGui::Combo("Playback",&playback,"Loop\0Once\0");
        if(changed)value.playback=static_cast<LightFlickerPlayback>(playback);return changed;
    },"Loop"));
    fields.number(Control::Duration,"Duration (s)",value.duration,defaults.duration,.01f,3600,.1f);
    fields.number(Control::StartDelay,"Start Delay (s)",value.startDelay,defaults.startDelay,0,3600,.1f);
    fields.number(Control::TimeOffset,"Time Offset (s)",value.timeOffset,defaults.timeOffset,-1000000,1000000,.1f);
    fields.integer(Control::Seed,"Seed",value.seed,defaults.seed,0,UINT32_MAX);
    fields.number(Control::TimingRandomness,"Timing Randomness",value.timingRandomness,defaults.timingRandomness,0,1);
    fields.number(Control::BrightnessRandomness,"Brightness Randomness",value.brightnessRandomness,defaults.brightnessRandomness,0,1);
    if(ImGui::TreeNodeEx("Behavior",ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto behavior=static_cast<unsigned>(value.behavior);
        if(behavior<=static_cast<unsigned>(LightFlickerBehavior::RoaringFire)) {
            fields.number(Control::SlowAmount,"Slow Variation",value.slowAmount,defaults.slowAmount,0,1);
            fields.number(Control::SlowRate,"Slow Rate (Hz)",value.slowRate,defaults.slowRate,0,1000);
            fields.number(Control::FlutterAmount,"Flutter Amount",value.flutterAmount,defaults.flutterAmount,0,1);
            fields.number(Control::FlutterRate,"Flutter Rate (Hz)",value.flutterRate,defaults.flutterRate,0,1000,.1f);
            fields.number(Control::Smoothness,"Smoothness",value.smoothness,defaults.smoothness,0,1);
            fields.number(Control::BrightnessBias,"Brightness Bias",value.brightnessBias,defaults.brightnessBias,-1,1);
        } else if(behavior<=static_cast<unsigned>(LightFlickerBehavior::PowerBrownout)) {
            if(value.behavior!=LightFlickerBehavior::FluorescentStartup || value.playback==LightFlickerPlayback::Loop)
                fields.range(Control::StableMinimum,Control::StableMaximum,"Stable Minimum (s)","Stable Maximum (s)",value.stableMinimum,value.stableMaximum,defaults.stableMinimum,defaults.stableMaximum,0,3600);
            fields.range(Control::FailureMinimum,Control::FailureMaximum,"Failure Minimum (s)","Failure Maximum (s)",value.failureMinimum,value.failureMaximum,defaults.failureMinimum,defaults.failureMaximum,0,3600);
            fields.number(Control::FailureLevel,"Failure Level",value.failureLevel,defaults.failureLevel,0,1);
            fields.number(Control::FallTime,"Fall Time (s)",value.fallTime,defaults.fallTime,0,3600);
            fields.number(Control::RecoveryTime,"Recovery Time (s)",value.recoveryTime,defaults.recoveryTime,0,3600);
        } else if(behavior<=static_cast<unsigned>(LightFlickerBehavior::Heartbeat)) {
            fields.number(Control::Period,"Period (s)",value.period,defaults.period,.001f,3600);
            fields.number(Control::PulseWidth,"Pulse Width",value.pulseWidth,defaults.pulseWidth,0,1);
            fields.number(Control::RiseTime,"Rise Time (s)",value.riseTime,defaults.riseTime,0,3600);
            fields.number(Control::FallTime,"Fall Time (s)",value.fallTime,defaults.fallTime,0,3600);
            if(value.behavior==LightFlickerBehavior::DoubleFlash || value.behavior==LightFlickerBehavior::Heartbeat) {
                fields.number(Control::SecondaryStrength,"Secondary Strength",value.secondaryStrength,defaults.secondaryStrength,0,1);
                fields.number(Control::SecondarySpacing,"Secondary Spacing",value.secondarySpacing,defaults.secondarySpacing,0,1);
            }
        } else {
            fields.number(Control::EventFrequency,"Event Frequency (Hz)",value.eventFrequency,defaults.eventFrequency,0,1000);
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Target burst rate; flash durations and rest limits may reduce it.");
            fields.range(Control::RestMinimum,Control::RestMaximum,"Rest Minimum (s)","Rest Maximum (s)",value.restMinimum,value.restMaximum,defaults.restMinimum,defaults.restMaximum,0,3600);
            fields.number(Control::RiseTime,"Attack (s)",value.riseTime,defaults.riseTime,0,3600);
            fields.number(Control::FallTime,"Decay (s)",value.fallTime,defaults.fallTime,0,3600);
            fields.number(Control::Afterglow,"Afterglow",value.afterglow,defaults.afterglow,0,1);
        }
        if((behavior>=static_cast<unsigned>(LightFlickerBehavior::UnstableIncandescent) && behavior<=static_cast<unsigned>(LightFlickerBehavior::PowerBrownout)) ||
           behavior>=static_cast<unsigned>(LightFlickerBehavior::Lightning)) {
            const auto integerRange=[&](Control control,const char* label,std::uint32_t& target,std::uint32_t fallback,bool minimum) {
                const bool changed=resettableProperty(target,fallback,[&]{const std::uint32_t low=1,high=64;return ImGui::DragScalar(label,ImGuiDataType_U32,&target,.1f,&low,&high,"%u",ImGuiSliderFlags_AlwaysClamp);});
                if(changed){if(minimum)value.flashesMaximum=std::max(value.flashesMaximum,value.flashesMinimum);else value.flashesMinimum=std::min(value.flashesMinimum,value.flashesMaximum);}
                edit(control,"Change light flashes per burst",changed);
            };
            integerRange(Control::FlashesMinimum,"Flashes Minimum",value.flashesMinimum,defaults.flashesMinimum,true);
            integerRange(Control::FlashesMaximum,"Flashes Maximum",value.flashesMaximum,defaults.flashesMaximum,false);
            fields.range(Control::FlashMinimum,Control::FlashMaximum,"Flash Minimum (s)","Flash Maximum (s)",value.flashMinimum,value.flashMaximum,defaults.flashMinimum,defaults.flashMaximum,0,3600);
            fields.range(Control::GapMinimum,Control::GapMaximum,"Gap Minimum (s)","Gap Maximum (s)",value.gapMinimum,value.gapMaximum,defaults.gapMinimum,defaults.gapMaximum,0,3600);
        }
        ImGui::TreePop();
    }
    channel("Intensity",value.intensity,defaults.intensity,Control::IntensityEnabled,fields);
    ImGui::PushID("FlickerColor");
    if(ImGui::TreeNodeEx("Color / Temperature",ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& color=value.color;const auto& fallback=defaults.color;
        fields.toggle(Control::ColorEnabled,"Enabled",color.enabled,fallback.enabled);ImGui::BeginDisabled(!color.enabled);
        if(light.colorMode==LightColorMode::Temperature) {
            fields.range(Control::ColorMinimumTemperature,Control::ColorMaximumTemperature,"Minimum Shift (K)","Maximum Shift (K)",color.minimumTemperatureShift,color.maximumTemperatureShift,fallback.minimumTemperatureShift,fallback.maximumTemperatureShift,-14000,14000);
        } else {
            edit(Control::ColorMinimumTint,"Change light minimum tint",resettableProperty(color.minimumTint,fallback.minimumTint,[&]{return colorEdit("Minimum Tint",color.minimumTint);}));
            edit(Control::ColorMaximumTint,"Change light maximum tint",resettableProperty(color.maximumTint,fallback.maximumTint,[&]{return colorEdit("Maximum Tint",color.maximumTint);}));
        }
        fields.toggle(Control::ColorInvert,"Invert",color.invert,fallback.invert);
        fields.number(Control::ColorResponse,"Response",color.response,fallback.response,.05f,10);
        fields.number(Control::ColorAmount,"Amount",color.amount,fallback.amount,0,1);
        ImGui::EndDisabled();ImGui::TreePop();
    }
    ImGui::PopID();
    if(!light.directional)channel("Radius",value.radius,defaults.radius,Control::RadiusEnabled,fields);
    ImGui::EndDisabled();
}
}
