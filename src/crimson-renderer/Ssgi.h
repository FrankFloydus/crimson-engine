#pragma once

#include <cmath>

namespace crimson {

enum class SsgiQuality { Low, Medium, High, Custom };
struct SsgiSettings {
    bool enabled{false};
    float strength{1.f},maxDistance{3.f},thickness{.1f},temporalStability{.85f};
    SsgiQuality quality{SsgiQuality::Medium};
    int bounces{1};
    int rays{4},steps{16};
    float rayBias{.02f};
    int denoiseRadius{3};
    float denoiseStrength{1.f},radianceClamp{10.f};
    bool operator==(const SsgiSettings&) const = default;
};

inline bool validSsgiSettings(const SsgiSettings& value) {
    return std::isfinite(value.strength) && value.strength>=0 && value.strength<=4 &&
        std::isfinite(value.maxDistance) && value.maxDistance>=.1f && value.maxDistance<=20 &&
        std::isfinite(value.thickness) && value.thickness>=.01f && value.thickness<=2 &&
        std::isfinite(value.temporalStability) && value.temporalStability>=0 && value.temporalStability<=.98f &&
        (value.quality==SsgiQuality::Low || value.quality==SsgiQuality::Medium || value.quality==SsgiQuality::High || value.quality==SsgiQuality::Custom) &&
        value.bounces>=1 && value.bounces<=4 && value.rays>=1 && value.rays<=16 && value.steps>=8 && value.steps<=64 &&
        std::isfinite(value.rayBias) && value.rayBias>=.001f && value.rayBias<=.5f && value.denoiseRadius>=0 && value.denoiseRadius<=4 &&
        std::isfinite(value.denoiseStrength) && value.denoiseStrength>=0 && value.denoiseStrength<=1 &&
        std::isfinite(value.radianceClamp) && value.radianceClamp>=0 && value.radianceClamp<=100;
}
inline bool effectiveSsgi(const SsgiSettings& value) { return value.enabled && value.strength>0; }

struct SsgiQualityBudget { unsigned rays,steps; };
inline constexpr SsgiQualityBudget ssgiQualityBudget(SsgiQuality quality) {
    switch(quality) {
    case SsgiQuality::Low:return {2,12};
    case SsgiQuality::High:return {8,24};
    default:return {4,16};
    }
}
inline constexpr SsgiQualityBudget ssgiQualityBudget(const SsgiSettings& settings) {
    return settings.quality==SsgiQuality::Custom?SsgiQualityBudget{unsigned(settings.rays),unsigned(settings.steps)}:ssgiQualityBudget(settings.quality);
}

} // namespace crimson
