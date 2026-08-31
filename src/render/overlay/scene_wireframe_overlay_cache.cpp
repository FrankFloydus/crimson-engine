////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/scene_wireframe_overlay_cache.hpp"

#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <bit>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

std::uint32_t wire_color(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  if (mesh.selected) {
    return style.selection_wire_color;
  }
  constexpr std::uint8_t kWireframeAlpha = static_cast<std::uint8_t>(
      (kOverlayWireframeXrayColor >> 24u) & 0xffu);
  return rgba8(normalized_to_byte(mesh.wireframe_tint.x),
               normalized_to_byte(mesh.wireframe_tint.y),
               normalized_to_byte(mesh.wireframe_tint.z), kWireframeAlpha);
}

ViewportOverlayLayer wire_layer(const RenderOverlayMeshSnapshot &mesh) {
  return mesh.selected ? ViewportOverlayLayer::SelectedEdge
                       : ViewportOverlayLayer::WireframeXray;
}

float wire_width(const RenderOverlayMeshSnapshot &mesh,
                 const ViewportPersistentOverlayStyleSnapshot &style) {
  return mesh.selected
             ? style.selection_edge_line_width_pixels
             : viewport_overlay_line_width_pixels(wire_layer(mesh), 0.0f);
}

std::uint64_t wire_geometry_identity(const RenderOverlayMeshSnapshot &mesh) {
  if (mesh.wire_geometry_payload != nullptr &&
      mesh.wire_geometry_payload->content_id != 0u) {
    return mesh.wire_geometry_payload->content_id;
  }
  return RenderSignatureBuilder{}
      .add_value(mesh.mesh_id)
      .add_value(mesh.mesh_revision)
      .value();
}

std::uint64_t wire_geometry_signature(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return RenderSignatureBuilder{}
      .add_value(wire_geometry_identity(mesh))
      .add_value(wire_color(mesh, style))
      .value();
}

RenderInstancePrimitiveKey wire_key(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style,
    const RenderInstanceEditScope &instance_edit_scope) {
  return {
      // These exact runtime tokens deliberately avoid grouping on a content
      // hash. The remaining exact style fields distinguish the derived VB and
      // its per-batch material state without scene or AssetId identity.
      .geometry_payload = wire_geometry_identity(mesh),
      .material_payload = wire_geometry_identity(mesh),
      .index_offset = wire_color(mesh, style),
      .index_count = std::bit_cast<std::uint32_t>(wire_width(mesh, style)),
      .layer_mask = static_cast<std::uint8_t>(wire_layer(mesh)),
      .channel = kViewportDefaultRenderableChannel,
      .presentation_flags = static_cast<std::uint8_t>(
          is_instance_edit_context(instance_edit_scope, mesh.occurrence,
                                   mesh.selected)
              ? RenderInstancePresentationFlag::Dimmed
              : RenderInstancePresentationFlag::None),
      .double_sided = true,
      .editor_unlit = true,
  };
}

/// Hashes every exact field used to group one derived wire primitive.
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

std::uint64_t participant_token(const RenderOverlayMeshSnapshot &mesh) {
  return mesh.participant_token != 0u ? mesh.participant_token : mesh.entity;
}

/// Holds indexed scene-wire occurrences before instance batch planning.
struct SceneWireframeOccurrencePreparation final {
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  std::unordered_map<RenderInstancePrimitiveKey,
                     const RenderOverlayMeshSnapshot *, PrimitiveKeyHash>
      record_by_key;
  SceneWireframeOverlayPlanWorkStats work;
  std::size_t unique_geometry_style_count = 0u;
};

