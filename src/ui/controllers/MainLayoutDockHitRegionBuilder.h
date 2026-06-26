////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "ui/components/common/UIGeometry.h"
#include "ui/components/organisms/MainLayoutModel.h"
#include "ui/controllers/MainLayoutDockDragController.h"

#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {

/// Measures one backend-projected MainLayout element in framebuffer pixels.
using MainLayoutElementMeasure =
        std::function<std::optional<UIPixelRect>(std::string_view elementId)>;

/// Stores measured MainLayout dock drag sources and drop zones.
struct MainLayoutDockHitRegions {
    std::vector<MainLayoutDockDragHitRegion> panelDragRegions;
    std::vector<MainLayoutDockDropZone> dockDropZones;
    std::vector<MainLayoutPanelDropZone> panelDropZones;
};

/**
 * Builds MainLayout-specific dock drag/drop hit regions from measured presentation geometry.
 */
[[nodiscard]] MainLayoutDockHitRegions buildMainLayoutDockHitRegions(
        const MainLayoutModel& model,
        const MainLayoutElementMeasure& measureElement,
        float dpRatio);

} // namespace quader
