////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/instance_selection_overlay_cache.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/InstanceBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr std::uint64_t kFillMaterialPayload = 0x6f7665726c617901ULL;

/// Stores one shader-expanded semantic edge-ribbon vertex for instance wires.
struct InstanceSelectionWireVertex final {
  OverlayFloat4 position;
  OverlayFloat4 start;
  OverlayFloat4 end;
  OverlayFloat4 control;
  std::uint32_t color = 0u;
};

filament::math::mat4f make_transform(const QMat4 &transform) {
  const glm::mat4 glm_transform = to_glm(transform);
  filament::math::mat4f result;
  for (int column = 0; column < 4; ++column) {
    for (int row = 0; row < 4; ++row) {
      result[column][row] = glm_transform[column][row];
    }
  }
  return result;
}

filament::Box filament_bounds(const RenderInstanceBounds &bounds) {
  if (!bounds.present) {
    return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
  }
  filament::Box result;
  result.set({bounds.min.x, bounds.min.y, bounds.min.z},
             {bounds.max.x, bounds.max.y, bounds.max.z});
  return result;
}

RenderInstanceBounds compute_local_bounds(
    std::span<const MeshVertex> vertices) {
  RenderInstanceBounds bounds;
  for (const MeshVertex &vertex : vertices) {
    if (!bounds.present) {
      bounds.min = vertex.position;
      bounds.max = vertex.position;
      bounds.present = true;
      continue;
    }
    bounds.min.x = std::min(bounds.min.x, vertex.position.x);
    bounds.min.y = std::min(bounds.min.y, vertex.position.y);
    bounds.min.z = std::min(bounds.min.z, vertex.position.z);
    bounds.max.x = std::max(bounds.max.x, vertex.position.x);
    bounds.max.y = std::max(bounds.max.y, vertex.position.y);
    bounds.max.z = std::max(bounds.max.z, vertex.position.z);
  }
  return bounds;
}

std::uint64_t overlay_occurrence_token(EntityId occurrence,
                                       EntityId member) {
  return RenderSignatureBuilder{}
      .add_value(0x696e73746f766c79ULL)
      .add_value(occurrence)
      .add_value(member)
      .value();
}

RenderInstancePrimitiveKey fill_key(
    const RenderOverlayInstanceSelectionFillRecord &record) {
  return {
      .geometry_payload = static_cast<std::uint64_t>(reinterpret_cast<
          std::uintptr_t>(record.geometry_payload.get())),
      .material_payload = kFillMaterialPayload,
      .index_count = static_cast<std::uint32_t>(record.indices.size()),
      .layer_mask = kViewportOverlayLayerMask,
      .channel = kViewportDefaultRenderableChannel,
      .double_sided = true,
      .editor_unlit = true,
  };
}

RenderInstancePrimitiveKey wire_key(
    const RenderOverlayInstanceSelectionWireRecord &record) {
  const std::uint64_t wire_geometry =
      record.wire_geometry_payload != nullptr
          ? record.wire_geometry_payload->content_id
          : 0u;
  return {
      .geometry_payload = wire_geometry,
      // Keep every style field exact in the key. RenderSignatureBuilder is
      // still suitable for the unordered-map hash, but a hash must never be
      // the equality identity that decides which record drives a batch.
      .material_payload =
          std::bit_cast<std::uint32_t>(record.wire_width_pixels),
      .index_offset = record.wire_color,
      .index_count = static_cast<std::uint32_t>(record.wire_segments.size()),
      .layer_mask = kViewportOverlayLayerMask,
      .channel = kViewportDefaultRenderableChannel,
      .double_sided = true,
      .editor_unlit = true,
  };
}

/// Hashes every exact field used to group one selected-instance primitive.
struct PrimitiveKeyHash final {
  std::size_t operator()(const RenderInstancePrimitiveKey &key) const noexcept {
    return static_cast<std::size_t>(
        RenderSignatureBuilder{}
            .add_value(key.geometry_payload)
            .add_value(key.material_payload)
            .add_value(key.index_offset)
            .add_value(key.index_count)
            .add_value(key.layer_mask)
            .add_value(key.channel)
            .add_value(key.presentation_flags)
            .add_bool(key.cast_shadows)
            .add_bool(key.receive_shadows)
            .add_bool(key.double_sided)
            .add_bool(key.editor_unlit)
            .value());
  }
};

