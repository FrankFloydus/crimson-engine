////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/render_scene_snapshot.hpp"
#include "render/render_signature_builder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

std::size_t overlay_layer_index(ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(layer);
}

std::vector<OverlayFloat4> &position_stream(
    OverlayRenderableProxy &proxy, OverlayPositionStream stream) {
  switch (stream) {
  case OverlayPositionStream::Secondary: return proxy.secondary_position_stream4;
  case OverlayPositionStream::Support0: return proxy.support0_position_stream4;
  case OverlayPositionStream::Support1: return proxy.support1_position_stream4;
  case OverlayPositionStream::Primary: return proxy.primary_position_stream4;
  }
  return proxy.primary_position_stream4;
}

bool overlay_camera_material_state_changed(
    const ViewportOverlayCamera &before,
    const ViewportOverlayCamera &after) {
  return before.projection != after.projection ||
         before.near_plane != after.near_plane ||
         before.far_plane != after.far_plane;
}

bool component_point_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::UnselectedVertex ||
         layer == ViewportOverlayLayer::SelectedVertex;
}

bool component_points_support_retention(const ViewportOverlayFrame &frame) {
  struct PointClassStyle {
    bool present = false;
    float size = 0.0f;
    float outline_growth = 0.0f;
  };
  std::array<PointClassStyle, 2u> classes;
  bool has_points = false;
  for (const auto &point : frame.points) {
    if (!component_point_layer(point.layer)) continue;
    has_points = true;
    const float size =
        viewport_overlay_point_size_pixels(point.layer, point.size_pixels);
    const float outline_growth = point.outline_growth_pixels.value_or(
        viewport_overlay_point_outline_growth_pixels(point.layer));
    if (!std::isfinite(size) || !std::isfinite(outline_growth)) return false;
    auto &style = classes[point.layer == ViewportOverlayLayer::SelectedVertex
                              ? 1u : 0u];
    // The old builder orders heterogeneous size/depth batches separately.
    // Keep that path when a class cannot preserve its exact pass order.
    if ((style.present && (style.size != size ||
                           style.outline_growth != outline_growth)) ||
        viewport_overlay_depth_mode(point) !=
            viewport_overlay_depth_mode(point.layer,
                                        ViewportOverlayGeometryKind::Points)) {
      return false;
    }
    style = {true, size, outline_growth};
  }
  return has_points;
}

bool component_point_geometry_matches(const ViewportOverlayFrame &before,
                                     const ViewportOverlayFrame &after) {
  auto old_point = before.points.begin();
  for (const auto &point : after.points) {
    if (!component_point_layer(point.layer)) continue;
    while (old_point != before.points.end() &&
           !component_point_layer(old_point->layer)) ++old_point;
    if (old_point == before.points.end()) return false;
    const auto &left = old_point->element;
    const auto &right = point.element;
    if (old_point->position.x != point.position.x ||
        old_point->position.y != point.position.y ||
        old_point->position.z != point.position.z ||
        old_point->source != point.source ||
        left.object_id != right.object_id || left.vertex_id != right.vertex_id ||
        left.edge_a != right.edge_a || left.edge_b != right.edge_b ||
        left.face_id != right.face_id) return false;
    ++old_point;
  }
  while (old_point != before.points.end() &&
         !component_point_layer(old_point->layer)) ++old_point;
  return old_point == before.points.end();
}

std::uint64_t component_point_build_signature(
    const OverlayFrameSignatures &signatures, std::uint64_t point_order) {
  return RenderSignatureBuilder()
      .add_value(signatures.layer_signatures[
          overlay_layer_index(ViewportOverlayLayer::UnselectedVertex)])
      .add_value(signatures.layer_signatures[
          overlay_layer_index(ViewportOverlayLayer::SelectedVertex)])
      .add_value(point_order)
      .value();
}

std::uint64_t point_frame_order_signature(const ViewportOverlayFrame &frame) {
  RenderSignatureBuilder signature;
  signature.add_size(frame.points.size());
  for (const auto &point : frame.points) {
    // Selected/unselected markers share one retained family and frame mapping.
    const auto layer = component_point_layer(point.layer)
                           ? ViewportOverlayLayer::UnselectedVertex
                           : point.layer;
    signature.add_value(static_cast<std::uint64_t>(layer));
  }
  return signature.value();
}

} // namespace

OverlayRenderableCache::~OverlayRenderableCache() {
    shutdown();
}

void OverlayRenderableCache::initialize(OverlayProxyFactory &factory,
                                        std::uint16_t blend_order_bias) {
    factory_ = &factory;
    blend_order_bias_ = blend_order_bias;
}

void OverlayRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    for (OverlaySlot& slot : slots_) {
      destroy_slot(slot);
    }
    transform_sources_.clear();
    transform_source_offsets_.clear();
    shared_transform_positions_.clear();
    shared_transform_positions_ready_ = false;
    transform_layout_signature_ = 0u;
    transform_position_signature_ = 0u;
    transform_stream_active_ = false;
    transform_coordinates_accepted_ = false;
    blend_order_bias_ = 0u;
    factory_ = nullptr;
}

bool OverlayRenderableCache::set_overlay_frame(ViewportOverlayFrame frame,
                                               ViewportOverlayCamera camera) {
  return set_overlay_frame(0u, kViewportOverlayLayerMask, std::move(frame),
                           camera);
}

bool OverlayRenderableCache::set_overlay_frame(std::size_t viewport_index,
                                               std::uint8_t layer_mask,
                                               ViewportOverlayFrame frame,
                                               ViewportOverlayCamera camera) {
  QDR_PROFILE_FUNCTION();
  if (viewport_index >= slots_.size()) {
    return false;
  }

  if (layer_mask == 0u) {
    layer_mask = kViewportOverlayLayerMask;
  }

  OverlaySlot &slot = slots_[viewport_index];
  const OverlayFrameSignatures signatures =
      OverlayRenderableBuilder::frame_signatures(frame);
  const std::uint64_t signature = signatures.frame_signature;
  camera = OverlayCameraProjector::sanitized_camera(camera);
  const std::uint64_t camera_signature =
      OverlayRenderableBuilder::camera_signature(camera);
  const std::uint64_t retained_frame_signature =
      slot.transform_active ? slot.transform_base_frame_signature
                            : slot.frame_signature;
  const bool frame_changed =
      !slot.has_frame || signature != retained_frame_signature;
  const bool camera_changed = camera_signature != slot.camera_signature;
  const bool camera_material_state_changed =
      overlay_camera_material_state_changed(slot.camera, camera);
  const bool layer_changed = layer_mask != slot.layer_mask;
  if (!frame_changed) {
    if (!camera_changed && !layer_changed) {
      return false;
    }
    slot.camera = camera;
    slot.camera_signature = camera_signature;
    slot.layer_mask = layer_mask;
    if (layer_changed) {
      for (OverlayProxyGroup &group : slot.proxy_groups) {
        group.signature = 0;
      }
    }
    if (factory_ && factory_->initialized()) {
      rebuild_slot(slot, layer_changed);
      if (camera_material_state_changed && !layer_changed) {
        update_retained_camera_state(slot);
      }
    }
    return true;
  }

  slot.component_point_geometry_retained =
      slot.component_point_family_active &&
      signatures.component_point_geometry_signature ==
          slot.signatures.component_point_geometry_signature &&
      component_point_geometry_matches(slot.frame, frame);
  bool retain_transform_bindings =
      slot.transform_active && slot.component_point_geometry_retained &&
      component_points_support_retention(frame) && !layer_changed;
  for (std::size_t index = 0u;
       retain_transform_bindings && index < kViewportOverlayLayerCount; ++index) {
    if (!component_point_layer(static_cast<ViewportOverlayLayer>(index)) &&
        signatures.layer_signatures[index] != slot.signatures.layer_signatures[index]) {
      retain_transform_bindings = false;
    }
  }
  const bool resume_transform = transform_stream_active_;
  if (slot.transform_active && !retain_transform_bindings) {
    restore_transform_base(slot, false);
  }

  const auto point_order = point_frame_order_signature(frame);
  if (point_order != slot.point_order_signature) {
    // Interleaved point layers share frame indices. Refresh their mappings
    // after an insertion even when a layer's own geometry remains unchanged.
    for (std::size_t index = 0u; index < slot.proxy_groups.size(); ++index) {
      if (slot.signatures.point_counts[index] != 0u || signatures.point_counts[index] != 0u) {
        slot.proxy_groups[index].signature = 0u;
      }
    }
  }
  slot.point_order_signature = point_order;
  slot.frame = std::move(frame);
  slot.accepted_visibility_valid = false;
  // An unrelated frame layer can change while point proxies remain retained.
  // Keep their viewport-skip proof until strict point reclassification occurs.
  if (!signatures.has_source_wire_depth_stamp) {
    slot.point_visibility.invalidate();
  }
  slot.has_frame = true;
  slot.frame_signature = signature;
  slot.signatures = signatures;
  if (!retain_transform_bindings) {
    slot.transform_layers.fill(false);
    slot.transform_binding_ranges.clear();
    slot.transform_source_binding_spans.clear();
    slot.transform_layout_signature = 0u;
    slot.transform_bindings_valid = false;
  }
  slot.camera = camera;
  slot.camera_signature = camera_signature;
  slot.layer_mask = layer_mask;
  if (layer_changed) {
    for (OverlayProxyGroup &group : slot.proxy_groups) {
      group.signature = 0;
    }
  }

  if (retain_transform_bindings) {
    slot.transform_base_signatures = signatures;
    slot.transform_base_frame_signature = signature;
    if (factory_ && factory_->initialized()) rebuild_slot(slot, true);
    slot.frame_signature = RenderSignatureBuilder(signature)
                               .add_value(slot.transform_position_signature)
                               .value();
    slot.signatures.frame_signature = slot.frame_signature;
  } else if (resume_transform) {
    if (factory_ && factory_->initialized()) {
      rebuild_slot(slot, true);
    }
    begin_transform_stream(slot, transform_layout_signature_);
    apply_transform_stream(slot, transform_position_signature_);
  } else if (factory_ && factory_->initialized()) {
    rebuild_slot(slot, true);
  }
  if (camera_material_state_changed) {
    // Unrelated semantic edits may keep point geometry while changing its clip
    // plane. Retained batches still need the current material camera state.
    update_retained_camera_state(slot);
  }
  return true;
}

