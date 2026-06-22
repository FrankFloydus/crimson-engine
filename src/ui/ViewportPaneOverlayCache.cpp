#include "ui/ViewportPaneOverlayCache.h"

#include <utility>

namespace quader {

bool ViewportPaneOverlayCache::needsRefresh(const ViewportPaneOverlayCacheKey& key) const {
    return dirty_ ||
            !valid_ ||
            key_.contentRevision != key.contentRevision ||
            key_.selectionRevision != key.selectionRevision ||
            key_.sceneRevision != key.sceneRevision ||
            key_.assetRevision != key.assetRevision ||
            key_.overlaySignature != key.overlaySignature ||
            key_.selectionMode != key.selectionMode;
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
