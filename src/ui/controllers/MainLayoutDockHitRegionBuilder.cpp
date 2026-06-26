////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/controllers/MainLayoutDockHitRegionBuilder.h"

#include "ui/backends/rml/MainLayoutRmlBuilder.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace quader {
namespace {

constexpr int kMainLayoutSidebarDropZoneWidthDp = 64;
constexpr int kMainLayoutBottomDockDropZoneHeightDp = 64;

int physicalPixelsForDp(int dp, float dpRatio) {
    return static_cast<int>(std::lround(static_cast<float>(dp) * dpRatio));
}

void appendSideDockDropZones(
        MainLayoutDockHitRegions& result,
        const MainLayoutModel& model,
        const UIPixelRect& mainLayoutRect,
        float dpRatio) {
    const int dropZoneWidthPx = std::min(
            std::max(0, mainLayoutRect.width),
            std::max(48, physicalPixelsForDp(kMainLayoutSidebarDropZoneWidthDp, dpRatio)));
    if (dropZoneWidthPx <= 0) {
        return;
    }

    if (!mainLayoutSideDockVisibleOnSide(model, MainLayoutSidebarSide::Left)) {
        result.dockDropZones.push_back({
                .location = mainLayoutDockLocationForSidebarSide(
                        model,
                        MainLayoutSidebarSide::Left).value_or(DockLocation::AuxiliaryBar),
                .rect = {
                        .left = mainLayoutRect.left,
                        .top = mainLayoutRect.top,
                        .width = dropZoneWidthPx,
                        .height = mainLayoutRect.height,
                },
        });
    }
    if (!mainLayoutSideDockVisibleOnSide(model, MainLayoutSidebarSide::Right)) {
        result.dockDropZones.push_back({
                .location = mainLayoutDockLocationForSidebarSide(
                        model,
                        MainLayoutSidebarSide::Right).value_or(DockLocation::Sidebar),
                .rect = {
                        .left = mainLayoutRect.left + mainLayoutRect.width - dropZoneWidthPx,
                        .top = mainLayoutRect.top,
                        .width = dropZoneWidthPx,
                        .height = mainLayoutRect.height,
                },
        });
    }
}

void appendBottomDockDropZone(
        MainLayoutDockHitRegions& result,
        const MainLayoutModel& model,
        const UIPixelRect& bottomDropRect,
        float dpRatio) {
    if (mainLayoutBottomDockVisibleForLocation(model)) {
        return;
    }

    const int dropZoneHeightPx = std::min(
            std::max(0, bottomDropRect.height),
            std::max(48, physicalPixelsForDp(kMainLayoutBottomDockDropZoneHeightDp, dpRatio)));
    if (dropZoneHeightPx <= 0) {
        return;
    }

    result.dockDropZones.push_back({
            .location = DockLocation::Panel,
            .rect = {
                    .left = bottomDropRect.left,
                    .top = bottomDropRect.top + bottomDropRect.height - dropZoneHeightPx,
                    .width = bottomDropRect.width,
                    .height = dropZoneHeightPx,
            },
    });
}

void appendPanelDragAndBodyDropZones(
        MainLayoutDockHitRegions& result,
        const MainLayoutModel& model,
        const MainLayoutElementMeasure& measureElement,
        bool& bottomPanelBodyDropZoneAdded) {
    for (std::string_view panelId : mainLayoutDockablePanelIds()) {
        if (!mainLayoutPanelVisibleInRenderedDock(model, panelId)) {
            continue;
        }
        const std::optional<DockLocation> location = dockPanelLocation(model.dockLayout, panelId);
        if (!location) {
            continue;
        }
        const std::string dragElementId = *location == DockLocation::Panel ?
                mainLayoutPanelTabElementId(panelId) :
                mainLayoutPanelHeaderElementId(panelId);
        if (const std::optional<UIPixelRect> rect = measureElement(dragElementId)) {
            result.panelDragRegions.push_back({
                    .panelId = std::string(panelId),
                    .rect = *rect,
            });
            if (*location == DockLocation::Panel) {
                result.panelDropZones.push_back({
                        .location = *location,
                        .panelId = std::string(panelId),
                        .rect = *rect,
                        .kind = MainLayoutPanelDropZoneKind::BottomTab,
                });
            }
        }
        if (const std::optional<UIPixelRect> rect = measureElement(mainLayoutPanelElementId(panelId))) {
            result.panelDropZones.push_back({
                    .location = *location,
                    .panelId = std::string(panelId),
                    .rect = *rect,
                    .kind = MainLayoutPanelDropZoneKind::PanelBody,
            });
            if (*location == DockLocation::Panel) {
                bottomPanelBodyDropZoneAdded = true;
            }
        }
    }
}

void appendBottomBodyFallbackDropZone(
        MainLayoutDockHitRegions& result,
        const MainLayoutModel& model,
        const std::optional<UIPixelRect>& bottomDockContentRect,
        bool bottomPanelBodyDropZoneAdded) {
    if (!mainLayoutBottomDockVisibleForLocation(model) ||
            bottomPanelBodyDropZoneAdded ||
            !bottomDockContentRect) {
        return;
    }

    const std::optional<std::string_view> activePanelId =
            dockActivePanelId(model.dockLayout, DockLocation::Panel);
    if (!activePanelId ||
            !mainLayoutPanelVisibleInLocation(model, *activePanelId, DockLocation::Panel)) {
        return;
    }

    result.panelDropZones.push_back({
            .location = DockLocation::Panel,
            .panelId = std::string(*activePanelId),
            .rect = *bottomDockContentRect,
            .kind = MainLayoutPanelDropZoneKind::PanelBody,
    });
}

void appendBottomTabTrailingDropZone(
        MainLayoutDockHitRegions& result,
        const MainLayoutModel& model,
        const MainLayoutElementMeasure& measureElement) {
    if (!mainLayoutBottomDockVisibleForLocation(model)) {
        return;
    }

    std::string_view trailingTargetPanelId;
    for (std::string_view panelId : dockTabPanelsInLocation(model.dockLayout, DockLocation::Panel)) {
        if (mainLayoutPanelVisibleInLocation(model, panelId, DockLocation::Panel)) {
            trailingTargetPanelId = panelId;
        }
    }
    if (trailingTargetPanelId.empty()) {
        return;
    }

    const std::optional<UIPixelRect> stripRect = measureElement(mainLayoutBottomDockTabStripElementId());
    const std::optional<UIPixelRect> lastTabRect = measureElement(mainLayoutPanelTabElementId(trailingTargetPanelId));
    if (!stripRect || !lastTabRect) {
        return;
    }

    const int trailingLeft = lastTabRect->left + lastTabRect->width;
    const int trailingWidth = stripRect->left + stripRect->width - trailingLeft;
    if (trailingWidth <= 0) {
        return;
    }

    result.panelDropZones.push_back({
            .location = DockLocation::Panel,
            .panelId = std::string(trailingTargetPanelId),
            .rect = {
                    .left = trailingLeft,
                    .top = stripRect->top,
                    .width = trailingWidth,
                    .height = stripRect->height,
            },
            .kind = MainLayoutPanelDropZoneKind::BottomTabTrailing,
    });
}

} // namespace