using LocalBoundsByGeometry =
    std::unordered_map<const MeshGeometryPayload *, RenderInstanceBounds>;

RenderInstanceBounds shared_local_bounds(
    const std::shared_ptr<const MeshGeometryPayload> &geometry_payload,
    std::span<const MeshVertex> vertices,
    LocalBoundsByGeometry &bounds_by_geometry,
    InstanceSelectionOverlayPlanWorkStats &work) {
  const MeshGeometryPayload *geometry = geometry_payload.get();
  const auto cached = geometry != nullptr
                          ? bounds_by_geometry.find(geometry)
                          : bounds_by_geometry.end();
  if (cached != bounds_by_geometry.end()) {
    ++work.local_bounds_cache_hit_count;
    return cached->second;
  }
  const RenderInstanceBounds bounds = compute_local_bounds(vertices);
  work.local_bounds_vertex_visit_count +=
      static_cast<std::uint64_t>(vertices.size());
  if (geometry != nullptr) {
    bounds_by_geometry.try_emplace(geometry, bounds);
  }
  return bounds;
}

/// Holds indexed fill and wire occurrences before instance batch planning.
struct InstanceSelectionOccurrencePreparation final {
  std::vector<RenderInstancePrimitiveOccurrence> fills;
  std::vector<RenderInstancePrimitiveOccurrence> wires;
  std::unordered_map<RenderInstancePrimitiveKey,
                     const RenderOverlayInstanceSelectionFillRecord *,
                     PrimitiveKeyHash>
      fill_record_by_key;
  std::unordered_map<RenderInstancePrimitiveKey,
                     const RenderOverlayInstanceSelectionWireRecord *,
                     PrimitiveKeyHash>
      wire_record_by_key;
  InstanceSelectionOverlayPlanWorkStats work;
};

InstanceSelectionOccurrencePreparation prepare_selection_occurrences(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  InstanceSelectionOccurrencePreparation prepared;
  prepared.fills.reserve(snapshot.instance_selection_fills.size());
  prepared.wires.reserve(snapshot.instance_selection_wires.size());
  prepared.fill_record_by_key.reserve(
      snapshot.instance_selection_fills.size());
  prepared.wire_record_by_key.reserve(
      snapshot.instance_selection_wires.size());
  LocalBoundsByGeometry bounds_by_geometry;
  bounds_by_geometry.reserve(snapshot.instance_selection_fills.size() +
                             snapshot.instance_selection_wires.size());

  for (const RenderOverlayInstanceSelectionFillRecord &record :
       snapshot.instance_selection_fills) {
    const RenderInstancePrimitiveKey key = fill_key(record);
    prepared.fill_record_by_key.try_emplace(key, &record);
    if (record.geometry_payload == nullptr || record.vertices.empty() ||
        record.indices.size() < 3u) {
      continue;
    }
    prepared.fills.push_back({
        .key = key,
        .occurrence = overlay_occurrence_token(record.occurrence,
                                               record.member),
        .world_transform = record.world_transform,
        .local_bounds =
            shared_local_bounds(record.geometry_payload, record.vertices,
                                bounds_by_geometry, prepared.work),
    });
  }
  for (const RenderOverlayInstanceSelectionWireRecord &record :
       snapshot.instance_selection_wires) {
    const RenderInstancePrimitiveKey key = wire_key(record);
    prepared.wire_record_by_key.try_emplace(key, &record);
    if (record.geometry_content_id.value == 0u || record.vertices.empty() ||
        record.indices.size() < 3u) {
      continue;
    }
    prepared.wires.push_back({
        .key = key,
        .occurrence = overlay_occurrence_token(record.occurrence,
                                               record.member),
        .world_transform = record.world_transform,
        .local_bounds =
            shared_local_bounds(record.geometry_payload, record.vertices,
                                bounds_by_geometry, prepared.work),
    });
  }
  prepared.work.fill_record_index_entry_count =
      prepared.fill_record_by_key.size();
  prepared.work.wire_record_index_entry_count =
      prepared.wire_record_by_key.size();
  return prepared;
}

