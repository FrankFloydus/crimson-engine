////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_manifest.hpp"
#include "io/project/project_types.hpp"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/// Identifies a saved scene in one physical project, independently of open tabs.
struct RecentSceneEntry {
  SceneId scene_id;
  std::string scene_name;
  std::string project_name;
  std::filesystem::path manifest_path;
  ProjectRelativePath scene_path;
  bool available = false;
};

/// Persists the five most recently used saved scenes in per-user storage.
class RecentSceneStore final {
public:
  static constexpr std::size_t kMaximumEntries = 5u;

  explicit RecentSceneStore(std::filesystem::path storage_path)
      : storage_path_(std::move(storage_path)) {}

  [[nodiscard]] const std::filesystem::path &storage_path() const noexcept {
    return storage_path_;
  }

  [[nodiscard]] ProjectIoResult<std::vector<RecentSceneEntry>> load() const;

  /**
   * Call after a saved scene is successfully opened, saved or edited. Repeated
   * calls for the unchanged first entry do not rewrite the storage file.
   */
  [[nodiscard]] ProjectIoResult<void>
  remember(const ProjectManifest &manifest,
           const std::filesystem::path &manifest_path, const SceneId &scene_id,
           std::string scene_name, const ProjectRelativePath &scene_path) const;

  /// Updates moved scenes and removes deleted entries without changing MRU order.
  [[nodiscard]] ProjectIoResult<void>
  synchronize_project(const ProjectManifest &manifest,
                      const std::filesystem::path &manifest_path) const;

private:
  [[nodiscard]] ProjectIoResult<void>
  save(std::span<const RecentSceneEntry> entries) const;

  std::filesystem::path storage_path_;
};

} // namespace quader
