////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <vector>

namespace quader {

/// Reports actual completion for an original project path, never queue success.
struct ProjectAssetRecycleItem final {
  std::filesystem::path original_path;
  bool recycled = false;
  std::string message;
  /// Shell-reported restoration folder, when exposed by the Recycle Bin item.
  std::filesystem::path restore_directory;
  /// Actual recycled destination, empty when the shell exposes no filesystem path.
  std::filesystem::path recycled_path;
};

/// A partial shell operation is failure even when some entries were recycled.
struct ProjectAssetRecycleResult final {
  bool ok = false;
  bool aborted = false;
  std::string message;
  std::vector<ProjectAssetRecycleItem> items;
};

/// The platform implementation must recycle original paths without deleting fallback.
using ProjectAssetRecyclePort = std::function<ProjectAssetRecycleResult(
    std::span<const std::filesystem::path>)>;

} // namespace quader
