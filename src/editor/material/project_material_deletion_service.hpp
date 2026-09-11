////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/session/project_session.hpp"
#include "io/project/project_asset_dependency_types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

/// Binds one permanent-deletion confirmation to the inspected project sources.
struct ProjectMaterialFolderDeletionPreview final {
  ProjectRelativePath folder;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> material_names;
  std::vector<ProjectSceneAssetDependencies> affected_scenes;
  std::vector<std::string> errors;
  std::size_t folder_count = 0u;
  bool discards_material_draft = false;
};

/// Binds one material-selection deletion confirmation to inspected sources.
struct ProjectMaterialDeletionPreview final {
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> material_names;
  std::vector<ProjectSceneAssetDependencies> affected_scenes;
  std::vector<std::string> errors;
  bool discards_material_draft = false;
};

/// Supplies deterministic transaction-boundary observation for owner tests.
struct ProjectMaterialDeletionRuntimeHooks final {
  std::function<void()> after_quarantine;
};

/// Coordinates permanent material removal across saved and resident documents.
class ProjectMaterialDeletionService final {
public:
  explicit ProjectMaterialDeletionService(
      ProjectSession &project,
      ProjectMaterialDeletionRuntimeHooks hooks = {})
      : project_(&project), hooks_(std::move(hooks)) {}

  [[nodiscard]] ProjectMaterialFolderDeletionPreview
  preview_folder(std::string_view folder) const;
  [[nodiscard]] ProjectMaterialDeletionPreview
  preview_materials(std::span<const ProjectAssetId> material_ids) const;
  [[nodiscard]] ProjectSessionResult
  delete_folder(std::string_view folder, std::uint64_t preview_revision) const;
  [[nodiscard]] ProjectSessionResult
  delete_materials(std::span<const ProjectAssetId> material_ids,
                   std::uint64_t preview_revision) const;

  /// Recovers interrupted deletion before catalogs or scene documents are opened.
  [[nodiscard]] static ProjectIoResult<void>
  recover(ProjectWorkspaceStorage &workspace);

private:
  [[nodiscard]] ProjectSessionResult delete_preview(
      const ProjectMaterialDeletionPreview &preview,
      const ProjectMaterialFolderDeletionPreview *folder_preview) const;

  ProjectSession *project_;
  ProjectMaterialDeletionRuntimeHooks hooks_;
};

} // namespace quader
