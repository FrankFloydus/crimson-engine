////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"
#include "io/project/project_material_catalog.hpp"

#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace quader {

class ProjectSession;

/// One source image that became a durable project Standard Material.
struct ProjectMaterialImportSuccess final {
  std::filesystem::path source_path;
  ProjectMaterialCatalogEntry catalog_entry;
};

/// One source image rejected without preventing the rest of the batch.
struct ProjectMaterialImportFailure final {
  std::filesystem::path source_path;
  std::string error_message;
};

/// Internal transactional result consumed by the public project-assets facade.
struct ProjectMaterialImportReport final {
  bool ok = false;
  std::string message;
  std::vector<ProjectMaterialImportSuccess> imported;
  std::vector<ProjectMaterialImportFailure> failed;

  [[nodiscard]] bool changed() const noexcept { return !imported.empty(); }
};

/// Imports each still image as the sRGB albedo of one new Standard Material.
class ProjectMaterialImportService final {
public:
  explicit ProjectMaterialImportService(ProjectSession &project)
      : project_(&project) {}

  [[nodiscard]] ProjectMaterialImportReport import_textures_as_materials(
      std::span<const std::filesystem::path> source_paths) const;

private:
  ProjectSession *project_ = nullptr;
};

} // namespace quader
