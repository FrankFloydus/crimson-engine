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
#include <limits>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

std::size_t overlay_layer_index(ViewportOverlayLayer layer) {
  return static_cast<std::size_t>(layer);
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
    transform_layout_signature_ = 0u;
    transform_position_signature_ = 0u;
    transform_stream_active_ = false;
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
    }
    return true;
  }

  const bool resume_transform = transform_stream_active_;
  if (slot.transform_active) {
    restore_transform_base(slot, false);
  }

  slot.frame = std::move(frame);
  slot.has_frame = true;
  slot.frame_signature = signature;
  slot.signatures = signatures;
  slot.transform_layers.fill(false);
  slot.transform_layout_signature = 0u;
  slot.transform_bindings_valid = false;
  slot.camera = camera;
  slot.camera_signature = camera_signature;
  slot.layer_mask = layer_mask;
  if (layer_changed) {
    for (OverlayProxyGroup &group : slot.proxy_groups) {
      group.signature = 0;
    }
  }

  if (resume_transform) {
    if (factory_ && factory_->initialized()) {
      rebuild_slot(slot, true);
    }
    begin_transform_stream(slot, transform_layout_signature_);
    apply_transform_stream(slot, transform_position_signature_);
  } else if (factory_ && factory_->initialized()) {
    rebuild_slot(slot, true);
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
  }
  return true;
}

