#include "ParticleAppearanceEditor.h"
#include "PropertyReset.h"
#include <imgui.h>

namespace crimson {
ParticleAppearanceEdit drawParticleGradientEditor(ParticleColorGradient& gradient,ParticleGradientEditorState& state,const Vec4& resetColor) {
    ParticleAppearanceEdit edit;
    ImGui::PushID("Particle gradient");
    int interpolation=gradient.interpolation==ParticleInterpolation::Linear?0:1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize()*9);
    const bool changed=resettableProperty(interpolation,static_cast<int>(ParticleColorGradient{}.interpolation),[&]{return ImGui::Combo("Interpolation",&interpolation,"Linear\0Constant\0");},"Linear");
    edit.include(changed);
    if(changed)gradient.interpolation=interpolation==0?ParticleInterpolation::Linear:ParticleInterpolation::Constant;
    ColorGradientValue value;
    value.interpolation=static_cast<ColorGradientInterpolation>(gradient.interpolation);
    for(const auto& stop:gradient.stops)value.stops.push_back({stop.id,stop.age,stop.color});
    ColorGradientEditorOptions options;
    options.defaults.stops={{1,0,resetColor},{2,1,resetColor}};
    options.maximumStops=MaximumParticleAppearanceKeys;options.minimumSpacing=MinimumParticleKeySpacing;options.maximumColor=MaximumParticleColor;
    const auto result=drawColorGradientEditor(value,state,options);
    edit.changed|=result.changed;edit.activated|=result.activated;edit.active|=result.active;edit.deactivated|=result.deactivated;
    if(result.changed) {
        gradient.interpolation=static_cast<ParticleInterpolation>(value.interpolation);
        gradient.stops.clear();for(const auto& stop:value.stops)gradient.stops.push_back({stop.id,stop.position,stop.color});
    }
    ImGui::PopID();return edit;
}
} // namespace crimson
