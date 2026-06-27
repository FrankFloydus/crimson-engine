////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "viewport/viewport_settings.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace quader {

/**
 * Enumerates ViewportOverlaySelectionContext values used by the render layer.
 */
enum class ViewportOverlaySelectionContext {
    Any,
    Object,
    Group,
    Vertex,
    Edge,
    Face,
};

/**
 * Represents a Viewport Overlay Style Override value used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayStyleOverride {
    ViewportOverlayStyleRole role = ViewportOverlayStyleRole::LayerDefault;
    ViewportOverlaySelectionContext selectionMode = ViewportOverlaySelectionContext::Any;
    std::optional<std::uint32_t> color;
    std::optional<float> lineWidthPixels;
    std::optional<float> pointSizePixels;
    std::optional<float> outlineGrowthPixels;
};

/**
 * Stores the Viewport Overlay Style Context data contract used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayStyleContext {
    ViewportOverlaySelectionContext selectionMode = ViewportOverlaySelectionContext::Any;
    std::vector<ViewportOverlayStyleOverride> overrides;
};

/**
 * Owns Viewport Overlay Style Resolver behavior within the render extraction and Filament resource layer.
 */
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
