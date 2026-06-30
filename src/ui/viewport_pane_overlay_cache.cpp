////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_pane_overlay_cache.hpp"

#include <utility>

namespace quader {

bool ViewportPaneOverlayCache::needsRefresh(const ViewportPaneOverlayCacheKey& key) const {
    return refreshReason(key) != ViewportPaneOverlayCacheRefreshReason::None;
}

ViewportPaneOverlayCacheRefreshReason ViewportPaneOverlayCache::refreshReason(
        const ViewportPaneOverlayCacheKey& key) const {
    if (dirty_) {
        return ViewportPaneOverlayCacheRefreshReason::Dirty;
    }
    if (!valid_) {
        return ViewportPaneOverlayCacheRefreshReason::Invalid;
    }
    if (key_.contentRevision != key.contentRevision) {
        return ViewportPaneOverlayCacheRefreshReason::ContentRevision;
    }
    if (key_.selectionRevision != key.selectionRevision) {
        return ViewportPaneOverlayCacheRefreshReason::SelectionRevision;
    }
    if (key_.sceneRevision != key.sceneRevision) {
        return ViewportPaneOverlayCacheRefreshReason::SceneRevision;
    }
    if (key_.assetRevision != key.assetRevision) {
        return ViewportPaneOverlayCacheRefreshReason::AssetRevision;
    }
    if (key_.overlaySignature != key.overlaySignature) {
        return ViewportPaneOverlayCacheRefreshReason::OverlaySignature;
    }
    if (key_.selectionMode != key.selectionMode) {
        return ViewportPaneOverlayCacheRefreshReason::SelectionMode;
    }
    return ViewportPaneOverlayCacheRefreshReason::None;
}

void ViewportPaneOverlayCache::markDirty() {
    dirty_ = true;
}

void ViewportPaneOverlayCache::update(ViewportOverlayFrame frame, const ViewportPaneOverlayCacheKey& key) {
    frame_ = std::move(frame);
    key_ = key;
    valid_ = true;
    dirty_ = false;
}

} // namespace quader