SceneWireframeOccurrencePreparation prepare_wire_occurrences(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  SceneWireframeOccurrencePreparation prepared;
  prepared.occurrences.reserve(snapshot.meshes.size());
  prepared.record_by_key.reserve(snapshot.meshes.size());
  std::unordered_set<RenderInstancePrimitiveKey, PrimitiveKeyHash> unique_keys;
  unique_keys.reserve(snapshot.meshes.size());
  std::unordered_map<const MeshGeometryPayload *, RenderInstanceBounds>
      local_bounds_by_geometry;
  local_bounds_by_geometry.reserve(snapshot.meshes.size());
  for (const RenderOverlayMeshSnapshot &mesh : snapshot.meshes) {
    if (!mesh.has_wire_geometry()) {
      continue;
    }
    const RenderInstancePrimitiveKey key =
        wire_key(mesh, snapshot.style, snapshot.instance_edit_scope);
    prepared.record_by_key.try_emplace(key, &mesh);
    const std::uint64_t participant = participant_token(mesh);
    if (participant == 0u) {
      continue;
    }

    RenderInstanceBounds bounds;
    const MeshGeometryPayload *geometry = mesh.geometry_payload.get();
    const auto cached = geometry != nullptr
                            ? local_bounds_by_geometry.find(geometry)
                            : local_bounds_by_geometry.end();
    if (cached != local_bounds_by_geometry.end()) {
      bounds = cached->second;
      ++prepared.work.local_bounds_cache_hit_count;
    } else {
      bounds = compute_local_bounds(mesh.vertices);
      prepared.work.local_bounds_vertex_visit_count +=
          static_cast<std::uint64_t>(mesh.vertices.size());
      if (geometry != nullptr) {
        local_bounds_by_geometry.try_emplace(geometry, bounds);
      }
    }

    // Context styling separates materials, not the immutable edge buffers.
    RenderInstancePrimitiveKey geometry_key = key;
    geometry_key.presentation_flags =
        static_cast<std::uint8_t>(RenderInstancePresentationFlag::None);
    unique_keys.insert(geometry_key);
    prepared.occurrences.push_back({
        .key = key,
        .occurrence = participant,
        .world_transform = mesh.world_transform,
        .local_bounds = bounds,
    });
  }
  prepared.work.primitive_record_index_entry_count =
      prepared.record_by_key.size();
  prepared.unique_geometry_style_count = unique_keys.size();
  return prepared;
}

/// Couples planned wire batches with their source mesh records.
struct SceneWireframeBatchPreparation final {
  SceneWireframeOverlayBatchPlan planned;
  std::vector<const RenderOverlayMeshSnapshot *> records;
};

SceneWireframeBatchPreparation prepare_wire_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout) {
  SceneWireframeOccurrencePreparation occurrences =
      prepare_wire_occurrences(snapshot);
  SceneWireframeBatchPreparation prepared;
  prepared.planned.batches = plan_render_instance_batches(
      occurrences.occurrences, max_instances_per_chunk, retained_layout);
  prepared.planned.participant_count = occurrences.occurrences.size();
  prepared.planned.unique_geometry_style_count =
      occurrences.unique_geometry_style_count;
  prepared.planned.work = occurrences.work;
  prepared.records.reserve(prepared.planned.batches.chunks.size());
  for (const RenderInstanceBatchChunk &chunk :
       prepared.planned.batches.chunks) {
    ++prepared.planned.work.chunk_record_lookup_count;
    const auto found = occurrences.record_by_key.find(chunk.key);
    prepared.records.push_back(
        found == occurrences.record_by_key.end() ? nullptr : found->second);
  }
  return prepared;
}

InstancedWireGeometryInput wire_geometry_input(
    const RenderOverlayMeshSnapshot &mesh,
    const ViewportPersistentOverlayStyleSnapshot &style) {
  return {
      .signature = wire_geometry_signature(mesh, style),
      .wire_geometry = mesh.wire_geometry_payload,
      .color = wire_color(mesh, style),
      .segments = mesh.wire_segments,
  };
}