void append_line_quad(std::vector<InstanceSelectionWireVertex> &vertices,
                      std::vector<std::uint32_t> &indices, QVec3 start,
                      QVec3 end, std::uint32_t color) {
  const std::uint32_t linear_color = rgba8_srgb_to_linear(color);
  const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
  const OverlayFloat4 origin{0.0f, 0.0f, 0.0f, 1.0f};
  const OverlayFloat4 a{start.x, start.y, start.z, 1.0f};
  const OverlayFloat4 b{end.x, end.y, end.z, 1.0f};
  vertices.push_back({origin, a, b, {-1.0f, -1.0f, 0.0f, 0.0f},
                      linear_color});
  vertices.push_back(
      {origin, a, b, {-1.0f, 1.0f, 1.0f, 0.0f}, linear_color});
  vertices.push_back(
      {origin, a, b, {1.0f, 1.0f, 1.0f, 0.0f}, linear_color});
  vertices.push_back(
      {origin, a, b, {1.0f, -1.0f, 0.0f, 0.0f}, linear_color});
  indices.insert(indices.end(), {offset, offset + 1u, offset + 2u, offset,
                                 offset + 2u, offset + 3u});
}

RenderMeshAssetSnapshot fill_mesh_snapshot(
    const RenderOverlayInstanceSelectionFillRecord &record) {
  RenderMeshAssetSnapshot mesh{
      .mesh_id = record.mesh_id,
      .mesh_revision = record.mesh_revision,
      .geometry_payload = record.geometry_payload,
      .geometry_content_id = record.geometry_content_id,
      .vertices = record.vertices,
      .indices = record.indices,
      .present = true,
  };
  if (record.geometry_payload != nullptr) {
    mesh.geometry_token = reinterpret_cast<std::uintptr_t>(
        record.geometry_payload.get());
    mesh.primitives = record.geometry_payload->primitives;
  }
  return mesh;
}

/// Couples planned selection batches with their source overlay records.
struct InstanceSelectionBatchPreparation final {
  InstanceSelectionOverlayBatchPlans plans;
  std::vector<const RenderOverlayInstanceSelectionFillRecord *> fills;
  std::vector<const RenderOverlayInstanceSelectionWireRecord *> wires;
};

InstanceSelectionBatchPreparation prepare_selection_batches(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  InstanceSelectionOccurrencePreparation occurrences =
      prepare_selection_occurrences(snapshot);
  InstanceSelectionBatchPreparation prepared;
  prepared.plans.fills = plan_render_instance_batches(
      occurrences.fills, max_instances_per_chunk);
  prepared.plans.wires = plan_render_instance_batches(
      occurrences.wires, max_instances_per_chunk);
  prepared.plans.work = occurrences.work;

  prepared.fills.reserve(prepared.plans.fills.chunks.size());
  for (const RenderInstanceBatchChunk &chunk :
       prepared.plans.fills.chunks) {
    ++prepared.plans.work.fill_chunk_record_lookup_count;
    const auto found = occurrences.fill_record_by_key.find(chunk.key);
    prepared.fills.push_back(
        found == occurrences.fill_record_by_key.end() ? nullptr
                                                       : found->second);
  }
  prepared.wires.reserve(prepared.plans.wires.chunks.size());
  for (const RenderInstanceBatchChunk &chunk :
       prepared.plans.wires.chunks) {
    ++prepared.plans.work.wire_chunk_record_lookup_count;
    const auto found = occurrences.wire_record_by_key.find(chunk.key);
    prepared.wires.push_back(
        found == occurrences.wire_record_by_key.end() ? nullptr
                                                       : found->second);
  }
  return prepared;
}

} // namespace

/// Couples selected-instance batch plans with their source overlay records.
struct InstanceSelectionOverlayCache::PreparedPlans final {
  InstanceSelectionOverlayBatchPlans plans;
  std::vector<const RenderOverlayInstanceSelectionFillRecord *> fills;
  std::vector<const RenderOverlayInstanceSelectionWireRecord *> wires;
};

