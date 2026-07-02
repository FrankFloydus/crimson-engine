////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_renderable_cache.hpp"

#include "diagnostics/profile.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"

#include <utility>

namespace quader {

OverlayRenderableCache::~OverlayRenderableCache() {
    shutdown();
}

void OverlayRenderableCache::initialize(OverlayProxyFactory& factory) {
    factory_ = &factory;
}

void OverlayRenderableCache::shutdown() {
    if (!factory_) {
        return;
    }

    for (OverlaySlot& slot : slots_) {
      destroy_slot(slot);
    }
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
  const bool frame_changed = signature != slot.frame_signature;
  const bool camera_changed = camera_signature != slot.camera_signature;
  const bool layer_changed = layer_mask != slot.layer_mask;
  if (!frame_changed && !camera_changed && !layer_changed) {
    slot.frame = std::move(frame);
    return false;
  }

  slot.frame = std::move(frame);
  slot.frame_signature = signature;
  if (frame_changed) {
    slot.signatures = signatures;
  }
  if (camera_changed || layer_changed) {
    slot.camera = camera;
    slot.camera_signature = camera_signature;
    slot.layer_mask = layer_mask;
  }
  if (layer_changed) {
    for (OverlayProxyGroup &group : slot.proxy_groups) {
      group.signature = 0;
    }
  }

  if (factory_ && factory_->initialized()) {
    rebuild_slot(slot, frame_changed || layer_changed);
  }
  return true;
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
        slot.frame, slot.camera, layer, &slot.signatures);
    if (!factory_->update_layer_proxies(group.proxies, plan, slot.layer_mask)) {
      destroy_group(group);
      group.proxies = factory_->create_layer_proxies(plan, slot.layer_mask);
    }
    group.signature = layer_signature;
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
