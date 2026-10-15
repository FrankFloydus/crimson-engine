#pragma once

#include "crimson-renderer/LightingSettings.h"
#include <cstdint>
#include <functional>

namespace crimson {
struct AmbientLightSettings;
enum class AmbientLightControl { Source, Color, SkyContribution, Energy };
void drawAmbientLightControls(AmbientLightSettings&,
    const std::function<void(AmbientLightControl,bool)>& edit);
bool drawLightingMaskControl(const char* label,std::uint32_t& mask);
enum class AmbientVisibilityControl { Enabled,Strength,DarknessFloor,Threshold,Transition,ShadowAware,Emission };
void drawAmbientVisibilityControls(AmbientVisibilitySettings&,
    const std::function<void(AmbientVisibilityControl,bool)>& edit);

// Widget identity is mapped into the Character Inspector's existing property
// transaction owner. These controls never own a document or a separate panel.
enum class PlayerLightRigControl {
    Enabled,ShowRig,
    KeyEnabled,KeyDirection,KeyColor,KeyIntensity,KeyDiffuse,KeySpecular,
    FillEnabled,FillDirection,FillColor,FillIntensity,FillDiffuse,FillSpecular,
    WorldEnabled,WorldHeight,WorldColor,WorldIntensity,WorldShadows,WorldDepthBias,WorldNormalBias,
    WorldRadius,WorldNearFalloff,WorldSpecular,WorldFalloffExponent,WorldEdgeSoftness,WorldSourceRadius,WorldAttenuation,
    ShadowEnabled,ShadowStrength,ShadowDistance,ShadowDepthBias,ShadowNormalBias
};
void drawPlayerLightRigControls(PlayerLightRigSettings&,
    const std::function<void(PlayerLightRigControl,const char*,bool)>& edit);
} // namespace crimson
