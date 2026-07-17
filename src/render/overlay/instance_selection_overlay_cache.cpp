////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/instance_selection_overlay_cache.hpp"

#include "assets/mesh_asset_payload.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "mesh/polygon/document_model.hpp"

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
constexpr std::uint64_t kWireMaterialPayload = 0x6f7665726c617902ULL;

/// Stores one shader-expanded authored edge-ribbon vertex for instance wires.
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

RenderInstanceBounds local_bounds(std::span<const MeshVertex> vertices) {
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
  const std::uint64_t topology =
      record.authored_topology_payload != nullptr
          ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                record.authored_topology_payload.get()))
          : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                record.authored_vertices.data()));
  return {
      .geometry_payload = static_cast<std::uint64_t>(reinterpret_cast<
          std::uintptr_t>(record.geometry_payload.get())),
      .material_payload = RenderSignatureBuilder{}
                              .add_value(kWireMaterialPayload)
                              .add_value(topology)
                              .value(),
      .index_count = static_cast<std::uint32_t>(record.indices.size()),
      .layer_mask = kViewportOverlayLayerMask,
      .channel = kViewportDefaultRenderableChannel,
      .double_sided = true,
      .editor_unlit = true,
  };
}

std::vector<RenderInstancePrimitiveOccurrence> fill_occurrences(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(snapshot.instance_selection_fills.size());
  for (const RenderOverlayInstanceSelectionFillRecord &record :
       snapshot.instance_selection_fills) {
    if (record.geometry_content_id.value == 0u || record.vertices.empty() ||
        record.indices.size() < 3u) {
      continue;
    }
    occurrences.push_back({
        .key = fill_key(record),
        .occurrence = overlay_occurrence_token(record.occurrence,
                                               record.member),
        .world_transform = record.world_transform,
        .local_bounds = local_bounds(record.vertices),
    });
  }
  return occurrences;
}

std::vector<RenderInstancePrimitiveOccurrence> wire_occurrences(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(snapshot.instance_selection_wires.size());
  for (const RenderOverlayInstanceSelectionWireRecord &record :
       snapshot.instance_selection_wires) {
    if (record.geometry_content_id.value == 0u || record.vertices.empty() ||
        record.indices.size() < 3u) {
      continue;
    }
    occurrences.push_back({
        .key = wire_key(record),
        .occurrence = overlay_occurrence_token(record.occurrence,
                                               record.member),
        .world_transform = record.world_transform,
        .local_bounds = local_bounds(record.vertices),
    });
  }
  return occurrences;
}

const RenderOverlayInstanceSelectionFillRecord *fill_record_for_chunk(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    const RenderInstanceBatchChunk &chunk) {
  const auto found = std::ranges::find_if(
      snapshot.instance_selection_fills, [&](const auto &record) {
        return fill_key(record) == chunk.key;
      });
  return found == snapshot.instance_selection_fills.end() ? nullptr : &*found;
}

const RenderOverlayInstanceSelectionWireRecord *wire_record_for_chunk(
    const RenderOverlaySelectedObjectSnapshot &snapshot,
    const RenderInstanceBatchChunk &chunk) {
  const auto found = std::ranges::find_if(
      snapshot.instance_selection_wires, [&](const auto &record) {
        return wire_key(record) == chunk.key;
      });
  return found == snapshot.instance_selection_wires.end() ? nullptr : &*found;
}

std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) {
  const std::uint32_t low = std::min(a, b);
  const std::uint32_t high = std::max(a, b);
  return (static_cast<std::uint64_t>(low) << 32u) | high;
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

} // namespace

/// Couples selected-instance batch plans with their source overlay records.
struct InstanceSelectionOverlayCache::PreparedPlans final {
  InstanceSelectionOverlayBatchPlans plans;
  std::vector<const RenderOverlayInstanceSelectionFillRecord *> fills;
  std::vector<const RenderOverlayInstanceSelectionWireRecord *> wires;
};

/// Owns one shared authored-edge geometry resource for selected wires.
struct InstanceSelectionOverlayCache::WireGeometryResource final {
  std::uint64_t signature = 0u;
  std::shared_ptr<const MeshGeometryPayload> payload;
  std::shared_ptr<const quader_poly::Document> authored_topology;
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
  const std::vector<RenderInstancePrimitiveOccurrence> fills =
      fill_occurrences(snapshot);
  const std::vector<RenderInstancePrimitiveOccurrence> wires =
      wire_occurrences(snapshot);
  return {
      .fills = plan_render_instance_batches(fills, max_instances_per_chunk),
      .wires = plan_render_instance_batches(wires, max_instances_per_chunk),
  };
}

