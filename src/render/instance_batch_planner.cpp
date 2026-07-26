////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/instance_batch_planner.hpp"

#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <unordered_map>

namespace quader {
namespace {

/// Hashes the complete compatibility key used to group instanced primitives.
struct PrimitiveKeyHash final {
  std::size_t operator()(const RenderInstancePrimitiveKey &key) const noexcept {
    RenderSignatureBuilder signature;
    signature.add_value(key.geometry_payload)
        .add_value(key.material_payload)
        .add_value(key.index_offset)
        .add_value(key.index_count)
        .add_value(key.layer_mask)
        .add_value(key.channel)
        .add_value(key.presentation_flags)
        .add_bool(key.cast_shadows)
        .add_bool(key.receive_shadows)
        .add_bool(key.double_sided)
        .add_bool(key.editor_unlit);
    return static_cast<std::size_t>(signature.value());
  }
};

/// Compares every slot-layout field while allowing immutable geometry storage
/// to advance. The full key remains different so a cache cannot reuse stale
/// geometry buffers.
bool same_slot_key(const RenderInstancePrimitiveKey &left,
                   const RenderInstancePrimitiveKey &right) {
  return left.material_payload == right.material_payload &&
         left.index_offset == right.index_offset &&
         left.index_count == right.index_count &&
         left.layer_mask == right.layer_mask &&
         left.channel == right.channel &&
         left.presentation_flags == right.presentation_flags &&
         left.cast_shadows == right.cast_shadows &&
         left.receive_shadows == right.receive_shadows &&
         left.double_sided == right.double_sided &&
         left.editor_unlit == right.editor_unlit;
}

/// Identifies one semantic occurrence slot independently of its current
/// immutable geometry allocation.
struct PrimitiveSlotKey final {
  RenderInstancePrimitiveKey key;
  std::uint64_t occurrence = 0u;

  friend bool operator==(const PrimitiveSlotKey &left,
                         const PrimitiveSlotKey &right) {
    return left.occurrence == right.occurrence &&
           same_slot_key(left.key, right.key);
  }
};

/// Hashes a primitive slot together with its stable occurrence ordinal.
struct PrimitiveSlotKeyHash final {
  std::size_t operator()(const PrimitiveSlotKey &slot) const noexcept {
    RenderSignatureBuilder signature;
    signature.add_value(slot.key.material_payload)
        .add_value(slot.key.index_offset)
        .add_value(slot.key.index_count)
        .add_value(slot.key.layer_mask)
        .add_value(slot.key.channel)
        .add_value(slot.key.presentation_flags)
        .add_bool(slot.key.cast_shadows)
        .add_bool(slot.key.receive_shadows)
        .add_bool(slot.key.double_sided)
        .add_bool(slot.key.editor_unlit)
        .add_value(slot.occurrence);
    return static_cast<std::size_t>(signature.value());
  }
};

/// Collects compatible occurrences before Morton ordering and device chunking.
struct PrimitiveGroup final {
  /// Stores one source occurrence plus its derived bounds and spatial key.
  struct Occurrence final {
    const RenderInstancePrimitiveOccurrence *source = nullptr;
    RenderInstanceBounds world_bounds;
    std::uint32_t morton = 0u;
    std::size_t input_index = 0u;
  };

