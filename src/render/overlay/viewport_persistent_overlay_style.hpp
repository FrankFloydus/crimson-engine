////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_edit_style.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace quader {

/**
 * Renderer-neutral style values consumed by persistent overlay caches. The
 * narrow signatures let each cache invalidate only the selected renderables
 * affected by a settings mutation.
 */
struct ViewportPersistentOverlayStyleSnapshot {
  /** Combined app interface scale and the native-surface DPR for this group. */
  float raster_scale = 1.0f;
  std::uint32_t selection_face_color = kOverlaySelectionFaceColor;
  std::uint32_t selection_wire_color = kOverlaySelectionWireColor;
  std::uint32_t active_selection_face_color = viewport_active_selection_color(kOverlaySelectionFaceColor);
  std::uint32_t active_selection_wire_color = viewport_active_selection_color(kOverlaySelectionWireColor);
  /** Resolved logical width before interface scale and monitor DPR. */
  float selection_edge_logical_width_pixels =
      kOverlaySelectionWireLineWidthPixels;
  float selection_edge_line_width_pixels =
      kOverlaySelectionWireLineWidthPixels;
  float active_selection_logical_width_pixels =
      kOverlaySelectionWireLineWidthPixels;
  float active_selection_line_width_pixels =
      kOverlaySelectionWireLineWidthPixels;
  std::uint64_t face_signature = 0u;
  std::uint64_t wire_signature = 0u;
  InstanceEditStyle instance_edit_style;
  /** Context uniforms invalidate independently of selected face/wire geometry. */
  std::uint64_t instance_edit_context_signature = 0u;
};

constexpr std::size_t kMaxViewportPersistentOverlayRasterGroups = 4u;

/** Binds one effective persistent-wire style to the panes that consume it. */
struct ViewportPersistentOverlayRasterGroup final {
  std::uint8_t layer_mask = 0u;
  ViewportPersistentOverlayStyleSnapshot style;
};

[[nodiscard]] inline bool viewport_persistent_overlay_wire_styles_match(
    const ViewportPersistentOverlayStyleSnapshot &left,
    const ViewportPersistentOverlayStyleSnapshot &right) noexcept {
  return left.raster_scale == right.raster_scale &&
         left.selection_wire_color == right.selection_wire_color &&
         left.active_selection_wire_color == right.active_selection_wire_color &&
         left.selection_edge_logical_width_pixels ==
             right.selection_edge_logical_width_pixels &&
         left.selection_edge_line_width_pixels ==
             right.selection_edge_line_width_pixels &&
         left.active_selection_logical_width_pixels ==
             right.active_selection_logical_width_pixels &&
         left.active_selection_line_width_pixels ==
             right.active_selection_line_width_pixels &&
         left.wire_signature == right.wire_signature &&
         left.instance_edit_context_signature ==
             right.instance_edit_context_signature;
}

/** Fixed-capacity, allocation-free pane grouping for the four viewport slots. */
class ViewportPersistentOverlayRasterGroupSet final {
public:
  /** Returns false only if a fifth distinct effective style is requested. */
  bool add(std::uint8_t layer_mask,
           ViewportPersistentOverlayStyleSnapshot style) noexcept {
    if (layer_mask == 0u) {
      return true;
    }
    for (std::size_t index = 0u; index < size_; ++index) {
      if (viewport_persistent_overlay_wire_styles_match(groups_[index].style,
                                                         style)) {
        groups_[index].layer_mask = static_cast<std::uint8_t>(
            groups_[index].layer_mask | layer_mask);
        return true;
      }
    }
    if (size_ >= groups_.size()) {
      return false;
    }
    groups_[size_++] = {.layer_mask = layer_mask, .style = std::move(style)};
    return true;
  }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0u; }
  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] std::span<const ViewportPersistentOverlayRasterGroup>
  groups() const noexcept {
    return std::span<const ViewportPersistentOverlayRasterGroup>(groups_)
        .first(size_);
  }

private:
  std::array<ViewportPersistentOverlayRasterGroup,
             kMaxViewportPersistentOverlayRasterGroups>
      groups_{};
  std::size_t size_ = 0u;
};

[[nodiscard]] inline bool viewport_persistent_overlay_raster_groups_match(
    const ViewportPersistentOverlayRasterGroupSet &left,
    const ViewportPersistentOverlayRasterGroupSet &right) noexcept {
  if (left.size() != right.size()) {
    return false;
  }
  for (const ViewportPersistentOverlayRasterGroup &left_group :
       left.groups()) {
    bool matched = false;
    for (const ViewportPersistentOverlayRasterGroup &right_group :
         right.groups()) {
      if (left_group.layer_mask == right_group.layer_mask &&
          viewport_persistent_overlay_wire_styles_match(left_group.style,
                                                         right_group.style)) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      return false;
    }
  }
  return true;
}

} // namespace quader
