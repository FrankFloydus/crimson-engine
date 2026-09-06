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
#include <array>
#include <bit>
#include <cmath>
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
      // Color stays exact because it is baked into the derived vertices. The
      // logical width keeps independently authored widths in separate batches
      // while allowing a global raster-scale change to patch the uniform.
      .material_payload =
          std::bit_cast<std::uint32_t>(record.logical_wire_width_pixels),
      .index_offset = record.wire_color,
      .index_count = static_cast<std::uint32_t>(record.wire_segments.size()),
      .layer_mask = kViewportOverlayLayerMask,
      .channel = kViewportDefaultRenderableChannel,
      .double_sided = true,
      .editor_unlit = true,
  };
}

/// Hashes every exact identity field used to group one selected-instance
/// primitive.
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
    if (snapshot.selected_mesh_outline_only) {
      continue;
    }
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
    ViewportCameraClipPlanes clip_planes,
    const ViewportPersistentOverlayRasterGroup &raster_group) {
  const std::uint16_t blend_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(ViewportOverlayLayer::SourceWire) * 64u);
  return {
      .render_state = OverlayRenderableBuilder::make_state(
          ViewportOverlayLayer::SourceWire,
          ViewportOverlayGeometryKind::Lines, blend_order),
      // Selected-occurrence wires never receive the scene context effect.
      .instance_edit_style = {},
      .line_width_pixels =
          record.logical_wire_width_pixels * raster_group.style.raster_scale,
      .near_plane = clip_planes.near_plane,
      .instance_edit_dim = 0.0f,
      .layer_mask = raster_group.layer_mask,
      .channel = kViewportDefaultRenderableChannel,
  };
}

ViewportPersistentOverlayRasterGroupSet selected_wire_raster_groups(
    const RenderOverlaySelectedObjectSnapshot &snapshot) {
  ViewportPersistentOverlayRasterGroupSet result = snapshot.raster_groups;
  if (result.empty()) {
    const bool added = result.add(kViewportOverlayLayerMask, snapshot.style);
    (void)added;
  }
  return result;
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
  synced_raster_groups_ = {};
  has_synced_ = false;
  stats_ = {};
}