bool OverlayRenderableCache::update_overlay_camera(
    std::size_t viewport_index, std::uint8_t layer_mask,
    ViewportOverlayCamera camera) {
  QDR_PROFILE_FUNCTION();
  if (viewport_index >= slots_.size()) {
    return false;
  }

  OverlaySlot &slot = slots_[viewport_index];
  if (!slot.has_frame) {
    return false;
  }
  if (layer_mask == 0u) {
    layer_mask = kViewportOverlayLayerMask;
  }

  camera = OverlayCameraProjector::sanitized_camera(camera);
  const std::uint64_t camera_signature =
      OverlayRenderableBuilder::camera_signature(camera);
  const bool camera_changed = camera_signature != slot.camera_signature;
  const bool camera_material_state_changed =
      overlay_camera_material_state_changed(slot.camera, camera);
  const bool layer_changed = layer_mask != slot.layer_mask;
  if (!camera_changed && !layer_changed) {
    return true;
  }

  slot.camera = camera;
  slot.camera_signature = camera_signature;
  slot.layer_mask = layer_mask;
  if (layer_changed) {
    for (OverlayProxyGroup &group : slot.proxy_groups) {
      group.signature = 0;
    }
  }
  if (factory_ && factory_->initialized()) {
    rebuild_slot(slot, layer_changed);
    if (camera_material_state_changed && !layer_changed) {
      update_retained_camera_state(slot);
    }
  }
  return true;
}

