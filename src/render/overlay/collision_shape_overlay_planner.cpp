////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/collision_shape_overlay_planner.hpp"

#include "assets/mesh_collision_geometry_payload.hpp"
#include "render/render_layers.hpp"
#include "render/render_signature_builder.hpp"

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

} // namespace quader
