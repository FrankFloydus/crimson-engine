#pragma once

#include "RenderTypes.h"
#include <array>
#include <cstdint>
#include <span>

namespace crimson::procedural {

inline constexpr std::uint16_t DiffuseSize=16,SpecularSize=128;
inline constexpr std::uint8_t SpecularMipCount=8;
inline constexpr std::uint16_t DiffuseSampleCount=512,SpecularSampleCount=256;
inline constexpr float RoughnessFloor=.045f;
inline constexpr std::uint32_t EvaluationVersion=1;

// Linear HDR uniforms shared by the visible sky and environment convolution.
// Array layout matches procedural_sky.sh. Authoring colors are decoded once.
struct Parameters {
    std::array<Vec4,7> values{};
    bool operator==(const Parameters&) const = default;
};
Parameters parameters(const ProceduralSkySettings&,const ProceduralSun&);
std::uint64_t cacheKey(const Parameters&);
std::uint64_t cacheKey(const ProceduralSkySettings&,const ProceduralSun&);

// CPU reference for the shared shader. Footprint is a solid angle in steradians;
// zero evaluates a point, a positive footprint antialiases the solar disk.
Vec3 radiance(const Parameters&,const Vec3& direction,float footprint=0);

struct FilterKernel {
    std::array<Vec4,DiffuseSampleCount> samples{}; // Tangent direction, source solid angle.
    std::uint16_t count{};
    bool diffuse{};
};
const FilterKernel& diffuseKernel();
const FilterKernel& specularKernel(std::uint8_t mip);
Vec3 filteredRadiance(const Parameters&,const Vec3& normal,const FilterKernel&,float outputSolidAngle=0);

} // namespace crimson::procedural
