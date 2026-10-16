#pragma once

#include "LightingSettings.h"
#include <limits>

namespace crimson::ambient_visibility {
// Perspective planes in view space; a reveal has exact finite 8*Spread support.
// A padded sphere test retains offscreen centers whose support reaches the view.
inline bool revealIntersectsView(const Vec3& center,float spread,float tanX,float tanY,float nearPlane,float farPlane) {
    if(!math::finite(center)||!std::isfinite(spread)||spread<=0||!std::isfinite(tanX)||!std::isfinite(tanY)||tanX<=0||tanY<=0)
        return false;
    const double depth=-double(center[2]);
    double radius=double(spread)*8;
    radius+=(std::abs(double(center[0]))+std::abs(double(center[1]))+std::abs(depth)+radius)*
        (16*std::numeric_limits<float>::epsilon())+1e-5;
    if(depth+radius<nearPlane||depth-radius>farPlane)return false;
    return std::abs(double(center[0]))<=depth*tanX+radius*std::sqrt(1.+double(tanX)*tanX)&&
        std::abs(double(center[1]))<=depth*tanY+radius*std::sqrt(1.+double(tanY)*tanY);
}
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
