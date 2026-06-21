#pragma once

#include "render/ViewportOverlayFrame.h"
#include "render/overlay/OverlayProxyFactory.h"
#include "render/overlay/OverlayRenderableProxy.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

struct OverlayRenderableCacheStats {
    std::uint64_t frameSignature = 0;
    std::uint64_t cameraSignature = 0;
    std::uint8_t layerMask = 0;
    std::size_t proxyCount = 0;
};

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
    struct OverlayProxyGroup {
        std::uint64_t signature = 0;
        std::vector<OverlayRenderableProxy> proxies;
    };

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
