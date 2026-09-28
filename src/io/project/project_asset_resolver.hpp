////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_file_system.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace quader {

class ProjectManifest;

/// Resolves canonical/legacy image IDs before considering an unregistered hint.
[[nodiscard]] ProjectIoResult<ProjectRelativePath> resolve_project_texture_path(
    const ProjectManifest &manifest, ProjectAssetId id,
    const ProjectRelativePath &path_hint);

/// Classifies reusable project assets without coupling to a native registry.
enum class ProjectAssetKind : std::uint8_t {
  Unknown = 0,
  Material = 1,
  Texture = 2,
  Model = 3,
  Scene = 4,
  Folder = 5,
};

/// Pairs a stable project-asset identity with its portable storage location.
struct ProjectAssetReference final {
  ProjectAssetId stable_id;
  ProjectAssetKind kind = ProjectAssetKind::Unknown;
  ProjectRelativePath path;
};

/**
 * Minimal project-asset resolution seam. It deliberately owns no registry or
 * import policy; all containment and link validation remains in
 * ProjectFileSystem.
 */
class ProjectAssetResolver final {
public:
  explicit ProjectAssetResolver(const ProjectFileSystem &file_system,
                                const ProjectManifest *manifest = nullptr)
      : file_system_(&file_system), manifest_(manifest) {}

  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve_existing(const ProjectAssetReference &reference) const;

private:
  const ProjectFileSystem *file_system_ = nullptr;
  const ProjectManifest *manifest_ = nullptr;
};

} // namespace quader
