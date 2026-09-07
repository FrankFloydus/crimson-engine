////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/collision_shape_overlay_cache.hpp"

#include "assets/mesh_collision_geometry_payload.hpp"
#include "diagnostics/profile.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <filament/Engine.h>

#include <algorithm>
#include <array>
#include <limits>
#include <set>
#include <utility>

namespace quader {
namespace {

constexpr std::uint64_t kCollisionFillMaterialPayload =
    0x636f6c6c66696c6cULL;

bool same_color(const ViewportSettingColor &left,
                const ViewportSettingColor &right) {
  return left.r == right.r && left.g == right.g && left.b == right.b &&
         left.a == right.a;
}

std::uint64_t occurrence_token(const RenderOverlayCollisionMeshSnapshot &mesh) {
  return RenderSignatureBuilder{}
      .add_value(mesh.entity)
      .add_value(mesh.occurrence)
      .add_value(mesh.definition)
      .add_value(mesh.member)
      .value();
}

} // namespace

ViewportSettingColor collision_shape_fill_color(ViewportSettingColor color) {
  color.a *= 0.0625f;
  return color;
}

ViewportSettingColor collision_shape_wire_color(ViewportSettingColor color,
                                               bool selected) {
  color.a *= selected ? 1.6f : 0.4f;
  return color;
}

std::shared_ptr<const MeshWireGeometryPayload>
build_collision_shape_wire_geometry(const MeshCollisionGeometryPayload &geometry) {
  auto result = std::make_shared<MeshWireGeometryPayload>();
  result->content_id = geometry.content_id;
  if (!geometry.error_message.empty() || geometry.faces.size() % 3u != 0u) {
    return result;
  }
  std::set<std::array<float, 6>> edges;
  for (std::size_t index = 0u; index < geometry.faces.size(); index += 3u) {
    for (std::size_t edge = 0u; edge < 3u; ++edge) {
      QVec3 start = geometry.faces[index + edge];
      QVec3 end = geometry.faces[index + (edge + 1u) % 3u];
      if (std::array{end.x, end.y, end.z} < std::array{start.x, start.y, start.z}) {
        std::swap(start, end);
      }
      if (edges.insert({start.x, start.y, start.z, end.x, end.y, end.z}).second) {
        result->segments.push_back({.start = start, .end = end});
      }
    }
  }
  return result;
}

CollisionShapeOverlayUpdate CollisionShapeOverlayPlanner::prepare(
    const RenderOverlayCollisionSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  const std::size_t instance_limit =
      std::max<std::size_t>(1u, max_instances_per_chunk);
  CollisionShapeOverlayUpdate update{
      .color_changed = !has_synced_ || !same_color(color_, snapshot.color),
      .visibility_changed = !has_synced_ || visible_ != snapshot.visible,
      .source_changed =
          !has_synced_ ||
          !render_source_dependencies_match(
              synced_source_stamp_, snapshot,
              kRenderOverlayCollisionSourceDependencies),
  };
  const bool selection_changed = !has_synced_ ||
      synced_source_stamp_.selection_revision != snapshot.selection_revision;
  color_ = snapshot.color;
  // Color, camera, and selection changes must stop before mesh enumeration.
  if (has_synced_ && !update.source_changed && !update.visibility_changed &&
      plan_.max_instances_per_chunk == instance_limit) {
    if (snapshot.visible && selection_changed) {
      update.wire_batches = prepare_wire_plan(snapshot, instance_limit);
    }
    synced_source_stamp_ = snapshot;
    return update;
  }
  synced_source_stamp_ = snapshot;
  visible_ = snapshot.visible;
  has_synced_ = true;

  if (!snapshot.visible) {
    // Global overlay visibility changes placements only. Hidden snapshots
    // intentionally omit meshes; retain the last visible plan until shown.
    return update;
  }

  ++stats_.prepare_count;
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(snapshot.meshes.size());
  PayloadTokens next_payload_tokens;
  next_payload_tokens.reserve(snapshot.meshes.size());
  for (const RenderOverlayCollisionMeshSnapshot &mesh : snapshot.meshes) {
    ++stats_.source_record_visit_count;
    const auto &payload = mesh.collision_payload;
    if (payload == nullptr || !payload->error_message.empty() ||
        payload->faces.empty() || payload->faces.size() % 3u != 0u ||
        payload->faces.size() > std::numeric_limits<std::uint32_t>::max()) {
      continue;
    }
    const std::uint64_t token = retain_geometry(payload);
    next_payload_tokens.try_emplace(payload, token);
    const GeometryRecord &record = geometry_.at(token);
    occurrences.push_back({
        .key = {
            .geometry_payload = token,
            .material_payload = kCollisionFillMaterialPayload,
            .index_count = static_cast<std::uint32_t>(payload->faces.size()),
            .layer_mask = kViewportOverlayLayerMask,
            .channel = kViewportDefaultRenderableChannel,
            .double_sided = false,
            .editor_unlit = true,
        },
        .occurrence = occurrence_token(mesh),
        .world_transform = mesh.world_transform,
        .local_bounds = record.local_bounds,
    });
  }
  RenderInstanceBatchPlan next =
      plan_render_instance_batches(occurrences, instance_limit, &plan_);
  update.batches = classify_instance_batch_update(plan_, next);
  plan_ = std::move(next);
  payload_tokens_.swap(next_payload_tokens);
  prune_geometry();
  update.wire_batches = prepare_wire_plan(snapshot, instance_limit);
  return update;
}

RenderInstanceBatchUpdateKind CollisionShapeOverlayPlanner::prepare_wire_plan(
    const RenderOverlayCollisionSnapshot &snapshot,
    std::size_t max_instances_per_chunk) {
  ++stats_.wire_prepare_count;
  std::vector<RenderInstancePrimitiveOccurrence> occurrences;
  occurrences.reserve(snapshot.meshes.size());
  for (const RenderOverlayCollisionMeshSnapshot &mesh : snapshot.meshes) {
    const auto found = payload_tokens_.find(mesh.collision_payload);
    if (found == payload_tokens_.end()) {
      continue;
    }
    const GeometryRecord &geometry = geometry_.at(found->second);
    if (geometry.wires->segments.empty()) {
      continue;
    }
    occurrences.push_back({
        .key = {
            .geometry_payload = found->second,
            .material_payload = mesh.selected ? 1u : 0u,
            .index_count = static_cast<std::uint32_t>(geometry.wires->segments.size() * 6u),
            .layer_mask = kViewportOverlayLayerMask,
            .channel = kViewportDefaultRenderableChannel,
            .double_sided = true,
            .editor_unlit = true,
        },
        .occurrence = occurrence_token(mesh),
        .world_transform = mesh.world_transform,
        .local_bounds = geometry.local_bounds,
    });
  }
  auto next = plan_render_instance_batches(occurrences, max_instances_per_chunk,
                                           &wire_plan_);
  const auto update = classify_instance_batch_update(wire_plan_, next);
  wire_plan_ = std::move(next);
  return update;
}

std::uint64_t CollisionShapeOverlayPlanner::retain_geometry(
    const std::shared_ptr<const MeshCollisionGeometryPayload> &payload) {
  if (const auto known = payload_tokens_.find(payload);
      known != payload_tokens_.end()) {
    GeometryRecord &record = geometry_.at(known->second);
    record.last_seen_preparation = stats_.prepare_count;
    ++stats_.local_bounds_cache_hit_count;
    return known->second;
  }
  std::vector<std::uint64_t> &candidates = candidates_[payload->content_id];
  for (const std::uint64_t token : candidates) {
    GeometryRecord &record = geometry_.at(token);
    if (record.payload->faces.size() != payload->faces.size()) {
      continue;
    }
    if (!std::equal(record.payload->faces.begin(), record.payload->faces.end(),
                    payload->faces.begin(), [this](QVec3 left, QVec3 right) {
                      ++stats_.geometry_comparison_vertex_visit_count;
                      return left.x == right.x && left.y == right.y &&
                             left.z == right.z;
                    })) {
      continue;
    }
    record.last_seen_preparation = stats_.prepare_count;
    payload_tokens_.try_emplace(payload, token);
    ++stats_.local_bounds_cache_hit_count;
    return token;
  }
  const std::uint64_t token = next_geometry_token_++;
  geometry_.emplace(token, GeometryRecord{
                               .payload = payload,
                               .local_bounds =
                                   render_instance_local_bounds(payload->faces),
                               .wires = build_collision_shape_wire_geometry(*payload),
                               .last_seen_preparation = stats_.prepare_count,
                           });
  stats_.local_bounds_vertex_visit_count += payload->faces.size();
  ++stats_.wire_geometry_build_count;
  candidates.push_back(token);
  payload_tokens_.try_emplace(payload, token);
  return token;
}

void CollisionShapeOverlayPlanner::prune_geometry() {
  std::erase_if(geometry_, [this](const auto &entry) {
    return entry.second.last_seen_preparation != stats_.prepare_count;
  });
  for (auto &[content_id, candidates] : candidates_) {
    (void)content_id;
    std::erase_if(candidates, [this](std::uint64_t token) {
      return !geometry_.contains(token);
    });
  }
  std::erase_if(candidates_, [](const auto &entry) {
    return entry.second.empty();
  });
}

void CollisionShapeOverlayPlanner::clear() {
  synced_source_stamp_ = {};
  color_ = {};
  visible_ = false;
  has_synced_ = false;
  plan_ = {};
  wire_plan_ = {};
  next_geometry_token_ = 1u;
  geometry_.clear();
  candidates_.clear();
  payload_tokens_.clear();
  stats_ = {};
}

std::shared_ptr<const MeshCollisionGeometryPayload>
CollisionShapeOverlayPlanner::geometry(std::uint64_t batch_key) const {
  const auto found = geometry_.find(batch_key);
  return found != geometry_.end() ? found->second.payload : nullptr;
}

std::shared_ptr<const MeshWireGeometryPayload>
CollisionShapeOverlayPlanner::wire_geometry(std::uint64_t batch_key) const {
  const auto found = geometry_.find(batch_key);
  return found != geometry_.end() ? found->second.wires : nullptr;
}

CollisionShapeOverlayCache::~CollisionShapeOverlayCache() { shutdown(); }

void CollisionShapeOverlayCache::initialize(
    filament::Engine &engine, filament::Scene &scene,
    MeshRenderableFactory &mesh_factory, filament::Material &fill_material,
    filament::Material &line_material,
    std::size_t max_instances_per_chunk) {
  shutdown();
  mesh_factory_ = &mesh_factory;
  fill_material_ = &fill_material;
  max_instances_per_chunk_ = std::max<std::size_t>(
      1u, std::min<std::size_t>(max_instances_per_chunk,
                                engine.getMaxAutomaticInstances()));
  wire_backend_.initialize(engine, scene, line_material, max_instances_per_chunk_);
}

void CollisionShapeOverlayCache::shutdown() {
  clear();
  wire_backend_.shutdown();
  mesh_factory_ = nullptr;
  fill_material_ = nullptr;
  max_instances_per_chunk_ = 1u;
  stats_ = {};
}

void CollisionShapeOverlayCache::clear() {
  destroy_proxies();
  destroy_wires();
  wire_backend_.destroy_geometry_resources();
  planner_.clear();
  stats_.fill_chunk_count = 0u;
  stats_.member_count = 0u;
  stats_.wire_chunk_count = 0u;
}

bool CollisionShapeOverlayCache::sync(
    const RenderOverlayCollisionSnapshot &snapshot,
    const RenderOverlaySelectedObjectSnapshot &presentation) {
  QDR_PROFILE_FUNCTION();
  if (!initialized()) {
    return false;
  }
  const CollisionShapeOverlayUpdate update =
      planner_.prepare(snapshot, max_instances_per_chunk_);
  const ViewportSettingColor fill_color = collision_shape_fill_color(snapshot.color);
  if (update.batches == RenderInstanceBatchUpdateKind::Rebuild) {
    if (!rebuild(fill_color)) {
      clear();
      return false;
    }
  }
  if (update.batches == RenderInstanceBatchUpdateKind::TransformsOnly) {
    if (!update_transforms()) {
      if (!rebuild(fill_color)) {
        clear();
        return false;
      }
    }
    ++stats_.transform_update_count;
  }
  if (update.color_changed && !proxies_.empty()) {
    for (const MeshRenderableProxy &proxy : proxies_) {
      mesh_factory_->update_collision_shape_fill_color(proxy, fill_color);
    }
    ++stats_.color_update_count;
  }
  if (update.visibility_changed) {
    for (const MeshRenderableProxy &proxy : proxies_) {
      mesh_factory_->update_layer_mask(
          proxy, snapshot.visible ? kViewportOverlayLayerMask : 0u);
    }
  }
  const bool wires_changed = sync_wires(snapshot, presentation, update);
  return update.batches != RenderInstanceBatchUpdateKind::Unchanged ||
         update.color_changed || update.visibility_changed ||
         update.source_changed || wires_changed;
}

bool CollisionShapeOverlayCache::rebuild(const ViewportSettingColor &color) {
  std::vector<MeshRenderableProxy> next;
  next.reserve(planner_.plan().chunks.size());
  for (const RenderInstanceBatchChunk &chunk : planner_.plan().chunks) {
    MeshRenderableProxy proxy = mesh_factory_->create_collision_shape_fill(
        planner_.geometry(chunk.key.geometry_payload), *fill_material_,
        chunk.world_transforms, chunk.world_bounds, color,
        static_cast<std::uint16_t>(
            viewport_overlay_draw_order(ViewportOverlayLayer::CollisionFill) *
            64u));
    if (!proxy.entity || proxy.instance_buffer == nullptr) {
      mesh_factory_->destroy(proxy);
      for (MeshRenderableProxy &created : next) {
        mesh_factory_->destroy(created);
      }
      return false;
    }
    next.push_back(std::move(proxy));
  }
  destroy_proxies();
  proxies_.swap(next);
  stats_.fill_chunk_count = proxies_.size();
  stats_.member_count = planner_.plan().occurrence_count;
  ++stats_.rebuild_count;
  return true;
}

bool CollisionShapeOverlayCache::update_transforms() {
  if (proxies_.size() != planner_.plan().chunks.size()) {
    return false;
  }
  for (std::size_t index = 0u; index < proxies_.size(); ++index) {
    const RenderInstanceBatchChunk &chunk = planner_.plan().chunks[index];
    if (!mesh_factory_->update_explicit_instances(
            proxies_[index], chunk.world_transforms, chunk.world_bounds)) {
      return false;
    }
  }
  return true;
}

bool CollisionShapeOverlayCache::sync_wires(
    const RenderOverlayCollisionSnapshot &snapshot,
    const RenderOverlaySelectedObjectSnapshot &presentation,
    const CollisionShapeOverlayUpdate &update) {
  if (!snapshot.visible) {
    if (update.visibility_changed) {
      for (const WireRasterGroup &group : wire_groups_) {
        wire_backend_.set_layer_mask(group.proxies, 0u);
      }
    }
    return update.visibility_changed;
  }
  ViewportPersistentOverlayRasterGroupSet raster_groups = presentation.raster_groups;
  if (raster_groups.empty()) {
    (void)raster_groups.add(kViewportOverlayLayerMask, presentation.style);
  }
  const auto targets = raster_groups.groups();
  const auto &chunks = planner_.wire_plan().chunks;
  const float near_plane = presentation.clip_planes.near_plane;
  const bool rebuild = update.wire_batches == RenderInstanceBatchUpdateKind::Rebuild ||
      wire_groups_.size() != targets.size() ||
      std::ranges::any_of(wire_groups_, [&](const WireRasterGroup &group) {
        return group.proxies.size() != chunks.size();
      });
  bool raster_changed = wire_groups_.size() != targets.size();
  if (!raster_changed) {
    for (std::size_t index = 0u; index < targets.size(); ++index) {
      raster_changed = raster_changed ||
          wire_groups_[index].layer_mask != targets[index].layer_mask ||
          wire_groups_[index].raster_scale != targets[index].style.raster_scale;
    }
  }
  if (!rebuild && !raster_changed && wire_near_plane_ == near_plane &&
      !update.visibility_changed && !update.color_changed &&
      update.wire_batches == RenderInstanceBatchUpdateKind::Unchanged) {
    return false;
  }
  if (rebuild) {
    std::vector<WireRasterGroup> created;
    created.reserve(targets.size());
    for (const auto &target : targets) {
      WireRasterGroup group;
      group.layer_mask = target.layer_mask;
      group.raster_scale = target.style.raster_scale;
      group.proxies.reserve(chunks.size());
      for (const RenderInstanceBatchChunk &chunk : chunks) {
        const auto geometry = planner_.wire_geometry(chunk.key.geometry_payload);
        const InstancedWireGeometryInput input{
            .signature = chunk.key.geometry_payload,
            .wire_geometry = geometry,
            .color = rgba8(255, 255, 255, 255),
            .segments = geometry->segments,
        };
        const InstancedWireBatchStyle style{
            .render_state = OverlayRenderableBuilder::make_state(
                ViewportOverlayLayer::CollisionFill,
                ViewportOverlayGeometryKind::Lines,
                static_cast<std::uint16_t>(viewport_overlay_draw_order(
                    ViewportOverlayLayer::CollisionFill) * 64u + 1u)),
            .line_width_pixels = target.style.raster_scale,
            .near_plane = near_plane,
            .layer_mask = target.layer_mask,
            .channel = kViewportDefaultRenderableChannel,
            .line_tint = collision_shape_wire_color(
                snapshot.color, chunk.key.material_payload != 0u),
        };
        auto proxy = wire_backend_.create_batch(input, chunk, style);
        if (!wire_backend_.valid(proxy)) {
          wire_backend_.destroy_batch(proxy);
          wire_backend_.destroy_batches(group.proxies);
          for (auto &prior : created) {
            wire_backend_.destroy_batches(prior.proxies);
          }
          prune_wire_resources();
          return false;
        }
        group.proxies.push_back(std::move(proxy));
      }
      created.push_back(std::move(group));
    }
    destroy_wires();
    wire_groups_.swap(created);
    wire_near_plane_ = near_plane;
    stats_.wire_chunk_count = chunks.size() * wire_groups_.size();
    prune_wire_resources();
    return true;
  }
  bool changed = false;
  for (std::size_t group_index = 0u; group_index < wire_groups_.size(); ++group_index) {
    WireRasterGroup &group = wire_groups_[group_index];
    const auto &target = targets[group_index];
    for (std::size_t chunk_index = 0u; chunk_index < chunks.size(); ++chunk_index) {
      auto &proxy = group.proxies[chunk_index];
      if (update.wire_batches == RenderInstanceBatchUpdateKind::TransformsOnly) {
        changed = wire_backend_.update_batch(proxy, chunks[chunk_index]) || changed;
      }
      if (update.color_changed) {
        wire_backend_.update_line_tint(
            std::span<InstancedWireRenderBackend::BatchProxy>(&proxy, 1u),
            collision_shape_wire_color(snapshot.color,
                chunks[chunk_index].key.material_payload != 0u));
        changed = true;
      }
      if (group.raster_scale != target.style.raster_scale) {
        changed = wire_backend_.update_line_width(proxy, target.style.raster_scale) || changed;
      }
    }
    if (group.layer_mask != target.layer_mask || update.visibility_changed) {
      wire_backend_.set_layer_mask(group.proxies, target.layer_mask);
      changed = true;
    }
    if (wire_near_plane_ != near_plane) {
      wire_backend_.update_clip_plane(group.proxies, near_plane);
      changed = true;
    }
    group.layer_mask = target.layer_mask;
    group.raster_scale = target.style.raster_scale;
  }
  wire_near_plane_ = near_plane;
  return changed;
}

void CollisionShapeOverlayCache::destroy_wires() {
  for (WireRasterGroup &group : wire_groups_) {
    wire_backend_.destroy_batches(group.proxies);
  }
  wire_groups_.clear();
}

void CollisionShapeOverlayCache::prune_wire_resources() {
  std::vector<std::span<const InstancedWireRenderBackend::BatchProxy>> groups;
  groups.reserve(wire_groups_.size());
  for (const WireRasterGroup &group : wire_groups_) {
    groups.emplace_back(group.proxies);
  }
  wire_backend_.prune_unused_geometry_resources(groups);
}

void CollisionShapeOverlayCache::destroy_proxies() {
  if (mesh_factory_ != nullptr) {
    for (MeshRenderableProxy &proxy : proxies_) {
      mesh_factory_->destroy(proxy);
    }
  }
  proxies_.clear();
}

} // namespace quader
