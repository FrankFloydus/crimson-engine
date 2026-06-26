////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/MainLayoutSashRegistry.h"

#include "ui/controllers/MainLayoutController.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace quader {
namespace {

constexpr std::string_view kSidebarWidthHandleId = "sidebar";
constexpr std::string_view kAuxiliarySidebarWidthHandleId = "auxiliary-sidebar";
constexpr std::string_view kSidePanelStackHandleId = "panels";
constexpr std::string_view kBottomDockHeightHandleId = "bottom-dock";
constexpr std::string_view kBottomPanelSplitHandlePrefix = "bottom-split:";

bool startsWith(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

bool sideDockVisibleForLocation(const MainLayoutModel& model, DockLocation location) {
    if (!mainLayoutLocationIsSideDock(location) || mainLayoutSideDockCollapsed(model, location)) {
        return false;
    }
    return mainLayoutVisibleDockPanelCount(model, location) > 0;
}

int visibleSideDockPanelCountMax(const MainLayoutModel& model) {
    return std::max(
            mainLayoutVisibleDockPanelCount(model, DockLocation::Sidebar),
            mainLayoutVisibleDockPanelCount(model, DockLocation::AuxiliaryBar));
}

int otherVisibleSideDockWidthDp(const MainLayoutModel& model, DockLocation location) {
    const DockLocation otherLocation = location == DockLocation::AuxiliaryBar ?
            DockLocation::Sidebar :
            DockLocation::AuxiliaryBar;
    return sideDockVisibleForLocation(model, otherLocation) ?
            mainLayoutSideDockWidthDp(model, otherLocation) :
            0;
}

bool bottomDockVisible(const MainLayoutModel& model) {
    return !mainLayoutBottomDockCollapsed(model) &&
            mainLayoutVisibleDockPanelCount(model, DockLocation::Panel) > 0;
}

int bottomSplitRemainingPaneCount(std::size_t splitIndex, std::size_t splitCount) {
    return splitCount > splitIndex + 1 ?
            static_cast<int>(splitCount - splitIndex - 1) :
            0;
}

int bottomSplitEffectiveAvailableWidthDp(
        int availableWidthDp,
        int trackWidthDp,
        std::size_t splitIndex,
        std::size_t splitCount) {
    const int remainingPaneCount = bottomSplitRemainingPaneCount(splitIndex, splitCount);
    const int minimumAvailableWidthDp =
            (remainingPaneCount + 1) * kMainLayoutBottomSplitPanelMinWidthDp;
    const int trackAvailableWidthDp = splitIndex == 0 ? trackWidthDp : availableWidthDp;
    return std::max(minimumAvailableWidthDp, std::max(availableWidthDp, trackAvailableWidthDp));
}

int bottomSplitMaxLeadingWidthDp(
        int availableWidthDp,
        int trackWidthDp,
        std::size_t splitIndex,
        std::size_t splitCount) {
    return std::max(
            kMainLayoutBottomSplitPanelMinWidthDp,
            bottomSplitEffectiveAvailableWidthDp(availableWidthDp, trackWidthDp, splitIndex, splitCount) -
                    bottomSplitRemainingPaneCount(splitIndex, splitCount) * kMainLayoutBottomSplitPanelMinWidthDp);
}

int bottomSplitDefaultLeadingWidthDp(
        int availableWidthDp,
        int trackWidthDp,
        std::size_t splitIndex,
        std::size_t splitCount) {
    const int paneCountFromHandle = bottomSplitRemainingPaneCount(splitIndex, splitCount) + 1;
    const int fairWidthDp = paneCountFromHandle > 0 ?
            bottomSplitEffectiveAvailableWidthDp(availableWidthDp, trackWidthDp, splitIndex, splitCount) /
                    paneCountFromHandle :
            kMainLayoutBottomSplitPanelMinWidthDp;
    return std::clamp(
            fairWidthDp,
            kMainLayoutBottomSplitPanelMinWidthDp,
            bottomSplitMaxLeadingWidthDp(availableWidthDp, trackWidthDp, splitIndex, splitCount));
}

int bottomSplitCurrentLeadingWidthDp(
        int measuredWidthDp,
        int availableWidthDp,
        int trackWidthDp,
        std::size_t splitIndex,
        std::size_t splitCount,
        std::optional<int> rememberedSizeDp) {
    const int maxWidthDp = bottomSplitMaxLeadingWidthDp(availableWidthDp, trackWidthDp, splitIndex, splitCount);
    if (rememberedSizeDp) {
        return std::clamp(*rememberedSizeDp, kMainLayoutBottomSplitPanelMinWidthDp, maxWidthDp);
    }
    if (measuredWidthDp > kMainLayoutBottomSplitPanelMinWidthDp && measuredWidthDp <= maxWidthDp) {
        return measuredWidthDp;
    }
    return bottomSplitDefaultLeadingWidthDp(availableWidthDp, trackWidthDp, splitIndex, splitCount);
}

} // namespace

void MainLayoutSashRegistry::clear() {
    measured_ = {};
    registrations_.clear();
}

void MainLayoutSashRegistry::setMeasuredLayout(MainLayoutSashMeasuredLayout measured) {
    measured_ = measured;
}

void MainLayoutSashRegistry::addSideDockWidthSash(
        const MainLayoutModel& model,
        DockLocation location,
        MainLayoutSidebarSide side,
        UIPixelRect hitRect) {
    if (!mainLayoutLocationIsSideDock(location)) {
        return;
    }

    const std::string handleId = sideDockWidthHandleId(location);
    addRegistration({
            .kind = SashKind::SideDockWidth,
            .location = location,
            .hitRegion = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Horizontal,
                    .rect = hitRect,
            },
            .handle = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = side == MainLayoutSidebarSide::Right ?
                            ResizableSplitGrowthDirection::Negative :
                            ResizableSplitGrowthDirection::Positive,
                    .sizeDp = mainLayoutSideDockCollapsed(model, location) ?
                            0 :
                            mainLayoutSideDockWidthDp(model, location),
                    .minSize = ResizableSplitSizeRule::dp(kMainLayoutSidebarMinWidthDp),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(
                            kMainLayoutViewportMinSizeDp + otherVisibleSideDockWidthDp(model, location)),
                    .containerSizeDp = measured_.mainWidthDp,
                    .collapse = {
                            .enabled = true,
                            .collapsedSizeDp = 0,
                    },
            },
    });
}

