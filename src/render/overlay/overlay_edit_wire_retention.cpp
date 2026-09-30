////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_edit_wire_retention.hpp"

#include "diagnostics/profile.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quader {
namespace {

bool same_endpoint(QVec3 left, QVec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

bool finite_endpoint(QVec3 point) {
  return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

bool same_slot(const ViewportOverlayLine &left, const ViewportOverlayLine &right) {
  return same_endpoint(left.start, right.start) && same_endpoint(left.end, right.end) &&
      left.color == right.color && left.element.object_id == right.element.object_id &&
      left.element.edge_a == right.element.edge_a && left.element.edge_b == right.element.edge_b;
}

} // namespace

std::size_t OverlayEditWireRetention::EdgeKeyHash::operator()(const EdgeKey &key) const {
  std::size_t hash = static_cast<std::uint32_t>(key.object);
  hash = hash * 16777619u ^ static_cast<std::uint32_t>(key.first);
  return hash * 16777619u ^ static_cast<std::uint32_t>(key.second);
}

bool OverlayEditWireRetention::prepare(std::span<const ViewportOverlayLine> lines,
                                       ViewportOverlayLayer layer) {
  QDR_PROFILE_FUNCTION();
  const auto prepare_ordered = [&]() {
    if (active_slots_.empty()) return false;
    std::size_t source_count = 0u;
    float width = 0.0f;
    for (const auto &line : lines) {
      if (line.layer != layer) continue;
      if (source_count == active_slots_.size()) return false;
      const auto &previous = slots_[active_slots_[source_count]];
      const float line_width = viewport_overlay_line_width_pixels(line.layer, line.width_pixels);
      if (line.source != ViewportOverlaySource::ComponentSelection ||
          !finite_endpoint(line.start) || !finite_endpoint(line.end) ||
          !std::isfinite(line_width) || (source_count != 0u && line_width != width) ||
          line.element.object_id != previous.element.object_id ||
          line.element.edge_a != previous.element.edge_a ||
          line.element.edge_b != previous.element.edge_b) {
        return false;
      }
      width = line_width;
      ++source_count;
    }
    if (source_count != active_slots_.size()) return false;

    // Exact ordered, directed identities inherit the accepted population's
    // validity and uniqueness. Validate all inputs before touching retained data.
    QDR_PROFILE_SCOPE("Overlay refresh ordered source wire slots");
    // Reuse this buffer as slot flags, then compact it in slot order. Capacity
    // was established by the initial full population; its bounded scan avoids
    // sorting dirty slots after a previous membership reorder.
    dirty_slots_.assign(slots_.size(), 0u);
    std::size_t source_index = 0u;
    for (const auto &line : lines) {
      if (line.layer != layer) continue;
      const auto slot = active_slots_[source_index];
      if (!same_slot(slots_[slot], line)) {
        slots_[slot] = line;
        dirty_slots_[slot] = 1u;
        bindings_[source_index * 2u].base_position =
            {line.start.x, line.start.y, line.start.z, 1.0f};
        bindings_[source_index * 2u + 1u].base_position =
            {line.end.x, line.end.y, line.end.z, 1.0f};
      }
      ++source_index;
    }
    std::size_t dirty_count = 0u;
    for (std::size_t slot = 0u; slot < dirty_slots_.size(); ++slot) {
      if (dirty_slots_[slot] != 0u) {
        dirty_slots_[dirty_count++] = static_cast<std::uint32_t>(slot);
      }
    }
    dirty_slots_.resize(dirty_count);
    indices_changed_ = false;
    requires_recreation_ = false;
    width_pixels_ = width;
    return true;
  };
  if (prepare_ordered()) return true;

  std::vector<const ViewportOverlayLine *> source;
  source.reserve(lines.size());
  std::unordered_map<EdgeKey, std::uint32_t, EdgeKeyHash> next_slots;
  next_slots.reserve(lines.size());
  float width = 0.0f;
  for (const auto &line : lines) {
    if (line.layer != layer) continue;
    const float line_width = viewport_overlay_line_width_pixels(line.layer, line.width_pixels);
    if (line.source != ViewportOverlaySource::ComponentSelection ||
        line.element.object_id < 0 || line.element.edge_a < 0 || line.element.edge_b < 0 ||
        line.element.edge_a == line.element.edge_b || !finite_endpoint(line.start) ||
        !finite_endpoint(line.end) || !std::isfinite(line_width) ||
        (!source.empty() && line_width != width)) {
      clear();
      return false;
    }
    const EdgeKey key{line.element.object_id, std::min(line.element.edge_a, line.element.edge_b),
                      std::max(line.element.edge_a, line.element.edge_b)};
    if (!next_slots.emplace(key, 0u).second) {
      // Multiple placements of one authored edge require explicit occurrence
      // identity. The generic path preserves their separate ordered geometry.
      clear();
      return false;
    }
    width = line_width;
    source.push_back(&line);
  }
  if (source.empty() || source.size() > std::numeric_limits<std::uint32_t>::max() / 6u) {
    clear();
    return false;
  }
  requires_recreation_ = source.size() > slots_.size() || source.size() * 4u < slots_.size();
  std::vector<std::uint32_t> free_slots;
  if (requires_recreation_) {
    clear();
    requires_recreation_ = true;
    slots_.resize(source.size());
  }
  std::vector<bool> retained(slots_.size(), false);
  for (auto &[key, slot] : next_slots) {
    const auto old = slot_by_edge_.find(key);
    if (old != slot_by_edge_.end()) {
      slot = old->second;
      retained[slot] = true;
    } else {
      slot = std::numeric_limits<std::uint32_t>::max();
    }
  }
  free_slots.reserve(slots_.size());
  for (std::size_t index = slots_.size(); index > 0u; --index) {
    if (!retained[index - 1u]) {
      free_slots.push_back(static_cast<std::uint32_t>(index - 1u));
      slots_[index - 1u] = {};
    }
  }
  dirty_slots_.clear();
  std::vector<std::uint32_t> next_active;
  next_active.reserve(source.size());
  for (const auto *line : source) {
    const EdgeKey key{line->element.object_id, std::min(line->element.edge_a, line->element.edge_b),
                      std::max(line->element.edge_a, line->element.edge_b)};
    auto &slot = next_slots.at(key);
    const bool added = slot == std::numeric_limits<std::uint32_t>::max();
    if (added) {
      slot = free_slots.back();
      free_slots.pop_back();
    }
    if (added || !same_slot(slots_[slot], *line)) {
      slots_[slot] = *line;
      dirty_slots_.push_back(slot);
    }
    next_active.push_back(slot);
  }
  indices_changed_ = requires_recreation_ || next_active != active_slots_;
  if (indices_changed_) {
    indices_.clear();
    indices_.reserve(next_active.size() * 6u);
    for (const auto slot : next_active) {
      const std::uint32_t base = slot * 4u;
      indices_.insert(indices_.end(), {base, base + 1u, base + 2u, base, base + 2u, base + 3u});
    }
  }
  if (indices_changed_ || !dirty_slots_.empty()) {
    bindings_.clear();
    bindings_.reserve(next_active.size() * 2u);
    for (const auto slot : next_active) {
      const auto &line = slots_[slot];
      bindings_.push_back({.object_id = line.element.object_id, .vertex_id = line.element.edge_a,
          .first_vertex = slot * 4u, .vertex_count = 4u, .stream = OverlayPositionStream::Primary,
          .base_position = {line.start.x, line.start.y, line.start.z, 1.0f}});
      bindings_.push_back({.object_id = line.element.object_id, .vertex_id = line.element.edge_b,
          .first_vertex = slot * 4u, .vertex_count = 4u, .stream = OverlayPositionStream::Secondary,
          .base_position = {line.end.x, line.end.y, line.end.z, 1.0f}});
    }
  }
  std::sort(dirty_slots_.begin(), dirty_slots_.end());
  slot_by_edge_ = std::move(next_slots);
  active_slots_ = std::move(next_active);
  width_pixels_ = width;
  return true;
}

void OverlayEditWireRetention::clear() {
  *this = OverlayEditWireRetention{};
}

} // namespace quader
