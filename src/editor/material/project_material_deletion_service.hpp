////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/session/project_session.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace quader {

/// Binds one permanent-deletion confirmation to the inspected project sources.
struct ProjectMaterialFolderDeletionPreview final {
  ProjectRelativePath folder;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> material_names;
  std::vector<std::string> affected_scenes;
  std::vector<std::string> errors;
  std::size_t folder_count = 0u;
  bool discards_material_draft = false;
};

/// Coordinates permanent material removal across saved and resident documents.
class ProjectMaterialDeletionService final {
public:
  explicit ProjectMaterialDeletionService(ProjectSession &project)
      : project_(&project) {}

  [[nodiscard]] ProjectMaterialFolderDeletionPreview
  preview_folder(std::string_view folder) const;
  [[nodiscard]] ProjectSessionResult
  delete_folder(std::string_view folder, std::uint64_t preview_revision) const;

  /// Recovers interrupted deletion before catalogs or scene documents are opened.
  [[nodiscard]] static ProjectIoResult<void>
  recover(ProjectWorkspaceStorage &workspace);

private:
  ProjectSession *project_;
};

} // namespace quader
