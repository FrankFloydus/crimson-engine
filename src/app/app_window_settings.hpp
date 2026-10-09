////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "platform/window_placement.hpp"

#include <filesystem>
#include <optional>

namespace quader {

/**
 * Owns persisted application window settings for the native app shell.
 */
class AppWindowSettings {
public:
    static AppWindowSettings defaults();
    static AppWindowSettings load(const std::filesystem::path& path);
    static std::filesystem::path
    resolve_path(const std::filesystem::path &executable_dir);
    /// Per-user placement keyed by canonical manifest path; Home has no file.
    static std::filesystem::path resolve_project_path(
        const std::filesystem::path &executable_dir,
        const std::filesystem::path &manifest_path);

    bool save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::optional<PlatformWindowPlacement>& placement() const { return placement_; }
    void set_placement(PlatformWindowPlacement placement);

  private:
    std::optional<PlatformWindowPlacement> placement_;
};

} // namespace quader
