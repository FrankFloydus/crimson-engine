#pragma once

#include <cmath>

namespace crimson {

// A view's post-processing settings, independent of environment illumination.
// Disabled by default so older scenes retain their authored appearance.
struct GlowSettings {
    bool enabled{false};
    float strength{0.8f};
    float radius{1.0f};
    float threshold{1.0f};
    bool operator==(const GlowSettings&) const = default;
};

inline bool validGlowSettings(const GlowSettings& glow) {
    return std::isfinite(glow.strength) && glow.strength>=0 && glow.strength<=10 &&
        std::isfinite(glow.radius) && glow.radius>=0 && glow.radius<=8 &&
        std::isfinite(glow.threshold) && glow.threshold>=0 && glow.threshold<=100000;
}

} // namespace crimson
