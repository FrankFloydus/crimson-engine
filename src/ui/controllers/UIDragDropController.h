////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/UIDragDrop.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Tracks one active UI drag gesture from measured source regions to measured drop targets.
 */
class UIDragDropController {
public:
    void setStartThresholdPx(float thresholdPx);
    void setSources(std::vector<UIDragSourceRegion> sources);
    void setDropZones(std::vector<UIDropZoneRegion> zones);
    void clearSources();
    void clearDropZones();

    [[nodiscard]] bool beginCandidate(UIPixelPoint pointer);
    [[nodiscard]] std::optional<UIDragDropTarget> update(UIPixelPoint pointer);
    [[nodiscard]] std::optional<UIDragDropResult> end(UIPixelPoint pointer);
    void cancel();

    [[nodiscard]] UIDragDropPhase phase() const { return phase_; }
    [[nodiscard]] bool active() const { return phase_ != UIDragDropPhase::Idle; }
    [[nodiscard]] bool dragging() const { return phase_ == UIDragDropPhase::Dragging; }
    [[nodiscard]] std::string_view activeSourceId() const { return activeSourceId_; }
    [[nodiscard]] const std::optional<UIDragDropItem>& activeItem() const { return activeItem_; }
    [[nodiscard]] std::optional<UIDragDropTarget> target() const { return target_; }

private:
    [[nodiscard]] std::optional<UIDragSourceRegion> sourceAt(float x, float y) const;
    [[nodiscard]] std::optional<UIDropZoneRegion> zoneAt(UIPixelPoint pointer) const;
    [[nodiscard]] UIDragDropTarget targetFromZone(
            const UIDropZoneRegion& zone,
            UIPixelPoint pointer) const;
    [[nodiscard]] static bool itemAcceptedByZone(
            const UIDragDropItem& item,
            const UIDropZoneRegion& zone);
    [[nodiscard]] static bool sameItem(
            const UIDragDropItem& lhs,
            const UIDragDropItem& rhs);
    [[nodiscard]] static UIDropPlacement placementForZone(
            const UIDropZoneRegion& zone,
            UIPixelPoint pointer);
    [[nodiscard]] static bool contains(const UIPixelRect& rect, float x, float y);
    [[nodiscard]] static float distanceSquared(float fromX, float fromY, float toX, float toY);

    std::vector<UIDragSourceRegion> sources_;
    std::vector<UIDropZoneRegion> dropZones_;
    std::string activeSourceId_;
    std::optional<UIDragDropItem> activeItem_;
    UIPixelPoint startPointer_;
    std::optional<UIDragDropTarget> target_;
    float startThresholdPx_ = kUIDragDropDefaultStartThresholdPx;
    UIDragDropPhase phase_ = UIDragDropPhase::Idle;
};

} // namespace quader
