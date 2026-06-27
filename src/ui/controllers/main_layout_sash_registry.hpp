////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/resizable_split.hpp"
#include "ui/components/common/ui_geometry.hpp"
#include "ui/components/organisms/main_layout_model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class MainLayoutController;

/// Captures measured split-axis geometry used to build main-layout sash constraints.
struct MainLayoutSashMeasuredLayout {
    int mainWidthDp = 0;
    int mainHeightDp = 0;
    int sideDockHeightDp = 0;
    int bottomDockTrackWidthDp = 0;
};

/// Captures one measured bottom-panel split pane.
struct MainLayoutBottomSplitPaneMeasurement {
    std::string panelId;
    int widthDp = 0;
    int availableWidthDp = 0;
    int trackWidthDp = 0;
    std::size_t splitIndex = 0;
    std::size_t splitCount = 0;
};

/// Owns the measured main-layout sash registrations and resize policy.
class MainLayoutSashRegistry {
public:
    /// Classifies a registered sash by the layout policy that consumes its drag result.
    enum class SashKind {
        SideDockWidth,
        SidePanelStack,
        BottomDockHeight,
        BottomPanelSplit,
    };

    /// Identifies the active sash in presentation terms.
    struct ActiveSash {
        SashKind kind = SashKind::SideDockWidth;
        DockLocation location = DockLocation::Sidebar;
        std::string panelId;
    };

    void clear();
    void setMeasuredLayout(MainLayoutSashMeasuredLayout measured);

    void addSideDockWidthSash(
            const MainLayoutModel& model,
            DockLocation location,
            MainLayoutSidebarSide side,
            UIPixelRect hitRect);
    void addSidePanelStackSash(
            const MainLayoutModel& model,
            UIPixelRect hitRect);
    void addBottomDockHeightSash(
            const MainLayoutModel& model,
            UIPixelRect hitRect);
    void addBottomPanelSplitSash(
            const MainLayoutModel& model,
            MainLayoutBottomSplitPaneMeasurement measurement,
            UIPixelRect hitRect);

    [[nodiscard]] std::vector<ResizableSplitHitRegion> hitRegions() const;
    [[nodiscard]] std::optional<ResizableSplitHandle> handle(std::string_view handleId) const;
    [[nodiscard]] std::optional<ActiveSash> activeSash(std::string_view handleId) const;

    [[nodiscard]] bool applyResize(
            MainLayoutModel& model,
            MainLayoutController& controller,
            const ResizableSplitResizeResult& resize) const;
    [[nodiscard]] bool normalizeMeasuredBounds(
            MainLayoutModel& model,
            MainLayoutController& controller) const;

    [[nodiscard]] static std::string sideDockWidthHandleId(DockLocation location);
    [[nodiscard]] static std::string sidePanelStackHandleId();
    [[nodiscard]] static std::string bottomDockHeightHandleId();
    [[nodiscard]] static std::string bottomPanelSplitHandleId(std::string_view panelId);
    [[nodiscard]] static std::optional<std::string_view> bottomPanelSplitPanelId(std::string_view handleId);

private:
    /// Stores the hit target, split handle constraints, and owner-specific metadata for one sash.
    struct Registration {
        SashKind kind = SashKind::SideDockWidth;
        DockLocation location = DockLocation::Sidebar;
        std::string panelId;
        ResizableSplitHitRegion hitRegion;
        ResizableSplitHandle handle;
        std::optional<MainLayoutBottomSplitPaneMeasurement> bottomSplitMeasurement;
    };

    void addRegistration(Registration registration);
    [[nodiscard]] const Registration* registrationForHandle(std::string_view handleId) const;

    MainLayoutSashMeasuredLayout measured_;
    std::vector<Registration> registrations_;
};

} // namespace quader
