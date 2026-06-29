////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "platform/platform_window.hpp"

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
    static std::filesystem::path resolvePath(const std::filesystem::path& executableDir);

    bool save(const std::filesystem::path& path) const;

    [[nodiscard]] const std::optional<PlatformWindowPlacement>& placement() const { return placement_; }
    void setPlacement(PlatformWindowPlacement placement);

private:
    std::optional<PlatformWindowPlacement> placement_;
};

} // namespace quader
