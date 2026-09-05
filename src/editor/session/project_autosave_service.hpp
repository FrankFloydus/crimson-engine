////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/session_preferences.hpp"
#include "editor/session/scene_session.hpp"
#include "io/project/project_types.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class ProjectSession;
struct ProjectSessionResult;

/// One recoverable scene copy, distinct from its authored file and live tab.
struct ProjectAutosaveEntry final {
  ProjectRelativePath path;
  SceneId scene_id;
  std::string scene_name;
  std::optional<ProjectRelativePath> source_path;
  std::int64_t saved_at_unix_ms = 0;
};

/// Reports copies written, edits deferred, and storage warnings for one poll.
struct ProjectAutosaveResult final {
  bool ok = true;
  std::size_t written = 0;
  std::size_t deferred = 0;
  std::vector<std::string> warnings;
};

/**
 * Schedules project-contained recovery copies on the editor thread. Interval,
 * dirty and revision gates precede snapshot allocation. A copy never changes
 * the live document, clean baseline, authored path, selection or undo history.
 */
class ProjectAutosaveService final {
public:
  [[nodiscard]] ProjectAutosaveResult poll(
      ProjectSession &project, const SessionPreferences &settings,
      std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now(),
      bool external_edit_active = false);

  [[nodiscard]] ProjectIoResult<std::vector<ProjectAutosaveEntry>>
  entries(const ProjectSession &project) const;
  /// Creates and resolves the contained recovery folder for the native file picker.
  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  directory(const ProjectSession &project) const;
  [[nodiscard]] ProjectSessionResult
  recover(ProjectSession &project, const ProjectRelativePath &path) const;

private:
  /// Last published scene state used to skip unchanged recovery snapshots.
  struct SavedState final {
    SceneId scene_id;
    SceneContentRevisionStamp revision;
    std::optional<ProjectRelativePath> source_path;
    std::string scene_name;
  };

  ProjectId project_id_;
  std::filesystem::path project_root_;
  std::optional<std::chrono::steady_clock::time_point> next_attempt_;
  std::optional<SessionPreferences> settings_;
  bool retention_pending_ = false;
  std::int64_t last_saved_at_unix_ms_ = 0;
  std::unordered_map<std::uint64_t, SavedState> saved_states_;
};

} // namespace quader