bool OverlayRenderableCache::set_transform_position_streams(
    const std::vector<RenderToolPreviewMeshRecord> &meshes,
    bool accepted_coordinates) {
  QDR_PROFILE_FUNCTION();
  const bool acceptance_changed =
      transform_coordinates_accepted_ != accepted_coordinates;
  transform_coordinates_accepted_ = accepted_coordinates;
  const auto is_dynamic_source = [](const RenderToolPreviewMeshRecord &mesh) {
    return mesh.dynamic_positions && mesh.source_object_id >= 0 &&
           mesh.layout_key != 0u && mesh.dynamic_position_payload &&
           mesh.dynamic_position_payload->size() ==
               mesh.mesh.geometry().vertices.size();
  };
  const auto refresh_world_positions = [](TransformPositionSource &source) {
    source.world_positions.resize(source.position_count);
    if (!source.positions) {
      return;
    }
    for (std::size_t index = 0u; index < source.position_count; ++index) {
      source.world_positions[index] =
          transform_point(source.transform, (*source.positions)[index]);
    }
  };

  RenderSignatureBuilder layout_signature;
  RenderSignatureBuilder position_signature;
  std::size_t dynamic_source_count = 0u;
  bool stable_layout = true;
  {
    QDR_PROFILE_SCOPE("Overlay transform stream scan");
    for (const RenderToolPreviewMeshRecord &mesh : meshes) {
      if (!is_dynamic_source(mesh)) {
        continue;
      }
      if (dynamic_source_count >= transform_sources_.size()) {
        stable_layout = false;
      } else {
        const TransformPositionSource &source =
            transform_sources_[dynamic_source_count];
        stable_layout =
            stable_layout && source.object_id == mesh.source_object_id &&
            source.layout_key == mesh.layout_key &&
            source.position_count == mesh.dynamic_position_payload->size();
      }
      ++dynamic_source_count;
      layout_signature
          .add_value(static_cast<std::uint64_t>(mesh.source_object_id))
          .add_value(mesh.layout_key)
          .add_size(mesh.dynamic_position_payload->size());
      position_signature
          .add_value(static_cast<std::uint64_t>(mesh.source_object_id))
          .add_value(mesh.layout_key)
          .add_value(mesh.position_revision)
          .add_mat4(mesh.transform);
    }
  }
  stable_layout =
      stable_layout && dynamic_source_count == transform_sources_.size();
  layout_signature.add_size(dynamic_source_count);
  position_signature.add_size(dynamic_source_count);
  if (dynamic_source_count == 0u) {
    if (!transform_stream_active_) {
      return false;
    }
    QDR_PROFILE_SCOPE("Overlay transform stream retain topology");
    transform_stream_active_ = false;
    transform_position_signature_ = 0u;
    for (TransformPositionSource &source : transform_sources_) {
      source.position_revision = 0u;
      source.positions.reset();
    }
    for (OverlaySlot &slot : slots_) {
      restore_transform_base(slot, true);
    }
    return true;
  }

  const std::uint64_t next_layout = layout_signature.value();
  const std::uint64_t next_position = position_signature.value();
  const bool layout_changed =
      next_layout != transform_layout_signature_ || !stable_layout;
  std::vector<std::uint8_t> dirty_source_mask(
      dynamic_source_count, layout_changed ? 1u : 0u);
  bool any_source_dirty = layout_changed;
  if (!layout_changed) {
    QDR_PROFILE_SCOPE("Overlay transform stream refresh dirty sources");
    std::size_t source_index = 0u;
    for (const RenderToolPreviewMeshRecord &mesh : meshes) {
      if (!is_dynamic_source(mesh)) {
        continue;
      }
      TransformPositionSource &source = transform_sources_[source_index];
      const bool source_dirty =
          source.position_revision != mesh.position_revision ||
          source.positions != mesh.dynamic_position_payload ||
          source.transform.values != mesh.transform.values;
      if (!source_dirty) {
        ++source_index;
        continue;
      }
      dirty_source_mask[source_index] = 1u;
      any_source_dirty = true;
      source.position_revision = mesh.position_revision;
      source.position_count = mesh.dynamic_position_payload->size();
      source.transform = mesh.transform;
      source.positions = mesh.dynamic_position_payload;
      refresh_world_positions(source);
      ++source_index;
    }
  } else {
    QDR_PROFILE_SCOPE("Overlay transform stream rebuild source layout");
    std::unordered_map<int, std::size_t> previous_source_by_object;
    previous_source_by_object.reserve(transform_sources_.size());
    for (std::size_t index = 0u; index < transform_sources_.size(); ++index) {
      previous_source_by_object.try_emplace(transform_sources_[index].object_id,
                                            index);
    }

    std::vector<TransformPositionSource> next_sources;
    next_sources.reserve(dynamic_source_count);
    for (const RenderToolPreviewMeshRecord &mesh : meshes) {
      if (!is_dynamic_source(mesh)) {
        continue;
      }

      std::shared_ptr<const std::unordered_map<std::uint32_t, std::uint32_t>>
          vertex_lookup;
      const auto previous =
          previous_source_by_object.find(mesh.source_object_id);
      if (previous != previous_source_by_object.end()) {
        const TransformPositionSource &source =
            transform_sources_[previous->second];
        if (source.layout_key == mesh.layout_key &&
            source.position_count == mesh.dynamic_position_payload->size()) {
          vertex_lookup = source.source_vertex_to_position;
        }
      }
      if (!vertex_lookup) {
        auto lookup = std::make_shared<
            std::unordered_map<std::uint32_t, std::uint32_t>>();
        lookup->reserve(mesh.mesh.geometry().vertices.size());
        for (std::size_t index = 0u;
             index < mesh.mesh.geometry().vertices.size(); ++index) {
          lookup->try_emplace(
              mesh.mesh.geometry().vertices[index].source_vertex_id,
              static_cast<std::uint32_t>(index));
        }
        vertex_lookup = std::move(lookup);
      }
      next_sources.push_back({
          .object_id = mesh.source_object_id,
          .layout_key = mesh.layout_key,
          .position_revision = mesh.position_revision,
          .position_count = mesh.dynamic_position_payload->size(),
          .transform = mesh.transform,
          .positions = mesh.dynamic_position_payload,
          .source_vertex_to_position = std::move(vertex_lookup),
      });
      refresh_world_positions(next_sources.back());
    }
    transform_sources_ = std::move(next_sources);
  }

  // position_revision is participant-local. If every retained participant
  // kept its revision, payload and transform, no world-position work or GPU
  // upload is required even when the caller republishes the aggregate vector.
  if (!layout_changed && !any_source_dirty && transform_stream_active_) {
    transform_position_signature_ = next_position;
    if (acceptance_changed && accepted_coordinates) {
      for (auto &slot : slots_) {
        slot.accepted_visibility_valid = false;
        update_point_visibility(slot);
      }
    }
    return false;
  }
  transform_layout_signature_ = next_layout;
  transform_position_signature_ = next_position;
  transform_stream_active_ = true;
  if (layout_changed) {
    // Release old index bindings before a different layout can grow the texture.
    for (OverlaySlot &slot : slots_) restore_transform_base(slot, false);
  }
  const bool previously_shared = shared_transform_positions_ready_;
  shared_transform_positions_ready_ = false;
  if (factory_ && factory_->initialized()) {
    QDR_PROFILE_SCOPE("Overlay upload shared transform coordinates");
    constexpr std::size_t kMaximumSharedPositions = 4096u * 4096u;
    std::size_t position_count = 0u;
    for (const auto &source : transform_sources_) position_count += source.position_count;
    if (position_count <= kMaximumSharedPositions) {
      shared_transform_positions_.resize(position_count);
      transform_source_offsets_.resize(transform_sources_.size());
      std::size_t offset = 0u;
      for (std::size_t index = 0u; index < transform_sources_.size(); ++index) {
        transform_source_offsets_[index] = static_cast<std::uint32_t>(offset);
        const auto &source = transform_sources_[index];
        if (layout_changed || !previously_shared || dirty_source_mask[index] != 0u) {
          for (std::size_t vertex = 0u; vertex < source.world_positions.size(); ++vertex) {
            const auto &position = source.world_positions[vertex];
            shared_transform_positions_[offset + vertex] = {position.x, position.y, position.z};
          }
        }
        offset += source.position_count;
      }
      shared_transform_positions_ready_ = factory_->upload_shared_positions(shared_transform_positions_);
    }
    if (previously_shared && !shared_transform_positions_ready_) {
      for (auto &slot : slots_) {
        for (auto &group : slot.proxy_groups) {
          for (auto &proxy : group.proxies) (void)factory_->clear_shared_positions(proxy);
        }
      }
      std::fill(dirty_source_mask.begin(), dirty_source_mask.end(), 1u);
    } else if (!previously_shared && shared_transform_positions_ready_) {
      for (auto &slot : slots_) {
        restore_transform_base(slot, false);
        slot.transform_bindings_valid = false;
      }
    }
  }
  {
    QDR_PROFILE_SCOPE("Overlay transform stream apply slots");
    for (OverlaySlot &slot : slots_) {
      slot.accepted_visibility_valid = false;
      if (!slot.has_frame) {
        continue;
      }
      // Packed positions leave the semantic source frame untouched. Its owned
      // BVHs and strict flags remain valid until that source changes; cameras
      // classify the same base points before GPU position displacement.
      bool upload_all_sources = false;
      if (layout_changed) {
        if (slot.transform_active) {
          restore_transform_base(slot, false);
        }
        begin_transform_stream(slot, next_layout);
        upload_all_sources = true;
      } else if (!slot.transform_active) {
        begin_transform_stream(slot, next_layout);
        upload_all_sources = true;
      } else if (!slot.transform_bindings_valid) {
        resolve_transform_stream_bindings(slot);
        upload_all_sources = true;
      }
      apply_transform_stream(slot, next_position,
                             upload_all_sources ? nullptr
                                                : &dirty_source_mask);
      if (accepted_coordinates) update_point_visibility(slot);
    }
  }
  return true;
}

void OverlayRenderableCache::reset_transform_position_streams() {
  QDR_PROFILE_FUNCTION();
  transform_stream_active_ = false;
  transform_coordinates_accepted_ = false;
  transform_sources_.clear();
  transform_source_offsets_.clear();
  shared_transform_positions_.clear();
  shared_transform_positions_ready_ = false;
  transform_layout_signature_ = 0u;
  transform_position_signature_ = 0u;
  for (OverlaySlot &slot : slots_) {
    restore_transform_base(slot, true);
    slot.transform_layers.fill(false);
    slot.transform_binding_ranges.clear();
    slot.transform_source_binding_spans.clear();
    slot.transform_layout_signature = 0u;
    slot.transform_bindings_valid = false;
  }
}

void OverlayRenderableCache::begin_transform_stream(
    OverlaySlot &slot, std::uint64_t layout_signature) {
  QDR_PROFILE_FUNCTION();
  if (slot.point_viewport_skips_applied) {
    // A skipped offscreen ray may have retained an otherwise hidden point.
    // Restore the original source-face classification before position streams
    // can move that point onscreen. Further preview camera rebuilds keep the
    // viewport shortcut disabled until the transform stream ends.
    QDR_PROFILE_SCOPE("Overlay restore strict point visibility for transform");
    update_point_visibility(slot);
    slot.point_viewport_skips_applied = false;
  }
  slot.transform_base_signatures = slot.signatures;
  slot.transform_base_frame_signature = slot.frame_signature;
  slot.transform_active = true;

  if (slot.transform_bindings_valid &&
      slot.transform_layout_signature == layout_signature) {
    bind_shared_transform_positions(slot);
    return;
  }
  slot.transform_layout_signature = layout_signature;
  resolve_transform_stream_bindings(slot);
}

