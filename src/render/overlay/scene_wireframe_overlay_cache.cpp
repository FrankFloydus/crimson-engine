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
#include <array>
#include <cmath>
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
    return mesh.active_selected ? style.active_selection_wire_color
                                : style.selection_wire_color;
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
             ? (mesh.active_selected ? style.active_selection_line_width_pixels
                                     : style.selection_edge_line_width_pixels)
             : viewport_overlay_line_width_pixels(wire_layer(mesh), 0.0f) *
                   style.raster_scale;
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
      // hash. Baked color and geometry stay in identity; mutable material line
      // width deliberately does not.
      .geometry_payload = wire_geometry_identity(mesh),
      .material_payload = wire_geometry_identity(mesh),
      .index_offset = wire_color(mesh, style),
      .index_count = static_cast<std::uint32_t>(mesh.wire_segments.size()),
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
    if (!mesh.has_wire_geometry() ||
        (!mesh.selected && snapshot.layer_mask == 0u)) {
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

bool has_selected_scene_wire(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  return std::ranges::any_of(snapshot.meshes,
                             [](const RenderOverlayMeshSnapshot &mesh) {
                               return mesh.selected &&
                                      mesh.has_wire_geometry();
                             });
}

ViewportPersistentOverlayRasterGroupSet scene_wire_raster_groups(
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  const bool selected_wire_present = has_selected_scene_wire(snapshot);
  ViewportPersistentOverlayRasterGroupSet result;
  if (snapshot.raster_groups.empty()) {
    if (selected_wire_present || snapshot.layer_mask != 0u) {
      const bool added =
          result.add(kViewportOverlayLayerMask, snapshot.style);
      (void)added;
    }
    return result;
  }
  for (const ViewportPersistentOverlayRasterGroup &raster_group :
       snapshot.raster_groups.groups()) {
    if (selected_wire_present ||
        (raster_group.layer_mask & snapshot.layer_mask) != 0u) {
      const bool added = result.add(raster_group.layer_mask,
                                    raster_group.style);
      (void)added;
    }
  }
  return result;
}

std::uint8_t scene_wire_batch_layer_mask(
    const RenderOverlayMeshSnapshot &mesh,
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    const ViewportPersistentOverlayRasterGroup &raster_group) {
  if (snapshot.raster_groups.empty()) {
    return mesh.selected ? kViewportOverlayLayerMask : snapshot.layer_mask;
  }
  return mesh.selected
             ? raster_group.layer_mask
             : static_cast<std::uint8_t>(raster_group.layer_mask &
                                         snapshot.layer_mask);
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
        before.active_selected != after.active_selected ||
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
  synced_wire_style_ = {};
  synced_raster_groups_ = {};
  synced_instance_edit_context_signature_ = 0u;
  synced_selected_mesh_wire_visible_ = true;
  synced_selected_mesh_outline_only_ = false;
  synced_clip_planes_ = {};
  synced_layer_mask_ = 0u;
  layer_masks_forced_hidden_ = false;
  has_synced_ = false;
  stats_ = {};
}

void SceneWireframeOverlayCache::clear() {
  destroy_raster_groups(raster_proxy_groups_);
  raster_proxy_groups_.clear();
  plan_ = {};
  synced_meshes_.clear();
  synced_raster_groups_ = {};
  has_synced_ = false;
  stats_.participant_count = 0u;
  stats_.chunk_count = 0u;
  stats_.raster_group_count = 0u;
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

  const bool selected_wire_present = has_selected_scene_wire(snapshot);
  if (snapshot.layer_mask == 0u && !selected_wire_present) {
    set_all_layer_masks(0u);
    synced_layer_mask_ = 0u;
    return;
  }
  const ViewportPersistentOverlayRasterGroupSet raster_groups =
      scene_wire_raster_groups(snapshot);
  const bool clip_changed =
      !has_synced_ || synced_clip_planes_ != snapshot.clip_planes;
  const bool style_signature_changed =
      !has_synced_ ||
      synced_wire_style_signature_ != snapshot.style.wire_signature;
  const bool wire_color_changed =
      !has_synced_ || synced_wire_style_.selection_wire_color !=
                          snapshot.style.selection_wire_color ||
      synced_wire_style_.active_selection_wire_color !=
          snapshot.style.active_selection_wire_color;
  const bool line_width_changed =
      !has_synced_ || synced_wire_style_.raster_scale !=
                          snapshot.style.raster_scale ||
      synced_wire_style_.selection_edge_line_width_pixels !=
          snapshot.style.selection_edge_line_width_pixels ||
      synced_wire_style_.active_selection_line_width_pixels !=
          snapshot.style.active_selection_line_width_pixels;
  const bool style_changed = style_signature_changed || wire_color_changed ||
                             line_width_changed;
  const bool line_width_only_style_changed =
      has_synced_ && style_changed && !wire_color_changed &&
      line_width_changed;
  const bool style_requires_rebuild =
      style_changed && !line_width_only_style_changed;
  const bool raster_groups_changed =
      !has_synced_ || !viewport_persistent_overlay_raster_groups_match(
                          synced_raster_groups_, raster_groups);
  const bool layer_mask_changed =
      !has_synced_ || synced_layer_mask_ != snapshot.layer_mask;
  const bool context_style_changed =
      !has_synced_ || synced_instance_edit_context_signature_ !=
                          snapshot.style.instance_edit_context_signature;
  if (has_synced_ && !layer_masks_forced_hidden_ &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          kRenderOverlaySceneWireframeCacheNoWorkDependencies) &&
       synced_selected_mesh_wire_visible_ ==
           snapshot.selected_mesh_wire_visible &&
       synced_selected_mesh_outline_only_ ==
           snapshot.selected_mesh_outline_only &&
       !clip_changed && !raster_groups_changed && !layer_mask_changed &&
       !style_changed) {
    if (context_style_changed) {
      update_instance_edit_style(snapshot.style.instance_edit_style);
      synced_instance_edit_context_signature_ =
          snapshot.style.instance_edit_context_signature;
    }
    return;
  }

  const bool mesh_inputs_match =
      has_synced_ && !layer_masks_forced_hidden_ &&
      !style_requires_rebuild && !layer_mask_changed &&
      synced_selected_mesh_wire_visible_ ==
          snapshot.selected_mesh_wire_visible &&
      synced_selected_mesh_outline_only_ ==
          snapshot.selected_mesh_outline_only &&
      render_source_dependencies_match(
          synced_source_stamp_, snapshot,
          RenderSourceDependency::InstanceEditScope) &&
      scene_wireframe_overlay_mesh_inputs_match(synced_meshes_,
                                                snapshot.meshes);
  if (mesh_inputs_match) {
    // Global revisions also advance for curve edits and transforms. Their
    // unchanged mesh inputs must not rebuild a spatial plan or touch buffers.
    ++stats_.mesh_input_reuse_count;
    if (line_width_only_style_changed || raster_groups_changed) {
      ++stats_.plan_prepare_count;
      const PreparedPlan prepared = prepare(snapshot);
      if (!reconcile_raster_groups(prepared, snapshot, raster_groups)) {
        has_synced_ = false;
        return;
      }
    }
  } else {
    ++stats_.plan_prepare_count;
    const PreparedPlan prepared = prepare(snapshot);
    const RenderInstanceBatchUpdateKind update =
        classify_instance_batch_update(plan_, prepared.planned.batches);
    bool synchronized = false;
    bool rebuilt = false;
    if (!style_requires_rebuild &&
        update == RenderInstanceBatchUpdateKind::TransformsOnly) {
      synchronized = update_transforms(prepared);
      if (synchronized) {
        plan_ = prepared.planned.batches;
        ++stats_.transform_update_count;
      }
    } else if (!style_requires_rebuild &&
               update == RenderInstanceBatchUpdateKind::Unchanged) {
      synchronized = true;
    }
    if (!synchronized) {
      synchronized = rebuild(prepared, snapshot);
      rebuilt = synchronized;
    }
    if (!synchronized) {
      // A failed update can have changed earlier instance buffers already.
      // Force the next frame to retry rather than reuse the former input key.
      has_synced_ = false;
      return;
    }
    if (!rebuilt &&
        !reconcile_raster_groups(prepared, snapshot, raster_groups)) {
      has_synced_ = false;
      return;
    }
    synced_meshes_ = snapshot.meshes;
    stats_.participant_count = prepared.planned.participant_count;
    stats_.chunk_count = prepared.planned.batches.chunks.size() *
                         raster_proxy_groups_.size();
    stats_.raster_group_count = raster_proxy_groups_.size();
  }
  if (clip_changed) {
    update_clip_planes(snapshot.clip_planes);
  }
  if (context_style_changed) {
    update_instance_edit_style(snapshot.style.instance_edit_style);
  }
  synced_source_stamp_ = snapshot;
  synced_wire_style_signature_ = snapshot.style.wire_signature;
  synced_wire_style_ = snapshot.style;
  synced_raster_groups_ = raster_groups;
  synced_instance_edit_context_signature_ =
      snapshot.style.instance_edit_context_signature;
  synced_selected_mesh_wire_visible_ =
      snapshot.selected_mesh_wire_visible;
  synced_selected_mesh_outline_only_ = snapshot.selected_mesh_outline_only;
  synced_clip_planes_ = snapshot.clip_planes;
  synced_layer_mask_ = snapshot.layer_mask;
  layer_masks_forced_hidden_ = false;
  has_synced_ = true;
}

