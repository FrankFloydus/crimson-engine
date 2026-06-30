////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_viewport_layout_types.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace quader {

constexpr std::size_t kViewportPaneCount = 4u;

std::size_t viewportPaneIndex(ViewportPaneId paneId);
const std::array<ViewportLayoutOption, 8>& viewportLayoutOptions();
std::string_view viewportLayoutStableId(ViewportLayoutId layoutId);
std::string_view viewportLayoutLabel(ViewportLayoutId layoutId);
std::string_view viewportPaneStableId(ViewportPaneId paneId);
std::string_view viewportPaneLabel(ViewportPaneId paneId);
ViewportView viewportViewForPane(ViewportPaneId paneId);
inline std::string_view viewportViewStableId(ViewportView view) {
    switch (view) {
    case ViewportView::Perspective:
        return "perspective";
    case ViewportView::Top:
        return "top";
    case ViewportView::Bottom:
        return "bottom";
    case ViewportView::Front:
        return "front";
    case ViewportView::Back:
        return "back";
    case ViewportView::Side:
        return "right";
    case ViewportView::Left:
        return "left";
    }
    return "perspective";
}

std::optional<ViewportView> viewportViewFromStableId(std::string_view stableId);
inline std::string_view viewportViewLabel(ViewportView view) {
    switch (view) {
    case ViewportView::Perspective:
        return "Perspective";
    case ViewportView::Top:
        return "Top";
    case ViewportView::Bottom:
        return "Bottom";
    case ViewportView::Front:
        return "Front";
    case ViewportView::Back:
        return "Back";
    case ViewportView::Side:
        return "Right";
    case ViewportView::Left:
        return "Left";
    }
    return "Perspective";
}

std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId);
std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId, const ViewportLayoutState& state);
std::vector<ViewportSplitHandle> viewportLayoutSplitHandles(ViewportLayoutId layoutId, const ViewportLayoutState& state);
ViewportNormalizedRect viewportAbsolutePaneRect(ViewportNormalizedRect viewportRect, ViewportNormalizedRect normalizedRect);

} // namespace quader