/// Owns one shared semantic-edge geometry resource for selected wires.
struct InstanceSelectionOverlayCache::WireGeometryResource final {
  std::uint64_t signature = 0u;
  std::shared_ptr<const MeshWireGeometryPayload> wire_geometry;
  std::uint32_t wire_color = 0u;
  filament::VertexBuffer *vertex_buffer = nullptr;
  filament::IndexBuffer *index_buffer = nullptr;
  std::uint32_t index_count = 0u;
  std::uint64_t vertex_bytes = 0u;
  std::uint64_t index_bytes = 0u;
};

/// Owns one Filament renderable and instance buffer for a selected-wire chunk.
struct InstanceSelectionOverlayCache::WireBatchProxy final {
  WireBatchProxy() = default;
  WireBatchProxy(const WireBatchProxy &) = delete;
  WireBatchProxy &operator=(const WireBatchProxy &) = delete;
  WireBatchProxy(WireBatchProxy &&other) noexcept
      : entity(other.entity),
        instance_buffer(std::exchange(other.instance_buffer, nullptr)),
        material_instance(std::exchange(other.material_instance, nullptr)),
        geometry(std::exchange(other.geometry, nullptr)) {
    other.entity.clear();
  }
  WireBatchProxy &operator=(WireBatchProxy &&) = delete;

  utils::Entity entity;
  filament::InstanceBuffer *instance_buffer = nullptr;
  filament::MaterialInstance *material_instance = nullptr;
  WireGeometryResource *geometry = nullptr;
};

InstanceSelectionOverlayBatchPlans plan_instance_selection_overlay_batches(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  InstanceSelectionBatchPreparation prepared =
      prepare_selection_batches(snapshot, max_instances_per_chunk);
  return std::move(prepared.plans);
}

std::vector<InstanceSelectionWireEdge> instance_selection_wire_edges(
    const RenderOverlayInstanceSelectionWireRecord &record) {
  std::vector<InstanceSelectionWireEdge> edges;
  edges.reserve(record.wire_segments.size());
  for (const MeshWireSegment &segment : record.wire_segments) {
    edges.push_back({.start = segment.start, .end = segment.end});
  }
  return edges;
}

InstanceSelectionOverlayCache::InstanceSelectionOverlayCache() = default;

InstanceSelectionOverlayCache::~InstanceSelectionOverlayCache() { shutdown(); }

void InstanceSelectionOverlayCache::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MeshRenderableFactory &mesh_factory, filament::Material &fill_material,
    filament::Material &line_material,
    std::size_t max_instances_per_chunk) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  mesh_factory_ = &mesh_factory;
  fill_material_ = &fill_material;
  line_material_ = &line_material;
  max_instances_per_chunk_ =
      std::max<std::size_t>(1u, max_instances_per_chunk);
}

void InstanceSelectionOverlayCache::shutdown() {
  clear(true);
  engine_ = nullptr;
  scene_ = nullptr;
  mesh_factory_ = nullptr;
  fill_material_ = nullptr;
  line_material_ = nullptr;
  max_instances_per_chunk_ = 1u;
  clip_planes_ = {};
  synced_local_visibility_revision_ = 0u;
  stats_ = {};
}

void InstanceSelectionOverlayCache::clear(bool release_geometry_resources) {
  if (mesh_factory_ != nullptr) {
    for (MeshRenderableProxy &proxy : fill_proxies_) {
      mesh_factory_->destroy(proxy);
    }
  }
  for (WireBatchProxy &proxy : wire_proxies_) {
    destroy_wire_batch(proxy);
  }
  fill_proxies_.clear();
  wire_proxies_.clear();
  fill_plan_ = {};
  wire_plan_ = {};
  stats_.fill_chunk_count = 0u;
  stats_.wire_chunk_count = 0u;
  stats_.selected_member_count = 0u;
  if (release_geometry_resources) {
    destroy_wire_resources();
  }
}

InstanceSelectionOverlayCache::PreparedPlans
InstanceSelectionOverlayCache::prepare(
    const RenderOverlaySelectedObjectSnapshot &snapshot) const {
  InstanceSelectionBatchPreparation batch_preparation =
      prepare_selection_batches(snapshot, max_instances_per_chunk_);
  PreparedPlans prepared;
  prepared.plans = std::move(batch_preparation.plans);
  prepared.fills = std::move(batch_preparation.fills);
  prepared.wires = std::move(batch_preparation.wires);
  return prepared;
}

