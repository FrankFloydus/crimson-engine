#pragma once
#include "scene/LightAuthoring.h"
#include <imgui.h>
#include <functional>

namespace crimson {
struct LightPreviewContext;
// Each field has its own transaction identity, including paired range endpoints.
enum class LightInspectorControl : unsigned {
    FixturePreset,ColorMode,Color,Temperature,
    FlickerEnabled,FlickerPreset,Speed,Playback,Duration,StartDelay,TimeOffset,Seed,TimingRandomness,BrightnessRandomness,
    SlowAmount,SlowRate,FlutterAmount,FlutterRate,Smoothness,BrightnessBias,
    StableMinimum,StableMaximum,FailureMinimum,FailureMaximum,FailureLevel,FlashesMinimum,FlashesMaximum,
    FlashMinimum,FlashMaximum,GapMinimum,GapMaximum,RestMinimum,RestMaximum,EventFrequency,
    RiseTime,FallTime,RecoveryTime,Afterglow,Period,PulseWidth,SecondaryStrength,SecondarySpacing,
    IntensityEnabled,IntensityMinimum,IntensityMaximum,IntensityInvert,IntensityResponse,
    ColorEnabled,ColorMinimumTint,ColorMaximumTint,ColorMinimumTemperature,ColorMaximumTemperature,ColorInvert,ColorResponse,ColorAmount,
    RadiusEnabled,RadiusMinimum,RadiusMaximum,RadiusInvert,RadiusResponse,Count
};
struct LightInspectorState { ImGuiTextFilter fixtureFilter,flickerFilter; };
struct LightInspectorValue {
    bool directional{};
    LightColorMode colorMode{LightColorMode::Color};
    Vec3 color{1,1,1};
    float temperatureKelvin{6500},intensity{35},radius{20};
    LightFlickerSettings flicker;
};
using LightInspectorEdit=std::function<void(LightInspectorControl,const char*,bool)>;
void drawLightAppearanceControls(LightInspectorValue&,LightInspectorState&,const LightInspectorEdit&);
void drawLightFlickerControls(LightInspectorValue&,LightInspectorState&,const LightInspectorEdit&,const LightPreviewContext&);
}