void MainLayoutSashRegistry::addSidePanelStackSash(
        const MainLayoutModel& model,
        UIPixelRect hitRect) {
    const std::optional<DockLocation> location = mainLayoutSidePanelStackLocation(model);
    if (!location) {
        return;
    }
    const std::optional<std::string_view> leadingPanel =
            mainLayoutLeadingSidePanelStackPanelId(model, *location);
    if (!leadingPanel) {
        return;
    }
    const std::string handleId = sidePanelStackHandleId();
    addRegistration({
            .kind = SashKind::SidePanelStack,
            .location = *location,
            .panelId = std::string(*leadingPanel),
            .hitRegion = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Vertical,
                    .rect = hitRect,
            },
            .handle = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Vertical,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = mainLayoutSidePanelStackHeightDp(model, *location),
                    .minSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMinHeightDp),
                    .maxSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMaxHeightDp),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMinHeightDp),
                    .containerSizeDp = measured_.sideDockHeightDp,
            },
    });
}

void MainLayoutSashRegistry::addBottomDockHeightSash(
        const MainLayoutModel& model,
        UIPixelRect hitRect) {
    const std::string handleId = bottomDockHeightHandleId();
    addRegistration({
            .kind = SashKind::BottomDockHeight,
            .location = DockLocation::Panel,
            .hitRegion = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Vertical,
                    .rect = hitRect,
            },
            .handle = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Vertical,
                    .growth = ResizableSplitGrowthDirection::Negative,
                    .sizeDp = mainLayoutBottomDockCollapsed(model) ?
                            0 :
                            mainLayoutBottomDockHeightDp(model),
                    .minSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMinHeightDp),
                    .maxSize = ResizableSplitSizeRule::dp(kMainLayoutPanelMaxHeightDp),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(kMainLayoutViewportMinSizeDp),
                    .containerSizeDp = measured_.mainHeightDp,
                    .collapse = {
                            .enabled = true,
                            .collapsedSizeDp = 0,
                    },
            },
    });
}

