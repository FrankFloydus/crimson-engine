#pragma once

#include "math/Math.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crimson::ssgi {
// Geometry alpha uses an exactly representable 23-bit integer: two validity /
// reactive bits and three seven-bit linear diffuse-response channels. Keeping
// this in the existing RGBA32F attachment avoids another full-resolution target.
inline constexpr std::uint32_t MaximumPackedSurface=(1u<<23)-1;
inline std::uint32_t packSourceSurface(const Vec3& response,bool reactive) {
    std::uint32_t packed=reactive?2u:1u;
    for(unsigned channel=0;channel<3;++channel) {
        const auto value=std::isfinite(response[channel])?std::clamp(response[channel],0.f,1.f):0.f;
        const auto quantized=static_cast<std::uint32_t>(std::floor(value*127.f+.5f));
        packed|=quantized<<(2+channel*7);
    }
    return packed;
}
inline std::uint32_t sourceSurfaceBits(float packed) {
    return std::isfinite(packed)&&packed>=1&&packed<=MaximumPackedSurface?static_cast<std::uint32_t>(packed+.5f):0;
}
inline unsigned sourceSurfaceFlags(float packed) {return sourceSurfaceBits(packed)&3u;}
inline Vec3 sourceDiffuseResponse(float packed) {
    const auto bits=sourceSurfaceBits(packed);Vec3 result;
    for(unsigned channel=0;channel<3;++channel)result[channel]=float((bits>>(2+channel*7))&127u)/127.f;
    return result;
}
}