MainLayoutDockHitRegions buildMainLayoutDockHitRegions(
        const MainLayoutModel& model,
        const MainLayoutElementMeasure& measureElement,
        float dpRatio) {
    MainLayoutDockHitRegions result;

    const std::optional<UIPixelRect> mainLayoutSlotRect = measureElement("main-layout-slot");
    if (mainLayoutSlotRect) {
        appendSideDockDropZones(result, model, *mainLayoutSlotRect, dpRatio);
    }

    const std::optional<UIPixelRect> editorColumnRect = measureElement(mainLayoutEditorColumnElementId());
    const std::optional<UIPixelRect> bottomDropRect = editorColumnRect ? editorColumnRect : mainLayoutSlotRect;
    if (bottomDropRect) {
        appendBottomDockDropZone(result, model, *bottomDropRect, dpRatio);
    }

    const std::optional<UIPixelRect> bottomDockContentRect =
            mainLayoutBottomDockVisibleForLocation(model) ?
                    measureElement(mainLayoutBottomDockContentElementId()) :
                    std::nullopt;
    bool bottomPanelBodyDropZoneAdded = false;
    appendPanelDragAndBodyDropZones(result, model, measureElement, bottomPanelBodyDropZoneAdded);
    appendBottomBodyFallbackDropZone(
            result,
            model,
            bottomDockContentRect,
            bottomPanelBodyDropZoneAdded);
    appendBottomTabTrailingDropZone(result, model, measureElement);

    return result;
}

} // namespace quader
