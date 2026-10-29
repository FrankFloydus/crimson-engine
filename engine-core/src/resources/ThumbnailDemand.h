#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crimson {
inline std::uint32_t thumbnailPixelEdge(std::uint32_t logicalEdge,float dpiScale) {
    const auto scale=std::isfinite(dpiScale)?std::max(1.f,dpiScale):1.f;
    return static_cast<std::uint32_t>(std::clamp(std::ceil(static_cast<float>(logicalEdge)*scale),16.f,512.f));
}
} // namespace crimson
