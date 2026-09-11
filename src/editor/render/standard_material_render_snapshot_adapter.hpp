////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/project_material_resolver.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace quader {

class ProjectSession;

/**
 * API-free Standard Material values shared by project and draft render
 * adapters. Source owners convert into this neutral record; the projection to
 * renderer fields remains centralized here.
 */
struct StandardMaterialRenderValues final {
  std::array<float, 3> base_color{1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  float normal_strength = 1.0f;
  bool directx_normal = false;
  std::array<float, 2> texture_center{0.5f, 0.5f};
  std::array<float, 2> texture_scale{1.0f, 1.0f};
  std::array<float, 2> texture_offset{0.0f, 0.0f};
  float texture_rotation_degrees = 0.0f;
  MaterialDiffuseModel diffuse_model = MaterialDiffuseModel::Burley;
};

[[nodiscard]] RenderStandardMaterialEvaluationSnapshot
make_standard_material_render_evaluation(
    const StandardMaterialRenderValues &values);

/**
 * Adapts validated project-owned material state to immutable render snapshots.
 *
 * The project session owns catalog and decoded-image caching. This adapter owns
 * the one projection policy used by scene rendering, so application bootstrap
 * never rebuilds material payloads itself.
 */
class ProjectSessionRenderMaterialResolver final
    : public RenderProjectMaterialResolver {
public:
  explicit ProjectSessionRenderMaterialResolver(ProjectSession &project)
      : project_(&project) {}

  [[nodiscard]] std::uint64_t revision() const noexcept override;
  [[nodiscard]] ProjectAssetId
  canonical_material_id(ProjectAssetId id) const override;
  [[nodiscard]] std::optional<RenderProjectMaterialSnapshot>
  resolve(ProjectAssetId id) const override;

private:
  ProjectSession *project_ = nullptr;
};

} // namespace quader
