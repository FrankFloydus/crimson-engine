////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>

namespace quader {

inline constexpr std::size_t kViewportCircularHandleSegments = 32u;

/** Shared disk appearance for creation controls and other viewport handles. */
struct ViewportCircularHandleStyle final {
  std::uint32_t fill_color = kOverlayPrimitiveCubeFaceColor;
  std::optional<std::uint32_t> outline_color =
      kOverlayPrimitiveCubeFaceOutlineColor;
  ViewportOverlaySource source = ViewportOverlaySource::Tool;
  ViewportOverlayElementRef element;
};

/** Appends the existing creation disk and non-overlapping rim in one triangle batch. */
inline void append_viewport_circular_handle(
    ViewportOverlayFrame &frame, QVec3 center, QVec3 right, QVec3 down,
    const ViewportCircularHandleStyle &style = {}) {
  static const std::array<QVec2, kViewportCircularHandleSegments> kDirections = [] {
    std::array<QVec2, kViewportCircularHandleSegments> result{};
    for (std::size_t index = 0u; index < result.size(); ++index) {
      const float angle = 2.0f * std::numbers::pi_v<float> *
                          static_cast<float>(index) /
                          static_cast<float>(result.size());
      result[index] = {std::cos(angle), std::sin(angle)};
    }
    return result;
  }();
  const auto append = [&](QVec3 a, QVec3 b, QVec3 c, std::uint32_t color) {
    frame.triangles.push_back({
        .layer = ViewportOverlayLayer::ToolHandle,
        .a = a,
        .b = b,
        .c = c,
        .color = color,
        .style_role = ViewportOverlayStyleRole::ExplicitColor,
        .source = style.source,
        .element = style.element,
    });
  };
  for (std::size_t segment = 0u; segment < kDirections.size(); ++segment) {
    const QVec2 a = kDirections[segment];
    const QVec2 b = kDirections[(segment + 1u) % kDirections.size()];
    const QVec3 outer_a = right * a.x + down * a.y;
    const QVec3 outer_b = right * b.x + down * b.y;
    if (style.outline_color.has_value()) {
      const QVec3 inner_a = center + outer_a * kOverlayCircularHandleInnerRadius;
      const QVec3 inner_b = center + outer_b * kOverlayCircularHandleInnerRadius;
      append(center, inner_a, inner_b, style.fill_color);
      append(inner_a, center + outer_a, center + outer_b, *style.outline_color);
      append(inner_a, center + outer_b, inner_b, *style.outline_color);
    } else {
      append(center, center + outer_a, center + outer_b, style.fill_color);
    }
  }
}

} // namespace quader
