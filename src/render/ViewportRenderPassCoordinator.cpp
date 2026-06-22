////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/ViewportRenderPassCoordinator.h"

#include "render/RenderLayers.h"

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

std::uint8_t ViewportRenderPassCoordinator::visibleLayerMask() const {
    std::uint8_t visibleLayers = sceneVisible_ ? kViewportSceneLayerMask : 0u;
    if (groundGridVisible_ || overlayVisible_) {
        visibleLayers |= gridLayerMask_;
    }
    return visibleLayers;
}

void ViewportRenderPassCoordinator::applyVisibleLayers(filament::View* view) const {
    if (!view) {
        return;
    }
    view->setVisibleLayers(kViewportAllLayerMask, visibleLayerMask());
}

void ViewportRenderPassCoordinator::render(filament::Renderer& renderer, filament::View* view, filament::RenderTarget* renderTarget) const {
    if (!view || !renderTarget) {
        return;
    }

    filament::Renderer::ClearOptions clear;
    clear.clear = true;
    clear.discard = true;
    clear.clearColor = {backgroundColor_.r, backgroundColor_.g, backgroundColor_.b, backgroundColor_.a};
    renderer.setClearOptions(clear);
    renderer.render(view);
}

} // namespace quader
