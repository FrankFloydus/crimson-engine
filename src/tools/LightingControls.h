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
bool drawIndirectEnergyControl(float& energy);
} // namespace crimson
