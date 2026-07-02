////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/render_layers.hpp"
#include "viewport/viewport_settings.hpp"

#include <cstdint>

namespace filament {
class RenderTarget;
class Renderer;
class View;
} // namespace filament

namespace quader {

/**
 * Owns Viewport Render Pass Coordinator behavior within the render extraction and Filament resource layer.
 */
class ViewportRenderPassCoordinator {
public:
  void initialize(std::uint8_t grid_layer_mask);

  void set_background_color(ViewportSettingColor color);

  bool set_grid_visible(bool visible);

  bool set_overlay_visible(bool visible);

  bool set_scene_visible(bool visible);

  bool set_scene_layer_mask(std::uint8_t layer_mask);

  std::uint8_t scene_visible_layer_mask() const;
  std::uint8_t overlay_visible_layer_mask() const;
  std::uint8_t visible_layer_mask() const;

  void apply_visible_layers(filament::View *scene_view,
                            filament::View *overlay_view) const;
  void render(filament::Renderer &renderer, filament::View *scene_view,
              filament::View *overlay_view,
              bool overlay_uses_separate_target) const;

private:
  bool ground_grid_visible_ = true;
  bool overlay_visible_ = false;
  bool scene_visible_ = true;
  std::uint8_t scene_layer_mask_ = kViewportShadedSceneLayerMask;
  ViewportSettingColor background_color_ = {0.0f, 0.0f, 0.0f, 1.0f};
  std::uint8_t grid_layer_mask_ = 0x04u;
};

} // namespace quader