std::vector<InstanceSelectionWireEdge> instance_selection_wire_edges(
    const RenderOverlayInstanceSelectionWireRecord &record) {
  std::vector<InstanceSelectionWireEdge> edges;
  if (record.has_authored_polygon_geometry()) {
    std::unordered_map<quader_poly::ElementId, QVec3> positions;
    positions.reserve(record.authored_vertices.size());
    for (const quader_poly::Vertex &vertex : record.authored_vertices) {
      positions.emplace(vertex.id, vertex.position);
    }
    std::unordered_set<std::uint64_t> emitted;
    for (const quader_poly::Face &face : record.authored_faces) {
      for (std::size_t index = 0u; index < face.vertices.size(); ++index) {
        if (face.vertices.size() < 2u) {
          break;
        }
        const quader_poly::ElementId a_id = face.vertices[index];
        const quader_poly::ElementId b_id =
            face.vertices[(index + 1u) % face.vertices.size()];
        if (a_id == b_id || !emitted.insert(edge_key(a_id, b_id)).second) {
          continue;
        }
        const auto a = positions.find(a_id);
        const auto b = positions.find(b_id);
        if (a != positions.end() && b != positions.end()) {
          edges.push_back({.start = a->second, .end = b->second});
        }
      }
    }
    if (!edges.empty()) {
      return edges;
    }
  }

  std::unordered_set<std::uint64_t> emitted;
  emitted.reserve(record.indices.size());
  const auto append_edge = [&](std::uint32_t a, std::uint32_t b) {
    if (a >= record.vertices.size() || b >= record.vertices.size() || a == b ||
        !emitted.insert(edge_key(a, b)).second) {
      return;
    }
    edges.push_back({.start = record.vertices[a].position,
                     .end = record.vertices[b].position});
  };
  for (std::size_t index = 0u; index + 2u < record.indices.size(); index += 3u) {
    append_edge(record.indices[index], record.indices[index + 1u]);
    append_edge(record.indices[index + 1u], record.indices[index + 2u]);
    append_edge(record.indices[index + 2u], record.indices[index]);
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
  PreparedPlans prepared;
  prepared.plans = plan_instance_selection_overlay_batches(
      snapshot, max_instances_per_chunk_);
  prepared.fills.reserve(prepared.plans.fills.chunks.size());
  for (const RenderInstanceBatchChunk &chunk : prepared.plans.fills.chunks) {
    prepared.fills.push_back(fill_record_for_chunk(snapshot, chunk));
  }
  prepared.wires.reserve(prepared.plans.wires.chunks.size());
  for (const RenderInstanceBatchChunk &chunk : prepared.plans.wires.chunks) {
    prepared.wires.push_back(wire_record_for_chunk(snapshot, chunk));
  }
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
        chunk.world_bounds, record->color, record->blend_order);
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
  const std::uint64_t topology =
      record.authored_topology_payload != nullptr
          ? static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                record.authored_topology_payload.get()))
          : static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(
                record.authored_vertices.data()));
  const std::uint64_t signature = RenderSignatureBuilder{}
                                      .add_value(record.geometry_content_id.value)
                                      .add_value(topology)
                                      .value();
  auto &bucket = wire_resources_[signature];
  for (const std::unique_ptr<WireGeometryResource> &resource : bucket) {
    const bool same_payload =
        resource->payload.get() == record.geometry_payload.get() ||
        (resource->payload != nullptr && record.geometry_payload != nullptr &&
         mesh_geometry_payload_equal(*resource->payload,
                                     *record.geometry_payload));
    const bool same_topology =
        resource->authored_topology.get() ==
        record.authored_topology_payload.get();
    if (same_payload && same_topology) {
      ++stats_.wire_geometry_reuse_count;
      return resource.get();
    }
  }

  std::vector<InstanceSelectionWireVertex> vertices;
  std::vector<std::uint32_t> indices;
  for (const InstanceSelectionWireEdge &edge :
       instance_selection_wire_edges(record)) {
    append_line_quad(vertices, indices, edge.start, edge.end, record.color);
  }
  if (vertices.empty() || indices.empty()) {
    return nullptr;
  }
  auto resource = std::make_unique<WireGeometryResource>();
  resource->signature = signature;
  resource->payload = record.geometry_payload;
  resource->authored_topology = record.authored_topology_payload;
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
  stats_.wire_derived_vertex_bytes += resource->vertex_bytes;
  stats_.wire_derived_index_bytes += resource->index_bytes;
  bucket.push_back(std::move(resource));
  std::size_t resource_count = 0u;
  for (const auto &[key, resources] : wire_resources_) {
    (void)key;
    resource_count += resources.size();
  }
  stats_.unique_wire_geometry_resource_count = resource_count;
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
                                        record.line_width_pixels);
  proxy.material_instance->setParameter("featherPixels",
                                        kOverlayLineFeatherPixels);
  proxy.material_instance->setParameter("nearPlane", clip_planes.near_plane);

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
      .blendOrder(0u, record.blend_order)
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
