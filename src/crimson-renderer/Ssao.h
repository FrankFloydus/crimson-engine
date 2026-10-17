#pragma once
#include <cmath>

namespace crimson {
enum class SsaoQuality { Low,Medium,High };
struct SsaoSettings {
    bool enabled{false};
    float strength{1.f},radius{.5f};
    SsaoQuality quality{SsaoQuality::Medium};
    bool operator==(const SsaoSettings&) const = default;
};
inline bool validSsaoSettings(const SsaoSettings& value) {
    return std::isfinite(value.strength)&&value.strength>=0&&value.strength<=2&&
        std::isfinite(value.radius)&&value.radius>=.05f&&value.radius<=5&&
        (value.quality==SsaoQuality::Low||value.quality==SsaoQuality::Medium||value.quality==SsaoQuality::High);
}
inline bool effectiveSsao(const SsaoSettings& value) {return value.enabled&&value.strength>0;}
} // namespace crimson
