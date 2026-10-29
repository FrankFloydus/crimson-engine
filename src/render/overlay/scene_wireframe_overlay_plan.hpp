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
[[nodiscard]] constexpr bool scene_wireframe_style_changed(
    bool has_synced, std::uint64_t synced_signature,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return !has_synced || synced_signature != style.wire_signature;
}

/// Reports deterministic renderer-neutral work performed by one batch prepare.
struct SceneWireframeOverlayPlanWorkStats final {
  std::size_t primitive_record_index_entry_count = 0u;
  std::size_t chunk_record_lookup_count = 0u;
  std::size_t local_bounds_cache_hit_count = 0u;
  std::uint64_t local_bounds_vertex_visit_count = 0u;
};

/// Carries the renderer-neutral full-scene wire plan and sharing counts.
struct SceneWireframeOverlayBatchPlan final {
  RenderInstanceBatchPlan batches;
  std::size_t participant_count = 0u;
  std::size_t unique_geometry_style_count = 0u;
  SceneWireframeOverlayPlanWorkStats work;
};

/**
 * Groups ordinary meshes and expanded formal-instance members by immutable
 * topology/style, then chunks each compatible group at the backend limit.
 */
[[nodiscard]] SceneWireframeOverlayBatchPlan
plan_scene_wireframe_overlay_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout = nullptr);

/// Compares pinned mesh inputs without allocating or inspecting mesh vertices.
/// Scene/asset revisions may advance for unrelated curves, lights or materials.
[[nodiscard]] bool scene_wireframe_overlay_mesh_inputs_match(
    std::span<const RenderOverlayMeshSnapshot> previous,
    std::span<const RenderOverlayMeshSnapshot> next);

/// Resolves an optional mesh RGB override with the consuming pane's opacity.
[[nodiscard]] ViewportSettingColor scene_wireframe_shaded_color(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style);

} // namespace quader