void OverlayRenderableCache::resolve_transform_stream_bindings(
    OverlaySlot &slot) {
  QDR_PROFILE_SCOPE("Overlay resolve retained GPU stream bindings");
  slot.transform_layers.fill(false);
  slot.transform_binding_ranges.clear();
  slot.transform_binding_ranges.reserve(transform_sources_.size() * 4u);
  slot.transform_source_binding_spans.clear();
  slot.transform_source_binding_spans.resize(transform_sources_.size());

  std::unordered_map<int, std::uint32_t> source_by_object;
  source_by_object.reserve(transform_sources_.size());
  for (std::size_t index = 0u; index < transform_sources_.size(); ++index) {
    source_by_object.emplace(transform_sources_[index].object_id,
                             static_cast<std::uint32_t>(index));
  }
  for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
    const std::size_t layer_index = overlay_layer_index(layer);
    OverlayProxyGroup &group = slot.proxy_groups[layer_index];
    for (std::size_t proxy_index = 0u; proxy_index < group.proxies.size();
         ++proxy_index) {
      OverlayRenderableProxy &proxy = group.proxies[proxy_index];
      for (std::size_t binding_index = 0u;
           binding_index < proxy.position_stream_bindings.size();
           ++binding_index) {
        OverlayPositionStreamBinding &binding =
            proxy.position_stream_bindings[binding_index];
        binding.source_index = std::numeric_limits<std::uint32_t>::max();
        binding.position_index = std::numeric_limits<std::uint32_t>::max();
        const auto source = source_by_object.find(binding.object_id);
        if (source == source_by_object.end() || binding.vertex_id < 0) {
          continue;
        }
        const auto &lookup =
            transform_sources_[source->second].source_vertex_to_position;
        if (!lookup) {
          continue;
        }
        const auto position =
            lookup->find(static_cast<std::uint32_t>(binding.vertex_id));
        if (position == lookup->end()) {
          continue;
        }
        binding.source_index = source->second;
        binding.position_index = position->second;
        slot.transform_layers[layer_index] = true;
        if (!slot.transform_binding_ranges.empty() &&
            slot.transform_binding_ranges.back().source_index ==
                source->second &&
            slot.transform_binding_ranges.back().layer_index == layer_index &&
            slot.transform_binding_ranges.back().proxy_index == proxy_index &&
            static_cast<std::uint64_t>(
                slot.transform_binding_ranges.back().first_binding) +
                    slot.transform_binding_ranges.back().binding_count ==
                binding_index) {
          ++slot.transform_binding_ranges.back().binding_count;
        } else {
          slot.transform_binding_ranges.push_back({
              .source_index = source->second,
              .layer_index = static_cast<std::uint32_t>(layer_index),
              .proxy_index = static_cast<std::uint32_t>(proxy_index),
              .first_binding = static_cast<std::uint32_t>(binding_index),
              .binding_count = 1u,
          });
        }
      }
    }
  }
  std::sort(slot.transform_binding_ranges.begin(),
            slot.transform_binding_ranges.end(),
            [](const TransformBindingRange &a,
               const TransformBindingRange &b) {
              if (a.source_index != b.source_index) {
                return a.source_index < b.source_index;
              }
              if (a.layer_index != b.layer_index) {
                return a.layer_index < b.layer_index;
              }
              if (a.proxy_index != b.proxy_index) {
                return a.proxy_index < b.proxy_index;
              }
              return a.first_binding < b.first_binding;
            });
  for (std::size_t range_index = 0u;
       range_index < slot.transform_binding_ranges.size(); ++range_index) {
    const TransformBindingRange &range =
        slot.transform_binding_ranges[range_index];
    if (range.source_index >= slot.transform_source_binding_spans.size()) {
      continue;
    }
    TransformSourceBindingSpan &span =
        slot.transform_source_binding_spans[range.source_index];
    if (span.range_count == 0u) {
      span.first_range = static_cast<std::uint32_t>(range_index);
    }
    ++span.range_count;
  }
  slot.transform_bindings_valid = true;
  bind_shared_transform_positions(slot);
}

bool retained_face_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedVisibleFace ||
         layer == ViewportOverlayLayer::SelectedXrayFace ||
         layer == ViewportOverlayLayer::SelectedFaceMask;
}

bool retained_selection_wire_layer(ViewportOverlayLayer layer) {
  return layer == ViewportOverlayLayer::SelectedFaceEdge ||
         layer == ViewportOverlayLayer::SelectedEdge;
}

void OverlayRenderableCache::bind_shared_transform_positions(OverlaySlot &slot) {
  if (!shared_transform_positions_ready_ || !factory_ || !factory_->initialized()) return;
  QDR_PROFILE_SCOPE("Overlay bind shared transform coordinates");
  constexpr auto kUnmapped = std::numeric_limits<std::uint32_t>::max();
  std::size_t shared_count = 0u;
  std::size_t fallback_count = 0u;
  for (std::size_t layer = 0u; layer < slot.proxy_groups.size(); ++layer) {
    if (!slot.transform_layers[layer]) continue;
    for (auto &proxy : slot.proxy_groups[layer].proxies) {
      if (!proxy.shared_positions_bound) {
        std::vector<std::uint32_t> primary(proxy.vertex_count, kUnmapped);
        std::vector<std::uint32_t> secondary(
            proxy.secondary_position_stream4.empty() ? 0u : proxy.vertex_count, kUnmapped);
        std::vector<std::uint32_t> support0(
            proxy.support0_position_stream4.empty() ? 0u : proxy.vertex_count, kUnmapped);
        std::vector<std::uint32_t> support1(
            proxy.support1_position_stream4.empty() ? 0u : proxy.vertex_count, kUnmapped);
        for (const auto &binding : proxy.position_stream_bindings) {
          if (binding.source_index >= transform_sources_.size() ||
              binding.position_index >= transform_sources_[binding.source_index].position_count) continue;
          auto &indices = binding.stream == OverlayPositionStream::Secondary ? secondary :
              binding.stream == OverlayPositionStream::Support0 ? support0 :
              binding.stream == OverlayPositionStream::Support1 ? support1 : primary;
          const std::size_t end = static_cast<std::size_t>(binding.first_vertex) + binding.vertex_count;
          if (end > indices.size()) continue;
          std::fill(indices.begin() + binding.first_vertex, indices.begin() + end,
                    transform_source_offsets_[binding.source_index] + binding.position_index);
        }
        if (proxy.edit_wire_slot_capacity != 0u || !proxy.face_fill_colors.empty()) {
          // Spare slots have no draw indices. Give them a valid harmless fetch;
          // missing mappings on any active endpoint still select the CPU path.
          std::vector<std::uint8_t> active(proxy.vertex_count, 0u);
          std::span<const std::uint32_t> active_indices;
          if (!proxy.face_fill_colors.empty()) {
            active_indices = slot.proxy_groups[layer].face_indices;
          } else if (layer == overlay_layer_index(ViewportOverlayLayer::SourceWire)) {
            active_indices = slot.source_wire_retention.active_indices();
          } else {
            const auto retained_index = layer == overlay_layer_index(ViewportOverlayLayer::SelectedFaceEdge) ? 0u : 1u;
            active_indices = slot.selection_wire_retention[retained_index].active_indices();
          }
          for (const auto index : active_indices) {
            if (index < active.size()) active[index] = 1u;
          }
          for (std::size_t index = 0u; index < active.size(); ++index) {
            if (active[index] != 0u) continue;
            if (primary[index] == kUnmapped) primary[index] = 0u;
            if (index < secondary.size() && secondary[index] == kUnmapped) secondary[index] = 0u;
            if (index < support0.size() && support0[index] == kUnmapped) support0[index] = 0u;
            if (index < support1.size() && support1[index] == kUnmapped) support1[index] = 0u;
          }
        }
        (void)factory_->bind_shared_positions(proxy, primary, secondary, support0, support1);
      }
      if (proxy.shared_positions_bound) ++shared_count;
      else ++fallback_count;
    }
  }
  QDR_PROFILE_PLOT("Quader overlay shared position proxies", static_cast<std::int64_t>(shared_count));
  QDR_PROFILE_PLOT("Quader overlay fallback position proxies", static_cast<std::int64_t>(fallback_count));
}

