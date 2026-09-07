////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"
#include "viewport/material_preview_state.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace filament {
class Engine;
class Material;
class Renderer;
} // namespace filament

namespace quader {

using MaterialPreviewTextureSlot = RenderStandardMaterialTextureSlot;
using MaterialPreviewTextureSnapshot =
    RenderMaterialTextureEvaluationSnapshot;
using StandardMaterialPreviewSnapshot = RenderMaterialEvaluationSnapshot;

/**
 * Owns the Filament scene and GPU resources for one isolated material preview.
 */
class MaterialPreviewRenderer final {
public:
  MaterialPreviewRenderer();
  ~MaterialPreviewRenderer();

  MaterialPreviewRenderer(const MaterialPreviewRenderer &) = delete;
  MaterialPreviewRenderer &operator=(const MaterialPreviewRenderer &) = delete;
  MaterialPreviewRenderer(MaterialPreviewRenderer &&) = delete;
  MaterialPreviewRenderer &operator=(MaterialPreviewRenderer &&) = delete;

  [[nodiscard]] bool initialize(
      filament::Engine &engine, filament::Material &shaded_mesh_material,
      filament::Material &rendered_mesh_material,
      const std::filesystem::path &resource_root);
  void shutdown();

  void resize(std::uint32_t width, std::uint32_t height);
  void set_srgb_swapchain(bool enabled);
  void set_preview_state(const MaterialPreviewState &state);
  void set_material(StandardMaterialPreviewSnapshot snapshot);
  void render(filament::Renderer &renderer);

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
