////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "math/types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace quader {

/// Selects the renderer presentation variant that participates in a batch key.
enum class RenderInstancePresentationFlag : std::uint8_t {
  None = 0u,
  Dimmed = 1u << 0u,
};

/// Identifies immutable render payloads without scene or asset identities.
struct RenderInstancePrimitiveKey final {
  std::uint64_t geometry_payload = 0u;
  std::uint64_t material_payload = 0u;
  std::uint32_t index_offset = 0u;
  std::uint32_t index_count = 0u;
  std::uint8_t layer_mask = 0u;
  std::uint8_t channel = 0u;
  std::uint8_t presentation_flags = 0u;
  bool cast_shadows = false;
  bool receive_shadows = false;
  bool double_sided = false;
  bool editor_unlit = false;

  friend bool operator==(const RenderInstancePrimitiveKey &,
                         const RenderInstancePrimitiveKey &) = default;
};

/// Stores one finite axis-aligned box used by renderer-neutral batch planning.
struct RenderInstanceBounds final {
  QVec3 min;
  QVec3 max;
  bool present = false;

  friend bool operator==(const RenderInstanceBounds &left,
                         const RenderInstanceBounds &right) {
    return left.min.x == right.min.x && left.min.y == right.min.y &&
           left.min.z == right.min.z && left.max.x == right.max.x &&
           left.max.y == right.max.y && left.max.z == right.max.z &&
           left.present == right.present;
  }
};

/// Supplies one transform of one primitive payload to the instance planner.
struct RenderInstancePrimitiveOccurrence final {
  RenderInstancePrimitiveKey key;
  std::uint64_t occurrence = 0u;
  QMat4 world_transform;
  QMat4 sampling_from_local;
  RenderInstanceBounds local_bounds;
  /** Retains a stable batch slot while excluding this occurrence from draw. */
  bool visible = true;
};

/// Describes one device-cap-sized indexed-instanced draw.
struct RenderInstanceBatchChunk final {
  RenderInstancePrimitiveKey key;
  std::vector<std::uint64_t> occurrences;
  std::vector<QMat4> world_transforms;
  std::vector<QMat4> sampling_from_local;
  /** One byte per stable occurrence slot; hidden slots are not submitted. */
  std::vector<std::uint8_t> visibility;
  std::size_t visible_occurrence_count = 0u;
  RenderInstanceBounds world_bounds;
  std::uint64_t transform_signature = 0u;
};

/// Records storage growth and spatial ordering for the published batch plan.
struct RenderInstanceBatchPlanWorkStats final {
  std::size_t chunk_storage_growth_count = 0u;
  std::size_t relocated_chunk_count = 0u;
  std::size_t morton_ordered_occurrence_count = 0u;
  bool retained_layout = false;
};

/// Owns the deterministic primitive/chunk plan for one render snapshot.
struct RenderInstanceBatchPlan final {
  std::size_t max_instances_per_chunk = 1u;
  std::size_t occurrence_count = 0u;
  std::vector<RenderInstanceBatchChunk> chunks;
  RenderInstanceBatchPlanWorkStats work;
};

/// Selects the least expensive synchronization path between two plans.
enum class RenderInstanceBatchUpdateKind : std::uint8_t {
  Unchanged,
  TransformsOnly,
  Rebuild,
};

/**
 * Groups primitive occurrences by immutable payload and splits them at the
 * backend instance limit. When retained_layout still describes the same
 * semantic slots, its occurrence/chunk order is preserved so transform,
 * visibility, bounds, and one-payload-per-chunk geometry updates cannot
 * reshuffle retained slots. Geometry-token changes still classify as rebuilds
 * so the affected proxy can bind current buffers. A null or incompatible
 * retained layout rebuilds the spatial Morton plan.
 */
[[nodiscard]] RenderInstanceBatchPlan plan_render_instance_batches(
    std::span<const RenderInstancePrimitiveOccurrence> occurrences,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout = nullptr);

/** Returns whether a new plan can update only existing InstanceBuffers. */
[[nodiscard]] RenderInstanceBatchUpdateKind classify_instance_batch_update(
    const RenderInstanceBatchPlan &previous,
    const RenderInstanceBatchPlan &next);

/** Computes local bounds directly from an immutable position payload. */
[[nodiscard]] RenderInstanceBounds render_instance_local_bounds(
    std::span<const QVec3> positions);

/** Expands one local box across a compact span of world transforms. */
[[nodiscard]] RenderInstanceBounds render_instance_world_bounds(
    const RenderInstanceBounds &local_bounds,
    std::span<const QMat4> world_transforms);

} // namespace quader
