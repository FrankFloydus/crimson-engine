////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_types.hpp"
#include "core/types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quader {

class ProjectSession;

/// A source that prevented a complete classification of unused materials.
struct ProjectMaterialUsageIssue final {
  SceneId scene_id;
  std::optional<SceneSessionId> session_id;
  std::string project_relative_path;
  std::string scene_name;
  std::string message;

  friend bool operator==(const ProjectMaterialUsageIssue &,
                         const ProjectMaterialUsageIssue &) = default;
};

/// Actual assigned surfaces, independently derived from deletion dependencies.
struct ProjectMaterialUsageSnapshot final {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  // Embedded identities belong to one scene; only its browser can display them.
  std::vector<AssetId> active_scene_embedded_material_ids;
  std::vector<ProjectMaterialUsageIssue> issues;
};

/// Observable derived-work counters used to protect retained refresh contracts.
struct ProjectMaterialUsageWorkStats final {
  std::uint64_t snapshot_build_count = 0u;
  std::uint64_t live_scene_scan_count = 0u;
  std::uint64_t saved_scene_read_count = 0u;
  std::uint64_t file_stamp_read_count = 0u;
  std::uint64_t geometry_slot_scan_count = 0u;

  friend bool operator==(const ProjectMaterialUsageWorkStats &,
                         const ProjectMaterialUsageWorkStats &) = default;
};

/// Retains actual project usage without loading textures or compiling geometry.
class ProjectMaterialUsageService final {
public:
  explicit ProjectMaterialUsageService(ProjectSession &project);
  ~ProjectMaterialUsageService();

  ProjectMaterialUsageService(const ProjectMaterialUsageService &) = delete;
  ProjectMaterialUsageService &operator=(const ProjectMaterialUsageService &) = delete;

  [[nodiscard]] std::shared_ptr<const ProjectMaterialUsageSnapshot>
  snapshot(bool refresh_files = false) const;
  void invalidate() noexcept;
  [[nodiscard]] const ProjectMaterialUsageWorkStats &work_stats() const noexcept;

private:
  struct State;
  std::unique_ptr<State> state_;
};

} // namespace quader