void OverlayRenderableCache::apply_transform_stream(
    OverlaySlot &slot, std::uint64_t position_signature,
    const std::vector<std::uint8_t> *dirty_source_mask) {
  QDR_PROFILE_FUNCTION();
  if (!slot.transform_active) {
    return;
  }
  const bool upload_all_sources = dirty_source_mask == nullptr;
  if (!upload_all_sources &&
      slot.transform_position_signature == position_signature &&
      std::none_of(dirty_source_mask->begin(), dirty_source_mask->end(),
                   [](std::uint8_t dirty) { return dirty != 0u; })) {
    return;
  }
  if (factory_ && factory_->initialized()) {
    QDR_PROFILE_SCOPE("Overlay upload retained GPU position streams");
    std::vector<std::uint64_t> dirty_proxy_keys;
    dirty_proxy_keys.reserve(16u);
    {
      QDR_PROFILE_SCOPE("Overlay update dirty position stream bindings");
      const std::size_t source_count =
          std::min(transform_sources_.size(),
                   slot.transform_source_binding_spans.size());
      for (std::size_t source_index = 0u; source_index < source_count;
           ++source_index) {
        if (!upload_all_sources &&
            (source_index >= dirty_source_mask->size() ||
             (*dirty_source_mask)[source_index] == 0u)) {
          continue;
        }
        const TransformPositionSource &source =
            transform_sources_[source_index];
        const TransformSourceBindingSpan &source_span =
            slot.transform_source_binding_spans[source_index];
        const std::size_t first_range = source_span.first_range;
        const std::size_t end_range =
            first_range + source_span.range_count;
        if (end_range > slot.transform_binding_ranges.size()) {
          continue;
        }
        for (std::size_t range_index = first_range;
             range_index < end_range; ++range_index) {
          const TransformBindingRange &target =
              slot.transform_binding_ranges[range_index];
          if (target.source_index != source_index) {
            continue;
          }
          if (target.layer_index >= slot.proxy_groups.size()) {
            continue;
          }
          OverlayProxyGroup &group = slot.proxy_groups[target.layer_index];
          if (target.proxy_index >= group.proxies.size()) {
            continue;
          }
          OverlayRenderableProxy &proxy = group.proxies[target.proxy_index];
          if (proxy.shared_positions_bound) continue;
          const std::size_t first_binding = target.first_binding;
          const std::size_t end_binding =
              first_binding + target.binding_count;
          if (end_binding > proxy.position_stream_bindings.size()) {
            continue;
          }
          bool proxy_stream_changed = false;
          for (std::size_t binding_index = first_binding;
               binding_index < end_binding; ++binding_index) {
            const OverlayPositionStreamBinding &binding =
                proxy.position_stream_bindings[binding_index];
            if (binding.source_index != source_index ||
                binding.position_index >= source.world_positions.size()) {
              continue;
            }

            const QVec3 position =
                source.world_positions[binding.position_index];
            const OverlayFloat4 position4{position.x, position.y, position.z,
                                          1.0f};
            const std::size_t first = binding.first_vertex;
            const std::size_t end = first + binding.vertex_count;
            if (!proxy.position_stream3.empty()) {
              if (end > proxy.position_stream3.size()) {
                continue;
              }
              std::fill(proxy.position_stream3.begin() + first,
                        proxy.position_stream3.begin() + end,
                        OverlayFloat3{position.x, position.y, position.z});
            } else {
              auto &stream = position_stream(proxy, binding.stream);
              if (end > stream.size()) {
                continue;
              }
              std::fill(stream.begin() + first, stream.begin() + end,
                        position4);
            }
            proxy_stream_changed = true;
          }
          if (!proxy_stream_changed) {
            continue;
          }
          dirty_proxy_keys.push_back(
              (static_cast<std::uint64_t>(target.layer_index) << 32u) |
              static_cast<std::uint64_t>(target.proxy_index));
        }
      }
    }
    std::sort(dirty_proxy_keys.begin(), dirty_proxy_keys.end());
    dirty_proxy_keys.erase(
        std::unique(dirty_proxy_keys.begin(), dirty_proxy_keys.end()),
        dirty_proxy_keys.end());
    {
      QDR_PROFILE_SCOPE("Overlay upload dirty position stream proxies");
      for (const std::uint64_t proxy_key : dirty_proxy_keys) {
        const std::size_t layer_index =
            static_cast<std::size_t>(proxy_key >> 32u);
        const std::size_t proxy_index = static_cast<std::size_t>(
            proxy_key & static_cast<std::uint64_t>(0xffffffffu));
        if (layer_index >= slot.proxy_groups.size()) {
          continue;
        }
        OverlayProxyGroup &group = slot.proxy_groups[layer_index];
        if (proxy_index >= group.proxies.size()) {
          continue;
        }
        auto &proxy = group.proxies[proxy_index];
        if (factory_->update_position_streams(proxy) &&
            proxy.kind == OverlayRenderableProxyKind::Point) {
          ++slot.point_position_upload_count;
        }
      }
    }
  }
  slot.signatures = slot.transform_base_signatures;
  // Transform positions live in retained GPU streams. Keeping per-layer
  // topology signatures at their semantic base prevents a later camera-only
  // update from mistaking a position upload for a geometry/style rebuild.
  slot.frame_signature =
      RenderSignatureBuilder(slot.transform_base_frame_signature)
          .add_value(position_signature)
          .value();
  slot.signatures.frame_signature = slot.frame_signature;
  slot.transform_position_signature = position_signature;
}

void OverlayRenderableCache::restore_transform_base(OverlaySlot &slot,
                                                    bool rebuild) {
  if (!slot.transform_active) {
    return;
  }
  if (factory_ && factory_->initialized()) {
    QDR_PROFILE_SCOPE("Overlay restore retained GPU position streams");
    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
      if (!slot.transform_layers[overlay_layer_index(layer)]) {
        continue;
      }
      OverlayProxyGroup &group = slot.proxy_groups[overlay_layer_index(layer)];
      for (OverlayRenderableProxy &proxy : group.proxies) {
        if (proxy.shared_positions_bound) {
          if (factory_->clear_shared_positions(proxy) &&
              proxy.kind == OverlayRenderableProxyKind::Point) {
            ++slot.point_position_upload_count;
          }
          continue;
        }
        bool changed = false;
        for (const OverlayPositionStreamBinding &binding :
             proxy.position_stream_bindings) {
          if (binding.source_index ==
              std::numeric_limits<std::uint32_t>::max()) {
            continue;
          }
          const std::size_t first = binding.first_vertex;
          const std::size_t end = first + binding.vertex_count;
          if (!proxy.position_stream3.empty()) {
            if (end > proxy.position_stream3.size()) {
              continue;
            }
            const OverlayFloat3 base{binding.base_position.x,
                                     binding.base_position.y,
                                     binding.base_position.z};
            std::fill(proxy.position_stream3.begin() + first,
                      proxy.position_stream3.begin() + end, base);
          } else {
            auto &stream = position_stream(proxy, binding.stream);
            if (end > stream.size()) {
              continue;
            }
            std::fill(stream.begin() + first, stream.begin() + end,
                      binding.base_position);
          }
          changed = true;
        }
        if (changed) {
          if (factory_->update_position_streams(proxy) &&
              proxy.kind == OverlayRenderableProxyKind::Point) {
            ++slot.point_position_upload_count;
          }
        }
      }
    }
  }
  slot.signatures = slot.transform_base_signatures;
  slot.frame_signature = slot.transform_base_frame_signature;
  slot.transform_position_signature = 0u;
  slot.transform_active = false;
  for (std::size_t index = 0u; index < slot.transform_layers.size(); ++index) {
    if (!slot.transform_layers[index]) {
      continue;
    }
    const auto layer = static_cast<ViewportOverlayLayer>(index);
    slot.proxy_groups[index].signature =
        slot.component_point_family_active &&
                layer == ViewportOverlayLayer::UnselectedVertex
            ? component_point_build_signature(slot.transform_base_signatures,
                                              slot.point_order_signature)
            : OverlayRenderableBuilder::layer_build_signature(
                  slot.transform_base_signatures, slot.camera, layer, true);
  }
  (void)rebuild;
}