bool SceneWireframeOverlayCache::rebuild(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  const ViewportPersistentOverlayRasterGroupSet raster_groups =
      scene_wire_raster_groups(snapshot);
  std::vector<RasterProxyGroup> created;
  created.reserve(raster_groups.size());
  if (!prepared.planned.batches.chunks.empty()) {
    for (const ViewportPersistentOverlayRasterGroup &raster_group :
         raster_groups.groups()) {
      RasterProxyGroup proxy_group;
      if (!create_raster_group(prepared, snapshot, raster_group,
                               proxy_group)) {
        destroy_raster_groups(created);
        prune_unused_wire_geometry();
        refresh_wire_resource_stats();
        return false;
      }
      created.push_back(std::move(proxy_group));
    }
  }

  destroy_raster_groups(raster_proxy_groups_);
  raster_proxy_groups_.swap(created);
  plan_ = prepared.planned.batches;
  ++stats_.rebuild_count;
  stats_.participant_count = prepared.planned.participant_count;
  stats_.chunk_count = prepared.planned.batches.chunks.size() *
                       raster_proxy_groups_.size();
  stats_.raster_group_count = raster_proxy_groups_.size();
  synced_raster_groups_ = raster_groups;
  layer_masks_forced_hidden_ = false;
  prune_unused_wire_geometry();
  refresh_wire_resource_stats();
  return true;
}

