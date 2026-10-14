#pragma once

#include "math/Math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace crimson {

enum class ParticleBlendMode { Alpha, Additive };

// Copied presentation values. RGB is linear radiance with intensity applied;
// alpha is opacity. Size is the full square billboard width in world meters.
struct RenderParticle {
    Vec3 position{};
    float size{.08f};
    Vec4 color{1,1,1,1};
    std::uint32_t materialIndex{}; // Index into this render frame's prepared material table.
};

struct ParticleRenderStats {
    std::size_t requested{}, submitted{}, invalid{}, dropped{};
    std::size_t drawCalls{};
};

inline constexpr std::size_t MaxParticleDrawCount = 65536;
inline constexpr std::size_t MaxParticleMaterialCount = 512;
inline constexpr std::size_t MaxParticleDrawCalls = 1024;
inline constexpr float MaxParticleRadiance = 100000.f; // Matches the authored intensity limit.

namespace particle_rendering {
inline bool valid(const RenderParticle& particle) {
    return math::finite(particle.position) && math::finite(particle.color) &&
        std::isfinite(particle.size) && particle.size > 0 && particle.size <= 1e6f &&
        std::all_of(particle.position.values.begin(),particle.position.values.end(),[](float value){return std::abs(value)<=1e9f;}) &&
        std::all_of(particle.color.values.begin(),particle.color.values.begin()+3,[](float value){return value>=0 && value<=MaxParticleRadiance;}) &&
        particle.color[3]>=0 && particle.color[3]<=1;
}

// The editor matte is display-linear and is composited after scene tone mapping.
inline Vec3 backgroundLinear(const Vec3& srgb) {
    Vec3 result;
    for (std::size_t channel=0;channel<3;++channel) {
        const auto value=std::clamp(std::isfinite(srgb[channel]) ? srgb[channel] : 0.f,0.f,1.f);
        const auto linear=value<=.04045f ? value/12.92f : std::pow((value+.055f)/1.055f,2.4f);
        result[channel]=linear;
    }
    return result;
}

struct SortKey { std::size_t index{}; double depth{}; };
inline double viewDepth(const Vec3& position,const Vec3& eye,const Vec3& forward) {
    double depth{};
    for(std::size_t axis=0;axis<3;++axis) depth+=(double(position[axis])-eye[axis])*forward[axis];
    return depth;
}
// Stable ties preserve extraction order without moving authoritative CPU state.
// Camera-space center ordering is an approximation for intersecting quads.
inline void sortBackToFront(std::vector<SortKey>& keys) {
    std::stable_sort(keys.begin(),keys.end(),[](const auto& a,const auto& b){return a.depth>b.depth;});
}
} // namespace particle_rendering
} // namespace crimson
