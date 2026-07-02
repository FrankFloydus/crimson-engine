////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/viewport_pane_overlay_cache.hpp"

#include <utility>

namespace quader {

bool ViewportPaneOverlayCache::needs_refresh(
    const ViewportPaneOverlayCacheKey &key) const {
  return refresh_reason(key) != ViewportPaneOverlayCacheRefreshReason::None;
}

ViewportPaneOverlayCacheRefreshReason ViewportPaneOverlayCache::refresh_reason(
    const ViewportPaneOverlayCacheKey &key) const {
  if (dirty_) {
    return ViewportPaneOverlayCacheRefreshReason::Dirty;
  }
  if (!valid_) {
    return ViewportPaneOverlayCacheRefreshReason::Invalid;
  }
  if (key_.content_revision != key.content_revision) {
    return ViewportPaneOverlayCacheRefreshReason::ContentRevision;
  }
  if (key_.selection_revision != key.selection_revision) {
    return ViewportPaneOverlayCacheRefreshReason::SelectionRevision;
  }
  if (key_.scene_revision != key.scene_revision) {
    return ViewportPaneOverlayCacheRefreshReason::SceneRevision;
  }
  if (key_.asset_revision != key.asset_revision) {
    return ViewportPaneOverlayCacheRefreshReason::AssetRevision;
  }
  if (key_.overlay_signature != key.overlay_signature) {
    return ViewportPaneOverlayCacheRefreshReason::OverlaySignature;
  }
  if (key_.selection_mode != key.selection_mode) {
    return ViewportPaneOverlayCacheRefreshReason::SelectionMode;
  }
  return ViewportPaneOverlayCacheRefreshReason::None;
}

void ViewportPaneOverlayCache::mark_dirty() { dirty_ = true; }

void ViewportPaneOverlayCache::update(ViewportOverlayFrame frame, const ViewportPaneOverlayCacheKey& key) {
    frame_ = std::move(frame);
    key_ = key;
    valid_ = true;
    dirty_ = false;
}

} // namespace quader
