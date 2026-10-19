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
#include <memory>
#include <unordered_map>
#include <vector>

namespace quader {

/** Godot editor collision colors preserve authored RGB and scale its opacity. */
[[nodiscard]] ViewportSettingColor
collision_shape_fill_color(ViewportSettingColor color);
[[nodiscard]] ViewportSettingColor
collision_shape_wire_color(ViewportSettingColor color, bool selected);
/** Includes triangulation edges, deduplicated by exact snapped endpoints. */
[[nodiscard]] std::shared_ptr<const MeshWireGeometryPayload>
build_collision_shape_wire_geometry(const MeshCollisionGeometryPayload &geometry);

/// Measures renderer-neutral collision preparation across retained frames.
struct CollisionShapeOverlayPlanWorkStats final {
  std::uint64_t prepare_count = 0u;
  std::uint64_t source_record_visit_count = 0u;
  std::uint64_t local_bounds_vertex_visit_count = 0u;
  std::uint64_t local_bounds_cache_hit_count = 0u;
  std::uint64_t geometry_comparison_vertex_visit_count = 0u;
  std::uint64_t wire_geometry_build_count = 0u;
  std::uint64_t wire_prepare_count = 0u;
};

/// Separates uniform changes from triangle and placement work.
struct CollisionShapeOverlayUpdate final {
  RenderInstanceBatchUpdateKind batches =
      RenderInstanceBatchUpdateKind::Unchanged;
  bool color_changed = false;
  bool visibility_changed = false;
  bool source_changed = false;
  RenderInstanceBatchUpdateKind wire_batches =
      RenderInstanceBatchUpdateKind::Unchanged;
};

/// Retains bounds and instance slots without requiring a Filament engine.
class CollisionShapeOverlayPlanner final {
public:
  [[nodiscard]] CollisionShapeOverlayUpdate prepare(
      const RenderOverlayCollisionSnapshot &snapshot,
      std::size_t max_instances_per_chunk);
  void clear();

  [[nodiscard]] const RenderInstanceBatchPlan &plan() const { return plan_; }
  [[nodiscard]] const RenderInstanceBatchPlan &wire_plan() const { return wire_plan_; }
  [[nodiscard]] const CollisionShapeOverlayPlanWorkStats &stats() const {
    return stats_;
  }
  [[nodiscard]] std::shared_ptr<const MeshCollisionGeometryPayload>
  geometry(std::uint64_t batch_key) const;
  [[nodiscard]] std::shared_ptr<const MeshWireGeometryPayload>
  wire_geometry(std::uint64_t batch_key) const;

private:
  /// Pins each immutable payload so local bounds survive transform changes.
  struct GeometryRecord final {
    std::shared_ptr<const MeshCollisionGeometryPayload> payload;
    RenderInstanceBounds local_bounds;
    std::shared_ptr<const MeshWireGeometryPayload> wires;
    std::uint64_t last_seen_preparation = 0u;
  };

  using PayloadTokens = std::unordered_map<
      std::shared_ptr<const MeshCollisionGeometryPayload>, std::uint64_t>;
  [[nodiscard]] std::uint64_t retain_geometry(
      const std::shared_ptr<const MeshCollisionGeometryPayload> &payload);
  void prune_geometry();
  [[nodiscard]] RenderInstanceBatchUpdateKind prepare_wire_plan(
      const RenderOverlayCollisionSnapshot &snapshot,
      std::size_t max_instances_per_chunk);

  RenderSourceStamp synced_source_stamp_;
  ViewportSettingColor color_;
  bool visible_ = false;
  bool has_synced_ = false;
  RenderInstanceBatchPlan plan_;
  RenderInstanceBatchPlan wire_plan_;
  std::uint64_t next_geometry_token_ = 1u;
  std::unordered_map<std::uint64_t, GeometryRecord> geometry_;
  std::unordered_map<std::uint64_t, std::vector<std::uint64_t>> candidates_;
  PayloadTokens payload_tokens_;
  CollisionShapeOverlayPlanWorkStats stats_;
};

} // namespace quader
