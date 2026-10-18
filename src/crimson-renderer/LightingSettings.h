#pragma once

#include "math/Math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace crimson {
namespace lighting_scope {
inline constexpr std::uint32_t World=1,Character=2,All=World|Character;
}

// These roles are independent; direct-light exclusion must not remove an
// object's shadow.
struct SurfaceLightingScopes {
    std::uint32_t directReceiverMask{lighting_scope::All};
    std::uint32_t shadowCasterMask{lighting_scope::All};
    bool operator==(const SurfaceLightingScopes&) const = default;
};
struct LightRoles {
    std::uint32_t receiverMask{lighting_scope::All};
    std::uint32_t shadowCasterMask{lighting_scope::All};
    bool operator==(const LightRoles&) const = default;
};
inline bool validLightingMask(std::uint32_t value) noexcept {
    return (value&~lighting_scope::All)==0;
}
inline bool validSurfaceLightingScopes(const SurfaceLightingScopes& value) noexcept {
    return validLightingMask(value.directReceiverMask)&&validLightingMask(value.shadowCasterMask);
}
inline bool validLightRoles(const LightRoles& value) noexcept {
    return validLightingMask(value.receiverMask)&&validLightingMask(value.shadowCasterMask);
}
inline bool validLightColor(const Vec3& color) noexcept {
    return math::finite(color)&&std::all_of(color.begin(),color.end(),[](float v){return v>=0&&v<=1;});
}
inline bool validLightDirection(const Vec3& direction) noexcept {
    return math::finite(direction)&&math::length(direction)>=1e-6&&
        std::all_of(direction.begin(),direction.end(),[](float v){return std::abs(v)<=1000000;});
}
inline bool validLightIntensity(float v) noexcept {return std::isfinite(v)&&v>=0&&v<=10000;}
inline bool validIndirectEnergy(float v) noexcept {return std::isfinite(v)&&v>=0&&v<=16;}
inline bool validLightRange(float v) noexcept {return std::isfinite(v)&&v>=.01f&&v<=100000;}
inline bool validShadowBiases(float depth,float normal) noexcept {
    return std::isfinite(depth)&&depth>=0&&std::isfinite(normal)&&normal>=0;
}
inline bool validPointLightResponse(float nearRadius,float specular,float exponent,float softness,float sourceRadius=0,float attenuation=2) noexcept {
    return std::isfinite(nearRadius)&&nearRadius>=0&&nearRadius<=100000&&
        std::isfinite(specular)&&specular>=0&&specular<=1&&
        std::isfinite(exponent)&&exponent>=.25f&&exponent<=8&&
        std::isfinite(softness)&&softness>=.05f&&softness<=1&&
        std::isfinite(sourceRadius)&&sourceRadius>=0&&sourceRadius<=100000&&
        std::isfinite(attenuation)&&attenuation>=-10&&attenuation<=10;
}
} // namespace crimson
