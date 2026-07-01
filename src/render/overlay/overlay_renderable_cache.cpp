////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"

#include <utility>

namespace quader {

OverlayRenderableCache::~OverlayRenderableCache() {
    shutdown();
}

void OverlayRenderableCache::initialize(OverlayProxyFactory& factory) {
    factory_ = &factory;
}

void OverlayRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    for (OverlaySlot& slot : slots_) {
        destroySlot(slot);
    }
    factory_ = nullptr;
}

bool OverlayRenderableCache::setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
    return setOverlayFrame(0u, kViewportOverlayLayerMask, std::move(frame), camera);
}

bool OverlayRenderableCache::setOverlayFrame(
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayFrame frame,
        ViewportOverlayCamera camera) {
    QDR_PROFILE_FUNCTION();
    if (viewportIndex >= slots_.size()) {
        return false;
    }

    if (layerMask == 0u) {
        layerMask = kViewportOverlayLayerMask;
    }

    OverlaySlot& slot = slots_[viewportIndex];
    const OverlayFrameSignatures signatures = OverlayRenderableBuilder::frameSignatures(frame);
    const std::uint64_t signature = signatures.frameSignature;
    camera = OverlayCameraProjector::sanitizedCamera(camera);
    const std::uint64_t cameraSignature = OverlayRenderableBuilder::cameraSignature(camera);
    const bool frameChanged = signature != slot.frameSignature;
    const bool cameraChanged = cameraSignature != slot.cameraSignature;
    const bool layerChanged = layerMask != slot.layerMask;
    if (!frameChanged && !cameraChanged && !layerChanged) {
        slot.frame = std::move(frame);
        return false;
    }

    slot.frame = std::move(frame);
    slot.frameSignature = signature;
    if (frameChanged) {
        slot.signatures = signatures;
    }
    if (cameraChanged || layerChanged) {
        slot.camera = camera;
        slot.cameraSignature = cameraSignature;
        slot.layerMask = layerMask;
    }
    if (layerChanged) {
        for (OverlayProxyGroup& group : slot.proxyGroups) {
            group.signature = 0;
        }
    }

    if (factory_ && factory_->initialized()) {
        rebuildSlot(slot, frameChanged || layerChanged);
    }
    return true;
}

void OverlayRenderableCache::clearSlot(std::size_t viewportIndex) {
    if (viewportIndex >= slots_.size()) {
        return;
    }
    destroySlot(slots_[viewportIndex]);
}

OverlayRenderableCacheStats OverlayRenderableCache::stats(std::size_t viewportIndex) const {
    if (viewportIndex >= slots_.size()) {
        return {};
    }

    const OverlaySlot& slot = slots_[viewportIndex];
    return {
            .frameSignature = slot.frameSignature,
            .cameraSignature = slot.cameraSignature,
            .layerMask = slot.layerMask,
            .proxyCount = activeProxyCount(viewportIndex),
    };
}

std::size_t OverlayRenderableCache::activeProxyCount(std::size_t viewportIndex) const {
    if (viewportIndex >= slots_.size()) {
        return 0;
    }

    std::size_t count = 0;
    for (const OverlayProxyGroup& group : slots_[viewportIndex].proxyGroups) {
        count += group.proxies.size();
    }
    return count;
}

void OverlayRenderableCache::rebuildSlot(OverlaySlot& slot, bool rebuildCameraIndependentLayers) {
    QDR_PROFILE_FUNCTION();
    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
        QDR_PROFILE_SCOPE("Overlay rebuild layer");
        if (!rebuildCameraIndependentLayers && !OverlayRenderableBuilder::layerBuildDependsOnCamera(layer)) {
            continue;
        }
        const std::uint64_t layerSignature =
                OverlayRenderableBuilder::layerBuildSignature(slot.signatures, slot.camera, layer);
        OverlayProxyGroup& group = slot.proxyGroups[static_cast<std::size_t>(layer)];
        if (group.signature == layerSignature) {
            continue;
        }

        if (!OverlayRenderableBuilder::layerHasRenderablePrimitives(slot.signatures, layer)) {
            destroyGroup(group);
            group.signature = layerSignature;
            continue;
        }

        const OverlayLayerBuildPlan plan =
                OverlayRenderableBuilder::buildLayer(slot.frame, slot.camera, layer, &slot.signatures);
        if (!factory_->updateLayerProxies(group.proxies, plan, slot.layerMask)) {
            destroyGroup(group);
            group.proxies = factory_->createLayerProxies(plan, slot.layerMask);
        }
        group.signature = layerSignature;
    }
}

void OverlayRenderableCache::destroySlot(OverlaySlot& slot) {
    for (OverlayProxyGroup& group : slot.proxyGroups) {
        destroyGroup(group);
    }
    slot.frame = {};
    slot.signatures = OverlayRenderableBuilder::frameSignatures(slot.frame);
    slot.frameSignature = slot.signatures.frameSignature;
    slot.camera = {};
    slot.cameraSignature = 0;
    slot.layerMask = 0;
}

void OverlayRenderableCache::destroyGroup(OverlayProxyGroup& group) {
    if (factory_) {
        for (OverlayRenderableProxy& proxy : group.proxies) {
            factory_->destroy(proxy);
        }
    }
    group.proxies.clear();
    group.signature = 0;
}

} // namespace quader
