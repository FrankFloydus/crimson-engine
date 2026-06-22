#pragma once

#include "math/Types.h"

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace quader {

enum class ViewportLayoutId {
    Single,
    TwoHorizontal,
    TwoVertical,
    ThreePrimaryRight,
    ThreePrimaryLeft,
    ThreePrimaryBottom,
    ThreePrimaryTop,
    FourQuad,
};

enum class ViewportPaneId {
    Perspective,
    Top,
    Front,
    Side,
};

enum class ViewportView {
    Perspective,
    Top,
    Bottom,
    Front,
    Back,
    Side,
    Left,
};

struct ViewportNormalizedRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

struct ViewportLayoutOption {
    ViewportLayoutId id = ViewportLayoutId::Single;
    std::string_view label;
};

struct ViewportPanePlacement {
    ViewportPaneId paneId = ViewportPaneId::Perspective;
    ViewportNormalizedRect normalizedRect;
};

enum class ViewportSplitHandleId {
    TwoHorizontal,
    TwoVertical,
    ThreePrimary,
    ThreeSecondary,
    FourVertical,
    FourHorizontal,
};

enum class ViewportSplitHandleAxis {
    Vertical,
    Horizontal,
};

struct ViewportSplitHandle {
    ViewportSplitHandleId id = ViewportSplitHandleId::TwoHorizontal;
    ViewportSplitHandleAxis axis = ViewportSplitHandleAxis::Vertical;
    float position = 0.5f;
    float start = 0.0f;
    float end = 1.0f;
};

class ViewportLayoutState {
public:
    static constexpr float MinSplit = 0.15f;
    static constexpr float MaxSplit = 0.85f;

    float twoHorizontalSplit() const { return twoHorizontalSplit_; }
    float twoVerticalSplit() const { return twoVerticalSplit_; }
    float threePrimarySplit() const { return threePrimarySplit_; }
    float threeSecondarySplit() const { return threeSecondarySplit_; }
    float fourVerticalSplit() const { return fourVerticalSplit_; }
    float fourHorizontalSplit() const { return fourHorizontalSplit_; }

    void setHandlePosition(ViewportSplitHandleId handleId, float normalizedPosition);

private:
    float twoHorizontalSplit_ = 0.5f;
    float twoVerticalSplit_ = 0.5f;
    float threePrimarySplit_ = 0.5f;
    float threeSecondarySplit_ = 0.5f;
    float fourVerticalSplit_ = 0.5f;
    float fourHorizontalSplit_ = 0.5f;
};

constexpr std::size_t kViewportPaneCount = 4u;

std::size_t viewportPaneIndex(ViewportPaneId paneId);
const std::array<ViewportLayoutOption, 8>& viewportLayoutOptions();
std::string_view viewportLayoutStableId(ViewportLayoutId layoutId);
std::string_view viewportLayoutLabel(ViewportLayoutId layoutId);
std::string_view viewportPaneStableId(ViewportPaneId paneId);
std::string_view viewportPaneLabel(ViewportPaneId paneId);
ViewportView viewportViewForPane(ViewportPaneId paneId);
std::string_view viewportViewStableId(ViewportView view);
std::string_view viewportViewLabel(ViewportView view);
std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId);
std::vector<ViewportPanePlacement> viewportLayoutPlacements(ViewportLayoutId layoutId, const ViewportLayoutState& state);
std::vector<ViewportSplitHandle> viewportLayoutSplitHandles(ViewportLayoutId layoutId, const ViewportLayoutState& state);
ViewportNormalizedRect viewportAbsolutePaneRect(ViewportNormalizedRect viewportRect, ViewportNormalizedRect normalizedRect);

} // namespace quader