void InstanceSelectionOverlayCache::clear(bool release_geometry_resources) {
  if (mesh_factory_ != nullptr) {
    for (MeshRenderableProxy &proxy : fill_proxies_) {
      mesh_factory_->destroy(proxy);
    }
  }
  destroy_wire_raster_groups(wire_proxy_groups_);
  fill_proxies_.clear();
  wire_proxy_groups_.clear();
  fill_plan_ = {};
  wire_plan_ = {};
  synced_raster_groups_ = {};
  has_synced_ = false;
  stats_.fill_chunk_count = 0u;
  stats_.wire_chunk_count = 0u;
  stats_.wire_raster_group_count = 0u;
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
  const ViewportPersistentOverlayRasterGroupSet raster_groups =
      selected_wire_raster_groups(snapshot);
  if (!snapshot.visible || (snapshot.instance_selection_fills.empty() &&
                            snapshot.instance_selection_wires.empty())) {
    const bool changed =
        !fill_proxies_.empty() || !wire_proxy_groups_.empty();
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
  const bool raster_groups_changed =
      !has_synced_ || !viewport_persistent_overlay_raster_groups_match(
                          synced_raster_groups_, raster_groups);
  if (has_synced_ &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderOverlayInstanceSelectionCacheNoWorkDependencies) &&
      synced_selected_mesh_outline_only_ == snapshot.selected_mesh_outline_only &&
      !wire_style_changed && !raster_groups_changed && !clip_changed) {
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
    const std::uint64_t prior_line_width_updates =
        wire_backend_.stats().line_width_update_count;
    if (!reconcile_wire_raster_groups(prepared, raster_groups,
                                      snapshot.clip_planes)) {
      const bool rebuilt = rebuild(prepared, snapshot);
      if (!rebuilt) {
        has_synced_ = false;
      }
      return rebuilt;
    }
    const bool line_width_updated =
        wire_backend_.stats().line_width_update_count !=
        prior_line_width_updates;
    if (clip_changed) {
      update_wire_clip_planes(snapshot.clip_planes);
    }
    synced_source_stamp_ = snapshot;
    synced_wire_style_signature_ = snapshot.style.wire_signature;
    synced_raster_groups_ = raster_groups;
    synced_selected_mesh_outline_only_ = snapshot.selected_mesh_outline_only;
    has_synced_ = true;
    return raster_groups_changed || line_width_updated || clip_changed ||
           local_visibility_changed;
  }
  if (fill_update != RenderInstanceBatchUpdateKind::Rebuild &&
      wire_update != RenderInstanceBatchUpdateKind::Rebuild &&
      update_transforms(prepared) &&
      reconcile_wire_raster_groups(prepared, raster_groups,
                                   snapshot.clip_planes)) {
    fill_plan_ = prepared.plans.fills;
    wire_plan_ = prepared.plans.wires;
    ++stats_.transform_update_count;
    if (clip_changed) {
      update_wire_clip_planes(snapshot.clip_planes);
    }
    synced_source_stamp_ = snapshot;
    synced_wire_style_signature_ = snapshot.style.wire_signature;
    synced_raster_groups_ = raster_groups;
    synced_selected_mesh_outline_only_ = snapshot.selected_mesh_outline_only;
    has_synced_ = true;
    return true;
  }
  const bool rebuilt = rebuild(prepared, snapshot);
  if (!rebuilt) {
    has_synced_ = false;
  }
  return rebuilt;
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

  const ViewportPersistentOverlayRasterGroupSet raster_groups =
      selected_wire_raster_groups(snapshot);
  std::vector<WireRasterProxyGroup> new_wires;
  new_wires.reserve(raster_groups.size());
  if (!prepared.plans.wires.chunks.empty()) {
    for (const ViewportPersistentOverlayRasterGroup &raster_group :
         raster_groups.groups()) {
      WireRasterProxyGroup proxy_group;
      if (!create_wire_raster_group(prepared, raster_group,
                                    snapshot.clip_planes, proxy_group)) {
        destroy_wire_raster_groups(new_wires);
        for (MeshRenderableProxy &fill : new_fills) {
          mesh_factory_->destroy(fill);
        }
        prune_unused_wire_geometry();
        refresh_wire_resource_stats();
        return false;
      }
      new_wires.push_back(std::move(proxy_group));
    }
  }

  for (MeshRenderableProxy &proxy : fill_proxies_) {
    mesh_factory_->destroy(proxy);
  }
  destroy_wire_raster_groups(wire_proxy_groups_);
  fill_proxies_.swap(new_fills);
  wire_proxy_groups_.swap(new_wires);
  fill_plan_ = prepared.plans.fills;
  wire_plan_ = prepared.plans.wires;
  clip_planes_ = snapshot.clip_planes;
  synced_source_stamp_ = snapshot;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  synced_raster_groups_ = raster_groups;
  synced_selected_mesh_outline_only_ = snapshot.selected_mesh_outline_only;
  has_synced_ = true;
  ++stats_.rebuild_count;
  stats_.fill_chunk_count = fill_proxies_.size();
  stats_.wire_chunk_count = prepared.plans.wires.chunks.size() *
                            wire_proxy_groups_.size();
  stats_.wire_raster_group_count = wire_proxy_groups_.size();
  stats_.selected_member_count = snapshot.instance_selection_fills.size();
  prune_unused_wire_geometry();
  refresh_wire_resource_stats();
  return true;
}

bool InstanceSelectionOverlayCache::update_transforms(
    const PreparedPlans &prepared) {
  if (prepared.plans.fills.chunks.size() != fill_proxies_.size()) {
    return false;
  }
  for (const WireRasterProxyGroup &proxy_group : wire_proxy_groups_) {
    if (prepared.plans.wires.chunks.size() != proxy_group.proxies.size()) {
      return false;
    }
  }
  for (std::size_t index = 0u; index < fill_proxies_.size(); ++index) {
    if (!mesh_factory_->update_explicit_instances(
            fill_proxies_[index],
            prepared.plans.fills.chunks[index].world_transforms,
            prepared.plans.fills.chunks[index].world_bounds)) {
      return false;
    }
  }
  for (WireRasterProxyGroup &proxy_group : wire_proxy_groups_) {
    for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
      if (!wire_backend_.update_batch(
              proxy_group.proxies[index],
              prepared.plans.wires.chunks[index])) {
        return false;
      }
    }
  }
  return true;
}