bool SceneWireframeOverlayCache::update_transforms(
    const PreparedPlan &prepared) {
  for (const RasterProxyGroup &proxy_group : raster_proxy_groups_) {
    if (prepared.planned.batches.chunks.size() !=
        proxy_group.proxies.size()) {
      return false;
    }
  }
  for (RasterProxyGroup &proxy_group : raster_proxy_groups_) {
    for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
      if (!wire_backend_.update_batch(
              proxy_group.proxies[index],
              prepared.planned.batches.chunks[index])) {
        return false;
      }
    }
  }
  return true;
}

bool SceneWireframeOverlayCache::reconcile_raster_groups(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    const ViewportPersistentOverlayRasterGroupSet &raster_groups) {
  if (prepared.planned.batches.chunks.empty() || raster_groups.empty()) {
    destroy_raster_groups(raster_proxy_groups_);
    raster_proxy_groups_.clear();
    stats_.chunk_count = 0u;
    stats_.raster_group_count = 0u;
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
         current_index < raster_proxy_groups_.size(); ++current_index) {
      if (claimed[current_index]) {
        continue;
      }
      const RasterProxyGroup &current =
          raster_proxy_groups_[current_index];
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

  std::array<RasterProxyGroup,
             kMaxViewportPersistentOverlayRasterGroups>
      created{};
  std::array<bool, kMaxViewportPersistentOverlayRasterGroups> has_created{};
  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    if (matched_indices[target_index] != kNoMatch) {
      continue;
    }
    if (!create_raster_group(prepared, snapshot, targets[target_index],
                             created[target_index])) {
      destroy_raster_groups(created);
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
    if (!update_raster_group(
            prepared, snapshot, targets[target_index],
            raster_proxy_groups_[matched_indices[target_index]])) {
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

  std::vector<RasterProxyGroup> next;
  next.reserve(targets.size());
  for (std::size_t target_index = 0u; target_index < targets.size();
       ++target_index) {
    if (matched_indices[target_index] != kNoMatch) {
      next.push_back(
          std::move(raster_proxy_groups_[matched_indices[target_index]]));
    } else {
      next.push_back(std::move(created[target_index]));
    }
  }
  for (std::size_t current_index = 0u;
       current_index < raster_proxy_groups_.size(); ++current_index) {
    if (!claimed[current_index]) {
      wire_backend_.destroy_batches(
          raster_proxy_groups_[current_index].proxies);
    }
  }
  raster_proxy_groups_ = std::move(next);
  stats_.chunk_count = prepared.planned.batches.chunks.size() *
                       raster_proxy_groups_.size();
  stats_.raster_group_count = raster_proxy_groups_.size();
  layer_masks_forced_hidden_ = false;
  prune_unused_wire_geometry();
  refresh_wire_resource_stats();
  return true;
}

bool SceneWireframeOverlayCache::create_raster_group(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    const ViewportPersistentOverlayRasterGroup &raster_group,
    RasterProxyGroup &result) {
  result.layer_mask = raster_group.layer_mask;
  result.style = raster_group.style;
  result.proxies.reserve(prepared.planned.batches.chunks.size());
  for (std::size_t index = 0u;
       index < prepared.planned.batches.chunks.size(); ++index) {
    const RenderOverlayMeshSnapshot *record = prepared.records[index];
    const float line_width_pixels =
        record == nullptr ? 0.0f : wire_width(*record, raster_group.style);
    if (record == nullptr || !std::isfinite(line_width_pixels) ||
        line_width_pixels <= 0.0f) {
      wire_backend_.destroy_batches(result.proxies);
      result.proxies.clear();
      return false;
    }
    const RenderInstanceBatchChunk &chunk =
        prepared.planned.batches.chunks[index];
    WireBatchProxy proxy = wire_backend_.create_batch(
        wire_geometry_input(*record, snapshot.style), chunk,
        wire_batch_style(
            *record, chunk, raster_group.style, snapshot.clip_planes,
            scene_wire_batch_layer_mask(*record, snapshot, raster_group)));
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

bool SceneWireframeOverlayCache::update_raster_group(
    const PreparedPlan &prepared,
    const RenderOverlaySceneWireframeSnapshot &snapshot,
    const ViewportPersistentOverlayRasterGroup &raster_group,
    RasterProxyGroup &proxy_group) {
  if (prepared.records.size() != proxy_group.proxies.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
    const RenderOverlayMeshSnapshot *record = prepared.records[index];
    const float line_width_pixels =
        record == nullptr ? 0.0f : wire_width(*record, raster_group.style);
    if (record == nullptr ||
        !wire_backend_.valid(proxy_group.proxies[index]) ||
        !std::isfinite(line_width_pixels) || line_width_pixels <= 0.0f) {
      return false;
    }
  }
  for (std::size_t index = 0u; index < proxy_group.proxies.size(); ++index) {
    const RenderOverlayMeshSnapshot &record = *prepared.records[index];
    const float line_width_pixels = wire_width(record, raster_group.style);
    if (!wire_backend_.update_line_width(proxy_group.proxies[index],
                                         line_width_pixels)) {
      return false;
    }
    const std::uint8_t layer_mask =
        scene_wire_batch_layer_mask(record, snapshot, raster_group);
    wire_backend_.set_layer_mask(
        std::span<const WireBatchProxy>(&proxy_group.proxies[index], 1u),
        layer_mask);
  }
  proxy_group.layer_mask = raster_group.layer_mask;
  proxy_group.style = raster_group.style;
  return true;
}

void SceneWireframeOverlayCache::destroy_raster_groups(
    std::span<RasterProxyGroup> proxy_groups) {
  for (RasterProxyGroup &proxy_group : proxy_groups) {
    wire_backend_.destroy_batches(proxy_group.proxies);
  }
}

void SceneWireframeOverlayCache::prune_unused_wire_geometry() {
  std::array<std::span<const WireBatchProxy>,
             kMaxViewportPersistentOverlayRasterGroups>
      live_groups{};
  for (std::size_t index = 0u; index < raster_proxy_groups_.size(); ++index) {
    live_groups[index] = raster_proxy_groups_[index].proxies;
  }
  wire_backend_.prune_unused_geometry_resources(
      std::span<const std::span<const WireBatchProxy>>(live_groups)
          .first(raster_proxy_groups_.size()));
}

void SceneWireframeOverlayCache::refresh_wire_resource_stats() {
  const InstancedWireRenderBackendStats &wire_stats = wire_backend_.stats();
  stats_.unique_geometry_resource_count =
      wire_stats.unique_geometry_resource_count;
  stats_.derived_vertex_bytes = wire_stats.derived_vertex_bytes;
  stats_.derived_index_bytes = wire_stats.derived_index_bytes;
  stats_.geometry_upload_count = wire_stats.geometry_upload_count;
  stats_.geometry_reuse_count = wire_stats.geometry_reuse_count;
  stats_.line_width_update_count = wire_stats.line_width_update_count;
}

void SceneWireframeOverlayCache::update_clip_planes(
    ViewportCameraClipPlanes clip_planes) {
  for (RasterProxyGroup &proxy_group : raster_proxy_groups_) {
    wire_backend_.update_clip_plane(proxy_group.proxies,
                                    clip_planes.near_plane);
  }
}

void SceneWireframeOverlayCache::update_instance_edit_style(
    const InstanceEditStyle &style) {
  for (RasterProxyGroup &proxy_group : raster_proxy_groups_) {
    wire_backend_.update_instance_edit_style(proxy_group.proxies, style,
                                             true);
  }
}

void SceneWireframeOverlayCache::set_all_layer_masks(
    std::uint8_t layer_mask) {
  if (layer_mask == 0u && layer_masks_forced_hidden_) {
    return;
  }
  for (const RasterProxyGroup &proxy_group : raster_proxy_groups_) {
    wire_backend_.set_layer_mask(proxy_group.proxies, layer_mask);
  }
  layer_masks_forced_hidden_ = layer_mask == 0u;
}

} // namespace quader
