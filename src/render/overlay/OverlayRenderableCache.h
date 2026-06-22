////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayProxyFactory.h"
#include "render/overlay/OverlayRenderableProxy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

/**
 * Represents an Overlay Renderable Cache Stats value used by the render extraction and Filament resource layer.
 */
struct OverlayRenderableCacheStats {
    std::uint64_t frameSignature = 0;
    std::uint64_t cameraSignature = 0;
    std::uint8_t layerMask = 0;
    std::size_t proxyCount = 0;
};

/**
 * Caches Overlay Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class OverlayRenderableCache {
public:
    static constexpr std::size_t kMaxViewportOverlaySlots = 4u;

    ~OverlayRenderableCache();

    void initialize(OverlayProxyFactory& factory);
    void shutdown();

    bool initialized() const {
        return factory_ != nullptr;
    }

    bool setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera);
    bool setOverlayFrame(std::size_t viewportIndex,
            std::uint8_t layerMask,
            ViewportOverlayFrame frame,
            ViewportOverlayCamera camera);
    void clearSlot(std::size_t viewportIndex);

    OverlayRenderableCacheStats stats(std::size_t viewportIndex) const;
    std::size_t activeProxyCount(std::size_t viewportIndex) const;

private:
    /**
     * Represents an Overlay Proxy Group value used by the render extraction and Filament resource layer.
     */
    struct OverlayProxyGroup {
        std::uint64_t signature = 0;
        std::vector<OverlayRenderableProxy> proxies;
    };

    /**
     * Represents an Overlay Slot value used by the render extraction and Filament resource layer.
     */
    struct OverlaySlot {
        ViewportOverlayFrame frame;
        std::uint64_t frameSignature = 0;
        ViewportOverlayCamera camera;
        std::uint64_t cameraSignature = 0;
        std::uint8_t layerMask = 0;
        std::array<OverlayProxyGroup, kViewportOverlayLayerCount> proxyGroups;
    };

    void rebuildSlot(OverlaySlot& slot);
    void destroySlot(OverlaySlot& slot);
    void destroyGroup(OverlayProxyGroup& group);

    OverlayProxyFactory* factory_ = nullptr;
    std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_;
};

} // namespace quader