bool InstanceSelectionOverlayCache::sync(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return false;
  }
  if (!snapshot.visible || (snapshot.instance_selection_fills.empty() &&
                            snapshot.instance_selection_wires.empty())) {
    const bool changed = !fill_proxies_.empty() || !wire_proxies_.empty();
    clear();
    const bool local_visibility_changed =
        synced_local_visibility_revision_ !=
        snapshot.local_visibility_revision;
    synced_local_visibility_revision_ = snapshot.local_visibility_revision;
    return changed || local_visibility_changed;
  }

  const PreparedPlans prepared = prepare(snapshot);
  const RenderInstanceBatchUpdateKind fill_update =
      classify_instance_batch_update(fill_plan_, prepared.plans.fills);
  const RenderInstanceBatchUpdateKind wire_update =
      classify_instance_batch_update(wire_plan_, prepared.plans.wires);
  const bool clip_changed = clip_planes_ != snapshot.clip_planes;
  const bool local_visibility_changed =
      synced_local_visibility_revision_ != snapshot.local_visibility_revision;
  if (fill_update == RenderInstanceBatchUpdateKind::Unchanged &&
      wire_update == RenderInstanceBatchUpdateKind::Unchanged) {
    if (clip_changed) {
      update_wire_clip_planes(snapshot.clip_planes);
    }
    synced_local_visibility_revision_ = snapshot.local_visibility_revision;
    return clip_changed || local_visibility_changed;
  }
  if (fill_update != RenderInstanceBatchUpdateKind::Rebuild &&
      wire_update != RenderInstanceBatchUpdateKind::Rebuild &&
      update_transforms(prepared)) {
    fill_plan_ = prepared.plans.fills;
    wire_plan_ = prepared.plans.wires;
    ++stats_.transform_update_count;
    if (clip_changed) {
      update_wire_clip_planes(snapshot.clip_planes);
    }
    synced_local_visibility_revision_ = snapshot.local_visibility_revision;
    return true;
  }
  return rebuild(prepared, snapshot);
}

bool InstanceSelectionOverlayCache::rebuild(
    const PreparedPlans &prepared,
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  std::vector<MeshRenderableProxy> new_fills;
  new_fills.reserve(prepared.plans.fills.chunks.size());
  for (std::size_t index = 0u; index < prepared.plans.fills.chunks.size();
       ++index) {
    const RenderInstanceBatchChunk &chunk = prepared.plans.fills.chunks[index];
    const RenderOverlayInstanceSelectionFillRecord *record =
        prepared.fills[index];
    if (record == nullptr) {
      for (MeshRenderableProxy &proxy : new_fills) {
        mesh_factory_->destroy(proxy);
      }
      return false;
    }
    MeshRenderableProxy proxy = mesh_factory_->create_instance_selection_fill(
        fill_mesh_snapshot(*record), *fill_material_, chunk.world_transforms,
        chunk.world_bounds, kOverlayInstanceSelectionFillColor,
        static_cast<std::uint16_t>(viewport_overlay_draw_order(
            ViewportOverlayLayer::SelectedVisibleFace) * 64));
    if (!proxy.entity || proxy.instance_buffer == nullptr) {
      mesh_factory_->destroy(proxy);
      for (MeshRenderableProxy &created : new_fills) {
        mesh_factory_->destroy(created);
      }
      return false;
    }
    new_fills.push_back(std::move(proxy));
  }

  std::vector<WireBatchProxy> new_wires;
  new_wires.reserve(prepared.plans.wires.chunks.size());
  for (std::size_t index = 0u; index < prepared.plans.wires.chunks.size();
       ++index) {
    const RenderInstanceBatchChunk &chunk = prepared.plans.wires.chunks[index];
    const RenderOverlayInstanceSelectionWireRecord *record =
        prepared.wires[index];
    if (record == nullptr) {
      for (WireBatchProxy &proxy : new_wires) {
        destroy_wire_batch(proxy);
      }
      for (MeshRenderableProxy &proxy : new_fills) {
        mesh_factory_->destroy(proxy);
      }
      prune_unused_wire_resources();
      return false;
    }
    WireBatchProxy proxy =
        create_wire_batch(*record, chunk, snapshot.clip_planes);
    if (!proxy.entity || proxy.instance_buffer == nullptr) {
      destroy_wire_batch(proxy);
      for (WireBatchProxy &created : new_wires) {
        destroy_wire_batch(created);
      }
      for (MeshRenderableProxy &fill : new_fills) {
        mesh_factory_->destroy(fill);
      }
      prune_unused_wire_resources();
      return false;
    }
    new_wires.push_back(std::move(proxy));
  }

  for (MeshRenderableProxy &proxy : fill_proxies_) {
    mesh_factory_->destroy(proxy);
  }
  for (WireBatchProxy &proxy : wire_proxies_) {
    destroy_wire_batch(proxy);
  }
  fill_proxies_.swap(new_fills);
  wire_proxies_.swap(new_wires);
  fill_plan_ = prepared.plans.fills;
  wire_plan_ = prepared.plans.wires;
  clip_planes_ = snapshot.clip_planes;
  synced_local_visibility_revision_ = snapshot.local_visibility_revision;
  ++stats_.rebuild_count;
  stats_.fill_chunk_count = fill_proxies_.size();
  stats_.wire_chunk_count = wire_proxies_.size();
  stats_.selected_member_count = snapshot.instance_selection_fills.size();
  prune_unused_wire_resources();
  return true;
}