bool InstanceSelectionOverlayCache::reconcile_wire_raster_groups(
    const PreparedPlans &prepared,
    const ViewportPersistentOverlayRasterGroupSet &raster_groups,
    ViewportCameraClipPlanes clip_planes) {
  if (prepared.plans.wires.chunks.empty()) {
    destroy_wire_raster_groups(wire_proxy_groups_);
    wire_proxy_groups_.clear();
    stats_.wire_chunk_count = 0u;
    stats_.wire_raster_group_count = 0u;
    prune_unused_wire_geometry();
    refresh_wire_resource_stats();
    return true;
  }

  constexpr std::size_t kNoMatch =
      kMaxViewportPersistentOverlayRasterGroups;
  const std::span<const ViewportPersistentOverlayRasterGroup> targets =
      raster_groups.groups();
  std::array<std::size_t, kMaxViewportPersistentOverlayRasterGroups>
      matched_indices{};
  matched_indices.fill(kNoMatch);
  std::array<bool, kMaxViewportPersistentOverlayRasterGroups> claimed{};
  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    int best_score = -1;
    std::size_t best_index = kNoMatch;
    for (std::size_t current_index = 0u;
         current_index < wire_proxy_groups_.size(); ++current_index) {
      if (claimed[current_index]) {
        continue;
      }
      const WireRasterProxyGroup &current =
          wire_proxy_groups_[current_index];
      const bool same_style = viewport_persistent_overlay_wire_styles_match(
          current.style, targets[target_index].style);
      const bool same_mask =
          current.layer_mask == targets[target_index].layer_mask;
      const bool overlaps =
          (current.layer_mask & targets[target_index].layer_mask) != 0u;
      const int score = same_style ? 3 : same_mask ? 2 : overlaps ? 1 : 0;
      if (score > best_score) {
        best_score = score;
        best_index = current_index;
      }
    }
    if (best_index != kNoMatch) {
      matched_indices[target_index] = best_index;
      claimed[best_index] = true;
    }
  }

  std::array<WireRasterProxyGroup,
             kMaxViewportPersistentOverlayRasterGroups>
      created{};
  std::array<bool, kMaxViewportPersistentOverlayRasterGroups> has_created{};
  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    if (matched_indices[target_index] != kNoMatch) {
      continue;
    }
    if (!create_wire_raster_group(prepared, targets[target_index], clip_planes,
                                  created[target_index])) {
      destroy_wire_raster_groups(created);
      prune_unused_wire_geometry();
      refresh_wire_resource_stats();
      return false;
    }
    has_created[target_index] = true;
  }

  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    if (matched_indices[target_index] == kNoMatch) {
      continue;
    }
    if (!update_wire_raster_group(
            prepared, targets[target_index],
            wire_proxy_groups_[matched_indices[target_index]])) {
      for (std::size_t index = 0u; index < created.size(); ++index) {
        if (has_created[index]) {
          wire_backend_.destroy_batches(created[index].proxies);
        }
      }
      prune_unused_wire_geometry();
      refresh_wire_resource_stats();
      return false;
    }
  }

  std::vector<WireRasterProxyGroup> next;
  next.reserve(targets.size());
  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    if (matched_indices[target_index] != kNoMatch) {
      next.push_back(
          std::move(wire_proxy_groups_[matched_indices[target_index]]));
    } else {
      next.push_back(std::move(created[target_index]));
    }
  }
  for (std::size_t current_index = 0u;
       current_index < wire_proxy_groups_.size(); ++current_index) {
    if (!claimed[current_index]) {
      wire_backend_.destroy_batches(
          wire_proxy_groups_[current_index].proxies);
    }
  }
  wire_proxy_groups_ = std::move(next);
  stats_.wire_chunk_count = prepared.plans.wires.chunks.size() *
                            wire_proxy_groups_.size();
  stats_.wire_raster_group_count = wire_proxy_groups_.size();
  prune_unused_wire_geometry();
  refresh_wire_resource_stats();
  return true;
}

