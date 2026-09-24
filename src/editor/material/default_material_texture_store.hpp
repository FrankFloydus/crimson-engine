////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/material/project_image_decoder.hpp"

#include <filesystem>

namespace quader {

/// Validates and retains the one application-level default material image.
class DefaultMaterialTextureStore final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectImportedImage>
  validate_image(const std::filesystem::path &source_path);

  /**
   * Stores an immutable PNG under the profile's settings directory. The caller
   * publishes its preference only after its own persistence succeeds. Empty
   * input selects the packaged default without touching the filesystem.
   */
  [[nodiscard]] static ProjectIoResult<std::filesystem::path>
  import_image(const std::filesystem::path &source_path,
               const std::filesystem::path &settings_directory);
};

} // namespace quader