bool InstanceSelectionOverlayCache::update_transforms(
    const PreparedPlans &prepared) {
  if (prepared.plans.fills.chunks.size() != fill_proxies_.size() ||
      prepared.plans.wires.chunks.size() != wire_proxies_.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < fill_proxies_.size(); ++index) {
    if (!mesh_factory_->update_explicit_instances(
            fill_proxies_[index],
            prepared.plans.fills.chunks[index].world_transforms,
            prepared.plans.fills.chunks[index].world_bounds)) {
      return false;
    }
  }
  for (std::size_t index = 0u; index < wire_proxies_.size(); ++index) {
    if (!update_wire_batch(wire_proxies_[index],
                           prepared.plans.wires.chunks[index])) {
      return false;
    }
  }
  return true;
}

InstanceSelectionOverlayCache::WireGeometryResource *
InstanceSelectionOverlayCache::acquire_wire_geometry(
    const RenderOverlayInstanceSelectionWireRecord &record) {
  const std::uint64_t signature =
      RenderSignatureBuilder{}
          .add_value(record.wire_geometry_payload != nullptr
                         ? record.wire_geometry_payload->content_id
                         : 0u)
          .add_value(record.wire_color)
          .value();
  auto &bucket = wire_resources_[signature];
  for (const std::unique_ptr<WireGeometryResource> &resource : bucket) {
    if (resource->wire_geometry != nullptr &&
        record.wire_geometry_payload != nullptr &&
        resource->wire_geometry->content_id ==
            record.wire_geometry_payload->content_id &&
        resource->wire_color == record.wire_color) {
      ++stats_.wire_geometry_reuse_count;
      return resource.get();
    }
  }

  std::vector<InstanceSelectionWireVertex> vertices;
  std::vector<std::uint32_t> indices;
  for (const InstanceSelectionWireEdge &edge :
       instance_selection_wire_edges(record)) {
    append_line_quad(vertices, indices, edge.start, edge.end,
                     record.wire_color);
  }
  if (vertices.empty() || indices.empty()) {
    return nullptr;
  }
  auto resource = std::make_unique<WireGeometryResource>();
  resource->signature = signature;
  resource->wire_geometry = record.wire_geometry_payload;
  resource->wire_color = record.wire_color;
  resource->index_count = static_cast<std::uint32_t>(indices.size());
  resource->vertex_bytes =
      vertices.size() * sizeof(InstanceSelectionWireVertex);
  resource->index_bytes = indices.size() * sizeof(std::uint32_t);
  resource->vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1u)
          .attribute(filament::VertexAttribute::POSITION, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstanceSelectionWireVertex, position),
                     sizeof(InstanceSelectionWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM0, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstanceSelectionWireVertex, start),
                     sizeof(InstanceSelectionWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM1, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstanceSelectionWireVertex, end),
                     sizeof(InstanceSelectionWireVertex))
          .attribute(filament::VertexAttribute::CUSTOM2, 0u,
                     AttributeType::FLOAT4,
                     offsetof(InstanceSelectionWireVertex, control),
                     sizeof(InstanceSelectionWireVertex))
          .attribute(filament::VertexAttribute::COLOR, 0u,
                     AttributeType::UBYTE4,
                     offsetof(InstanceSelectionWireVertex, color),
                     sizeof(InstanceSelectionWireVertex))
          .normalized(filament::VertexAttribute::COLOR)
          .build(*engine_);
  resource->vertex_buffer->setBufferAt(
      *engine_, 0u, FilamentBufferDescriptorFactory::vector(vertices));
  resource->index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(resource->index_count)
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);
  resource->index_buffer->setBuffer(
      *engine_, FilamentBufferDescriptorFactory::vector(indices));
  WireGeometryResource *result = resource.get();
  const std::uint64_t vertex_bytes = resource->vertex_bytes;
  const std::uint64_t index_bytes = resource->index_bytes;
  bucket.push_back(std::move(resource));
  stats_.wire_derived_vertex_bytes += vertex_bytes;
  stats_.wire_derived_index_bytes += index_bytes;
  ++stats_.unique_wire_geometry_resource_count;
  ++stats_.wire_geometry_upload_count;
  return result;
}