bool InstanceSelectionOverlayCache::create_wire_raster_group(
    const PreparedPlans &prepared,
    const ViewportPersistentOverlayRasterGroup &raster_group,
    ViewportCameraClipPlanes clip_planes, WireRasterProxyGroup &result) {
  result.layer_mask = raster_group.layer_mask;
  result.style = raster_group.style;
  result.proxies.reserve(prepared.plans.wires.chunks.size());
  for (std::size_t index = 0u;
       index < prepared.plans.wires.chunks.size(); ++index) {
    const RenderOverlayInstanceSelectionWireRecord *record =
        prepared.wires[index];
    const float line_width_pixels =
        record == nullptr
            ? 0.0f
            : record->logical_wire_width_pixels *
                  raster_group.style.raster_scale;
    if (record == nullptr || !std::isfinite(line_width_pixels) ||
        line_width_pixels <= 0.0f) {
      wire_backend_.destroy_batches(result.proxies);
      result.proxies.clear();
      return false;
    }
    WireBatchProxy proxy = wire_backend_.create_batch(
        wire_geometry_input(*record), prepared.plans.wires.chunks[index],
        wire_batch_style(*record, clip_planes, raster_group));
    if (!wire_backend_.valid(proxy)) {
      wire_backend_.destroy_batch(proxy);
      wire_backend_.destroy_batches(result.proxies);
      result.proxies.clear();
      return false;
    }
    result.proxies.push_back(std::move(proxy));
  }
  return true;
}

bool InstanceSelectionOverlayCache::update_wire_raster_group(
    const PreparedPlans &prepared,
    const ViewportPersistentOverlayRasterGroup &raster_group,
    WireRasterProxyGroup &proxy_group) {
  if (prepared.wires.size() != proxy_group.proxies.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
    const RenderOverlayInstanceSelectionWireRecord *record =
        prepared.wires[index];
    const float line_width_pixels =
        record == nullptr
            ? 0.0f
            : record->logical_wire_width_pixels *
                  raster_group.style.raster_scale;
    if (record == nullptr ||
        !wire_backend_.valid(proxy_group.proxies[index]) ||
        !std::isfinite(line_width_pixels) || line_width_pixels <= 0.0f) {
      return false;
    }
  }
  if (proxy_group.layer_mask != raster_group.layer_mask) {
    wire_backend_.set_layer_mask(proxy_group.proxies,
                                 raster_group.layer_mask);
  }
  for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
    const float line_width_pixels =
        prepared.wires[index]->logical_wire_width_pixels *
        raster_group.style.raster_scale;
    if (!wire_backend_.update_line_width(proxy_group.proxies[index],
                                         line_width_pixels)) {
      return false;
    }
  }
  proxy_group.layer_mask = raster_group.layer_mask;
  proxy_group.style = raster_group.style;
  return true;
}

void InstanceSelectionOverlayCache::destroy_wire_raster_groups(
    std::span<WireRasterProxyGroup> proxy_groups) {
  for (WireRasterProxyGroup &proxy_group : proxy_groups) {
    wire_backend_.destroy_batches(proxy_group.proxies);
  }
}

void InstanceSelectionOverlayCache::prune_unused_wire_geometry() {
  std::array<std::span<const WireBatchProxy>,
             kMaxViewportPersistentOverlayRasterGroups>
      live_groups{};
  for (std::size_t index = 0u; index < wire_proxy_groups_.size(); ++index) {
    live_groups[index] = wire_proxy_groups_[index].proxies;
  }
  wire_backend_.prune_unused_geometry_resources(
      std::span<const std::span<const WireBatchProxy>>(live_groups)
          .first(wire_proxy_groups_.size()));
}

void InstanceSelectionOverlayCache::refresh_wire_resource_stats() {
  const InstancedWireRenderBackendStats &wire_stats = wire_backend_.stats();
  stats_.unique_wire_geometry_resource_count =
      wire_stats.unique_geometry_resource_count;
  stats_.wire_derived_vertex_bytes = wire_stats.derived_vertex_bytes;
  stats_.wire_derived_index_bytes = wire_stats.derived_index_bytes;
  stats_.wire_geometry_upload_count = wire_stats.geometry_upload_count;
  stats_.wire_geometry_reuse_count = wire_stats.geometry_reuse_count;
  stats_.wire_line_width_update_count = wire_stats.line_width_update_count;
}

void InstanceSelectionOverlayCache::update_wire_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  clip_planes_ = clip_planes;
  for (WireRasterProxyGroup &proxy_group : wire_proxy_groups_) {
    wire_backend_.update_clip_plane(proxy_group.proxies,
                                    clip_planes.near_plane);
  }
}

} // namespace quader