std::uint8_t OverlayRenderableCache::effective_layer_mask(
    const OverlaySlot &slot, ViewportOverlayLayer layer) const {
  (void)layer;
  return slot.layer_mask;
}

void OverlayRenderableCache::clear_slot(std::size_t viewport_index) {
  if (viewport_index >= slots_.size()) {
    return;
  }
  destroy_slot(slots_[viewport_index]);
}

OverlayRenderableCacheStats
OverlayRenderableCache::stats(std::size_t viewport_index) const {
  if (viewport_index >= slots_.size()) {
    return {};
  }

  const OverlaySlot &slot = slots_[viewport_index];
  std::size_t point_vertex_count = 0u;
  std::size_t visible_point_vertex_count = 0u;
  std::size_t shared_position_proxy_count = 0u;
  for (const OverlayProxyGroup &group : slot.proxy_groups) {
    for (const OverlayRenderableProxy &proxy : group.proxies) {
      if (proxy.shared_positions_bound) ++shared_position_proxy_count;
      if (proxy.kind == OverlayRenderableProxyKind::Point) {
        point_vertex_count += proxy.vertex_count;
        visible_point_vertex_count += static_cast<std::size_t>(std::count_if(
            proxy.point_visibility_stream.begin(), proxy.point_visibility_stream.end(),
            [](float visible) { return visible > 0.5f; }));
      }
    }
  }
  return {
      .frame_signature = slot.frame_signature,
      .camera_signature = slot.camera_signature,
      .layer_mask = slot.layer_mask,
      .proxy_count = active_proxy_count(viewport_index),
      .point_vertex_count = point_vertex_count,
      .visible_point_vertex_count = visible_point_vertex_count,
      .point_geometry_update_count = slot.point_geometry_update_count,
      .point_visibility_upload_count = slot.point_visibility_upload_count,
      .point_position_upload_count = slot.point_position_upload_count,
      .uploads = factory_ != nullptr ? factory_->stats() : OverlayProxyUploadStats{},
      .point_visibility = slot.point_visibility.stats(),
      .retained_source_wire_edges = slot.source_wire_retention.active_edge_count(),
      .retained_source_wire_capacity = slot.source_wire_retention.edge_slots().size(),
      .shared_position_proxy_count = shared_position_proxy_count,
  };
}

std::size_t
OverlayRenderableCache::active_proxy_count(std::size_t viewport_index) const {
  if (viewport_index >= slots_.size()) {
    return 0;
  }

  std::size_t count = 0;
  for (const OverlayProxyGroup &group : slots_[viewport_index].proxy_groups) {
    count += group.proxies.size();
  }
  return count;
}

void OverlayRenderableCache::update_retained_camera_state(OverlaySlot &slot) {
  QDR_PROFILE_SCOPE("Overlay update retained camera material state");
  if (!factory_ || !factory_->initialized()) {
    return;
  }
  for (const OverlayProxyGroup &group : slot.proxy_groups) {
    factory_->update_camera_state(group.proxies, slot.camera);
  }
}

void OverlayRenderableCache::rebuild_slot(
    OverlaySlot &slot, bool rebuild_camera_independent_layers) {
  QDR_PROFILE_FUNCTION();
  if (rebuild_camera_independent_layers) {
    prepare_retained_face_source(slot);
    for (auto &line : slot.frame.lines) slot.wire_surface_support.apply(line);
  }
  bool transformed_binding_group_rebuilt = false;
  // Camera-only updates retain the exact semantic frame and its class partition.
  // Revalidate eligibility when frame content or layer publication changes.
  const bool component_point_family =
      rebuild_camera_independent_layers
          ? component_points_support_retention(slot.frame)
          : slot.component_point_family_active;
  if (component_point_family != slot.component_point_family_active) {
    for (const auto layer : {ViewportOverlayLayer::UnselectedVertex,
                             ViewportOverlayLayer::SelectedVertex}) {
      transformed_binding_group_rebuilt |=
          slot.transform_active && slot.transform_layers[overlay_layer_index(layer)];
      destroy_group(slot.proxy_groups[overlay_layer_index(layer)]);
    }
    slot.component_point_family_active = component_point_family;
    slot.component_point_geometry_retained = false;
  }
  for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
    if (component_point_family && component_point_layer(layer)) {
      if (layer == ViewportOverlayLayer::UnselectedVertex) {
        const bool rebuilt = rebuild_component_points(slot);
        transformed_binding_group_rebuilt |= rebuilt && slot.transform_active &&
            slot.transform_layers[overlay_layer_index(layer)];
      }
      continue;
    }
    if (!rebuild_camera_independent_layers &&
        !OverlayRenderableBuilder::layer_build_depends_on_camera(layer)) {
      continue;
    }
    QDR_PROFILE_SCOPE("Overlay rebuild layer");
    const std::uint64_t layer_signature =
        OverlayRenderableBuilder::layer_build_signature(slot.signatures,
                                                        slot.camera, layer, true);
    OverlayProxyGroup &group =
        slot.proxy_groups[static_cast<std::size_t>(layer)];
    if (group.signature == layer_signature) {
      continue;
    }
    const bool group_has_active_transform_bindings =
        slot.transform_active &&
        slot.transform_layers[overlay_layer_index(layer)];

    if (retained_face_layer(layer) && rebuild_retained_faces(slot, group, layer)) {
      group.signature = layer_signature;
      transformed_binding_group_rebuilt |= group_has_active_transform_bindings;
      continue;
    }

    if (!OverlayRenderableBuilder::layer_has_renderable_primitives(
            slot.signatures, layer)) {
      if (layer == ViewportOverlayLayer::SourceWire) slot.source_wire_retention.clear();
      if (retained_selection_wire_layer(layer)) {
        slot.selection_wire_retention[layer == ViewportOverlayLayer::SelectedFaceEdge ? 0u : 1u].clear();
      }
      destroy_group(group);
      group.signature = layer_signature;
      transformed_binding_group_rebuilt |=
          group_has_active_transform_bindings;
      continue;
    }

    if ((layer == ViewportOverlayLayer::SourceWire || retained_selection_wire_layer(layer)) &&
        rebuild_retained_source_wire(slot, group, layer)) {
      group.signature = layer_signature;
      transformed_binding_group_rebuilt |= group_has_active_transform_bindings;
      continue;
    }

    const auto rebuild_geometry = [&] {
      const OverlayLayerBuildPlan plan = [&] {
        QDR_PROFILE_SCOPE("Overlay prepare layer mesh");
        return OverlayRenderableBuilder::build_layer(
            slot.frame, slot.camera, layer, &slot.signatures, blend_order_bias_,
            nullptr, !transform_stream_active_, true, &slot.wire_surface_support);
      }();
      if (!plan.point_batches.empty()) ++slot.point_geometry_update_count;
      const std::uint8_t layer_mask = effective_layer_mask(slot, layer);
      const bool retained = [&] {
        QDR_PROFILE_SCOPE("Overlay update existing layer proxies");
        return factory_->update_layer_proxies(group.proxies, plan, layer_mask);
      }();
      if (!retained) {
        {
          QDR_PROFILE_SCOPE("Overlay destroy replaced layer proxies");
          destroy_group(group);
        }
        {
          QDR_PROFILE_SCOPE("Overlay create replacement layer proxies");
          group.proxies = factory_->create_layer_proxies(plan, layer_mask);
        }
      }
    };
    switch (layer) {
    case ViewportOverlayLayer::SourceWire: {
      QDR_PROFILE_SCOPE("Overlay rebuild source wire geometry");
      rebuild_geometry();
      break;
    }
    case ViewportOverlayLayer::SelectedEdge: {
      QDR_PROFILE_SCOPE("Overlay rebuild selected edge geometry");
      rebuild_geometry();
      break;
    }
    case ViewportOverlayLayer::SelectedFaceEdge: {
      QDR_PROFILE_SCOPE("Overlay rebuild selected face edge geometry");
      rebuild_geometry();
      break;
    }
    case ViewportOverlayLayer::SelectedVisibleFace: {
      QDR_PROFILE_SCOPE("Overlay rebuild selected face fill geometry");
      rebuild_geometry();
      break;
    }
    case ViewportOverlayLayer::SelectedFaceMask: {
      QDR_PROFILE_SCOPE("Overlay rebuild selected face mask geometry");
      rebuild_geometry();
      break;
    }
    default:
      rebuild_geometry();
      break;
    }
    group.signature = layer_signature;
    transformed_binding_group_rebuilt |= group_has_active_transform_bindings;
  }
  if (slot.transform_active && transformed_binding_group_rebuilt) {
    // A retained proxy update refreshes its base streams and binding keys.
    // Re-resolve only when the rebuilt group participated in the transform,
    // then restore current positions with one forced upload. Camera-only icon
    // rebuilds do not disturb dense point/line/face bindings.
    slot.transform_bindings_valid = false;
    resolve_transform_stream_bindings(slot);
    slot.transform_position_signature = 0u;
    apply_transform_stream(slot, transform_position_signature_);
  }
  update_point_visibility(slot);
}

