////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/qmat_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class ProjectManifest;

/// Rejects case, symbolic-link and hard-link aliases among existing asset files.
[[nodiscard]] ProjectIoResult<void> validate_project_asset_file_aliases(
    const ProjectFileSystem &file_system,
    std::span<const ProjectRelativePath> paths);

/// Stores one validated QMAT and its deterministic catalog metadata.
struct ProjectMaterialCatalogEntry final {
  ProjectMaterialDocument material;
  ProjectRelativePath path;
  std::uintmax_t file_size = 0u;
  std::uint64_t content_revision = 0u;

  friend bool operator==(const ProjectMaterialCatalogEntry &,
                         const ProjectMaterialCatalogEntry &) = default;
};

/// Carries encoded browser-preview bytes and their content identity.
struct ProjectMaterialPreview final {
  std::vector<std::byte> encoded_bytes;
  std::uint64_t content_revision = 0u;
};

/// Carries validated encoded texture bytes for native render adaptation.
struct ProjectMaterialTextureData final {
  ProjectAssetId id;
  ProjectTextureColorSpace color_space = ProjectTextureColorSpace::Srgb;
  std::vector<std::byte> encoded_bytes;
  std::uint64_t content_revision = 0u;
};

/// Validated, deterministic projection of project-owned QMAT files.
class ProjectMaterialCatalog final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectMaterialCatalog>
  scan(const ProjectFileSystem &file_system,
       ProjectFileSystemWorkStats *work_stats = nullptr,
       const ProjectManifest *manifest = nullptr);

  [[nodiscard]] std::span<const ProjectMaterialCatalogEntry>
  entries() const noexcept {
    return entries_;
  }
  /// Includes every visible project folder, even when empty; root is implicit.
  [[nodiscard]] std::span<const ProjectRelativePath> folders() const noexcept {
    return folders_;
  }
  [[nodiscard]] std::span<const std::string> warnings() const noexcept {
    return warnings_;
  }
  [[nodiscard]] std::uint64_t content_revision() const noexcept {
    return content_revision_;
  }
  /// Material rows only; folder organization does not change this projection.
  [[nodiscard]] std::uint64_t material_revision() const noexcept {
    return material_revision_;
  }
  [[nodiscard]] const ProjectMaterialCatalogEntry *
  find(ProjectAssetId id) const noexcept;

  [[nodiscard]] ProjectIoResult<ProjectMaterialPreview>
  read_preview(const ProjectFileSystem &file_system,
               ProjectAssetId id, const ProjectManifest *manifest = nullptr) const;
  [[nodiscard]] ProjectIoResult<ProjectMaterialTextureData>
  read_texture(const ProjectFileSystem &file_system, ProjectAssetId id,
               StandardMaterialTextureSlot slot,
               const ProjectManifest *manifest = nullptr) const;

private:
  std::vector<ProjectMaterialCatalogEntry> entries_;
  std::vector<ProjectRelativePath> folders_;
  std::unordered_map<ProjectAssetId, std::size_t, ProjectAssetIdHash>
      entry_index_by_id_;
  std::vector<std::string> warnings_;
  std::uint64_t content_revision_ = 0u;
  std::uint64_t material_revision_ = 0u;
};

} // namespace quader
