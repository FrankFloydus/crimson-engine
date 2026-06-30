////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_render_pass_coordinator.hpp"

#include "render/render_layers.hpp"

#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/View.h>

namespace quader {

void ViewportRenderPassCoordinator::initialize(std::uint8_t gridLayerMask) {
    gridLayerMask_ = gridLayerMask == 0u ? viewportGridLayerMask(0) : gridLayerMask;
}

void ViewportRenderPassCoordinator::setBackgroundColor(ViewportSettingColor color) {
    backgroundColor_ = color;
}

bool ViewportRenderPassCoordinator::setGridVisible(bool visible) {
    if (groundGridVisible_ == visible) {
        return false;
    }
    groundGridVisible_ = visible;
    return true;
}

bool ViewportRenderPassCoordinator::setOverlayVisible(bool visible) {
    if (overlayVisible_ == visible) {
        return false;
    }
    overlayVisible_ = visible;
    return true;
}

bool ViewportRenderPassCoordinator::setSceneVisible(bool visible) {
    if (sceneVisible_ == visible) {
        return false;
    }
    sceneVisible_ = visible;
    return true;
}

bool ViewportRenderPassCoordinator::setSceneLayerMask(std::uint8_t layerMask) {
    if (sceneLayerMask_ == layerMask) {
        return false;
    }
    sceneLayerMask_ = layerMask;
    return true;
}

std::uint8_t ViewportRenderPassCoordinator::sceneVisibleLayerMask() const {
    return sceneVisible_
            ? static_cast<std::uint8_t>(sceneLayerMask_ | kViewportToolPreviewLayerMask)
            : 0u;
}

std::uint8_t ViewportRenderPassCoordinator::overlayVisibleLayerMask() const {
    if (!groundGridVisible_ && !overlayVisible_) {
        return 0u;
    }
    std::uint8_t layerMask = gridLayerMask_;
    if (sceneVisible_) {
        layerMask = static_cast<std::uint8_t>(layerMask | kViewportOverlayLayerMask);
    }
    return layerMask;
}

std::uint8_t ViewportRenderPassCoordinator::visibleLayerMask() const {
    return static_cast<std::uint8_t>(sceneVisibleLayerMask() | overlayVisibleLayerMask());
}

void ViewportRenderPassCoordinator::applyVisibleLayers(filament::View* sceneView, filament::View* overlayView) const {
    if (sceneView) {
        sceneView->setVisibleLayers(kViewportAllLayerMask, sceneVisibleLayerMask());
    }
    if (overlayView) {
        overlayView->setVisibleLayers(kViewportAllLayerMask, overlayVisibleLayerMask());
    }
}

void ViewportRenderPassCoordinator::render(
        filament::Renderer& renderer,
        filament::View* sceneView,
        filament::View* overlayView,
        bool overlayUsesSeparateTarget) const {
    if (!sceneView) {
        return;
    }

    filament::Renderer::ClearOptions clear;
    clear.clear = true;
    clear.discard = false;
    clear.clearColor = {backgroundColor_.r, backgroundColor_.g, backgroundColor_.b, backgroundColor_.a};
    renderer.setClearOptions(clear);
    renderer.render(sceneView);

    if (!overlayView || (overlayVisibleLayerMask() == 0u && !overlayUsesSeparateTarget)) {
        return;
    }

    filament::Renderer::ClearOptions overlayClear;
    overlayClear.clear = overlayUsesSeparateTarget;
    overlayClear.discard = false;
    overlayClear.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
    renderer.setClearOptions(overlayClear);
    renderer.render(overlayView);
    renderer.setClearOptions(clear);
}

} // namespace quader
