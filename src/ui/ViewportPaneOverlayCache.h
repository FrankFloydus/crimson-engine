#pragma once

#include "modeling/ModelingSessionService.h"
#include "render/ViewportOverlayFrame.h"

#include <cstdint>

namespace quader {

struct ViewportPaneOverlayCacheKey {
    std::uint64_t contentRevision = 0;
    std::uint64_t selectionRevision = 0;
    std::uint64_t sceneRevision = 0;
    std::uint64_t assetRevision = 0;
    std::uint64_t overlaySignature = 0;
    ModelingSelectionMode selectionMode = ModelingSelectionMode::Object;
};

class ViewportPaneOverlayCache {
public:
    [[nodiscard]] bool valid() const { return valid_; }
    [[nodiscard]] const ViewportOverlayFrame& frame() const { return frame_; }
    [[nodiscard]] const ViewportPaneOverlayCacheKey& key() const { return key_; }
    [[nodiscard]] bool needsRefresh(const ViewportPaneOverlayCacheKey& key) const;

    void markDirty();
    void update(ViewportOverlayFrame frame, const ViewportPaneOverlayCacheKey& key);

private:
    ViewportOverlayFrame frame_;
    ViewportPaneOverlayCacheKey key_;
    bool valid_ = false;
    bool dirty_ = true;
};

} // namespace quader
