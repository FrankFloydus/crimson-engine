#pragma once

#include "assets/ParticleAsset.h"
#include <imgui.h>
#include <exception>
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace crimson {

inline float particleDurationLowerBound(const ParticleEmitterDefinition& emitter) {
    float minimum=MinimumParticleDuration;
    for(const auto& burst:emitter.bursts)
        minimum=std::max(minimum,std::nextafter(burst.time,std::numeric_limits<float>::infinity()));
    return minimum;
}

inline bool drawParticleDuration(ParticleEmitterDefinition& emitter) {
    const auto minimum=particleDurationLowerBound(emitter);
    ImGui::BeginDisabled(minimum>MaximumParticleTime);
    const bool changed=ImGui::DragFloat(emitter.finiteDuration?"Duration (s)":"Cycle Duration (s)",&emitter.duration,.01f,
        std::min(minimum,MaximumParticleTime),MaximumParticleTime,"%.3f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::EndDisabled();return changed;
}

// The document owns this widget's gesture. Legacy emission conversion is only
// attempted after an authored value change, and cannot unwind an ImGui frame.
inline bool drawParticleExplosiveness(ParticleEmitterDefinition& emitter,std::string& diagnostic) {
    float value=emitter.explosiveness;
    if(!ImGui::SliderFloat("Explosiveness",&value,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp))return false;
    try {
        const bool changed=setParticleExplosiveness(emitter,value);
        diagnostic.clear();return changed;
    } catch(const std::exception& error) {diagnostic=error.what();return false;}
}

} // namespace crimson
