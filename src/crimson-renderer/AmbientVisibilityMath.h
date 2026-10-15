#pragma once

#include "LightingSettings.h"

namespace crimson::ambient_visibility {
// This small exact integer is represented numerically in record3.z, not
// bit-cast through a float that could become NaN during driver transport.
inline std::uint32_t packLightRoles(const LightRoles& roles,bool castShadows) {
    return (roles.receiverMask&3u)|((roles.ambientRevealMask&3u)<<2)|
        (roles.ambientReveal?16u:0u)|(castShadows?32u:0u);
}
inline std::uint64_t packedTexelCount(std::uint32_t width,std::uint32_t height) {
    return (std::uint64_t(width)*height+3)/4;
}
} // namespace crimson::ambient_visibility
