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
  void *owner_window_handle = nullptr;
  std::filesystem::path initial_directory;
  std::filesystem::path initial_file_name;
};

[[nodiscard]] std::optional<std::filesystem::path>
platform_open_project_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_choose_project_folder_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_project_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_scene_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_scene_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_material_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_material_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_open_texture_file_dialog(const PlatformFileDialogOptions &options);
[[nodiscard]] std::optional<std::filesystem::path>
platform_save_obj_file_dialog(const PlatformFileDialogOptions &options);

} // namespace quader
