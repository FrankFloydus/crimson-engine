////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace quader {

/**
 * Stores native file dialog options used by the platform integration layer.
 */
struct PlatformFileDialogOptions {
  void *owner_window_handle = nullptr;
  std::filesystem::path initial_directory;
  std::filesystem::path initial_file_name;
};

/// Distinguishes cancellation from failure to open or read a native dialog.
struct PlatformFileDialogResult {
  std::optional<std::filesystem::path> path;
  std::string error_message;
};

[[nodiscard]] std::optional<std::filesystem::path>
platform_open_project_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_choose_project_folder_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_project_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_scene_file_dialog(const PlatformFileDialogOptions &options);
/// Always starts in the supplied recovery folder, ignoring remembered folders.
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_scene_recovery_file_dialog(const PlatformFileDialogOptions &options);
/// Requires the supplied starting folder; remembered locations never replace it.
[[nodiscard]] PlatformFileDialogResult
platform_save_scene_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_material_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_material_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_texture_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::vector<std::filesystem::path>>
platform_open_texture_files_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_obj_file_dialog(const PlatformFileDialogOptions &options);

namespace platform_file_dialog_detail {

/** Parses the two buffer layouts returned by an Explorer-style open dialog. */
[[nodiscard]] std::optional<std::vector<std::filesystem::path>>
parse_explorer_file_dialog_selection(bool accepted,
                                     std::span<const wchar_t> buffer);

/** Chooses a larger character buffer after FNERR_BUFFERTOOSMALL. */
[[nodiscard]] std::optional<std::size_t>
next_file_dialog_buffer_capacity(std::size_t current_capacity,
                                 std::size_t reported_required_capacity);

} // namespace platform_file_dialog_detail

} // namespace quader