InstancedWireBatchStyle wire_batch_style(
    const RenderOverlayMeshSnapshot &mesh,
    const RenderInstanceBatchChunk &chunk,
    const ViewportPersistentOverlayStyleSnapshot &style,
    ViewportCameraClipPlanes clip_planes, std::uint8_t layer_mask) {
  const ViewportOverlayLayer layer = wire_layer(mesh);
  const std::uint16_t blend_order = static_cast<std::uint16_t>(
      viewport_overlay_draw_order(layer) * 64u);
  const bool dimmed =
      (chunk.key.presentation_flags &
       static_cast<std::uint8_t>(RenderInstancePresentationFlag::Dimmed)) != 0u;
  return {
      .render_state = OverlayRenderableBuilder::make_state(
          layer, ViewportOverlayGeometryKind::Lines, blend_order),
      .instance_edit_style = style.instance_edit_style,
      .line_width_pixels = wire_width(mesh, style),
      .near_plane = clip_planes.near_plane,
      .instance_edit_dim = dimmed ? 1.0f : 0.0f,
      .layer_mask = layer_mask,
      .channel = kViewportDefaultRenderableChannel,
  };
}

} // namespace

/// Couples a full-scene wire batch plan with its source mesh records.
struct SceneWireframeOverlayCache::PreparedPlan final {
  SceneWireframeOverlayBatchPlan planned;
  std::vector<const RenderOverlayMeshSnapshot *> records;
};

SceneWireframeOverlayCache::SceneWireframeOverlayCache() = default;

SceneWireframeOverlayBatchPlan plan_scene_wireframe_overlay_batches(
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    std::size_t max_instances_per_chunk,
    const RenderInstanceBatchPlan *retained_layout) {
  SceneWireframeBatchPreparation prepared =
      prepare_wire_batches(snapshot, max_instances_per_chunk, retained_layout);
  return std::move(prepared.planned);
}

bool scene_wireframe_overlay_mesh_inputs_match(
    std::span<const RenderOverlayMeshSnapshot> previous,
    std::span<const RenderOverlayMeshSnapshot> next) {
  if (previous.size() != next.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < previous.size(); ++index) {
    const RenderOverlayMeshSnapshot &before = previous[index];
    const RenderOverlayMeshSnapshot &after = next[index];
    // Pointer equality only proves unchanged content for immutable, pinned
    // storage. Ownerless legacy spans may have been mutated in place.
    if ((!before.vertices.empty() &&
         (before.geometry_payload == nullptr ||
          before.vertices.data() != before.geometry_payload->vertices.data())) ||
        (!before.indices.empty() &&
         (before.geometry_payload == nullptr ||
          before.indices.data() != before.geometry_payload->indices.data())) ||
        (!before.wire_segments.empty() &&
         (before.wire_geometry_payload == nullptr ||
          before.wire_segments.data() !=
              before.wire_geometry_payload->segments.data())) ||
        before.entity != after.entity ||
        before.occurrence != after.occurrence ||
        before.definition != after.definition ||
        before.member != after.member ||
        before.participant_token != after.participant_token ||
        before.mesh_id != after.mesh_id ||
        before.mesh_revision != after.mesh_revision ||
        before.geometry_payload != after.geometry_payload ||
        before.geometry_content_id != after.geometry_content_id ||
        before.wire_geometry_payload != after.wire_geometry_payload ||
        before.world_transform.values != after.world_transform.values ||
        before.selected != after.selected ||
        before.wireframe_tint.x != after.wireframe_tint.x ||
        before.wireframe_tint.y != after.wireframe_tint.y ||
        before.wireframe_tint.z != after.wireframe_tint.z ||
        before.vertices.data() != after.vertices.data() ||
        before.vertices.size() != after.vertices.size() ||
        before.indices.data() != after.indices.data() ||
        before.indices.size() != after.indices.size() ||
        before.wire_segments.data() != after.wire_segments.data() ||
        before.wire_segments.size() != after.wire_segments.size()) {
      return false;
    }
  }
  return true;
}

SceneWireframeOverlayCache::~SceneWireframeOverlayCache() { shutdown(); }

void SceneWireframeOverlayCache::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &line_material,
    std::size_t max_instances_per_chunk) {
  shutdown();
  max_instances_per_chunk_ =
      std::max<std::size_t>(1u, max_instances_per_chunk);
  wire_backend_.initialize(engine, scene, line_material,
                           max_instances_per_chunk_);
}

