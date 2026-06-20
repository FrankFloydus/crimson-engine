#pragma once

#include <cstddef>
#include <cstdint>

namespace quader {

constexpr std::uint8_t kViewportAllLayerMask = 0xffu;
constexpr std::uint8_t kViewportSceneLayerMask = 0x01u;
constexpr std::uint8_t kViewportOverlayLayerMask = 0x02u;
constexpr std::uint8_t kViewportGridLayerBaseMask = 0x04u;

inline std::uint8_t viewportGridLayerMask(std::size_t viewportIndex) {
    return static_cast<std::uint8_t>(kViewportGridLayerBaseMask << viewportIndex);
}

} // namespace quader