bool OverlayRenderableCache::rebuild_retained_source_wire(
    OverlaySlot &slot, OverlayProxyGroup &group, ViewportOverlayLayer layer) {
  QDR_PROFILE_SCOPE("Overlay reconcile retained source wire slots");
  auto &retained = layer == ViewportOverlayLayer::SourceWire ? slot.source_wire_retention :
      slot.selection_wire_retention[layer == ViewportOverlayLayer::SelectedFaceEdge ? 0u : 1u];
  const auto layer_index = overlay_layer_index(layer);
  try {
    if (slot.signatures.triangle_counts[layer_index] != 0u ||
        slot.signatures.point_counts[layer_index] != 0u ||
        slot.signatures.icon_counts[layer_index] != 0u ||
        !retained.prepare(slot.frame.lines, layer)) {
      retained.clear();
      return false;
    }
    const bool recreate = retained.requires_recreation() || group.proxies.size() != 1u;
    auto update = factory_->prepare_retained_edit_wire_update(
        recreate ? nullptr : &group.proxies.front(), retained.edge_slots(),
        retained.dirty_slots(), retained.active_indices(), retained.active_bindings(),
        retained.indices_changed());
    if (!update) {
      retained.clear();
      return false;
    }
    const auto presentation = OverlayRenderableBuilder::edit_wire_presentation(
        layer, retained.width_pixels(), slot.camera,
        static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64u + blend_order_bias_));
    const auto layer_mask = effective_layer_mask(slot, layer);
    if (recreate) {
      group.proxies.reserve(1u);
      auto proxy = factory_->create_retained_edit_wire_proxy(presentation, layer_mask, std::move(update));
      if (proxy.entity_id == 0u) {
        retained.clear();
        return false;
      }
      destroy_group(group);
      group.proxies.push_back(std::move(proxy));
    } else if (!factory_->update_retained_edit_wire_proxy(
                   group.proxies.front(), presentation, layer_mask, std::move(update))) {
      retained.clear();
      return false;
    }
    return true;
  } catch (...) {
    retained.clear();
    return false;
  }
}

std::size_t OverlayRenderableCache::FaceTriangleKeyHash::operator()(
    const FaceTriangleKey &key) const {
  std::size_t hash = 2166136261u;
  for (const auto value : key.values) hash = (hash ^ static_cast<std::uint32_t>(value)) * 16777619u;
  return hash;
}

void OverlayRenderableCache::prepare_retained_face_source(OverlaySlot &slot) {
  slot.wire_surface_support.prepare(slot.frame, slot.signatures);
  const auto source_layer = overlay_layer_index(ViewportOverlayLayer::SourceWireDepthStamp);
  const auto signature = slot.signatures.layer_signatures[source_layer];
  if (slot.face_source.signature == signature) return;
  QDR_PROFILE_SCOPE("Overlay retain source face topology");
  RetainedFaceSource next;
  next.signature = signature;
  const auto count = slot.signatures.triangle_counts[source_layer];
  next.triangles.reserve(count);
  next.triangle_slots.reserve(count);
  next.valid = count != 0u && count <= std::numeric_limits<std::uint32_t>::max() / 3u;
  for (const auto &triangle : slot.frame.triangles) {
    if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp) continue;
    const auto &element = triangle.element;
    if (!next.valid || element.object_id < 0 || element.face_id < 0 ||
        element.vertex_id < 0 || element.edge_a < 0 || element.edge_b < 0 ||
        (triangle.source != ViewportOverlaySource::ComponentSelection &&
         triangle.source != ViewportOverlaySource::Selection)) {
      next.valid = false;
      break;
    }
    const FaceTriangleKey key{{element.object_id, element.face_id, element.vertex_id,
                               element.edge_a, element.edge_b}};
    if (!next.triangle_slots.emplace(key, static_cast<std::uint32_t>(next.triangles.size())).second) {
      next.valid = false;
      break;
    }
    next.triangles.push_back(triangle);
  }
  if (!next.valid) {
    next.triangles.clear();
    next.triangle_slots.clear();
  }
  slot.face_source = std::move(next);
  for (const auto layer : {ViewportOverlayLayer::SelectedVisibleFace,
                           ViewportOverlayLayer::SelectedXrayFace,
                           ViewportOverlayLayer::SelectedFaceMask}) {
    slot.proxy_groups[overlay_layer_index(layer)].signature = 0u;
  }
}

bool OverlayRenderableCache::rebuild_retained_faces(
    OverlaySlot &slot, OverlayProxyGroup &group, ViewportOverlayLayer layer) {
  const auto &source = slot.face_source;
  const auto layer_index = overlay_layer_index(layer);
  if (!source.valid || slot.signatures.line_counts[layer_index] != 0u ||
      slot.signatures.point_counts[layer_index] != 0u ||
      slot.signatures.icon_counts[layer_index] != 0u) return false;
  QDR_PROFILE_SCOPE("Overlay reconcile retained face selection");
  constexpr std::size_t kClassCount = static_cast<std::size_t>(OverlayFaceFillClass::RemoveSelection) + 1u;
  std::array<std::vector<std::uint32_t>, kClassCount> class_indices;
  const bool recreate = group.face_source_signature != source.signature ||
      group.proxies.size() != 1u || group.proxies.front().face_fill_colors.empty();
  if (recreate) group.face_colors.assign(source.triangles.size(), 0u);
  std::vector<std::uint8_t> seen(source.triangles.size(), 0u);
  const auto same_position = [](QVec3 left, QVec3 right) {
    return left.x == right.x && left.y == right.y && left.z == right.z;
  };
  for (const auto &triangle : slot.frame.triangles) {
    if (triangle.layer != layer) continue;
    if (triangle.source != ViewportOverlaySource::ComponentSelection &&
        triangle.source != ViewportOverlaySource::Selection) return false;
    const auto &element = triangle.element;
    const auto found = source.triangle_slots.find({{element.object_id, element.face_id,
        element.vertex_id, element.edge_a, element.edge_b}});
    if (found == source.triangle_slots.end() || seen[found->second] != 0u) return false;
    const auto triangle_slot = found->second;
    const auto &base = source.triangles[triangle_slot];
    if (!same_position(base.a, triangle.a) || !same_position(base.b, triangle.b) ||
        !same_position(base.c, triangle.c)) return false;
    seen[triangle_slot] = 1u;
    group.face_colors[triangle_slot] = triangle.color;
    auto &indices = class_indices[static_cast<std::size_t>(overlay_face_fill_class(triangle))];
    const auto first = triangle_slot * 3u;
    indices.insert(indices.end(), {first, first + 1u, first + 2u});
  }
  std::vector<std::uint32_t> indices;
  indices.reserve(slot.signatures.triangle_counts[layer_index] * 3u);
  std::vector<OverlayRetainedFaceDraw> draws;
  draws.reserve(kClassCount * 2u - 1u);
  auto blend_order = static_cast<std::uint16_t>(viewport_overlay_draw_order(layer) * 64u + blend_order_bias_);
  for (std::size_t fill_class = 0u; fill_class < kClassCount; ++fill_class) {
    const auto first_index = static_cast<std::uint32_t>(indices.size());
    const auto index_count = static_cast<std::uint32_t>(class_indices[fill_class].size());
    indices.insert(indices.end(), class_indices[fill_class].begin(), class_indices[fill_class].end());
    auto state = OverlayRenderableBuilder::make_state(layer,
        ViewportOverlayGeometryKind::Triangles, blend_order);
    if (index_count != 0u) ++blend_order;
    state.face_fill_class = static_cast<OverlayFaceFillClass>(fill_class);
    if (fill_class != 0u) {
      auto mask_state = state;
      mask_state.color_write = false;
      mask_state.face_fill_pass = OverlayFaceFillPass::Mask;
      mask_state.blend_order = overlay_face_fill_mask_blend_order(blend_order_bias_);
      draws.push_back({mask_state, first_index, index_count});
    }
    draws.push_back({state, first_index, index_count});
  }
  const auto mask = effective_layer_mask(slot, layer);
  if (recreate) {
    if (indices.empty()) {
      destroy_group(group);
      return true;
    }
    auto proxy = factory_->create_retained_face_proxy(source.triangles, group.face_colors,
        indices, draws, mask);
    if (proxy.entity_id == 0u) return false;
    auto colors = std::move(group.face_colors);
    destroy_group(group);
    group.face_colors = std::move(colors);
    group.proxies.push_back(std::move(proxy));
    group.face_source_signature = source.signature;
  } else if (!factory_->update_retained_face_proxy(group.proxies.front(), group.face_colors,
                 indices, draws, mask, indices != group.face_indices)) {
    return false;
  }
  group.face_indices = std::move(indices);
  return true;
}

