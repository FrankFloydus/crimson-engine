////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <filesystem>
#include <optional>

namespace quader {

/**
 * Stores native file dialog options used by the platform integration layer.
 */
struct PlatformFileDialogOptions {
    void* ownerWindowHandle = nullptr;
    std::filesystem::path initialDirectory;
    std::filesystem::path initialFileName;
};

[[nodiscard]] std::optional<std::filesystem::path> platformOpenProjectFileDialog(
        const PlatformFileDialogOptions& options);
[[nodiscard]] std::optional<std::filesystem::path> platformSaveProjectFileDialog(
        const PlatformFileDialogOptions& options);

} // namespace quader
