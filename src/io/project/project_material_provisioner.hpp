////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_material_catalog.hpp"

#include <filesystem>
#include <vector>

namespace quader {

/// Exact filesystem mutations made by default-material provisioning.
struct ProjectMaterialProvisioningChanges final {
  std::vector<std::filesystem::path> created_files;
  std::vector<std::filesystem::path> created_directories;

  [[nodiscard]] bool empty() const noexcept {
    return created_files.empty() && created_directories.empty();
  }
};

/**
 * Installs the built-in QMAT and albedo into new and legacy projects.
 * The reserved material identity, kind, and path are immutable; its authored
 * Standard payload remains user-editable. Existing files are never overwritten.
 */
class ProjectMaterialProvisioner final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectMaterialProvisioningChanges>
  ensure_default(const ProjectFileSystem &file_system,
                 const std::filesystem::path &resource_root,
                 const ProjectMaterialCatalog &catalog);

  static void rollback(
      const ProjectMaterialProvisioningChanges &changes) noexcept;
};

} // namespace quader
