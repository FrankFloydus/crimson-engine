////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"
#include "render/render_view.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace filament {
class Engine;
class Material;
class Texture;
} // namespace filament

namespace quader {

/// One retained offscreen sphere and at most one asynchronous GPU readback.
class MaterialThumbnailRenderer final {
public:
  static constexpr std::uint32_t kMaximumSize = kMaterialThumbnailMaximumSize;
  static constexpr std::uint32_t kPresetRevision = kMaterialThumbnailPresetRevision;

  MaterialThumbnailRenderer();
  ~MaterialThumbnailRenderer();
  MaterialThumbnailRenderer(const MaterialThumbnailRenderer &) = delete;
  MaterialThumbnailRenderer &operator=(const MaterialThumbnailRenderer &) = delete;

  [[nodiscard]] bool initialize(
      filament::Engine &engine, filament::Material &shaded_material,
      filament::Material &rendered_material, filament::Texture &solid_texture,
      const std::filesystem::path &resource_root);
  /// Retires outstanding GPU work before releasing borrowed engine resources.
  void shutdown();
  [[nodiscard]] std::shared_ptr<const MaterialThumbnailReadback>
  submit(RenderMaterialEvaluationSnapshot snapshot, std::uint32_t size);
  /// Dispatches finished driver callbacks without blocking on GPU completion.
  void poll();
  [[nodiscard]] bool busy() const noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
