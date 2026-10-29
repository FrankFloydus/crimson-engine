////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_retained_edit_wire_update.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace quader {

std::shared_ptr<OverlayProxyFactory::RetainedEditWireUpdate>
OverlayProxyFactory::prepare_retained_edit_wire_update(
    const OverlayRenderableProxy *existing,
    std::span<const ViewportOverlayLine> slots,
    std::span<const std::uint32_t> dirty_slots,
    std::span<const std::uint32_t> active_indices,
    std::span<const OverlayPositionStreamBinding> active_bindings,
    bool indices_changed) {
  QDR_PROFILE_FUNCTION();
  constexpr std::size_t kMaximumSlots =
      std::numeric_limits<std::uint32_t>::max() /
      (4u * sizeof(RetainedEditWireVertex));
  if (slots.empty() || slots.size() > kMaximumSlots ||
      active_indices.empty() || active_indices.size() % 6u != 0u ||
      active_indices.size() > slots.size() * 6u) {
    return {};
  }
  const auto vertex_count = static_cast<std::uint32_t>(slots.size() * 4u);
  if (existing != nullptr &&
      (existing->kind != OverlayRenderableProxyKind::EditWire ||
       existing->edit_wire_slot_capacity != slots.size() ||
       existing->vertex_count != vertex_count ||
       existing->vertex_buffer == nullptr || existing->index_buffer == nullptr ||
       existing->material_instance == nullptr || existing->entity_id == 0u ||
       existing->primary_position_stream4.size() != vertex_count ||
       existing->secondary_position_stream4.size() != vertex_count ||
       existing->support0_position_stream4.size() != vertex_count ||
       existing->support1_position_stream4.size() != vertex_count ||
       (!indices_changed && existing->index_count != active_indices.size()))) {
    return {};
  }
  constexpr std::array<std::uint32_t, 6u> kQuadIndices{0u, 1u, 2u, 0u, 2u, 3u};
  for (std::size_t index = 0u; index < active_indices.size(); index += 6u) {
    const auto first = active_indices[index];
    if (first % 4u != 0u || first >= vertex_count) {
      return {};
    }
    for (std::size_t corner = 0u; corner < kQuadIndices.size(); ++corner) {
      if (active_indices[index + corner] != first + kQuadIndices[corner]) {
        return {};
      }
    }
  }
  for (std::size_t index = 0u; index < dirty_slots.size(); ++index) {
    if (dirty_slots[index] >= slots.size() ||
        (index != 0u && dirty_slots[index - 1u] >= dirty_slots[index])) {
      return {};
    }
  }
  const auto finite_position = [](QVec3 point) {
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
  };
  const auto finite_line = [&finite_position](const ViewportOverlayLine &line) {
    if (!finite_position(line.start) || !finite_position(line.end)) return false;
    for (const auto &support : line.surface_support) {
      if (support.vertex_id >= 0 && !finite_position(support.position)) return false;
    }
    return true;
  };
  if (existing == nullptr) {
    for (const auto &line : slots) {
      if (!finite_line(line)) return {};
    }
  } else {
    for (const auto slot : dirty_slots) {
      if (!finite_line(slots[slot])) return {};
    }
  }
  for (const auto &binding : active_bindings) {
    if (binding.object_id < 0 || binding.vertex_id < 0 ||
        binding.first_vertex % 4u != 0u || binding.first_vertex >= vertex_count ||
        binding.vertex_count != 4u) {
      return {};
    }
    const auto &line = slots[binding.first_vertex / 4u];
    QVec3 position;
    int vertex_id = -1;
    switch (binding.stream) {
      case OverlayPositionStream::Primary:
        position = line.start;
        vertex_id = line.element.edge_a;
        break;
      case OverlayPositionStream::Secondary:
        position = line.end;
        vertex_id = line.element.edge_b;
        break;
      case OverlayPositionStream::Support0:
      case OverlayPositionStream::Support1: {
        const auto index = binding.stream == OverlayPositionStream::Support0 ? 0u : 1u;
        const auto &support = line.surface_support[index];
        position = support.vertex_id >= 0 ? support.position : line.start;
        vertex_id = support.vertex_id >= 0 ? support.vertex_id : line.element.edge_a;
        break;
      }
      default:
        return {};
    }
    if (binding.object_id != line.element.object_id ||
        binding.vertex_id != vertex_id ||
        binding.base_position.x != position.x || binding.base_position.y != position.y ||
        binding.base_position.z != position.z || binding.base_position.w != 1.0f) {
      return {};
    }
  }

  auto prepared = std::make_shared<RetainedEditWireUpdate>();
  prepared->slot_capacity = static_cast<std::uint32_t>(slots.size());
  prepared->index_count = static_cast<std::uint32_t>(active_indices.size());
  prepared->indices_changed = existing == nullptr || indices_changed;
  prepared->bindings.assign(active_bindings.begin(), active_bindings.end());
  if (existing != nullptr) {
    prepared->expected_vertices = existing->vertex_buffer;
    prepared->expected_indices = existing->index_buffer;
    prepared->expected_entity = existing->entity_id;
    prepared->expected_revision = existing->edit_wire_update_revision;
  }
  if (prepared->indices_changed) {
    prepared->index_upload = RetainedEditWireUpload::span(active_indices);
  }
  const auto append_range = [&](std::uint32_t first_slot, std::uint32_t end_slot,
                                bool preserve_unchanged = false) {
    auto &range = prepared->ranges.emplace_back();
    range.first_vertex = first_slot * 4u;
    const std::size_t count = static_cast<std::size_t>(end_slot - first_slot) * 4u;
    std::vector<RetainedEditWireVertex> vertices;
    vertices.reserve(count);
    range.starts.reserve(count);
    range.ends.reserve(count);
    range.support0.reserve(count);
    range.support1.reserve(count);
    constexpr std::array<OverlayFloat4, 4u> kControls{{
        {-1.0f, -1.0f, 0.0f, 0.0f}, {-1.0f, 1.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, -1.0f, 0.0f, 0.0f}}};
    auto dirty = std::lower_bound(dirty_slots.begin(), dirty_slots.end(), first_slot);
    for (std::uint32_t slot = first_slot; slot < end_slot; ++slot) {
      const auto &line = slots[slot];
      const auto color = rgba8_srgb_to_linear(line.color);
      const bool changed = !preserve_unchanged ||
                           (dirty != dirty_slots.end() && *dirty == slot);
      if (dirty != dirty_slots.end() && *dirty == slot) ++dirty;
      for (std::size_t corner = 0u; corner < kControls.size(); ++corner) {
        auto control = kControls[corner];
        control.w = line.style_role == ViewportOverlayStyleRole::ActiveSelectionWire ? 1.0f : 0.0f;
        vertices.push_back({{0.0f, 0.0f, 0.0f, 1.0f}, control, color});
        const std::size_t vertex = static_cast<std::size_t>(slot) * 4u + corner;
        // A consolidated gap may currently carry preview positions. Keep its
        // exact accepted endpoints rather than resetting it to the frame base.
        range.starts.push_back(changed
            ? OverlayFloat4{line.start.x, line.start.y, line.start.z, 1.0f}
            : existing->primary_position_stream4[vertex]);
        range.ends.push_back(changed
            ? OverlayFloat4{line.end.x, line.end.y, line.end.z, 1.0f}
            : existing->secondary_position_stream4[vertex]);
        const auto support_position = [&line](std::size_t index) {
          const auto &support = line.surface_support[index];
          const auto position = support.vertex_id >= 0 ? support.position : line.start;
          return OverlayFloat4{position.x, position.y, position.z, 1.0f};
        };
        range.support0.push_back(changed ? support_position(0u)
            : existing->support0_position_stream4[vertex]);
        range.support1.push_back(changed ? support_position(1u)
            : existing->support1_position_stream4[vertex]);
      }
    }
    range.static_upload = RetainedEditWireUpload::vector(std::move(vertices));
    range.start_upload = RetainedEditWireUpload::vector(range.starts);
    range.end_upload = RetainedEditWireUpload::vector(range.ends);
    range.support0_upload = RetainedEditWireUpload::vector(range.support0);
    range.support1_upload = RetainedEditWireUpload::vector(range.support1);
  };
  if (existing == nullptr) {
    append_range(0u, prepared->slot_capacity);
  } else {
    constexpr std::size_t kMaximumPartialRanges = 64u;
    std::size_t range_count = 0u;
    for (std::size_t index = 0u; index < dirty_slots.size(); ++index) {
      range_count += index == 0u || dirty_slots[index - 1u] + 1u != dirty_slots[index];
    }
    if (range_count > kMaximumPartialRanges) {
      // Each range emits five backend commands. A committed deformation can
      // dirty tens of thousands of isolated edges; stage one bounded submission
      // instead of overflowing Filament's fixed command arena before beginFrame.
      prepared->consolidated = true;
      append_range(dirty_slots.front(), dirty_slots.back() + 1u, true);
    } else {
      for (std::size_t index = 0u; index < dirty_slots.size();) {
        const auto first = dirty_slots[index++];
        auto end = first + 1u;
        while (index < dirty_slots.size() && dirty_slots[index] == end) {
          ++index;
          ++end;
        }
        append_range(first, end);
      }
    }
  }
  return prepared;
}

} // namespace quader