bool OverlayRenderableCache::set_transform_position_streams(
    const std::vector<RenderToolPreviewMeshRecord> &meshes) {
  QDR_PROFILE_FUNCTION();
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
  if (next_layout == transform_layout_signature_ &&
      next_position == transform_position_signature_ && stable_layout &&
      transform_stream_active_) {
    return false;
  }

  const bool layout_changed =
      next_layout != transform_layout_signature_ || !stable_layout;
  if (!layout_changed) {
    QDR_PROFILE_SCOPE("Overlay transform stream update retained sources");
    std::size_t source_index = 0u;
    for (const RenderToolPreviewMeshRecord &mesh : meshes) {
      if (!is_dynamic_source(mesh)) {
        continue;
      }
      TransformPositionSource &source = transform_sources_[source_index++];
      source.position_revision = mesh.position_revision;
      source.position_count = mesh.dynamic_position_payload->size();
      source.transform = mesh.transform;
      source.positions = mesh.dynamic_position_payload;
      refresh_world_positions(source);
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
  transform_layout_signature_ = next_layout;
  transform_position_signature_ = next_position;
  transform_stream_active_ = true;
  {
    QDR_PROFILE_SCOPE("Overlay transform stream apply slots");
    for (OverlaySlot &slot : slots_) {
      if (!slot.has_frame) {
        continue;
      }
      if (layout_changed || !slot.transform_active) {
        if (slot.transform_active) {
          restore_transform_base(slot, false);
        }
        begin_transform_stream(slot, next_layout);
      }
      apply_transform_stream(slot, next_position);
    }
  }
  return true;
}

void OverlayRenderableCache::reset_transform_position_streams() {
  QDR_PROFILE_FUNCTION();
  transform_stream_active_ = false;
  transform_sources_.clear();
  transform_layout_signature_ = 0u;
  transform_position_signature_ = 0u;
  for (OverlaySlot &slot : slots_) {
    restore_transform_base(slot, true);
    slot.transform_layers.fill(false);
    slot.transform_layout_signature = 0u;
    slot.transform_bindings_valid = false;
  }
}

void OverlayRenderableCache::begin_transform_stream(
    OverlaySlot &slot, std::uint64_t layout_signature) {
  QDR_PROFILE_FUNCTION();
  slot.transform_base_signatures = slot.signatures;
  slot.transform_base_frame_signature = slot.frame_signature;
  slot.transform_active = true;

  if (slot.transform_bindings_valid &&
      slot.transform_layout_signature == layout_signature) {
    return;
  }
  slot.transform_layout_signature = layout_signature;
  resolve_transform_stream_bindings(slot);
}

void OverlayRenderableCache::resolve_transform_stream_bindings(
    OverlaySlot &slot) {
  QDR_PROFILE_SCOPE("Overlay resolve retained GPU stream bindings");
  slot.transform_layers.fill(false);

  std::unordered_map<int, std::uint32_t> source_by_object;
  source_by_object.reserve(transform_sources_.size());
  for (std::size_t index = 0u; index < transform_sources_.size(); ++index) {
    source_by_object.emplace(transform_sources_[index].object_id,
                             static_cast<std::uint32_t>(index));
  }
  for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
    OverlayProxyGroup &group = slot.proxy_groups[overlay_layer_index(layer)];
    for (OverlayRenderableProxy &proxy : group.proxies) {
      for (OverlayPositionStreamBinding &binding :
           proxy.position_stream_bindings) {
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
        slot.transform_layers[overlay_layer_index(layer)] = true;
      }
    }
  }
  slot.transform_bindings_valid = true;
}

void OverlayRenderableCache::apply_transform_stream(
    OverlaySlot &slot, std::uint64_t position_signature) {
  QDR_PROFILE_FUNCTION();
  if (!slot.transform_active ||
      slot.transform_position_signature == position_signature) {
    return;
  }
  if (factory_ && factory_->initialized()) {
    QDR_PROFILE_SCOPE("Overlay upload retained GPU position streams");
    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
      if (!slot.transform_layers[overlay_layer_index(layer)]) {
        continue;
      }
      OverlayProxyGroup &group = slot.proxy_groups[overlay_layer_index(layer)];
      for (OverlayRenderableProxy &proxy : group.proxies) {
        bool changed = false;
        for (const OverlayPositionStreamBinding &binding :
             proxy.position_stream_bindings) {
          if (binding.source_index >= transform_sources_.size()) {
            continue;
          }
          const TransformPositionSource &source =
              transform_sources_[binding.source_index];
          if (binding.position_index >= source.world_positions.size()) {
            continue;
          }
          const QVec3 position = source.world_positions[binding.position_index];
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
            std::vector<OverlayFloat4> &stream =
                binding.stream == OverlayPositionStream::Secondary
                    ? proxy.secondary_position_stream4
                    : proxy.primary_position_stream4;
            if (end > stream.size()) {
              continue;
            }
            std::fill(stream.begin() + first, stream.begin() + end, position4);
          }
          changed = true;
        }
        if (changed) {
          (void)factory_->update_position_streams(proxy);
        }
      }
    }
  }
  slot.signatures = slot.transform_base_signatures;
  for (std::size_t index = 0u; index < slot.transform_layers.size(); ++index) {
    if (!slot.transform_layers[index]) {
      continue;
    }
    slot.signatures.layer_signatures[index] =
        RenderSignatureBuilder(
            slot.transform_base_signatures.layer_signatures[index])
            .add_value(position_signature)
            .value();
  }
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
            std::vector<OverlayFloat4> &stream =
                binding.stream == OverlayPositionStream::Secondary
                    ? proxy.secondary_position_stream4
                    : proxy.primary_position_stream4;
            if (end > stream.size()) {
              continue;
            }
            std::fill(stream.begin() + first, stream.begin() + end,
                      binding.base_position);
          }
          changed = true;
        }
        if (changed) {
          (void)factory_->update_position_streams(proxy);
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
        OverlayRenderableBuilder::layer_build_signature(
            slot.transform_base_signatures, slot.camera, layer);
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
  return {
      .frame_signature = slot.frame_signature,
      .camera_signature = slot.camera_signature,
      .layer_mask = slot.layer_mask,
      .proxy_count = active_proxy_count(viewport_index),
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

void OverlayRenderableCache::rebuild_slot(
    OverlaySlot &slot, bool rebuild_camera_independent_layers) {
  QDR_PROFILE_FUNCTION();
  for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
    QDR_PROFILE_SCOPE("Overlay rebuild layer");
    if (!rebuild_camera_independent_layers &&
        !OverlayRenderableBuilder::layer_build_depends_on_camera(layer)) {
      continue;
    }
    const std::uint64_t layer_signature =
        OverlayRenderableBuilder::layer_build_signature(slot.signatures,
                                                        slot.camera, layer);
    OverlayProxyGroup &group =
        slot.proxy_groups[static_cast<std::size_t>(layer)];
    if (group.signature == layer_signature) {
      continue;
    }

    if (!OverlayRenderableBuilder::layer_has_renderable_primitives(
            slot.signatures, layer)) {
      destroy_group(group);
      group.signature = layer_signature;
      continue;
    }

    const OverlayLayerBuildPlan plan = OverlayRenderableBuilder::build_layer(
        slot.frame, slot.camera, layer, &slot.signatures, blend_order_bias_);
    const std::uint8_t layer_mask = effective_layer_mask(slot, layer);
    if (!factory_->update_layer_proxies(group.proxies, plan, layer_mask)) {
      destroy_group(group);
      group.proxies = factory_->create_layer_proxies(plan, layer_mask);
    }
    group.signature = layer_signature;
  }
  if (slot.transform_active && rebuild_camera_independent_layers) {
    // A retained proxy update refreshes its base streams and binding keys.
    // Re-resolve once at that uncommon boundary, then restore the current
    // transform using position-only uploads; normal drag ticks never enter
    // this path.
    slot.transform_bindings_valid = false;
    resolve_transform_stream_bindings(slot);
    slot.transform_position_signature = 0u;
    apply_transform_stream(slot, transform_position_signature_);
  }
}

void OverlayRenderableCache::destroy_slot(OverlaySlot &slot) {
  for (OverlayProxyGroup &group : slot.proxy_groups) {
    destroy_group(group);
  }
  slot.frame = {};
  slot.signatures = OverlayRenderableBuilder::frame_signatures(slot.frame);
  slot.frame_signature = slot.signatures.frame_signature;
  slot.camera = {};
  slot.camera_signature = 0;
  slot.layer_mask = 0;
  slot.has_frame = false;
  slot.transform_base_signatures = {};
  slot.transform_base_frame_signature = 0u;
  slot.transform_layout_signature = 0u;
  slot.transform_position_signature = 0u;
  slot.transform_layers.fill(false);
  slot.transform_bindings_valid = false;
  slot.transform_active = false;
}

void OverlayRenderableCache::destroy_group(OverlayProxyGroup &group) {
  if (factory_) {
    for (OverlayRenderableProxy &proxy : group.proxies) {
      factory_->destroy(proxy);
    }
  }
  group.proxies.clear();
  group.signature = 0;
}

} // namespace quader
