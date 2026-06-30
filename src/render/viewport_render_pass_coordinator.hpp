////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_layers.hpp"
#include "viewport/viewport_settings.hpp"

#include <cstdint>

namespace filament {
class RenderTarget;
class Renderer;
class View;
} // namespace filament

namespace quader {

/**
 * Owns Viewport Render Pass Coordinator behavior within the render extraction and Filament resource layer.
 */
class ViewportRenderPassCoordinator {
public:
    void initialize(std::uint8_t gridLayerMask);

    void setBackgroundColor(ViewportSettingColor color);

    bool setGridVisible(bool visible);

    bool setOverlayVisible(bool visible);

    bool setSceneVisible(bool visible);

    bool setSceneLayerMask(std::uint8_t layerMask);

    std::uint8_t sceneVisibleLayerMask() const;
    std::uint8_t overlayVisibleLayerMask() const;
    std::uint8_t visibleLayerMask() const;

    void applyVisibleLayers(filament::View* sceneView, filament::View* overlayView) const;
    void render(filament::Renderer& renderer,
            filament::View* sceneView,
            filament::View* overlayView,
            bool overlayUsesSeparateTarget) const;

private:
    bool groundGridVisible_ = true;
    bool overlayVisible_ = false;
    bool sceneVisible_ = true;
    std::uint8_t sceneLayerMask_ = kViewportShadedSceneLayerMask;
    ViewportSettingColor backgroundColor_ = {0.0f, 0.0f, 0.0f, 1.0f};
    std::uint8_t gridLayerMask_ = 0x04u;
};

} // namespace quader
