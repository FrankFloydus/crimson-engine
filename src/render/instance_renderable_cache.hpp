////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/instance_batch_planner.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/render_scene_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace quader {

class MeshRenderableFactory;
class ViewportSettings;

/// Exposes render-owned explicit-instancing diagnostics without GPU handles.
struct InstanceRenderableCacheStats final {
  /** Logical scene participants before multiplying by render passes. */
  std::size_t participant_count = 0u;
  /** Participant submissions after expanding rendered/shaded/depth passes. */
  std::size_t occurrence_count = 0u;
  /** Planned primitive submissions, one per device-cap-sized chunk. */
  std::size_t chunk_count = 0u;
  std::uint64_t rebuild_count = 0u;
  std::uint64_t transform_update_count = 0u;
};

namespace instance_renderable_cache_detail {

/**
 * Reports the exact indexed lookup work used to associate planned chunks
 * with their first source primitive. This render-internal result also keeps
 * the hot-path complexity deterministic in headless regression coverage.
 */
struct PrimitiveRecordLookupResult final {
  std::vector<const RenderSceneInstancePrimitiveRecord *> records_by_chunk;
  std::size_t indexed_key_count = 0u;
  std::size_t key_lookup_count = 0u;
};

[[nodiscard]] PrimitiveRecordLookupResult resolve_primitive_records(
    std::span<const RenderSceneInstancePrimitiveRecord> primitives,
    std::span<const RenderInstanceBatchChunk> chunks);

/// Describes the retained proxy facts needed by the GPU-free reuse planner.
struct ChunkProxyReuseCandidate final {
  std::size_t instance_capacity = 0u;
  bool valid = false;
};

/**
 * Maps next-plan chunks to reusable old proxies. `no_old_chunk` is the
 * sentinel stored for chunks that need a new proxy.
 */
struct ChunkProxyReuseResult final {
  std::vector<std::size_t> old_chunk_by_next_chunk;
  std::size_t no_old_chunk = 0u;
  std::size_t indexed_candidate_count = 0u;
  std::size_t same_index_probe_count = 0u;
  std::size_t key_lookup_count = 0u;
  std::size_t candidate_probe_count = 0u;
};

[[nodiscard]] ChunkProxyReuseResult match_reusable_chunk_proxies(
    std::span<const RenderInstanceBatchChunk> old_chunks,
    std::span<const ChunkProxyReuseCandidate> old_candidates,
    std::span<const RenderInstanceBatchChunk> next_chunks);

} // namespace instance_renderable_cache_detail

/**
 * Owns the Filament renderables and InstanceBuffers produced by the neutral
 * primitive planner. Geometry storage remains shared by MeshRenderableFactory.
 */
class InstanceRenderableCache final {
public:
  InstanceRenderableCache() = default;
  InstanceRenderableCache(const InstanceRenderableCache &) = delete;
  InstanceRenderableCache &operator=(const InstanceRenderableCache &) = delete;
  ~InstanceRenderableCache();

  void initialize(MeshRenderableFactory &factory,
                  std::size_t max_instances_per_chunk);
  void shutdown();
  void clear();

  [[nodiscard]] bool initialized() const { return factory_ != nullptr; }
  [[nodiscard]] bool sync(
      std::span<const RenderSceneInstancePrimitiveRecord> primitives);
  /** Reports whether the latest sync reached a valid retained/new plan. */
  [[nodiscard]] bool last_sync_succeeded() const {
    return last_sync_succeeded_;
  }
  void set_preview_suppressed_entities(
      std::unordered_set<EntityId> entities,
      std::vector<RenderInstancePreviewSuppression> occurrence_suppressions,
      std::span<const RenderSceneInstancePrimitiveRecord> primitives);
  [[nodiscard]] RenderInstanceBatchUpdateKind classify_update(
      std::span<const RenderSceneInstancePrimitiveRecord> primitives) const;
  void update_surface_grid(float grid_world_spacing,
                           const ViewportSettings &settings);

  [[nodiscard]] const InstanceRenderableCacheStats &stats() const {
    return stats_;
  }

  [[nodiscard]] std::size_t max_instances_per_chunk() const {
    return max_instances_per_chunk_;
  }

private:
  /// Couples a renderer-neutral plan with the source records for each chunk.
  struct PreparedPlan final {
    RenderInstanceBatchPlan plan;
    std::vector<const RenderSceneInstancePrimitiveRecord *> records_by_chunk;
    std::size_t participant_count = 0u;
    std::size_t visible_occurrence_count = 0u;
  };

  [[nodiscard]] PreparedPlan prepare(
      std::span<const RenderSceneInstancePrimitiveRecord> primitives) const;
  [[nodiscard]] MeshRenderableProxy
  create_chunk_proxy(const PreparedPlan &prepared, std::size_t index);
  bool update_chunk_proxy(
      const MeshRenderableProxy &proxy,
      const RenderInstanceBatchChunk &before,
      const RenderInstanceBatchChunk &after,
      const RenderSceneInstancePrimitiveRecord *record);
  bool rebuild(const PreparedPlan &prepared);
  bool update_transforms(const PreparedPlan &prepared);

  MeshRenderableFactory *factory_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  RenderInstanceBatchPlan plan_;
  std::vector<MeshRenderableProxy> proxies_;
  std::unordered_set<EntityId> preview_suppressed_entities_;
  std::vector<RenderInstancePreviewSuppression>
      preview_occurrence_suppressions_;
  std::uint64_t material_uniform_signature_ = 0u;
  bool material_uniforms_dirty_ = true;
  bool last_sync_succeeded_ = true;
  InstanceRenderableCacheStats stats_;
};

} // namespace quader