void MainLayoutSashRegistry::addBottomPanelSplitSash(
        const MainLayoutModel& model,
        MainLayoutBottomSplitPaneMeasurement measurement,
        UIPixelRect hitRect) {
    if (measurement.splitIndex + 1 >= measurement.splitCount) {
        return;
    }

    const std::optional<int> rememberedSizeDp =
            dockSplitPanelSizeDp(model.dockLayout, DockLocation::Panel, measurement.panelId);
    const int sizeDp = bottomSplitCurrentLeadingWidthDp(
            measurement.widthDp,
            measurement.availableWidthDp,
            measurement.trackWidthDp,
            measurement.splitIndex,
            measurement.splitCount,
            rememberedSizeDp);
    const int remainingPaneCount =
            bottomSplitRemainingPaneCount(measurement.splitIndex, measurement.splitCount);
    const int oppositeMinSizeDp = remainingPaneCount * kMainLayoutBottomSplitPanelMinWidthDp;
    const int availableWidthDp = bottomSplitEffectiveAvailableWidthDp(
            measurement.availableWidthDp,
            measurement.trackWidthDp,
            measurement.splitIndex,
            measurement.splitCount);
    const std::string handleId = bottomPanelSplitHandleId(measurement.panelId);

    addRegistration({
            .kind = SashKind::BottomPanelSplit,
            .location = DockLocation::Panel,
            .panelId = measurement.panelId,
            .hitRegion = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Horizontal,
                    .rect = hitRect,
            },
            .handle = {
                    .id = handleId,
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = sizeDp,
                    .minSize = ResizableSplitSizeRule::dp(kMainLayoutBottomSplitPanelMinWidthDp),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(oppositeMinSizeDp),
                    .containerSizeDp = availableWidthDp,
            },
            .bottomSplitMeasurement = measurement,
    });
}

std::vector<ResizableSplitHitRegion> MainLayoutSashRegistry::hitRegions() const {
    std::vector<ResizableSplitHitRegion> result;
    result.reserve(registrations_.size());
    for (const Registration& registration : registrations_) {
        result.push_back(registration.hitRegion);
    }
    return result;
}

std::optional<ResizableSplitHandle> MainLayoutSashRegistry::handle(std::string_view handleId) const {
    const Registration* registration = registrationForHandle(handleId);
    if (registration == nullptr) {
        return std::nullopt;
    }
    return registration->handle;
}

std::optional<MainLayoutSashRegistry::ActiveSash> MainLayoutSashRegistry::activeSash(
        std::string_view handleId) const {
    const Registration* registration = registrationForHandle(handleId);
    if (registration == nullptr) {
        return std::nullopt;
    }
    return ActiveSash{
            .kind = registration->kind,
            .location = registration->location,
            .panelId = registration->panelId,
    };
}

bool MainLayoutSashRegistry::applyResize(
        MainLayoutModel& model,
        MainLayoutController& controller,
        const ResizableSplitResizeResult& resize) const {
    const Registration* registration = registrationForHandle(resize.id);
    if (registration == nullptr) {
        return false;
    }

    switch (registration->kind) {
    case SashKind::SideDockWidth: {
        const int restoredWidthDp = mainLayoutSideDockCollapsed(model, registration->location) ?
                std::max(resize.sizeDp, mainLayoutSideDockWidthDp(model, registration->location)) :
                resize.sizeDp;
        return resize.collapsed ?
                controller.setSidebarCollapsed(registration->location, true) :
                controller.resizeSidebar(registration->location, restoredWidthDp);
    }
    case SashKind::SidePanelStack:
        return controller.resizeSidePanelStack(registration->location, registration->panelId, resize.sizeDp);
    case SashKind::BottomDockHeight: {
        const int restoredHeightDp = mainLayoutBottomDockCollapsed(model) ?
                std::max(resize.sizeDp, mainLayoutBottomDockHeightDp(model)) :
                resize.sizeDp;
        return resize.collapsed ?
                controller.setBottomDockCollapsed(true) :
                controller.resizeBottomDock(restoredHeightDp);
    }
    case SashKind::BottomPanelSplit:
        return controller.resizeBottomSplitPanel(registration->panelId, resize.sizeDp);
    }

    return false;
}

