////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "viewport/viewport_layout.hpp"

#include <algorithm>

namespace quader {
namespace {

constexpr std::array<ViewportLayoutOption, 8> kViewportLayoutOptions{{
        {ViewportLayoutId::Single, "Single View"},
        {ViewportLayoutId::TwoHorizontal, "Two-View Horizontal"},
        {ViewportLayoutId::TwoVertical, "Two-View Vertical"},
        {ViewportLayoutId::ThreePrimaryRight, "Three-View Primary Right"},
        {ViewportLayoutId::ThreePrimaryLeft, "Three-View Primary Left"},
        {ViewportLayoutId::ThreePrimaryBottom, "Three-View Primary Bottom"},
        {ViewportLayoutId::ThreePrimaryTop, "Three-View Primary Top"},
        {ViewportLayoutId::FourQuad, "Four-View Quad"},
}};

ViewportPanePlacement placement(ViewportPaneId paneId, float x, float y, float width, float height) {
    return {
            .paneId = paneId,
            .normalizedRect = {.x = x, .y = y, .width = width, .height = height},
    };
}

ViewportSplitHandle verticalHandle(ViewportSplitHandleId id, float position, float start, float end) {
    return {
            .id = id,
            .axis = ViewportSplitHandleAxis::Vertical,
            .position = position,
            .start = start,
            .end = end,
    };
}

ViewportSplitHandle horizontalHandle(ViewportSplitHandleId id, float position, float start, float end) {
    return {
            .id = id,
            .axis = ViewportSplitHandleAxis::Horizontal,
            .position = position,
            .start = start,
            .end = end,
    };
}

} // namespace

void ViewportLayoutState::setHandlePosition(ViewportSplitHandleId handleId, float normalizedPosition) {
    const float clamped = std::clamp(normalizedPosition, MinSplit, MaxSplit);
    switch (handleId) {
    case ViewportSplitHandleId::TwoHorizontal:
        twoHorizontalSplit_ = clamped;
        break;
    case ViewportSplitHandleId::TwoVertical:
        twoVerticalSplit_ = clamped;
        break;
    case ViewportSplitHandleId::ThreePrimary:
        threePrimarySplit_ = clamped;
        break;
    case ViewportSplitHandleId::ThreeSecondary:
        threeSecondarySplit_ = clamped;
        break;
    case ViewportSplitHandleId::FourVertical:
        fourVerticalSplit_ = clamped;
        break;
    case ViewportSplitHandleId::FourHorizontal:
        fourHorizontalSplit_ = clamped;
        break;
    }
}

std::size_t viewportPaneIndex(ViewportPaneId paneId) {
    switch (paneId) {
    case ViewportPaneId::Perspective:
        return 0u;
    case ViewportPaneId::Top:
        return 1u;
    case ViewportPaneId::Front:
        return 2u;
    case ViewportPaneId::Side:
        return 3u;
    }
    return 0u;
}

const std::array<ViewportLayoutOption, 8>& viewportLayoutOptions() {
    return kViewportLayoutOptions;
}

std::string_view viewportLayoutStableId(ViewportLayoutId layoutId) {
    switch (layoutId) {
    case ViewportLayoutId::Single:
        return "single";
    case ViewportLayoutId::TwoHorizontal:
        return "two_horizontal";
    case ViewportLayoutId::TwoVertical:
        return "two_vertical";
    case ViewportLayoutId::ThreePrimaryRight:
        return "three_primary_right";
    case ViewportLayoutId::ThreePrimaryLeft:
        return "three_primary_left";
    case ViewportLayoutId::ThreePrimaryBottom:
        return "three_primary_bottom";
    case ViewportLayoutId::ThreePrimaryTop:
        return "three_primary_top";
    case ViewportLayoutId::FourQuad:
        return "four_quad";
    }
    return "unknown";
}

std::string_view viewportLayoutLabel(ViewportLayoutId layoutId) {
    const auto found = std::find_if(kViewportLayoutOptions.begin(), kViewportLayoutOptions.end(),
            [layoutId](const ViewportLayoutOption& option) {
                return option.id == layoutId;
            });
    return found != kViewportLayoutOptions.end() ? found->label : "Unknown Layout";
}

std::string_view viewportPaneStableId(ViewportPaneId paneId) {
    switch (paneId) {
    case ViewportPaneId::Perspective:
        return "perspective";
    case ViewportPaneId::Top:
        return "top";
    case ViewportPaneId::Front:
        return "front";
    case ViewportPaneId::Side:
        return "side";
    }
    return "viewport";
}

std::string_view viewportPaneLabel(ViewportPaneId paneId) {
    switch (paneId) {
    case ViewportPaneId::Perspective:
        return "Perspective";
    case ViewportPaneId::Top:
        return "Top";
    case ViewportPaneId::Front:
        return "Front";
    case ViewportPaneId::Side:
        return "Right";
    }
    return "Viewport";
}

ViewportView viewportViewForPane(ViewportPaneId paneId) {
    switch (paneId) {
    case ViewportPaneId::Perspective:
        return ViewportView::Perspective;
    case ViewportPaneId::Top:
        return ViewportView::Top;
    case ViewportPaneId::Front:
        return ViewportView::Front;
    case ViewportPaneId::Side:
        return ViewportView::Side;
    }
    return ViewportView::Perspective;
}

std::string_view viewportViewStableId(ViewportView view) {
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

std::string_view viewportViewLabel(ViewportView view) {
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

std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId) {
    return viewportLayoutPlacements(layoutId, ViewportLayoutState{});
}

std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId, const ViewportLayoutState& state) {
    switch (layoutId) {
    case ViewportLayoutId::Single:
        return {placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 1.0f)};
    case ViewportLayoutId::TwoHorizontal:
        return {
                placement(ViewportPaneId::Perspective, 0.0f, 0.0f, state.twoHorizontalSplit(), 1.0f),
                placement(ViewportPaneId::Top, state.twoHorizontalSplit(), 0.0f, 1.0f - state.twoHorizontalSplit(), 1.0f),
        };
    case ViewportLayoutId::TwoVertical:
        return {
                placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, state.twoVerticalSplit()),
                placement(ViewportPaneId::Top, 0.0f, state.twoVerticalSplit(), 1.0f, 1.0f - state.twoVerticalSplit()),
        };
    case ViewportLayoutId::ThreePrimaryRight:
        return {
                placement(ViewportPaneId::Top, 0.0f, 0.0f, state.threePrimarySplit(), state.threeSecondarySplit()),
                placement(ViewportPaneId::Side, 0.0f, state.threeSecondarySplit(), state.threePrimarySplit(), 1.0f - state.threeSecondarySplit()),
                placement(ViewportPaneId::Perspective, state.threePrimarySplit(), 0.0f, 1.0f - state.threePrimarySplit(), 1.0f),
        };
    case ViewportLayoutId::ThreePrimaryBottom:
        return {
                placement(ViewportPaneId::Top, 0.0f, 0.0f, state.threeSecondarySplit(), state.threePrimarySplit()),
                placement(ViewportPaneId::Side, state.threeSecondarySplit(), 0.0f, 1.0f - state.threeSecondarySplit(), state.threePrimarySplit()),
                placement(ViewportPaneId::Perspective, 0.0f, state.threePrimarySplit(), 1.0f, 1.0f - state.threePrimarySplit()),
        };
    case ViewportLayoutId::ThreePrimaryTop:
        return {
                placement(ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, state.threePrimarySplit()),
                placement(ViewportPaneId::Top, 0.0f, state.threePrimarySplit(), state.threeSecondarySplit(), 1.0f - state.threePrimarySplit()),
                placement(ViewportPaneId::Side, state.threeSecondarySplit(), state.threePrimarySplit(), 1.0f - state.threeSecondarySplit(), 1.0f - state.threePrimarySplit()),
        };
    case ViewportLayoutId::FourQuad:
        return {
                placement(ViewportPaneId::Perspective, 0.0f, 0.0f, state.fourVerticalSplit(), state.fourHorizontalSplit()),
                placement(ViewportPaneId::Top, state.fourVerticalSplit(), 0.0f, 1.0f - state.fourVerticalSplit(), state.fourHorizontalSplit()),
                placement(ViewportPaneId::Front, 0.0f, state.fourHorizontalSplit(), state.fourVerticalSplit(), 1.0f - state.fourHorizontalSplit()),
                placement(ViewportPaneId::Side, state.fourVerticalSplit(), state.fourHorizontalSplit(), 1.0f - state.fourVerticalSplit(), 1.0f - state.fourHorizontalSplit()),
        };
    case ViewportLayoutId::ThreePrimaryLeft:
        return {
                placement(ViewportPaneId::Perspective, 0.0f, 0.0f, state.threePrimarySplit(), 1.0f),
                placement(ViewportPaneId::Top, state.threePrimarySplit(), 0.0f, 1.0f - state.threePrimarySplit(), state.threeSecondarySplit()),
                placement(ViewportPaneId::Side, state.threePrimarySplit(), state.threeSecondarySplit(), 1.0f - state.threePrimarySplit(), 1.0f - state.threeSecondarySplit()),
        };
    }

    return {
            placement(ViewportPaneId::Perspective, 0.0f, 0.0f, state.threePrimarySplit(), 1.0f),
            placement(ViewportPaneId::Top, state.threePrimarySplit(), 0.0f, 1.0f - state.threePrimarySplit(), state.threeSecondarySplit()),
            placement(ViewportPaneId::Side, state.threePrimarySplit(), state.threeSecondarySplit(), 1.0f - state.threePrimarySplit(), 1.0f - state.threeSecondarySplit()),
    };
}

