////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <utils/Entity.h>

#include <cstdint>

namespace filament {
class Engine;
class IndexBuffer;
class Material;
class MaterialInstance;
class Scene;
class VertexBuffer;
} // namespace filament

namespace quader {

/** Owns one retained fullscreen reset of the pane's face-fill stencil bits. */
class OverlayStencilReset final {
public:
  OverlayStencilReset() = default;
  OverlayStencilReset(const OverlayStencilReset &) = delete;
  OverlayStencilReset &operator=(const OverlayStencilReset &) = delete;
  OverlayStencilReset(OverlayStencilReset &&) = delete;
  OverlayStencilReset &operator=(OverlayStencilReset &&) = delete;
  ~OverlayStencilReset();

  void initialize(filament::Engine &engine, filament::Scene &scene,
                  filament::Material &material, std::uint8_t layer_mask);
  void shutdown();

private:
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::VertexBuffer *vertex_buffer_ = nullptr;
  filament::IndexBuffer *index_buffer_ = nullptr;
  filament::MaterialInstance *material_instance_ = nullptr;
  utils::Entity entity_;
};

} // namespace quader
