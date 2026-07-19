////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/viewport_persistent_overlay_style.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "viewport/viewport_settings.hpp"

#include <cstdint>
#include <cstddef>
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
    ViewportOverlaySelectionContext selection_mode =
        ViewportOverlaySelectionContext::Any;
    std::optional<std::uint32_t> color;
    std::optional<float> line_width_pixels;
    std::optional<float> point_size_pixels;
    std::optional<float> outline_growth_pixels;
};

/**
 * Stores the Viewport Overlay Style Context data contract used by the render extraction and Filament resource layer.
 */
struct ViewportOverlayStyleContext {
  ViewportOverlaySelectionContext selection_mode =
      ViewportOverlaySelectionContext::Any;
  std::vector<ViewportOverlayStyleOverride> overrides;
};

/// Identifies the first newly appended record of each styled geometry kind.
struct ViewportOverlayStyleRange {
  std::size_t triangle_begin = 0u;
  std::size_t line_begin = 0u;
  std::size_t point_begin = 0u;
};

/**
 * Owns Viewport Overlay Style Resolver behavior within the render extraction and Filament resource layer.
 */
class ViewportOverlayStyleResolver {
public:
    [[nodiscard]] static ViewportPersistentOverlayStyleSnapshot
    persistent_style_snapshot(const ViewportSettings &settings);
    [[nodiscard]] static std::uint64_t
    style_signature(const ViewportSettings &settings);
    static ViewportOverlayFrame resolve(
            ViewportOverlayFrame frame,
            const ViewportSettings& settings,
            const ViewportOverlayStyleContext& context = {});
    static void apply(ViewportOverlayFrame& frame, const ViewportSettings& settings);
    static void apply(
            ViewportOverlayFrame& frame,
            const ViewportSettings& settings,
            const ViewportOverlayStyleContext& context);
    static void apply_from(
            ViewportOverlayFrame &frame,
            const ViewportSettings &settings,
            const ViewportOverlayStyleContext &context,
            ViewportOverlayStyleRange range);

private:
  static ViewportOverlayStyleRole
  role_for(const ViewportOverlayTriangle &triangle);
  static ViewportOverlayStyleRole role_for(const ViewportOverlayLine &line);
  static ViewportOverlayStyleRole role_for(const ViewportOverlayPoint &point);
  static const ViewportOverlayStyleOverride *
  override_for(ViewportOverlayStyleRole role,
               const ViewportOverlayStyleContext &context);
  static std::uint32_t color_for(ViewportOverlayStyleRole role,
                                 std::uint32_t authored_color,
                                 const ViewportSettings &settings);
  static float line_width_for(ViewportOverlayStyleRole role,
                              ViewportOverlayLayer layer,
                              float authored_width_pixels,
                              const ViewportSettings &settings);
  static float point_size_for(ViewportOverlayStyleRole role,
                              float authored_size_pixels,
                              const ViewportSettings &settings);
  static std::optional<float>
  point_outline_growth_for(ViewportOverlayStyleRole role,
                           std::optional<float> authored_outline_growth_pixels,
                           const ViewportSettings &settings);
  static std::uint32_t setting_color_rgba(ViewportSettingColor color);
  static std::uint32_t setting_color(const ViewportSettings &settings,
                                     ViewportColorSettingId id);
};

} // namespace quader
