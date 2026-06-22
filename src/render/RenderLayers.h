////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
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