void SceneWireframeOverlayCache::shutdown() {
  clear();
  wire_backend_.shutdown();
  max_instances_per_chunk_ = 1u;
  synced_source_stamp_ = {};
  synced_wire_style_signature_ = 0u;
  synced_instance_edit_context_signature_ = 0u;
  synced_selected_mesh_wire_visible_ = true;
  synced_clip_planes_ = {};
  layer_mask_ = 0u;
  has_synced_ = false;
  stats_ = {};
}

void SceneWireframeOverlayCache::clear() {
  wire_backend_.destroy_batches(proxies_);
  proxies_.clear();
  plan_ = {};
  synced_meshes_.clear();
  has_synced_ = false;
  stats_.participant_count = 0u;
  stats_.chunk_count = 0u;
}

SceneWireframeOverlayCache::PreparedPlan
SceneWireframeOverlayCache::prepare(
    const RenderOverlaySceneWireframeSnapshot &snapshot) const {
  SceneWireframeBatchPreparation batch_preparation =
      prepare_wire_batches(snapshot, max_instances_per_chunk_, &plan_);
  PreparedPlan prepared;
  prepared.planned = std::move(batch_preparation.planned);
  prepared.records = std::move(batch_preparation.records);
  return prepared;
}

void SceneWireframeOverlayCache::sync(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return;
  }
  if (has_synced_ &&
      !render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          RenderSourceDependency::SceneBinding)) {
    clear();
    wire_backend_.destroy_geometry_resources();
    refresh_wire_resource_stats();
    has_synced_ = false;
  }

  set_layer_mask(snapshot.layer_mask);
  if (snapshot.layer_mask == 0u) {
    return;
  }
  const bool clip_changed =
      !has_synced_ || synced_clip_planes_ != snapshot.clip_planes;
  const bool style_changed = scene_wireframe_style_changed(
      has_synced_, synced_wire_style_signature_, snapshot.style);
  const bool context_style_changed =
      !has_synced_ || synced_instance_edit_context_signature_ !=
                          snapshot.style.instance_edit_context_signature;
  if (has_synced_ &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderOverlaySceneWireframeCacheNoWorkDependencies) &&
      synced_selected_mesh_wire_visible_ ==
          snapshot.selected_mesh_wire_visible &&
      !clip_changed &&
      !style_changed) {
    if (context_style_changed) {
      update_instance_edit_style(snapshot.style.instance_edit_style);
      synced_instance_edit_context_signature_ =
          snapshot.style.instance_edit_context_signature;
    }
    return;
  }

  const bool mesh_inputs_match =
      has_synced_ && !style_changed &&
      synced_selected_mesh_wire_visible_ ==
          snapshot.selected_mesh_wire_visible &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          RenderSourceDependency::InstanceEditScope) &&
      scene_wireframe_overlay_mesh_inputs_match(synced_meshes_,
                                                snapshot.meshes);
  if (mesh_inputs_match) {
    // Global revisions also advance for curve edits and transforms. Their
    // unchanged mesh inputs must not rebuild a spatial plan or touch buffers.
    ++stats_.mesh_input_reuse_count;
  } else {
    ++stats_.plan_prepare_count;
    const PreparedPlan prepared = prepare(snapshot);
    const RenderInstanceBatchUpdateKind update =
        classify_instance_batch_update(plan_, prepared.planned.batches);
    bool synchronized = false;
    if (!style_changed &&
        update == RenderInstanceBatchUpdateKind::TransformsOnly) {
      synchronized = update_transforms(prepared);
      if (synchronized) {
        plan_ = prepared.planned.batches;
        ++stats_.transform_update_count;
      }
    } else if (!style_changed &&
               update == RenderInstanceBatchUpdateKind::Unchanged) {
      synchronized = true;
    }
    if (!synchronized) {
      synchronized = rebuild(prepared, snapshot);
    }
    if (!synchronized) {
      // A failed update can have changed earlier instance buffers already.
      // Force the next frame to retry rather than reuse the former input key.
      has_synced_ = false;
      return;
    }
    synced_meshes_ = snapshot.meshes;
    stats_.participant_count = prepared.planned.participant_count;
    stats_.chunk_count = prepared.planned.batches.chunks.size();
  }
  if (clip_changed) {
    update_clip_planes(snapshot.clip_planes);
  }
  if (context_style_changed) {
    update_instance_edit_style(snapshot.style.instance_edit_style);
  }
  synced_source_stamp_ = snapshot;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  synced_instance_edit_context_signature_ =
      snapshot.style.instance_edit_context_signature;
  synced_selected_mesh_wire_visible_ =
      snapshot.selected_mesh_wire_visible;
  synced_clip_planes_ = snapshot.clip_planes;
  has_synced_ = true;
}

