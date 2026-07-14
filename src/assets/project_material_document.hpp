////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace quader {

inline constexpr std::uint32_t kStandardMaterialVersion = 1u;

/// Selects the type-specific payload and editor for a project material.
enum class ProjectMaterialKind : std::uint8_t {
  Standard,
  Graph,
};

/// Declares how a project texture's encoded color values are interpreted.
enum class ProjectTextureColorSpace : std::uint8_t {
  Srgb,
  Linear,
};

/// Stores one stable project texture reference from a QMAT slot.
struct ProjectMaterialTextureSlot final {
  ProjectAssetId id;
  std::string project_relative_path;
  ProjectTextureColorSpace color_space = ProjectTextureColorSpace::Srgb;

  friend bool operator==(const ProjectMaterialTextureSlot &,
                         const ProjectMaterialTextureSlot &) = default;
};

/// Selects one texture input in the Standard Material payload.
enum class StandardMaterialTextureSlot : std::uint8_t {
  Albedo,
  Roughness,
  Metallic,
  Normal,
};

/// Declares the tangent-space convention used by a Standard normal map.
enum class NormalMapConvention : std::uint8_t {
  OpenGL,
  DirectX,
};

/// Renderer-neutral parameters and dependencies for a Standard Material.
struct ProjectStandardMaterialDefinition final {
  std::array<float, 3> base_color{1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  float normal_strength = 1.0f;
  NormalMapConvention normal_convention = NormalMapConvention::OpenGL;
  std::optional<ProjectMaterialTextureSlot> base_color_texture;
  std::optional<ProjectMaterialTextureSlot> roughness_texture;
  std::optional<ProjectMaterialTextureSlot> metallic_texture;
  std::optional<ProjectMaterialTextureSlot> normal_texture;

  [[nodiscard]] const std::optional<ProjectMaterialTextureSlot> *
  texture(StandardMaterialTextureSlot slot) const noexcept {
    switch (slot) {
    case StandardMaterialTextureSlot::Albedo:
      return &base_color_texture;
    case StandardMaterialTextureSlot::Roughness:
      return &roughness_texture;
    case StandardMaterialTextureSlot::Metallic:
      return &metallic_texture;
    case StandardMaterialTextureSlot::Normal:
      return &normal_texture;
    }
    return nullptr;
  }

  [[nodiscard]] std::optional<ProjectMaterialTextureSlot> *
  texture(StandardMaterialTextureSlot slot) noexcept {
    switch (slot) {
    case StandardMaterialTextureSlot::Albedo:
      return &base_color_texture;
    case StandardMaterialTextureSlot::Roughness:
      return &roughness_texture;
    case StandardMaterialTextureSlot::Metallic:
      return &metallic_texture;
    case StandardMaterialTextureSlot::Normal:
      return &normal_texture;
    }
    return nullptr;
  }

  friend bool operator==(const ProjectStandardMaterialDefinition &,
                         const ProjectStandardMaterialDefinition &) =
      default;
};

using ProjectMaterialPayload =
    std::variant<ProjectStandardMaterialDefinition>;

/// Common identity plus one explicitly typed, versioned QMAT payload.
struct ProjectMaterialDocument final {
  ProjectAssetId id;
  std::string name;
  ProjectMaterialKind kind = ProjectMaterialKind::Standard;
  std::uint32_t material_version = kStandardMaterialVersion;
  ProjectMaterialPayload payload = ProjectStandardMaterialDefinition{};

  [[nodiscard]] const ProjectStandardMaterialDefinition *
  standard_material() const noexcept {
    if (kind != ProjectMaterialKind::Standard) {
      return nullptr;
    }
    return std::get_if<ProjectStandardMaterialDefinition>(&payload);
  }

  [[nodiscard]] ProjectStandardMaterialDefinition *
  standard_material() noexcept {
    if (kind != ProjectMaterialKind::Standard) {
      return nullptr;
    }
    return std::get_if<ProjectStandardMaterialDefinition>(&payload);
  }

  friend bool operator==(const ProjectMaterialDocument &,
                         const ProjectMaterialDocument &) = default;
};

} // namespace quader
