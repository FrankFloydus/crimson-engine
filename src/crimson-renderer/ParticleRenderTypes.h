#pragma once

#include "math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace crimson {

// Copied presentation values. RGB is linear radiance with intensity applied;
// alpha is opacity. Size is the full square billboard width in world meters.
struct RenderParticle {
    Vec3 position{};
    float size{.08f};
    Vec4 color{1,1,1,1};
};

struct ParticleRenderStats {
    std::size_t requested{}, submitted{}, invalid{}, dropped{};
};

inline constexpr std::size_t MaxParticleDrawCount = 65536;
inline constexpr float MaxParticleRadiance = 100000.f; // Matches the authored intensity limit.

namespace particle_rendering {
inline bool valid(const RenderParticle& particle) {
    return math::finite(particle.position) && math::finite(particle.color) &&
        std::isfinite(particle.size) && particle.size > 0 && particle.size <= 1e6f &&
        std::all_of(particle.position.values.begin(),particle.position.values.end(),[](float value){return std::abs(value)<=1e9f;}) &&
        std::all_of(particle.color.values.begin(),particle.color.values.begin()+3,[](float value){return value>=0 && value<=MaxParticleRadiance;}) &&
        particle.color[3]>=0 && particle.color[3]<=1;
}

// This is the editor matte's inverse Reinhard value, not scene lighting.
// Its finite white endpoint still encodes to 8-bit white after tone mapping.
inline Vec3 backgroundRadiance(const Vec3& srgb) {
    Vec3 result;
    for (std::size_t channel=0;channel<3;++channel) {
        const auto value=std::clamp(std::isfinite(srgb[channel]) ? srgb[channel] : 0.f,0.f,1.f);
        const auto linear=value<=.04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);
        const auto bounded=std::min(linear,1.f-1e-6f);
        result[channel]=bounded/(1.f-bounded);
    }
    return result;
}
} // namespace particle_rendering
} // namespace crimson
