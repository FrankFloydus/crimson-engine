////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once
#include "render/instance_batch_planner.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
namespace quader {
/// Reports deterministic renderer-neutral work performed by one batch prepare.
struct InstanceSelectionOverlayPlanWorkStats final {
  std::size_t fill_record_index_entry_count = 0u;
  std::size_t wire_record_index_entry_count = 0u;
  std::size_t fill_chunk_record_lookup_count = 0u;
  std::size_t wire_chunk_record_lookup_count = 0u;
  std::size_t local_bounds_cache_hit_count = 0u;
  std::uint64_t local_bounds_vertex_visit_count = 0u;
};

/// Groups renderer-neutral selected-instance fill and wire batch plans.
struct InstanceSelectionOverlayBatchPlans final {
  RenderInstanceBatchPlan fills;
  RenderInstanceBatchPlan wires;
  InstanceSelectionOverlayPlanWorkStats work;
};

/// Stores one renderer-neutral segment for an instance selection wire.
struct InstanceSelectionWireEdge final {
  QVec3 start;
  QVec3 end;
};

/**
 * Copies the asset-owned semantic wire projection for cache upload.
 */
[[nodiscard]] std::vector<InstanceSelectionWireEdge>
instance_selection_wire_edges(
    const RenderOverlayInstanceSelectionWireRecord &record);

[[nodiscard]] InstanceSelectionOverlayBatchPlans
plan_instance_selection_overlay_batches(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    std::size_t max_instances_per_chunk);

} // namespace quader
