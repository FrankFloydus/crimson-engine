////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/overlay/overlay_mesh_types.hpp"
#include "render/viewport_overlay_frame.hpp"

#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace quader {

/// Stable edge slots retain endpoint buffers while selection changes draw indices.
/// Removed identities are erased immediately. Capacity never exceeds four times
/// the accepted active population; an empty or incompatible frame clears the arena.
class OverlayEditWireRetention final {
public:
  /// False means the caller must use the generic layer builder. No source edge
  /// may have ambiguous occurrence identity or a different width/depth policy.
  bool prepare(std::span<const ViewportOverlayLine> lines,
               ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire);
  void clear();

  [[nodiscard]] std::span<const ViewportOverlayLine> edge_slots() const { return slots_; }
  [[nodiscard]] std::span<const std::uint32_t> dirty_slots() const { return dirty_slots_; }
  [[nodiscard]] std::span<const std::uint32_t> active_indices() const { return indices_; }
  [[nodiscard]] std::span<const OverlayPositionStreamBinding> active_bindings() const {
    return bindings_;
  }
  [[nodiscard]] bool indices_changed() const { return indices_changed_; }
  [[nodiscard]] bool requires_recreation() const { return requires_recreation_; }
  [[nodiscard]] float width_pixels() const { return width_pixels_; }
  [[nodiscard]] std::size_t active_edge_count() const { return active_slots_.size(); }

private:
  /// Identifies one authored edge without conflating separate object occurrences.
  struct EdgeKey {
    int object = -1;
    int first = -1;
    int second = -1;
    [[nodiscard]] bool operator==(const EdgeKey &) const = default;
  };
  /// Hashes the full identity; exact equality remains the slot-reuse authority.
  struct EdgeKeyHash {
    std::size_t operator()(const EdgeKey &key) const;
  };
  std::unordered_map<EdgeKey, std::uint32_t, EdgeKeyHash> slot_by_edge_;
  std::vector<ViewportOverlayLine> slots_;
  std::vector<std::uint32_t> active_slots_;
  std::vector<std::uint32_t> dirty_slots_;
  std::vector<std::uint32_t> indices_;
  std::vector<OverlayPositionStreamBinding> bindings_;
  float width_pixels_ = 1.0f;
  bool indices_changed_ = false;
  bool requires_recreation_ = false;
};

} // namespace quader
