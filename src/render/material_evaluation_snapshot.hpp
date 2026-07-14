////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"
#include "core/color_space.hpp"
#include "math/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

namespace quader {

/// Produces a stable runtime cache identity from one persistent project UUID.
inline std::uint64_t
render_project_asset_identity_revision(ProjectAssetId id) noexcept {
  std::uint64_t hash = 14'695'981'039'346'656'037ull;
  for (const char value : id.str()) {
    hash ^= static_cast<unsigned char>(value);
    hash *= 1'099'511'628'211ull;
  }
  return hash;
}

/// Selects the typed renderer payload used to evaluate one material.
enum class RenderMaterialEvaluationKind : std::uint8_t {
  Standard,
  Graph,
};

/// Identifies one sampler in a renderer-neutral Standard Material payload.
enum class RenderStandardMaterialTextureSlot : std::uint8_t {
  Albedo,
  Roughness,
  Metallic,
  Normal,
};

/// Carries immutable encoded or decoded pixels across the render boundary.
struct RenderMaterialTextureEvaluationSnapshot final {
  ProjectAssetId project_texture_id;
  std::uint64_t identity_revision = 0u;
  std::uint64_t content_revision = 0u;
  TextureColorSpace color_space = TextureColorSpace::Srgb;
  std::shared_ptr<const std::vector<std::byte>> encoded_file_bytes;
  std::shared_ptr<const std::vector<std::byte>> decoded_rgba8;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;

  [[nodiscard]] bool decoded_present() const noexcept {
    if (decoded_rgba8 == nullptr || width == 0u || height == 0u) {
      return false;
    }
    const std::uint64_t pixel_count = static_cast<std::uint64_t>(width) *
                                      static_cast<std::uint64_t>(height);
    if (pixel_count >
        static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) /
            4u) {
      return false;
    }
    return decoded_rgba8->size() ==
           static_cast<std::size_t>(pixel_count * 4u);
  }

  [[nodiscard]] bool encoded_present() const noexcept {
    return encoded_file_bytes != nullptr && !encoded_file_bytes->empty();
  }

  [[nodiscard]] bool present() const noexcept {
    return decoded_present() || encoded_present();
  }

  friend bool operator==(
      const RenderMaterialTextureEvaluationSnapshot &,
      const RenderMaterialTextureEvaluationSnapshot &) = default;
};

/// Typed metallic-roughness evaluation consumed by preview and scene renderers.
struct RenderStandardMaterialEvaluationSnapshot final {
  QVec3 base_color = {1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  float normal_strength = 1.0f;
  bool directx_normal = false;
  std::array<RenderMaterialTextureEvaluationSnapshot, 4> textures;

  [[nodiscard]] const RenderMaterialTextureEvaluationSnapshot &
  texture(RenderStandardMaterialTextureSlot slot) const noexcept {
    return textures[static_cast<std::size_t>(slot) < textures.size()
                        ? static_cast<std::size_t>(slot)
                        : 0u];
  }

  friend bool operator==(
      const RenderStandardMaterialEvaluationSnapshot &left,
      const RenderStandardMaterialEvaluationSnapshot &right) {
    return left.base_color.x == right.base_color.x &&
           left.base_color.y == right.base_color.y &&
           left.base_color.z == right.base_color.z &&
           left.roughness == right.roughness &&
           left.metallic == right.metallic &&
           left.normal_strength == right.normal_strength &&
           left.directx_normal == right.directx_normal &&
           left.textures == right.textures;
  }
};

using RenderMaterialEvaluationPayload =
    std::variant<std::monostate, RenderStandardMaterialEvaluationSnapshot>;

/**
 * One typed, immutable material evaluation independent of QMAT and Qt.
 *
 * Graph compilation can add its output payload to the variant without
 * changing scene references, project identities, or renderer cache keys.
 */
struct RenderMaterialEvaluationSnapshot final {
  ProjectAssetId project_material_id;
  RenderMaterialEvaluationKind kind = RenderMaterialEvaluationKind::Standard;
  std::uint64_t identity_revision = 0u;
  std::uint64_t content_revision = 0u;
  RenderMaterialEvaluationPayload payload =
      RenderStandardMaterialEvaluationSnapshot{};

  [[nodiscard]] const RenderStandardMaterialEvaluationSnapshot *
  standard_material() const noexcept {
    return kind == RenderMaterialEvaluationKind::Standard
               ? std::get_if<RenderStandardMaterialEvaluationSnapshot>(
                     &payload)
               : nullptr;
  }

  [[nodiscard]] RenderStandardMaterialEvaluationSnapshot *
  standard_material() noexcept {
    return kind == RenderMaterialEvaluationKind::Standard
               ? std::get_if<RenderStandardMaterialEvaluationSnapshot>(
                     &payload)
               : nullptr;
  }

  friend bool operator==(const RenderMaterialEvaluationSnapshot &,
                         const RenderMaterialEvaluationSnapshot &) = default;
};

} // namespace quader
