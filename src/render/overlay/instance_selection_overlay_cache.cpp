////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/instance_selection_overlay_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <bit>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr std::uint64_t kFillMaterialPayload = 0x6f7665726c617901ULL;

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

InstancedWireGeometryInput wire_geometry_input(
    const RenderOverlayInstanceSelectionWireRecord &record) {
  const std::uint64_t signature =
      RenderSignatureBuilder{}
          .add_value(record.wire_geometry_payload != nullptr
                         ? record.wire_geometry_payload->content_id
                         : 0u)
          .add_value(record.wire_color)
          .value();
  return {
      .signature = signature,
      .wire_geometry = record.wire_geometry_payload,
      .color = record.wire_color,
      .segments = record.wire_segments,
  };
}

InstancedWireBatchStyle wire_batch_style(
    const RenderOverlayInstanceSelectionWireRecord &record,
    ViewportCameraClipPlanes clip_planes) {
  const std::uint16_t blend_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(ViewportOverlayLayer::SourceWire) * 64u);
  return {
      .render_state = OverlayRenderableBuilder::make_state(
          ViewportOverlayLayer::SourceWire,
          ViewportOverlayGeometryKind::Lines, blend_order),
      // Selected-occurrence wires never receive the scene context effect.
      .instance_edit_style = {},
      .line_width_pixels = record.wire_width_pixels,
      .near_plane = clip_planes.near_plane,
      .instance_edit_dim = 0.0f,
      .layer_mask = kViewportOverlayLayerMask,
      .channel = kViewportDefaultRenderableChannel,
  };
}

} // namespace

/// Couples selected-instance batch plans with their source overlay records.
struct InstanceSelectionOverlayCache::PreparedPlans final {
  InstanceSelectionOverlayBatchPlans plans;
  std::vector<const RenderOverlayInstanceSelectionFillRecord *> fills;
  std::vector<const RenderOverlayInstanceSelectionWireRecord *> wires;
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
  mesh_factory_ = &mesh_factory;
  fill_material_ = &fill_material;
  max_instances_per_chunk_ =
      std::max<std::size_t>(1u, max_instances_per_chunk);
  wire_backend_.initialize(engine, scene, line_material,
                           max_instances_per_chunk_);
}

void InstanceSelectionOverlayCache::shutdown() {
  clear(true);
  wire_backend_.shutdown();
  mesh_factory_ = nullptr;
  fill_material_ = nullptr;
  max_instances_per_chunk_ = 1u;
  clip_planes_ = {};
  synced_source_stamp_ = {};
  synced_wire_style_signature_ = 0u;
  has_synced_ = false;
  stats_ = {};
}

