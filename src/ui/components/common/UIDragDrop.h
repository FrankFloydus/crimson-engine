////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/UIGeometry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace quader {

inline constexpr float kUIDragDropDefaultStartThresholdPx = 4.0f;

/// Identifies the lifecycle phase of the active drag/drop gesture.
enum class UIDragDropPhase {
    Idle,
    Candidate,
    Dragging,
};

/// Identifies the UI-domain item being dragged.
enum class UIDragDropItemKind {
    DockPanel,
    OutlinerEntity,
    Asset,
    ViewportItem,
};

/// Identifies the UI-domain surface under the pointer.
enum class UIDropZoneKind {
    DockSurface,
    DockSidebar,
    DockPanel,
    OutlinerRow,
    AssetFolder,
    ViewportFrame,
};

/// Identifies the operation a drop target offers for the active payload.
enum class UIDragDropOperation {
    Move,
    Copy,
    Link,
};

/// Identifies where the payload would be placed relative to the target.
enum class UIDropPlacement {
    On,
    Before,
    After,
};

/// Identifies how a measured target rectangle maps pointer position to placement.
enum class UIDropPlacementPolicy {
    WholeRect,
    VerticalBeforeAfter,
    HorizontalBeforeAfter,
};

/// Identifies the feedback style a presenter should render for the target.
enum class UIDropFeedbackKind {
    None,
    Insertion,
    Target,
    Edge,
};

/// Identifies one measured drop zone without encoding application semantics in the generic layer.
struct UIDropZoneId {
    std::uint32_t value = 0;

    friend bool operator==(UIDropZoneId lhs, UIDropZoneId rhs) = default;
    friend bool operator!=(UIDropZoneId lhs, UIDropZoneId rhs) { return !(lhs == rhs); }
};

/// Carries the backend-neutral identity of the item being dragged.
struct UIDragDropItem {
    UIDragDropItemKind kind = UIDragDropItemKind::DockPanel;
    std::string stableId;
    std::vector<std::string> itemIds;
};

/// Stores one measured surface that can start a UI drag gesture.
struct UIDragSourceRegion {
    std::string sourceId;
    UIDragDropItem item;
    UIPixelRect rect;
    int priority = 0;
};

/// Stores one measured surface that can accept a compatible UI drag payload.
struct UIDropZoneRegion {
    UIDropZoneId id;
    UIDropZoneKind kind = UIDropZoneKind::DockPanel;
    UIPixelRect rect;
    UIDropPlacementPolicy placementPolicy = UIDropPlacementPolicy::WholeRect;
    std::vector<UIDragDropItemKind> accepts;
    std::optional<UIDragDropItem> representedItem;
    bool rejectSameItem = true;
    UIDragDropOperation operation = UIDragDropOperation::Move;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Target;
    int priority = 0;
};

/// Describes the current accepted drop target under the pointer.
struct UIDragDropTarget {
    UIDropZoneId zoneId;
    UIDropZoneKind kind = UIDropZoneKind::DockPanel;
    UIPixelRect rect;
    UIDragDropOperation operation = UIDragDropOperation::Move;
    UIDropPlacement placement = UIDropPlacement::On;
    UIDropFeedbackKind feedbackKind = UIDropFeedbackKind::Target;
    UIPixelPoint pointer;
};

/// Carries a completed UI drag/drop operation.
struct UIDragDropResult {
    UIDragDropItem item;
    UIDragDropTarget target;
};

} // namespace quader