bool SceneWireframeOverlayCache::rebuild(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  std::vector<WireBatchProxy> created;
  created.reserve(prepared.planned.batches.chunks.size());
  for (std::size_t index = 0u;
       index < prepared.planned.batches.chunks.size(); ++index) {
    const RenderOverlayMeshSnapshot *record = prepared.records[index];
    if (record == nullptr) {
      wire_backend_.destroy_batches(created);
      wire_backend_.prune_unused_geometry_resources(proxies_);
      refresh_wire_resource_stats();
      return false;
    }
    const RenderInstanceBatchChunk &chunk =
        prepared.planned.batches.chunks[index];
    WireBatchProxy proxy = wire_backend_.create_batch(
        wire_geometry_input(*record, snapshot.style), chunk,
        wire_batch_style(*record, chunk, snapshot.style,
                         snapshot.clip_planes, layer_mask_));
    if (!wire_backend_.valid(proxy)) {
      wire_backend_.destroy_batch(proxy);
      wire_backend_.destroy_batches(created);
      wire_backend_.prune_unused_geometry_resources(proxies_);
      refresh_wire_resource_stats();
      return false;
    }
    created.push_back(std::move(proxy));
  }

  wire_backend_.destroy_batches(proxies_);
  proxies_.swap(created);
  plan_ = prepared.planned.batches;
  ++stats_.rebuild_count;
  stats_.participant_count = prepared.planned.participant_count;
  stats_.chunk_count = proxies_.size();
  wire_backend_.prune_unused_geometry_resources(proxies_);
  refresh_wire_resource_stats();
  return true;
}

bool SceneWireframeOverlayCache::update_transforms(
    const PreparedPlan &prepared) {
  if (prepared.planned.batches.chunks.size() != proxies_.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxies_.size(); ++index) {
    if (!wire_backend_.update_batch(
            proxies_[index], prepared.planned.batches.chunks[index])) {
      return false;
    }
  }
  return true;
}

void SceneWireframeOverlayCache::refresh_wire_resource_stats() {
  const InstancedWireRenderBackendStats &wire_stats = wire_backend_.stats();
  stats_.unique_geometry_resource_count =
      wire_stats.unique_geometry_resource_count;
  stats_.derived_vertex_bytes = wire_stats.derived_vertex_bytes;
  stats_.derived_index_bytes = wire_stats.derived_index_bytes;
  stats_.geometry_upload_count = wire_stats.geometry_upload_count;
  stats_.geometry_reuse_count = wire_stats.geometry_reuse_count;
}

void SceneWireframeOverlayCache::update_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  wire_backend_.update_clip_plane(proxies_, clip_planes.near_plane);
}

void SceneWireframeOverlayCache::update_instance_edit_style(
    const InstanceEditStyle &style) {
  wire_backend_.update_instance_edit_style(proxies_, style, true);
}

void SceneWireframeOverlayCache::set_layer_mask(std::uint8_t layer_mask) {
  if (layer_mask_ == layer_mask) {
    return;
  }
  layer_mask_ = layer_mask;
  wire_backend_.set_layer_mask(proxies_, layer_mask_);
}

} // namespace quader
