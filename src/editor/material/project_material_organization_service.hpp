////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class ProjectSession;
struct ProjectSessionResult;

/// Reports a material-file organization mutation and the affected identities.
struct ProjectMaterialOrganizationResult final {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> warnings;
};

/// Publishes folder and material file organization through the project catalog.
class ProjectMaterialOrganizationService final {
public:
  explicit ProjectMaterialOrganizationService(ProjectSession &project)
      : project_(&project) {}

  [[nodiscard]] ProjectSessionResult create_folder(std::string_view parent,
                                                   std::string_view name) const;
  [[nodiscard]] ProjectSessionResult rename_folder(std::string_view path,
                                                   std::string_view name) const;
  [[nodiscard]] ProjectSessionResult
  move_folder(std::string_view path, std::string_view destination_parent) const;
  [[nodiscard]] ProjectSessionResult
  move_materials(std::span<const ProjectAssetId> materials,
                 std::string_view destination) const;
  [[nodiscard]] ProjectMaterialOrganizationResult
  rename_material(ProjectAssetId material, std::string_view name) const;
  [[nodiscard]] ProjectMaterialOrganizationResult
  duplicate_materials(std::span<const ProjectAssetId> materials) const;
  [[nodiscard]] ProjectMaterialOrganizationResult
  copy_materials(std::span<const ProjectAssetId> materials,
                 std::string_view destination) const;

private:
  [[nodiscard]] ProjectSessionResult
  relocate_folder(std::string_view source, std::string_view destination) const;
  [[nodiscard]] ProjectMaterialOrganizationResult
  clone_materials(std::span<const ProjectAssetId> materials,
                  std::string_view destination,
                  bool preserve_source_folders) const;

  ProjectSession *project_ = nullptr;
};

} // namespace quader