bool MainLayoutSashRegistry::normalizeMeasuredBounds(
        MainLayoutModel& model,
        MainLayoutController& controller) const {
    bool changed = false;

    if (measured_.mainWidthDp > 0) {
        const int auxiliaryWidthForPrimaryClamp = sideDockVisibleForLocation(model, DockLocation::AuxiliaryBar) ?
                mainLayoutSideDockWidthDp(model, DockLocation::AuxiliaryBar) :
                0;
        const int measuredMaxSidebarWidthDp = std::max(
                measured_.mainWidthDp - kMainLayoutViewportMinSizeDp - auxiliaryWidthForPrimaryClamp,
                kMainLayoutSidebarMinWidthDp);
        if (model.sidebar.widthDp > measuredMaxSidebarWidthDp) {
            model.sidebar.widthDp = measuredMaxSidebarWidthDp;
            changed = true;
        }

        const int primaryWidthForAuxiliaryClamp = sideDockVisibleForLocation(model, DockLocation::Sidebar) ?
                mainLayoutSideDockWidthDp(model, DockLocation::Sidebar) :
                0;
        const int measuredMaxAuxiliarySidebarWidthDp = std::max(
                measured_.mainWidthDp - kMainLayoutViewportMinSizeDp - primaryWidthForAuxiliaryClamp,
                kMainLayoutSidebarMinWidthDp);
        if (model.auxiliarySidebar.widthDp > measuredMaxAuxiliarySidebarWidthDp) {
            model.auxiliarySidebar.widthDp = measuredMaxAuxiliarySidebarWidthDp;
            changed = true;
        }
    }

    if (measured_.sideDockHeightDp > 0 && visibleSideDockPanelCountMax(model) > 1) {
        const int measuredMaxSideStackHeightDp = std::clamp(
                measured_.sideDockHeightDp - kMainLayoutPanelMinHeightDp - kResizableSplitDefaultHitThicknessDp,
                kMainLayoutPanelMinHeightDp,
                kMainLayoutPanelMaxHeightDp);
        for (DockLocation location : {DockLocation::Sidebar, DockLocation::AuxiliaryBar}) {
            const std::optional<std::string_view> leadingPanel =
                    mainLayoutLeadingSidePanelStackPanelId(model, location);
            if (!leadingPanel) {
                continue;
            }
            if (mainLayoutSidePanelStackHeightDp(model, location) > measuredMaxSideStackHeightDp &&
                    controller.resizeSidePanelStack(location, *leadingPanel, measuredMaxSideStackHeightDp)) {
                changed = true;
            }
        }
    }

    if (measured_.mainHeightDp > 0 && bottomDockVisible(model)) {
        const int measuredMaxBottomDockHeightDp = std::clamp(
                measured_.mainHeightDp - kMainLayoutViewportMinSizeDp - kResizableSplitDefaultHitThicknessDp,
                kMainLayoutPanelMinHeightDp,
                kMainLayoutPanelMaxHeightDp);
        if (model.bottomDock.heightDp > measuredMaxBottomDockHeightDp &&
                controller.resizeBottomDock(measuredMaxBottomDockHeightDp)) {
            changed = true;
        }
    }

    for (const Registration& registration : registrations_) {
        if (registration.kind != SashKind::BottomPanelSplit || !registration.bottomSplitMeasurement) {
            continue;
        }
        const MainLayoutBottomSplitPaneMeasurement& measurement = *registration.bottomSplitMeasurement;
        const std::optional<int> rememberedSizeDp =
                dockSplitPanelSizeDp(model.dockLayout, DockLocation::Panel, measurement.panelId);
        if (!rememberedSizeDp) {
            continue;
        }

        const int clampedWidthDp = std::clamp(
                *rememberedSizeDp,
                kMainLayoutBottomSplitPanelMinWidthDp,
                bottomSplitMaxLeadingWidthDp(
                        measurement.availableWidthDp,
                        measurement.trackWidthDp,
                        measurement.splitIndex,
                        measurement.splitCount));
        if (clampedWidthDp != *rememberedSizeDp &&
                controller.resizeBottomSplitPanel(measurement.panelId, clampedWidthDp)) {
            changed = true;
        }
    }

    return changed;
}

std::string MainLayoutSashRegistry::sideDockWidthHandleId(DockLocation location) {
    return location == DockLocation::AuxiliaryBar ?
            std::string(kAuxiliarySidebarWidthHandleId) :
            std::string(kSidebarWidthHandleId);
}

std::string MainLayoutSashRegistry::sidePanelStackHandleId() {
    return std::string(kSidePanelStackHandleId);
}

std::string MainLayoutSashRegistry::bottomDockHeightHandleId() {
    return std::string(kBottomDockHeightHandleId);
}

std::string MainLayoutSashRegistry::bottomPanelSplitHandleId(std::string_view panelId) {
    return std::string(kBottomPanelSplitHandlePrefix) + std::string(panelId);
}

std::optional<std::string_view> MainLayoutSashRegistry::bottomPanelSplitPanelId(std::string_view handleId) {
    if (!startsWith(handleId, kBottomPanelSplitHandlePrefix)) {
        return std::nullopt;
    }
    return handleId.substr(kBottomPanelSplitHandlePrefix.size());
}

void MainLayoutSashRegistry::addRegistration(Registration registration) {
    if (registration.hitRegion.rect.width <= 0 || registration.hitRegion.rect.height <= 0) {
        return;
    }
    registrations_.push_back(std::move(registration));
}

const MainLayoutSashRegistry::Registration* MainLayoutSashRegistry::registrationForHandle(
        std::string_view handleId) const {
    const auto registration = std::find_if(
            registrations_.begin(),
            registrations_.end(),
            [handleId](const Registration& candidate) {
                return candidate.handle.id == handleId;
            });
    return registration == registrations_.end() ? nullptr : &*registration;
}

} // namespace quader
