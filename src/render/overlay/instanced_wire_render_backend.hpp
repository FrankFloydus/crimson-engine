////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_wire_geometry.hpp"
#include "render/instance_batch_planner.hpp"
#include "render/instance_edit_style.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include <utils/Entity.h>

namespace filament {
class Engine;
class IndexBuffer;
class InstanceBuffer;
class Material;
class MaterialInstance;
class Scene;
class VertexBuffer;
} // namespace filament

namespace quader {

/// Describes one immutable semantic-edge payload and its derived wire color.
struct InstancedWireGeometryInput final {
  std::uint64_t signature = 0u;
  std::shared_ptr<const MeshWireGeometryPayload> wire_geometry;
  std::uint32_t color = 0u;
  std::span<const MeshWireSegment> segments;
};

/// Carries the cache-specific presentation policy for one wire batch.
struct InstancedWireBatchStyle final {
  OverlayRenderState render_state;
  InstanceEditStyle instance_edit_style;
  float line_width_pixels = 1.0f;
  float near_plane = 0.05f;
  float instance_edit_dim = 0.0f;
  std::uint8_t layer_mask = 0u;
  std::uint8_t channel = 0u;
};

/// Reports resident geometry and cumulative upload/reuse work.
struct InstancedWireRenderBackendStats final {
  std::size_t unique_geometry_resource_count = 0u;
  std::uint64_t derived_vertex_bytes = 0u;
  std::uint64_t derived_index_bytes = 0u;
  std::uint64_t geometry_upload_count = 0u;
  std::uint64_t geometry_reuse_count = 0u;
  std::uint64_t line_width_update_count = 0u;
};

/**
 * Owns the shared Filament resource and instance-batch mechanics used by
 * persistent scene wires and selected-instance wires. Callers retain their
 * semantic planning and styling policy while this backend is the sole owner of
 * edge-ribbon uploads, renderable lifetime, transform patches, and pruning.
 */
class InstancedWireRenderBackend final {
private:
  struct GeometryResource;

public:
  /** Owns the backend resources associated with one instanced wire batch. */
  class BatchProxy final {
  public:
    BatchProxy() = default;
    BatchProxy(const BatchProxy &) = delete;
    BatchProxy &operator=(const BatchProxy &) = delete;
    BatchProxy(BatchProxy &&other) noexcept;
    BatchProxy &operator=(BatchProxy &&) = delete;

  private:
    friend class InstancedWireRenderBackend;

    utils::Entity entity_;
    filament::InstanceBuffer *instance_buffer_ = nullptr;
    filament::MaterialInstance *material_instance_ = nullptr;
    GeometryResource *geometry_ = nullptr;
    float line_width_pixels_ = 1.0f;
    bool dimmed_ = false;
  };

  InstancedWireRenderBackend();
  InstancedWireRenderBackend(const InstancedWireRenderBackend &) = delete;
  InstancedWireRenderBackend &
  operator=(const InstancedWireRenderBackend &) = delete;
  ~InstancedWireRenderBackend();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  filament::Material &line_material,
                  std::size_t max_instances_per_chunk);
  void shutdown();

  [[nodiscard]] bool initialized() const;
  [[nodiscard]] const InstancedWireRenderBackendStats &stats() const {
    return stats_;
  }

  [[nodiscard]] BatchProxy
  create_batch(const InstancedWireGeometryInput &geometry,
               const RenderInstanceBatchChunk &chunk,
               const InstancedWireBatchStyle &style);
  [[nodiscard]] bool valid(const BatchProxy &proxy) const;
  bool update_batch(BatchProxy &proxy,
                    const RenderInstanceBatchChunk &chunk);
  bool update_line_width(BatchProxy &proxy, float line_width_pixels);
  void destroy_batch(BatchProxy &proxy);
  void destroy_batches(std::span<BatchProxy> proxies);

  void destroy_geometry_resources();
  void prune_unused_geometry_resources(
      std::span<const BatchProxy> live_proxies);
  void prune_unused_geometry_resources(
      std::span<const std::span<const BatchProxy>> live_proxy_groups);
  void update_clip_plane(std::span<BatchProxy> proxies, float near_plane);
  void update_instance_edit_style(std::span<BatchProxy> proxies,
                                  const InstanceEditStyle &style,
                                  bool dimmed_only);
  void set_layer_mask(std::span<const BatchProxy> proxies,
                      std::uint8_t layer_mask);

private:
  [[nodiscard]] GeometryResource *
  acquire_geometry(const InstancedWireGeometryInput &input);

  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Material *line_material_ = nullptr;
  std::size_t max_instances_per_chunk_ = 1u;
  std::unordered_map<std::uint64_t,
                     std::vector<std::unique_ptr<GeometryResource>>>
      geometry_resources_;
  InstancedWireRenderBackendStats stats_;
};

} // namespace quader
