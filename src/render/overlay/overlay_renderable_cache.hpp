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
#include <memory>
#include <unordered_map>
#include <vector>

namespace quader {

struct RenderToolPreviewMeshRecord;

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

    void initialize(OverlayProxyFactory &factory,
                    std::uint16_t blend_order_bias = 0u);
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
    /**
     * Reuses transform-preview packed positions to deform retained selection
     * overlays without rebuilding their semantic/projected frame in the UI.
     */
    bool set_transform_position_streams(
        const std::vector<RenderToolPreviewMeshRecord> &meshes);
    /// Drops retained transform topology/bindings when the bound document is
    /// replaced. Normal transform end keeps this topology warm for the next
    /// move/rotate/scale gesture.
    void reset_transform_position_streams();
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

    /// Pins one object's stable vertex mapping and reusable world-position
    /// storage for retained transform-overlay uploads.
    struct TransformPositionSource {
      int object_id = -1;
      std::uint64_t layout_key = 0u;
      std::uint64_t position_revision = 0u;
      std::size_t position_count = 0u;
      QMat4 transform;
      std::shared_ptr<const std::vector<QVec3>> positions;
      std::vector<QVec3> world_positions;
      std::shared_ptr<
          const std::unordered_map<std::uint32_t, std::uint32_t>>
          source_vertex_to_position;
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
        OverlayFrameSignatures transform_base_signatures;
        std::uint64_t transform_base_frame_signature = 0u;
        std::uint64_t transform_layout_signature = 0u;
        std::uint64_t transform_position_signature = 0u;
        std::array<bool, kViewportOverlayLayerCount> transform_layers{};
        bool transform_bindings_valid = false;
        bool transform_active = false;
    };

    void rebuild_slot(OverlaySlot &slot,
                      bool rebuild_camera_independent_layers);
    void begin_transform_stream(OverlaySlot &slot,
                                std::uint64_t layout_signature);
    void resolve_transform_stream_bindings(OverlaySlot &slot);
    void apply_transform_stream(OverlaySlot &slot,
                                std::uint64_t position_signature);
    void restore_transform_base(OverlaySlot &slot, bool rebuild);
    [[nodiscard]] std::uint8_t effective_layer_mask(
        const OverlaySlot &slot, ViewportOverlayLayer layer) const;
    void destroy_slot(OverlaySlot &slot);
    void destroy_group(OverlayProxyGroup &group);

    OverlayProxyFactory* factory_ = nullptr;
    std::uint16_t blend_order_bias_ = 0u;
    std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_;
    std::vector<TransformPositionSource> transform_sources_;
    std::uint64_t transform_layout_signature_ = 0u;
    std::uint64_t transform_position_signature_ = 0u;
    bool transform_stream_active_ = false;
};

} // namespace quader
