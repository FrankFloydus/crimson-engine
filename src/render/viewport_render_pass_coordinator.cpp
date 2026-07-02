////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/viewport_render_pass_coordinator.hpp"

#include "render/render_layers.hpp"

#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/View.h>

namespace quader {

void ViewportRenderPassCoordinator::initialize(std::uint8_t grid_layer_mask) {
  grid_layer_mask_ =
      grid_layer_mask == 0u ? viewport_grid_layer_mask(0) : grid_layer_mask;
}

void ViewportRenderPassCoordinator::set_background_color(
    ViewportSettingColor color) {
  background_color_ = color;
}

bool ViewportRenderPassCoordinator::set_grid_visible(bool visible) {
  if (ground_grid_visible_ == visible) {
    return false;
  }
  ground_grid_visible_ = visible;
  return true;
}

bool ViewportRenderPassCoordinator::set_overlay_visible(bool visible) {
  if (overlay_visible_ == visible) {
    return false;
  }
  overlay_visible_ = visible;
  return true;
}

bool ViewportRenderPassCoordinator::set_scene_visible(bool visible) {
  if (scene_visible_ == visible) {
    return false;
  }
  scene_visible_ = visible;
  return true;
}

bool ViewportRenderPassCoordinator::set_scene_layer_mask(
    std::uint8_t layer_mask) {
  if (scene_layer_mask_ == layer_mask) {
    return false;
  }
  scene_layer_mask_ = layer_mask;
  return true;
}

std::uint8_t ViewportRenderPassCoordinator::scene_visible_layer_mask() const {
  return scene_visible_ ? static_cast<std::uint8_t>(
                              scene_layer_mask_ | kViewportToolPreviewLayerMask)
                        : 0u;
}

std::uint8_t ViewportRenderPassCoordinator::overlay_visible_layer_mask() const {
  if (!ground_grid_visible_ && !overlay_visible_) {
    return 0u;
  }
  std::uint8_t layer_mask = grid_layer_mask_;
  if (scene_visible_) {
    layer_mask =
        static_cast<std::uint8_t>(layer_mask | kViewportOverlayLayerMask);
  }
  return layer_mask;
}

std::uint8_t ViewportRenderPassCoordinator::visible_layer_mask() const {
  return static_cast<std::uint8_t>(scene_visible_layer_mask() |
                                   overlay_visible_layer_mask());
}

void ViewportRenderPassCoordinator::apply_visible_layers(
    filament::View *scene_view, filament::View *overlay_view) const {
  if (scene_view) {
    scene_view->setVisibleLayers(kViewportAllLayerMask,
                                 scene_visible_layer_mask());
  }
  if (overlay_view) {
    overlay_view->setVisibleLayers(kViewportAllLayerMask,
                                   overlay_visible_layer_mask());
  }
}

void ViewportRenderPassCoordinator::render(
    filament::Renderer &renderer, filament::View *scene_view,
    filament::View *overlay_view, bool overlay_uses_separate_target) const {
  if (!scene_view) {
    return;
  }

  filament::Renderer::ClearOptions clear;
  clear.clear = true;
  clear.discard = false;
  clear.clearColor = {background_color_.r, background_color_.g,
                      background_color_.b, background_color_.a};
  renderer.setClearOptions(clear);
  renderer.render(scene_view);

  if (!overlay_view ||
      (overlay_visible_layer_mask() == 0u && !overlay_uses_separate_target)) {
    return;
  }

  filament::Renderer::ClearOptions overlay_clear;
  overlay_clear.clear = overlay_uses_separate_target;
  overlay_clear.discard = false;
  overlay_clear.clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
  renderer.setClearOptions(overlay_clear);
  renderer.render(overlay_view);
  renderer.setClearOptions(clear);
}

} // namespace quader
