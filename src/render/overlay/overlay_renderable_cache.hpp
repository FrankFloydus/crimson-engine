////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_proxy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

/**
 * Represents an Overlay Renderable Cache Stats value used by the render extraction and Filament resource layer.
 */
struct OverlayRenderableCacheStats {
  std::uint64_t frame_signature = 0;
  std::uint64_t camera_signature = 0;
  std::uint8_t layer_mask = 0;
  std::size_t proxy_count = 0;
};

/**
 * Caches Overlay Renderable Cache derived state for the render extraction and Filament resource layer.
 */
class OverlayRenderableCache {
public:
    static constexpr std::size_t kMaxViewportOverlaySlots = 4u;

    OverlayRenderableCache() = default;
    OverlayRenderableCache(const OverlayRenderableCache&) = delete;
    OverlayRenderableCache& operator=(const OverlayRenderableCache&) = delete;
    OverlayRenderableCache(OverlayRenderableCache&&) = delete;
    OverlayRenderableCache& operator=(OverlayRenderableCache&&) = delete;
    ~OverlayRenderableCache();

    void initialize(OverlayProxyFactory& factory);
    void shutdown();

    bool initialized() const {
        return factory_ != nullptr;
    }

    bool set_overlay_frame(ViewportOverlayFrame frame,
                           ViewportOverlayCamera camera);
    bool set_overlay_frame(std::size_t viewport_index, std::uint8_t layer_mask,
                           ViewportOverlayFrame frame,
                           ViewportOverlayCamera camera);
    [[nodiscard]] bool
    update_overlay_camera(std::size_t viewport_index, std::uint8_t layer_mask,
                          ViewportOverlayCamera camera);
    void clear_slot(std::size_t viewport_index);

    OverlayRenderableCacheStats stats(std::size_t viewport_index) const;
    std::size_t active_proxy_count(std::size_t viewport_index) const;

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
        OverlayFrameSignatures signatures;
        std::uint64_t frame_signature = 0;
        ViewportOverlayCamera camera;
        std::uint64_t camera_signature = 0;
        std::uint8_t layer_mask = 0;
        bool has_frame = false;
        std::array<OverlayProxyGroup, kViewportOverlayLayerCount> proxy_groups;
    };

    void rebuild_slot(OverlaySlot &slot,
                      bool rebuild_camera_independent_layers);
    void destroy_slot(OverlaySlot &slot);
    void destroy_group(OverlayProxyGroup &group);

    OverlayProxyFactory* factory_ = nullptr;
    std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_;
};

} // namespace quader
