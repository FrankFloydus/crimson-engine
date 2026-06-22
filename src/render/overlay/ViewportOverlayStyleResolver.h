#pragma once

#include "render/ViewportOverlayFrame.h"
#include "viewport/ViewportSettings.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

enum class ViewportOverlaySelectionContext {
    Any,
    Object,
    Group,
    Vertex,
    Edge,
    Face,
};

struct ViewportOverlayStyleOverride {
    ViewportOverlayStyleRole role = ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySelectionContext selectionMode = ViewportOverlaySelectionContext::Any;
    std::optional<std::uint32_t> color;
    std::optional<float> lineWidthPixels;
    std::optional<float> pointSizePixels;
    std::optional<float> outlineGrowthPixels;
};

struct ViewportOverlayStyleContext {
    ViewportOverlaySelectionContext selectionMode = ViewportOverlaySelectionContext::Any;
    std::vector<ViewportOverlayStyleOverride> overrides;
};

class ViewportOverlayStyleResolver {
public:
    static ViewportOverlayFrame resolve(
            ViewportOverlayFrame frame,
            const ViewportSettings& settings,
            const ViewportOverlayStyleContext& context = {});
    static void apply(ViewportOverlayFrame& frame, const ViewportSettings& settings);
    static void apply(
            ViewportOverlayFrame& frame,
            const ViewportSettings& settings,
            const ViewportOverlayStyleContext& context);

private:
    static ViewportOverlayStyleRole roleFor(const ViewportOverlayTriangle& triangle);
    static ViewportOverlayStyleRole roleFor(const ViewportOverlayLine& line);
    static ViewportOverlayStyleRole roleFor(const ViewportOverlayPoint& point);
    static const ViewportOverlayStyleOverride* overrideFor(
            ViewportOverlayStyleRole role,
            const ViewportOverlayStyleContext& context);
    static std::uint32_t colorFor(
            ViewportOverlayStyleRole role,
            std::uint32_t authoredColor,
            const ViewportSettings& settings);
    static float lineWidthFor(
            ViewportOverlayStyleRole role,
            ViewportOverlayLayer layer,
            float authoredWidthPixels,
            const ViewportSettings& settings);
    static float pointSizeFor(
            ViewportOverlayStyleRole role,
            float authoredSizePixels,
            const ViewportSettings& settings);
    static std::optional<float> pointOutlineGrowthFor(
            ViewportOverlayStyleRole role,
            std::optional<float> authoredOutlineGrowthPixels,
            const ViewportSettings& settings);
    static std::uint32_t settingColorRgba(ViewportSettingColor color);
    static std::uint32_t settingColor(const ViewportSettings& settings, ViewportColorSettingId id);
};

} // namespace quader
