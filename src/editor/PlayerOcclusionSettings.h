#pragma once
#include <cmath>

namespace crimson {
struct PlayerOcclusionSettings {
    float minimumOpacity{.12f};
    float fadeStartDistance{4.f},fullFadeDistance{.5f};
    float fadeOutSeconds{.045f},fadeInSeconds{.14f},restoreDelaySeconds{.07f};
    bool operator==(const PlayerOcclusionSettings&)const=default;
};
inline bool validPlayerOcclusionSettings(const PlayerOcclusionSettings& value)noexcept {
    return std::isfinite(value.minimumOpacity)&&value.minimumOpacity>=0&&value.minimumOpacity<=1&&
        std::isfinite(value.fullFadeDistance)&&value.fullFadeDistance>=0&&
        std::isfinite(value.fadeStartDistance)&&value.fadeStartDistance>value.fullFadeDistance&&value.fadeStartDistance<=1000&&
        std::isfinite(value.fadeOutSeconds)&&value.fadeOutSeconds>=0&&value.fadeOutSeconds<=10&&
        std::isfinite(value.fadeInSeconds)&&value.fadeInSeconds>=0&&value.fadeInSeconds<=10&&
        std::isfinite(value.restoreDelaySeconds)&&value.restoreDelaySeconds>=0&&value.restoreDelaySeconds<=10;
}
}
