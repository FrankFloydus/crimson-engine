////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <memory>

namespace filament {
class Engine;
class Material;
class Renderer;
class Scene;
}

namespace quader {
class ViewportCameraController;
struct ViewportPersistentOverlayStyleSnapshot;

/** Per-pane GPU occupancy masks and complementary depth-tested outline passes. */
class SelectedMeshOutlineRenderer final {
public:
  SelectedMeshOutlineRenderer();
  ~SelectedMeshOutlineRenderer();
  SelectedMeshOutlineRenderer(const SelectedMeshOutlineRenderer &) = delete;
  SelectedMeshOutlineRenderer &operator=(const SelectedMeshOutlineRenderer &) = delete;

  bool initialize(filament::Engine &engine, filament::Scene &main_scene,
                  filament::Scene &mask_scene, filament::Material &composite_material,
                  std::uint8_t pane_layer_mask);
  void shutdown();
  [[nodiscard]] bool initialized() const;
  void resize(std::uint32_t width, std::uint32_t height);
  void update_camera(const ViewportCameraController &controller);
  void set_style(const ViewportPersistentOverlayStyleSnapshot &style);
  void set_visible(bool visible);
  void set_geometry_available(bool ordinary, bool active);
  /** Called inside the viewport frame before the main/depth/overlay views. */
  void render_masks(filament::Renderer &renderer);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
