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

bool overlay_camera_material_state_changed(
    const ViewportOverlayCamera &before,
    const ViewportOverlayCamera &after) {
  return before.projection != after.projection ||
         before.near_plane != after.near_plane ||
         before.far_plane != after.far_plane;
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

  const bool resume_transform = transform_stream_active_;
  if (slot.transform_active) {
    restore_transform_base(slot, false);
  }

  slot.frame = std::move(frame);
  slot.has_frame = true;
  slot.frame_signature = signature;
  slot.signatures = signatures;
  slot.transform_layers.fill(false);
  slot.transform_binding_ranges.clear();
  slot.transform_source_binding_spans.clear();
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
    return false;
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
    slot.transform_binding_ranges.clear();
    slot.transform_source_binding_spans.clear();
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
              std::vector<OverlayFloat4> &stream =
                  binding.stream == OverlayPositionStream::Secondary
                      ? proxy.secondary_position_stream4
                      : proxy.primary_position_stream4;
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
        (void)factory_->update_position_streams(group.proxies[proxy_index]);
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
  bool transformed_binding_group_rebuilt = false;
  for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
    if (!rebuild_camera_independent_layers &&
        !OverlayRenderableBuilder::layer_build_depends_on_camera(layer)) {
      continue;
    }
    QDR_PROFILE_SCOPE("Overlay rebuild layer");
    const std::uint64_t layer_signature =
        OverlayRenderableBuilder::layer_build_signature(slot.signatures,
                                                        slot.camera, layer);
    OverlayProxyGroup &group =
        slot.proxy_groups[static_cast<std::size_t>(layer)];
    if (group.signature == layer_signature) {
      continue;
    }
    const bool group_has_active_transform_bindings =
        slot.transform_active &&
        slot.transform_layers[overlay_layer_index(layer)];

    if (!OverlayRenderableBuilder::layer_has_renderable_primitives(
            slot.signatures, layer)) {
      destroy_group(group);
      group.signature = layer_signature;
      transformed_binding_group_rebuilt |=
          group_has_active_transform_bindings;
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
  slot.transform_binding_ranges.clear();
  slot.transform_source_binding_spans.clear();
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
