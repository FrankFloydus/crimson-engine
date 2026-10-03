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
class RenderTarget;
class Texture;
} // namespace filament

namespace quader {

using MaterialPreviewTextureSlot = RenderStandardMaterialTextureSlot;
using MaterialPreviewTextureSnapshot =
    RenderMaterialTextureEvaluationSnapshot;
using StandardMaterialPreviewSnapshot = RenderMaterialEvaluationSnapshot;

/// Keeps the asset sphere's fixed lighting separate from interactive previews.
enum class MaterialPreviewLightingPreset : std::uint8_t {
  Interactive,
  AssetThumbnail,
};

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
      filament::Texture &solid_color_texture,
      const std::filesystem::path &resource_root,
      MaterialPreviewLightingPreset lighting =
          MaterialPreviewLightingPreset::Interactive);
  void shutdown();

  void resize(std::uint32_t width, std::uint32_t height);
  void set_preview_state(const MaterialPreviewState &state);
  bool set_material(StandardMaterialPreviewSnapshot snapshot);
  void render(filament::Renderer &renderer);
  void render_offscreen(filament::Renderer &renderer,
                        filament::RenderTarget &target);

  [[nodiscard]] bool initialized() const noexcept;
  [[nodiscard]] std::uint32_t width() const noexcept;
  [[nodiscard]] std::uint32_t height() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
