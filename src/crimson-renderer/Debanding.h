#pragma once

#include "math/Math.h"
#include <algorithm>
#include <cmath>

namespace crimson::debanding {
namespace equations {
inline float min(float a,float b) {return std::min(a,b);}
inline float max(float a,float b) {return std::max(a,b);}
inline float floor(float value) {return std::floor(value);}
#define CC_DEBANDING_CPU
#include "crimson-renderer/shaders/debanding.sh"
#undef CC_DEBANDING_CPU
}

// Reference for the final display-space stage. The shader's input has already
// been tone mapped and encoded once; dither must not enter HDR or IBL inputs.
inline Vec3 apply(const Vec3& encoded,float pixelX,float pixelY,bool enabled) {
    constexpr Vec3 divisors{103,71,97};
    Vec3 result;
    for(std::size_t c=0;c<3;++c)
        result[c]=equations::debandingChannel(encoded[c],equations::debandingNoise(pixelX,pixelY,divisors[c]),enabled?1.f:0.f);
    return result;
}
} // namespace crimson::debanding