  RenderInstancePrimitiveKey key;
  std::vector<Occurrence> occurrences;
};

void include_point(RenderInstanceBounds &bounds, QVec3 point) {
  if (!bounds.present) {
    bounds.min = point;
    bounds.max = point;
    bounds.present = true;
    return;
  }
  bounds.min.x = std::min(bounds.min.x, point.x);
  bounds.min.y = std::min(bounds.min.y, point.y);
  bounds.min.z = std::min(bounds.min.z, point.z);
  bounds.max.x = std::max(bounds.max.x, point.x);
  bounds.max.y = std::max(bounds.max.y, point.y);
  bounds.max.z = std::max(bounds.max.z, point.z);
}

void include_transformed_bounds(RenderInstanceBounds &target,
                                const RenderInstanceBounds &local,
                                const QMat4 &world_transform) {
  if (!local.present) {
    return;
  }
  const std::array<QVec3, 8u> corners{{
      {local.min.x, local.min.y, local.min.z},
      {local.max.x, local.min.y, local.min.z},
      {local.min.x, local.max.y, local.min.z},
      {local.max.x, local.max.y, local.min.z},
      {local.min.x, local.min.y, local.max.z},
      {local.max.x, local.min.y, local.max.z},
      {local.min.x, local.max.y, local.max.z},
      {local.max.x, local.max.y, local.max.z},
  }};
  for (const QVec3 corner : corners) {
    include_point(target, transform_point(world_transform, corner));
  }
}

QVec3 bounds_center(const RenderInstanceBounds &bounds) {
  return bounds.present ? (bounds.min + bounds.max) * 0.5f : QVec3{};
}

std::uint32_t spread_morton_bits(std::uint32_t value) {
  value &= 0x000003ffu;
  value = (value | (value << 16u)) & 0x030000ffu;
  value = (value | (value << 8u)) & 0x0300f00fu;
  value = (value | (value << 4u)) & 0x030c30c3u;
  value = (value | (value << 2u)) & 0x09249249u;
  return value;
}

std::uint32_t quantized_morton_axis(float value, float min_value,
                                    float max_value) {
  const float extent = max_value - min_value;
  if (!(extent > std::numeric_limits<float>::epsilon())) {
    return 0u;
  }
  const float normalized =
      std::clamp((value - min_value) / extent, 0.0f, 1.0f);
  return static_cast<std::uint32_t>(normalized * 1023.0f + 0.5f);
}

void morton_sort_group(PrimitiveGroup &group) {
  RenderInstanceBounds center_bounds;
  for (PrimitiveGroup::Occurrence &occurrence : group.occurrences) {
    include_point(center_bounds, bounds_center(occurrence.world_bounds));
  }
  for (PrimitiveGroup::Occurrence &occurrence : group.occurrences) {
    const QVec3 center = bounds_center(occurrence.world_bounds);
    const std::uint32_t x = quantized_morton_axis(
        center.x, center_bounds.min.x, center_bounds.max.x);
    const std::uint32_t y = quantized_morton_axis(
        center.y, center_bounds.min.y, center_bounds.max.y);
    const std::uint32_t z = quantized_morton_axis(
        center.z, center_bounds.min.z, center_bounds.max.z);
    occurrence.morton = spread_morton_bits(x) |
                        (spread_morton_bits(y) << 1u) |
                        (spread_morton_bits(z) << 2u);
  }
  std::stable_sort(
      group.occurrences.begin(), group.occurrences.end(),
      [](const PrimitiveGroup::Occurrence &left,
         const PrimitiveGroup::Occurrence &right) {
        if (left.morton != right.morton) {
          return left.morton < right.morton;
        }
        if (left.source->occurrence != right.source->occurrence) {
          return left.source->occurrence < right.source->occurrence;
        }
        return left.input_index < right.input_index;
      });
}

std::uint64_t transform_signature(
    std::span<const QMat4> world_transforms,
    std::span<const std::uint8_t> visibility) {
  RenderSignatureBuilder signature;
  signature.add_size(world_transforms.size());
  for (std::size_t index = 0u; index < world_transforms.size(); ++index) {
    const bool visible = index >= visibility.size() || visibility[index] != 0u;
    signature.add_bool(visible);
    if (visible) {
      signature.add_mat4(world_transforms[index]);
    }
  }
  return signature.value();
}

bool same_layout(const RenderInstanceBatchChunk &left,
                 const RenderInstanceBatchChunk &right) {
  return left.key == right.key && left.occurrences == right.occurrences &&
         left.world_transforms.size() == right.world_transforms.size() &&
         left.visibility.size() == right.visibility.size();
}

void append_occurrence(RenderInstanceBatchChunk &chunk,
                       const PrimitiveGroup::Occurrence &planned) {
  const RenderInstancePrimitiveOccurrence &occurrence = *planned.source;
  chunk.occurrences.push_back(occurrence.occurrence);
  chunk.world_transforms.push_back(occurrence.world_transform);
  chunk.visibility.push_back(occurrence.visible ? 1u : 0u);
  if (occurrence.visible) {
    ++chunk.visible_occurrence_count;
  }
  if (occurrence.visible && planned.world_bounds.present) {
    include_point(chunk.world_bounds, planned.world_bounds.min);
    include_point(chunk.world_bounds, planned.world_bounds.max);
  }
}

void finish_chunk(RenderInstanceBatchChunk &chunk) {
  chunk.transform_signature =
      transform_signature(chunk.world_transforms, chunk.visibility);
}

bool try_retain_layout(const std::vector<PrimitiveGroup> &groups,
                       const RenderInstanceBatchPlan &previous,
                       std::size_t occurrence_count,
                       std::size_t max_instances_per_chunk,
                       RenderInstanceBatchPlan &plan) {
  QDR_PROFILE_FUNCTION();
  if (previous.max_instances_per_chunk != max_instances_per_chunk ||
      previous.occurrence_count != occurrence_count ||
      previous.chunks.empty()) {
    return false;
  }

  std::unordered_map<PrimitiveSlotKey, const PrimitiveGroup::Occurrence *,
                     PrimitiveSlotKeyHash>
      occurrence_by_slot;
  occurrence_by_slot.reserve(occurrence_count);
  for (const PrimitiveGroup &group : groups) {
    for (const PrimitiveGroup::Occurrence &occurrence : group.occurrences) {
      const auto [unused, inserted] = occurrence_by_slot.emplace(
          PrimitiveSlotKey{.key = group.key,
                           .occurrence = occurrence.source->occurrence},
          &occurrence);
      (void)unused;
      if (!inserted) {
        // A duplicated semantic slot is ambiguous. Rebuild the spatial plan
        // instead of silently binding one occurrence's transform to another.
        return false;
      }
    }
  }

  RenderInstanceBatchPlan retained;
  retained.max_instances_per_chunk = max_instances_per_chunk;
  retained.occurrence_count = occurrence_count;
  retained.chunks.reserve(previous.chunks.size());
  for (const RenderInstanceBatchChunk &before : previous.chunks) {
    if (before.occurrences.empty() ||
        before.occurrences.size() > max_instances_per_chunk ||
        before.world_transforms.size() != before.occurrences.size() ||
        before.visibility.size() != before.occurrences.size() ||
        before.visible_occurrence_count > before.occurrences.size()) {
      return false;
    }

    RenderInstanceBatchChunk chunk;
    chunk.occurrences.reserve(before.occurrences.size());
    chunk.world_transforms.reserve(before.occurrences.size());
    chunk.visibility.reserve(before.occurrences.size());
    bool has_key = false;
    for (const std::uint64_t occurrence_id : before.occurrences) {
      const auto found = occurrence_by_slot.find(
          PrimitiveSlotKey{.key = before.key, .occurrence = occurrence_id});
      if (found == occurrence_by_slot.end()) {
        return false;
      }
      const PrimitiveGroup::Occurrence &occurrence = *found->second;
      if (!has_key) {
        chunk.key = occurrence.source->key;
        has_key = true;
      } else if (chunk.key != occurrence.source->key) {
        // One former chunk now spans multiple immutable payloads. It must be
        // rechunked so no proxy binds the wrong geometry or material.
        return false;
      }
      append_occurrence(chunk, occurrence);
      occurrence_by_slot.erase(found);
    }
    finish_chunk(chunk);
    retained.chunks.push_back(std::move(chunk));
  }

  if (!occurrence_by_slot.empty()) {
    return false;
  }
  plan = std::move(retained);
  return true;
}

} // namespace

RenderInstanceBatchPlan plan_render_instance_batches(
    std::span<const RenderInstancePrimitiveOccurrence> occurrences,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout) {
  QDR_PROFILE_FUNCTION();
  RenderInstanceBatchPlan plan;
  plan.max_instances_per_chunk = std::max<std::size_t>(
      1u, max_instances_per_chunk);
  plan.occurrence_count = occurrences.size();
  if (occurrences.empty()) {
    return plan;
  }

  std::vector<PrimitiveGroup> groups;
  groups.reserve(occurrences.size());
  std::unordered_map<RenderInstancePrimitiveKey, std::size_t,
                     PrimitiveKeyHash>
      group_by_key;
  group_by_key.reserve(occurrences.size());
  for (std::size_t input_index = 0u; input_index < occurrences.size();
       ++input_index) {
    const RenderInstancePrimitiveOccurrence &occurrence =
        occurrences[input_index];
    const auto [found, inserted] =
        group_by_key.try_emplace(occurrence.key, groups.size());
    if (inserted) {
      groups.push_back(PrimitiveGroup{.key = occurrence.key});
    }
    RenderInstanceBounds world_bounds;
    include_transformed_bounds(world_bounds, occurrence.local_bounds,
                               occurrence.world_transform);
    groups[found->second].occurrences.push_back({
        .source = &occurrence,
        .world_bounds = world_bounds,
        .input_index = input_index,
    });
  }

  if (retained_layout != nullptr) {
    QDR_PROFILE_SCOPE("Instance batch retain semantic slots");
    if (try_retain_layout(groups, *retained_layout, occurrences.size(),
                          plan.max_instances_per_chunk, plan)) {
      return plan;
    }
  }

  {
    QDR_PROFILE_SCOPE("Instance batch rebuild Morton layout");
    for (PrimitiveGroup &group : groups) {
      morton_sort_group(group);
      const std::size_t chunk_count =
          (group.occurrences.size() + plan.max_instances_per_chunk - 1u) /
          plan.max_instances_per_chunk;
      plan.chunks.reserve(plan.chunks.size() + chunk_count);
      for (std::size_t offset = 0u; offset < group.occurrences.size();
           offset += plan.max_instances_per_chunk) {
        const std::size_t end = std::min(
            group.occurrences.size(), offset + plan.max_instances_per_chunk);
        RenderInstanceBatchChunk chunk;
        chunk.key = group.key;
        chunk.occurrences.reserve(end - offset);
        chunk.world_transforms.reserve(end - offset);
        chunk.visibility.reserve(end - offset);
        for (std::size_t index = offset; index < end; ++index) {
          const PrimitiveGroup::Occurrence &planned =
              group.occurrences[index];
          append_occurrence(chunk, planned);
        }
        finish_chunk(chunk);
        plan.chunks.push_back(std::move(chunk));
      }
    }
  }
  return plan;
}

RenderInstanceBatchUpdateKind classify_instance_batch_update(
    const RenderInstanceBatchPlan &previous,
    const RenderInstanceBatchPlan &next) {
  if (previous.max_instances_per_chunk != next.max_instances_per_chunk ||
      previous.occurrence_count != next.occurrence_count ||
      previous.chunks.size() != next.chunks.size()) {
    return RenderInstanceBatchUpdateKind::Rebuild;
  }

  bool transforms_changed = false;
  for (std::size_t index = 0u; index < previous.chunks.size(); ++index) {
    const RenderInstanceBatchChunk &before = previous.chunks[index];
    const RenderInstanceBatchChunk &after = next.chunks[index];
    if (!same_layout(before, after)) {
      return RenderInstanceBatchUpdateKind::Rebuild;
    }
    transforms_changed = transforms_changed ||
                         before.transform_signature !=
                             after.transform_signature ||
                         before.world_bounds != after.world_bounds;
  }
  return transforms_changed ? RenderInstanceBatchUpdateKind::TransformsOnly
                            : RenderInstanceBatchUpdateKind::Unchanged;
}

RenderInstanceBounds
render_instance_local_bounds(std::span<const QVec3> positions) {
  RenderInstanceBounds bounds;
  for (const QVec3 position : positions) {
    include_point(bounds, position);
  }
  return bounds;
}

} // namespace quader