bool OverlayRenderableCache::rebuild_component_points(OverlaySlot &slot) {
  auto &group = slot.proxy_groups[
      overlay_layer_index(ViewportOverlayLayer::UnselectedVertex)];
  const auto signature = component_point_build_signature(
      slot.signatures, slot.point_order_signature);
  if (group.signature == signature) return false;
  QDR_PROFILE_SCOPE("Overlay update retained component point family");
  std::vector<const ViewportOverlayPoint *> points;
  std::vector<std::size_t> frame_indices;
  const auto point_count = slot.signatures.point_counts[
      overlay_layer_index(ViewportOverlayLayer::UnselectedVertex)] +
      slot.signatures.point_counts[
          overlay_layer_index(ViewportOverlayLayer::SelectedVertex)];
  points.reserve(point_count);
  frame_indices.reserve(point_count);
  for (std::size_t index = 0u; index < slot.frame.points.size(); ++index) {
    if (component_point_layer(slot.frame.points[index].layer)) {
      points.push_back(&slot.frame.points[index]);
      frame_indices.push_back(index);
    }
  }
  if (slot.component_point_geometry_retained && group.proxies.size() == 1u &&
      factory_->update_component_point_appearance(
          group.proxies.front(), points, frame_indices, slot.camera,
          blend_order_bias_, slot.layer_mask)) {
    group.signature = signature;
    return false;
  }
  destroy_group(group);
  group.proxies = factory_->create_component_point_proxies(
      points, frame_indices, slot.camera, blend_order_bias_, slot.layer_mask);
  group.signature = signature;
  slot.component_point_geometry_retained = true;
  ++slot.point_geometry_update_count;
  return true;
}

void OverlayRenderableCache::update_point_visibility(OverlaySlot &slot) {
  if (!factory_ || !factory_->initialized() || slot.frame.points.empty()) return;
  if (transform_coordinates_accepted_ && !slot.accepted_visibility_valid) {
    QDR_PROFILE_SCOPE("Overlay project accepted point visibility coordinates");
    std::unordered_map<int, const TransformPositionSource *> sources;
    sources.reserve(transform_sources_.size());
    for (const auto &source : transform_sources_) sources.emplace(source.object_id, &source);
    const auto position = [&sources](int object_id, int vertex_id, QVec3 baseline) {
      const auto source = sources.find(object_id);
      if (source == sources.end() || vertex_id < 0 ||
          !source->second->source_vertex_to_position) return baseline;
      const auto found = source->second->source_vertex_to_position->find(
          static_cast<std::uint32_t>(vertex_id));
      if (found == source->second->source_vertex_to_position->end() ||
          found->second >= source->second->world_positions.size()) return baseline;
      return source->second->world_positions[found->second];
    };
    auto &effective = slot.accepted_visibility_frame;
    effective.points = slot.frame.points;
    effective.triangles.clear();
    effective.triangles.reserve(slot.frame.triangles.size());
    for (auto &point : effective.points) {
      point.position = position(point.element.object_id, point.element.vertex_id, point.position);
    }
    for (auto triangle : slot.frame.triangles) {
      if (triangle.layer != ViewportOverlayLayer::SourceWireDepthStamp) continue;
      triangle.a = position(triangle.element.object_id, triangle.element.vertex_id, triangle.a);
      triangle.b = position(triangle.element.object_id, triangle.element.edge_a, triangle.b);
      triangle.c = position(triangle.element.object_id, triangle.element.edge_b, triangle.c);
      effective.triangles.push_back(triangle);
    }
    slot.accepted_visibility_signatures = OverlayRenderableBuilder::frame_signatures(effective);
    slot.accepted_visibility_valid = true;
  }
  const auto viewport_skips_before = slot.point_visibility.stats().viewport_skip_count;
  const auto visibility = slot.point_visibility.evaluate(
      transform_coordinates_accepted_ ? slot.accepted_visibility_frame : slot.frame,
      transform_coordinates_accepted_ ? slot.accepted_visibility_signatures : slot.signatures,
      slot.camera, !transform_stream_active_ || transform_coordinates_accepted_);
  slot.point_viewport_skips_applied |=
      slot.point_visibility.stats().viewport_skip_count > viewport_skips_before;
  QDR_PROFILE_SCOPE("Overlay upload retained point visibility flags");
  for (auto &group : slot.proxy_groups) {
    for (auto &proxy : group.proxies) {
      if (factory_->update_point_visibility(proxy, visibility)) {
        ++slot.point_visibility_upload_count;
      }
    }
  }
}

void OverlayRenderableCache::destroy_slot(OverlaySlot &slot) {
  for (OverlayProxyGroup &group : slot.proxy_groups) {
    destroy_group(group);
  }
  slot.frame = {};
  slot.accepted_visibility_frame = {};
  slot.accepted_visibility_valid = false;
  slot.component_point_family_active = false;
  slot.component_point_geometry_retained = false;
  slot.signatures = OverlayRenderableBuilder::frame_signatures(slot.frame);
  slot.frame_signature = slot.signatures.frame_signature;
  slot.camera = {};
  slot.camera_signature = 0;
  slot.layer_mask = 0;
  slot.has_frame = false;
  slot.point_visibility.invalidate();
  slot.source_wire_retention.clear();
  for (auto &retained : slot.selection_wire_retention) retained.clear();
  slot.face_source = {};
  slot.wire_surface_support = {};
  slot.transform_base_signatures = {};
  slot.transform_base_frame_signature = 0u;
  slot.transform_layout_signature = 0u;
  slot.transform_position_signature = 0u;
  slot.transform_layers.fill(false);
  slot.transform_binding_ranges.clear();
  slot.transform_source_binding_spans.clear();
  slot.transform_bindings_valid = false;
  slot.transform_active = false;
  slot.point_viewport_skips_applied = false;
  slot.point_order_signature = 0u;
}

void OverlayRenderableCache::destroy_group(OverlayProxyGroup &group) {
  if (factory_) {
    for (OverlayRenderableProxy &proxy : group.proxies) {
      factory_->destroy(proxy);
    }
  }
  group.proxies.clear();
  group.face_source_signature = 0u;
  group.face_indices.clear();
  group.face_colors.clear();
  group.signature = 0;
}

} // namespace quader