InstanceSelectionOverlayCache::WireBatchProxy
InstanceSelectionOverlayCache::create_wire_batch(
    const RenderOverlayInstanceSelectionWireRecord &record,
    const RenderInstanceBatchChunk &chunk,
    ViewportCameraClipPlanes clip_planes) {
  WireBatchProxy proxy;
  proxy.geometry = acquire_wire_geometry(record);
  if (proxy.geometry == nullptr || chunk.world_transforms.empty() ||
      chunk.world_transforms.size() > max_instances_per_chunk_) {
    return proxy;
  }
  proxy.entity = utils::EntityManager::get().create();
  proxy.material_instance = line_material_->createInstance();
  if (proxy.material_instance == nullptr) {
    return proxy;
  }
  proxy.material_instance->setColorWrite(true);
  proxy.material_instance->setDepthWrite(false);
  proxy.material_instance->setDepthCulling(false);
  proxy.material_instance->setDepthFunc(
      filament::MaterialInstance::DepthFunc::A);
  proxy.material_instance->setCullingMode(
      filament::MaterialInstance::CullingMode::NONE);
  proxy.material_instance->setParameter("lineWidth",
                                        record.wire_width_pixels);
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane", clip_planes.near_plane);
  // Selected-occurrence wires share the line material but never receive the
  // scene context effect, even while instance editing is active.
  proxy.material_instance->setParameter("instanceEditDim", 0.0f);
  bind_instance_edit_context_style(*proxy.material_instance, {});

  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer =
      filament::InstanceBuffer::Builder(transforms.size())
          .localTransforms(transforms.data())
          .build(*engine_);
  filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                proxy.geometry->vertex_buffer, proxy.geometry->index_buffer,
                0u, proxy.geometry->index_count)
      .material(0u, proxy.material_instance)
      .instances(transforms.size(), proxy.instance_buffer)
      .blendOrder(0u, static_cast<std::uint16_t>(
                           viewport_overlay_draw_order(
                               ViewportOverlayLayer::SourceWire) *
                           64))
      .globalBlendOrderEnabled(0u, true)
      .boundingBox(filament_bounds(chunk.world_bounds))
      .layerMask(kViewportAllLayerMask, kViewportOverlayLayerMask)
      .channel(kViewportDefaultRenderableChannel)
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(*engine_, proxy.entity);
  engine_->getTransformManager().create(proxy.entity);
  scene_->addEntity(proxy.entity);
  return proxy;
}