void InstanceSelectionOverlayCache::clear(bool release_geometry_resources) {
  if (mesh_factory_ != nullptr) {
    for (MeshRenderableProxy &proxy : fill_proxies_) {
      mesh_factory_->destroy(proxy);
    }
  }
  wire_backend_.destroy_batches(wire_proxies_);
  fill_proxies_.clear();
  wire_proxies_.clear();
  fill_plan_ = {};
  wire_plan_ = {};
  has_synced_ = false;
  stats_.fill_chunk_count = 0u;
  stats_.wire_chunk_count = 0u;
  stats_.selected_member_count = 0u;
  if (release_geometry_resources) {
    wire_backend_.destroy_geometry_resources();
    refresh_wire_resource_stats();
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
    const bool local_visibility_changed =
        !render_source_dependencies_match(
            synced_source_stamp_, snapshot,
            RenderSourceDependency::LocalVisibilityRevision);
    clear();
    synced_source_stamp_ = snapshot;
    synced_wire_style_signature_ = snapshot.style.wire_signature;
    clip_planes_ = snapshot.clip_planes;
    return changed || local_visibility_changed;
  }

  const bool clip_changed = clip_planes_ != snapshot.clip_planes;
  const bool wire_style_changed =
      !has_synced_ ||
      synced_wire_style_signature_ != snapshot.style.wire_signature;
  if (has_synced_ &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderOverlayInstanceSelectionCacheNoWorkDependencies) &&
      !wire_style_changed && !clip_changed) {
    return false;
  }
  const bool local_visibility_changed =
      !render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          RenderSourceDependency::LocalVisibilityRevision);
  const PreparedPlans prepared = prepare(snapshot);
  const RenderInstanceBatchUpdateKind fill_update =
      classify_instance_batch_update(fill_plan_, prepared.plans.fills);
  const RenderInstanceBatchUpdateKind wire_update =
      classify_instance_batch_update(wire_plan_, prepared.plans.wires);
  if (fill_update == RenderInstanceBatchUpdateKind::Unchanged &&
      wire_update == RenderInstanceBatchUpdateKind::Unchanged) {
    if (clip_changed) {
      update_wire_clip_planes(snapshot.clip_planes);
    }
    synced_source_stamp_ = snapshot;
    synced_wire_style_signature_ = snapshot.style.wire_signature;
    has_synced_ = true;
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
    synced_source_stamp_ = snapshot;
    synced_wire_style_signature_ = snapshot.style.wire_signature;
    has_synced_ = true;
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
      wire_backend_.destroy_batches(new_wires);
      for (MeshRenderableProxy &proxy : new_fills) {
        mesh_factory_->destroy(proxy);
      }
      wire_backend_.prune_unused_geometry_resources(wire_proxies_);
      refresh_wire_resource_stats();
      return false;
    }
    WireBatchProxy proxy = wire_backend_.create_batch(
        wire_geometry_input(*record), chunk,
        wire_batch_style(*record, snapshot.clip_planes));
    if (!wire_backend_.valid(proxy)) {
      wire_backend_.destroy_batch(proxy);
      wire_backend_.destroy_batches(new_wires);
      for (MeshRenderableProxy &fill : new_fills) {
        mesh_factory_->destroy(fill);
      }
      wire_backend_.prune_unused_geometry_resources(wire_proxies_);
      refresh_wire_resource_stats();
      return false;
    }
    new_wires.push_back(std::move(proxy));
  }

  for (MeshRenderableProxy &proxy : fill_proxies_) {
    mesh_factory_->destroy(proxy);
  }
  wire_backend_.destroy_batches(wire_proxies_);
  fill_proxies_.swap(new_fills);
  wire_proxies_.swap(new_wires);
  fill_plan_ = prepared.plans.fills;
  wire_plan_ = prepared.plans.wires;
  clip_planes_ = snapshot.clip_planes;
  synced_source_stamp_ = snapshot;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  has_synced_ = true;
  ++stats_.rebuild_count;
  stats_.fill_chunk_count = fill_proxies_.size();
  stats_.wire_chunk_count = wire_proxies_.size();
  stats_.selected_member_count = snapshot.instance_selection_fills.size();
  wire_backend_.prune_unused_geometry_resources(wire_proxies_);
  refresh_wire_resource_stats();
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
    if (!wire_backend_.update_batch(
            wire_proxies_[index], prepared.plans.wires.chunks[index])) {
      return false;
    }
  }
  return true;
}

void InstanceSelectionOverlayCache::refresh_wire_resource_stats() {
  const InstancedWireRenderBackendStats &wire_stats = wire_backend_.stats();
  stats_.unique_wire_geometry_resource_count =
      wire_stats.unique_geometry_resource_count;
  stats_.wire_derived_vertex_bytes = wire_stats.derived_vertex_bytes;
  stats_.wire_derived_index_bytes = wire_stats.derived_index_bytes;
  stats_.wire_geometry_upload_count = wire_stats.geometry_upload_count;
  stats_.wire_geometry_reuse_count = wire_stats.geometry_reuse_count;
}

void InstanceSelectionOverlayCache::update_wire_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  clip_planes_ = clip_planes;
  wire_backend_.update_clip_plane(wire_proxies_, clip_planes.near_plane);
}

} // namespace quader
