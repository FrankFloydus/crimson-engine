////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_manifest.hpp"
#include "io/project/project_types.hpp"

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/// Stores one normalized entry in the per-user recent-project list.
struct RecentProjectEntry {
  ProjectId project_id;
  std::string name;
  std::filesystem::path manifest_path;
  std::uint64_t last_opened_unix_ms = 0u;
  bool available = false;
  std::optional<std::uint64_t> manifest_last_modified_unix_ms;
  std::optional<std::uintmax_t> manifest_size_bytes;
  std::string last_saved_app_version;
};

/**
 * Persists the per-user recent project list. The caller supplies a path below
 * LocalAppData so this storage owner remains independent of Qt and app startup.
 */
class RecentProjectStore final {
public:
  static constexpr std::size_t kMaximumEntries = 20u;

  explicit RecentProjectStore(std::filesystem::path storage_path)
      : storage_path_(std::move(storage_path)) {}

  [[nodiscard]] const std::filesystem::path &storage_path() const noexcept {
    return storage_path_;
  }

  [[nodiscard]] ProjectIoResult<std::vector<RecentProjectEntry>> load() const;

  /** Call only after a project has been successfully created or opened. */
  [[nodiscard]] ProjectIoResult<void>
  remember(const ProjectManifest &manifest,
           const std::filesystem::path &manifest_path) const;

  [[nodiscard]] ProjectIoResult<void>
  remove(const std::filesystem::path &manifest_path) const;

private:
  [[nodiscard]] ProjectIoResult<void>
  save(std::span<const RecentProjectEntry> entries) const;

  std::filesystem::path storage_path_;
};

} // namespace quader
