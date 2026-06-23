////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/ViewportLayout.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace quader {

inline constexpr std::size_t kMaxViewportComponents = 4u;
inline constexpr std::string_view kMainViewportStableId = "main";
inline constexpr std::string_view kMainViewportTextureSource = "quader://viewport/main";

/**
 * Enumerates viewport component placement surfaces used by the UI presentation layer.
 */
enum class ViewportComponentSurface {
    Embedded,
    Window,
};

/**
 * Describes one UI viewport surface independently from the active UI backend.
 */
struct ViewportComponent {
    std::string stableId;
    std::string textureSource;
    ViewportPaneId pane;
    std::uint8_t layerIndex = 0u;
    ViewportComponentSurface surface = ViewportComponentSurface::Embedded;
};

inline ViewportComponent mainViewportComponent() {
    return {
            .stableId = std::string(kMainViewportStableId),
            .textureSource = std::string(kMainViewportTextureSource),
            .pane = ViewportPaneId::Perspective,
            .layerIndex = 0u,
            .surface = ViewportComponentSurface::Embedded,
    };
}

inline ViewportComponent windowedViewportComponent(std::uint8_t layerIndex) {
    return {
            .stableId = "viewport-" + std::to_string(static_cast<int>(layerIndex) + 1),
            .textureSource = {},
            .pane = ViewportPaneId::Perspective,
            .layerIndex = layerIndex,
            .surface = ViewportComponentSurface::Window,
    };
}

} // namespace quader
