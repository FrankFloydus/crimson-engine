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

/// Classifies reusable project assets without coupling to a native registry.
enum class ProjectAssetKind : std::uint8_t {
  Unknown = 0,
  Material = 1,
  Texture = 2,
  Model = 3,
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
  explicit ProjectAssetResolver(const ProjectFileSystem &file_system)
      : file_system_(&file_system) {}

  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve_existing(const ProjectAssetReference &reference) const;

private:
  const ProjectFileSystem *file_system_ = nullptr;
};

} // namespace quader
