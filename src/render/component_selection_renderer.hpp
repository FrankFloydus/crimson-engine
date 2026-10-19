////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "viewport/viewport_selection_depth.hpp"
#include "render/render_view.hpp"

#include <memory>

namespace filament {
class Engine;
class Material;
class Renderer;
class Texture;
}

namespace quader {

/// Owns retained conservative ID layers and asynchronous immutable readbacks.
class ComponentSelectionRenderer final : public RenderSelectionDepth {
public:
  ComponentSelectionRenderer();
  ~ComponentSelectionRenderer();
  ComponentSelectionRenderer(const ComponentSelectionRenderer &) = delete;
  ComponentSelectionRenderer &operator=(const ComponentSelectionRenderer &) = delete;

  bool initialize(filament::Engine &engine, filament::Material &material,
                  filament::Texture &fallback);
  void shutdown();
  /// Settled cameras warm once; active navigation never submits extra passes.
  [[nodiscard]] bool wants_geometry(const ViewportSelectionDepthCamera &camera, bool eligible);
  void prepare(std::shared_ptr<const ViewportSelectionDepthGeometry> geometry,
               const ViewportSelectionDepthCamera &camera);
  void render(filament::Renderer &renderer);
  [[nodiscard]] std::shared_ptr<const ViewportSelectionDepthSource> source() const;
  [[nodiscard]] ComponentSelectionRendererStats stats() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