bool InstanceSelectionOverlayCache::update_wire_batch(
    WireBatchProxy &proxy, const RenderInstanceBatchChunk &chunk) {
  if (proxy.instance_buffer == nullptr || !proxy.entity ||
      proxy.instance_buffer->getInstanceCount() !=
          chunk.world_transforms.size()) {
    return false;
  }
  std::vector<filament::math::mat4f> transforms;
  transforms.reserve(chunk.world_transforms.size());
  for (const QMat4 &transform : chunk.world_transforms) {
    transforms.push_back(make_transform(transform));
  }
  proxy.instance_buffer->setLocalTransforms(transforms.data(),
                                             transforms.size());
  filament::RenderableManager &renderables = engine_->getRenderableManager();
  const filament::RenderableManager::Instance instance =
      renderables.getInstance(proxy.entity);
  if (!instance) {
    return false;
  }
  renderables.setAxisAlignedBoundingBox(instance,
                                        filament_bounds(chunk.world_bounds));
  return true;
}

void InstanceSelectionOverlayCache::destroy_wire_batch(
    WireBatchProxy &proxy) {
  if (engine_ == nullptr) {
    return;
  }
  if (proxy.entity) {
    if (scene_ != nullptr) {
      scene_->remove(proxy.entity);
    }
    engine_->getRenderableManager().destroy(proxy.entity);
    engine_->getTransformManager().destroy(proxy.entity);
  }
  if (proxy.instance_buffer != nullptr) {
    engine_->destroy(proxy.instance_buffer);
    proxy.instance_buffer = nullptr;
  }
  if (proxy.material_instance != nullptr) {
    engine_->destroy(proxy.material_instance);
    proxy.material_instance = nullptr;
  }
  if (proxy.entity) {
    utils::EntityManager::get().destroy(proxy.entity);
    proxy.entity.clear();
  }
  proxy.geometry = nullptr;
}

void InstanceSelectionOverlayCache::destroy_wire_resources() {
  if (engine_ != nullptr) {
    for (auto &[signature, resources] : wire_resources_) {
      (void)signature;
      for (const std::unique_ptr<WireGeometryResource> &resource : resources) {
        if (resource->vertex_buffer != nullptr) {
          engine_->destroy(resource->vertex_buffer);
        }
        if (resource->index_buffer != nullptr) {
          engine_->destroy(resource->index_buffer);
        }
      }
    }
  }
  wire_resources_.clear();
  stats_.unique_wire_geometry_resource_count = 0u;
  stats_.wire_derived_vertex_bytes = 0u;
  stats_.wire_derived_index_bytes = 0u;
}

void InstanceSelectionOverlayCache::prune_unused_wire_resources() {
  std::unordered_set<const WireGeometryResource *> live;
  live.reserve(wire_proxies_.size());
  for (const WireBatchProxy &proxy : wire_proxies_) {
    if (proxy.geometry != nullptr) {
      live.insert(proxy.geometry);
    }
  }
  for (auto bucket_it = wire_resources_.begin();
       bucket_it != wire_resources_.end();) {
    auto &bucket = bucket_it->second;
    for (auto it = bucket.begin(); it != bucket.end();) {
      WireGeometryResource &resource = **it;
      if (live.contains(&resource)) {
        ++it;
        continue;
      }
      if (resource.vertex_buffer != nullptr) {
        engine_->destroy(resource.vertex_buffer);
      }
      if (resource.index_buffer != nullptr) {
        engine_->destroy(resource.index_buffer);
      }
      stats_.wire_derived_vertex_bytes -= resource.vertex_bytes;
      stats_.wire_derived_index_bytes -= resource.index_bytes;
      --stats_.unique_wire_geometry_resource_count;
      it = bucket.erase(it);
    }
    if (bucket.empty()) {
      bucket_it = wire_resources_.erase(bucket_it);
    } else {
      ++bucket_it;
    }
  }
}

void InstanceSelectionOverlayCache::update_wire_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  clip_planes_ = clip_planes;
  for (WireBatchProxy &proxy : wire_proxies_) {
    if (proxy.material_instance != nullptr) {
      proxy.material_instance->setParameter("nearPlane",
                                            clip_planes.near_plane);
    }
  }
}

} // namespace quader
