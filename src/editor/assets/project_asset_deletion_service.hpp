////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/session/project_session.hpp"
#include "io/project/project_asset_dependency_types.hpp"
#include "io/project/project_asset_recycle.hpp"

#include <functional>
#include <span>
#include <utility>

namespace quader {

/// Exact deletion selection and dependencies shown before a destructive action.
struct ProjectAssetDeletionPreview final {
  ProjectId project_id;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectRelativePath> paths;
  std::vector<SceneId> scene_ids;
  std::vector<ProjectAssetId> material_ids;
  std::vector<ProjectAssetId> texture_ids;
  std::vector<ProjectSceneAssetDependencies> affected_scenes;
  std::vector<ProjectMaterialAssetDependencies> affected_materials;
  std::vector<SceneSessionId> dirty_scene_sessions;
  std::vector<std::string> errors;
  bool discards_material_draft = false;
};

/// Deterministic failure seams exercise recovery without invoking the OS shell.
struct ProjectAssetDeletionRuntimeHooks final {
  std::function<void()> after_dependency_publication;
  std::function<void()> after_recycle;
};

/// Recycles mixed assets only after preparing exact reversible dependency edits.
class ProjectAssetDeletionService final {
public:
  explicit ProjectAssetDeletionService(
      ProjectSession &project, ProjectAssetRecyclePort recycle = {},
      ProjectAssetDeletionRuntimeHooks hooks = {})
      : project_(&project), recycle_(std::move(recycle)), hooks_(std::move(hooks)) {}

  [[nodiscard]] ProjectAssetDeletionPreview
  preview(std::span<const ProjectRelativePath> paths) const;
  /// Dirty scene decisions must have been collected for precisely this preview.
  [[nodiscard]] ProjectSessionResult erase(
      std::span<const ProjectRelativePath> paths, std::uint64_t preview_revision,
      std::span<const SceneSessionId> approved_dirty_scenes = {}) const;

  /// Runs before material catalogs and scene documents are opened.
  [[nodiscard]] static ProjectIoResult<void>
  recover(ProjectWorkspaceStorage &workspace);

private:
  ProjectSession *project_;
  ProjectAssetRecyclePort recycle_;
  ProjectAssetDeletionRuntimeHooks hooks_;
};

} // namespace quader
