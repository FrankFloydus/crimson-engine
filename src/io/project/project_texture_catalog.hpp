////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_manifest.hpp"

namespace quader {

class ProjectMaterialCatalog;

/// Excludes project bookkeeping and dot-prefixed paths from authored assets.
[[nodiscard]] bool project_asset_path_visible(const ProjectRelativePath &path);
/// Formats accepted by the existing image decoder; content is validated on import.
[[nodiscard]] bool
project_texture_extension_supported(const std::filesystem::path &path);

/// Builds a candidate catalog without changing the manifest or files on disk.
class ProjectTextureCatalog final {
public:
  /**
   * Registers loose images and legacy material references in a copied manifest.
   * Existing identities override stale QMAT location hints. Several legacy IDs
   * at one path become aliases; one previously unknown ID at different paths is
   * an error. The owner retains this candidate and persists it on authored save.
   */
  [[nodiscard]] static ProjectIoResult<ProjectManifest> discover(
      const ProjectFileSystem &file_system, const ProjectManifest &manifest,
      const ProjectMaterialCatalog &materials);
};

} // namespace quader
