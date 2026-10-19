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
