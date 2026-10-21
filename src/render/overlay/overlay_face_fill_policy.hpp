////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/viewport_overlay_frame.hpp"

#include <cstdint>

namespace quader {

/** Semantic priority of mutually exclusive face tints at one visible pixel. */
enum class OverlayFaceFillClass : std::uint8_t {
  None = 0u,
  Diagnostic = 1u,
  Selection = 2u,
  ActiveSelection = 3u,
  Hover = 4u,
  FacePlaneHover = 5u,
  RemoveSelection = 6u,
};

/** Mask draws choose a priority; color draws consume its coverage once. */
enum class OverlayFaceFillPass : std::uint8_t {
  Mask,
  Color,
};

/** Visible object contours exclude hidden contours at the same pixel. */
enum class OverlaySilhouettePass : std::uint8_t {
  None,
  Visible,
  Occluded,
};

/// Active component ribbons reserve coverage before ordinary wire colors blend.
enum class OverlayEditWirePass : std::uint8_t {
  Legacy,
  Ordinary,
  Active,
  ActiveMask,
  ActiveSupplement,
};

inline constexpr std::uint8_t kOverlayFaceFillPriorityMask = 0x07u;
inline constexpr std::uint8_t kOverlayFaceFillConsumedMask = 0x08u;
inline constexpr std::uint8_t kOverlayFaceFillStencilMask =
    kOverlayFaceFillPriorityMask | kOverlayFaceFillConsumedMask;
inline constexpr std::uint8_t kOverlaySilhouetteVisibleStencilMask = 0x10u;
inline constexpr std::uint8_t kOverlayActiveWireStencilMask = 0x20u;
inline constexpr std::uint8_t kOverlayActiveWireConsumedStencilMask = 0x40u;
inline constexpr std::uint8_t kOverlayCompositionStencilMask =
    kOverlayFaceFillStencilMask | kOverlaySilhouetteVisibleStencilMask |
    kOverlayActiveWireStencilMask | kOverlayActiveWireConsumedStencilMask;

/// Runs after stencil reset and before the first component wire color layer.
inline constexpr std::uint16_t kOverlayActiveWireMaskBlendOrder = 32u;

/// Fully transparent tints neither claim nor consume stencil coverage.
[[nodiscard]] constexpr OverlayFaceFillClass visible_overlay_face_fill_class(
    OverlayFaceFillClass fill_class, std::uint32_t color_abgr) {
  return (color_abgr >> 24u) == 0u ? OverlayFaceFillClass::None : fill_class;
}

[[nodiscard]] OverlayFaceFillClass
overlay_face_fill_class(const ViewportOverlayTriangle &triangle);

/// Slot zero resets stencil; all mask slots precede the first face color at 64.
[[nodiscard]] constexpr std::uint16_t
overlay_face_fill_mask_blend_order(std::uint16_t blend_order_bias = 0u) {
  return static_cast<std::uint16_t>(1u + blend_order_bias);
}

} // namespace quader