std::vector<ViewportSplitHandle> viewportLayoutSplitHandles(ViewportLayoutId layoutId, const ViewportLayoutState& state) {
    switch (layoutId) {
    case ViewportLayoutId::TwoHorizontal:
        return {verticalHandle(ViewportSplitHandleId::TwoHorizontal, state.twoHorizontalSplit(), 0.0f, 1.0f)};
    case ViewportLayoutId::TwoVertical:
        return {horizontalHandle(ViewportSplitHandleId::TwoVertical, state.twoVerticalSplit(), 0.0f, 1.0f)};
    case ViewportLayoutId::ThreePrimaryRight:
        return {
                verticalHandle(ViewportSplitHandleId::ThreePrimary, state.threePrimarySplit(), 0.0f, 1.0f),
                horizontalHandle(ViewportSplitHandleId::ThreeSecondary, state.threeSecondarySplit(), 0.0f, state.threePrimarySplit()),
        };
    case ViewportLayoutId::ThreePrimaryLeft:
        return {
                verticalHandle(ViewportSplitHandleId::ThreePrimary, state.threePrimarySplit(), 0.0f, 1.0f),
                horizontalHandle(ViewportSplitHandleId::ThreeSecondary, state.threeSecondarySplit(), state.threePrimarySplit(), 1.0f),
        };
    case ViewportLayoutId::ThreePrimaryBottom:
        return {
                horizontalHandle(ViewportSplitHandleId::ThreePrimary, state.threePrimarySplit(), 0.0f, 1.0f),
                verticalHandle(ViewportSplitHandleId::ThreeSecondary, state.threeSecondarySplit(), 0.0f, state.threePrimarySplit()),
        };
    case ViewportLayoutId::ThreePrimaryTop:
        return {
                horizontalHandle(ViewportSplitHandleId::ThreePrimary, state.threePrimarySplit(), 0.0f, 1.0f),
                verticalHandle(ViewportSplitHandleId::ThreeSecondary, state.threeSecondarySplit(), state.threePrimarySplit(), 1.0f),
        };
    case ViewportLayoutId::FourQuad:
        return {
                verticalHandle(ViewportSplitHandleId::FourVertical, state.fourVerticalSplit(), 0.0f, 1.0f),
                horizontalHandle(ViewportSplitHandleId::FourHorizontal, state.fourHorizontalSplit(), 0.0f, 1.0f),
        };
    case ViewportLayoutId::Single:
        return {};
    }

    return {};
}

ViewportNormalizedRect viewportAbsolutePaneRect(ViewportNormalizedRect viewportRect, ViewportNormalizedRect normalizedRect) {
    ViewportNormalizedRect rect{
            .x = viewportRect.x + viewportRect.width * normalizedRect.x,
            .y = viewportRect.y + viewportRect.height * normalizedRect.y,
            .width = viewportRect.width * normalizedRect.width,
            .height = viewportRect.height * normalizedRect.height,
    };
    const float gapX = normalizedRect.x > 0.0f ? 1.0f : 0.0f;
    const float gapY = normalizedRect.y > 0.0f ? 1.0f : 0.0f;
    rect.x += gapX;
    rect.y += gapY;
    rect.width = std::max(1.0f, rect.width - gapX);
    rect.height = std::max(1.0f, rect.height - gapY);
    return rect;
}

} // namespace quader
