////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>

namespace quader {

/**
 * Stores the Viewport Pane Overlay Cache Key data contract used by the ImGui editor presentation layer.
 */
struct ViewportPaneOverlayCacheKey {
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t overlaySignature = 0;
    ModelingSelectionMode selectionMode = ModelingSelectionMode::Object;
};

/**
 * Describes why the viewport pane overlay cache needs to rebuild.
 */
enum class ViewportPaneOverlayCacheRefreshReason {
    None,
    Dirty,
    Invalid,
    ContentRevision,
    SelectionRevision,
    SceneRevision,
    AssetRevision,
    OverlaySignature,
    SelectionMode,
};

/**
 * Caches Viewport Pane Overlay Cache derived state for the ImGui editor presentation layer.
 */
class ViewportPaneOverlayCache {
public:
    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const ViewportOverlayFrame& frame() const { return frame_; }
    [[nodiscard]] const ViewportPaneOverlayCacheKey& key() const { return key_; }
    [[nodiscard]] bool needsRefresh(const ViewportPaneOverlayCacheKey& key) const;
    [[nodiscard]] ViewportPaneOverlayCacheRefreshReason refreshReason(
            const ViewportPaneOverlayCacheKey& key) const;

    void markDirty();
    void update(ViewportOverlayFrame frame, const ViewportPaneOverlayCacheKey& key);

private:
    ViewportOverlayFrame frame_;
    ViewportPaneOverlayCacheKey key_;
    bool valid_ = false;
    bool dirty_ = true;
};

} // namespace quader
