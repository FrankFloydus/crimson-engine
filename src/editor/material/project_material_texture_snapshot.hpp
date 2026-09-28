////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_material_document.hpp"
#include "io/project/project_types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace quader {

/// Immutable project texture data shared by editor properties and rendering.
struct ProjectMaterialTextureSnapshot final {
  ProjectAssetId texture_id;
  std::string project_relative_path;
  std::uint64_t content_revision = 0u;
  std::shared_ptr<const std::vector<std::byte>> encoded;
  std::shared_ptr<const std::vector<std::byte>> decoded;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::string detected_format;
  std::uintmax_t encoded_byte_size = 0u;
  bool has_alpha_channel = false;
};

/// Validated committed albedo metadata backed by the project texture cache.
struct ProjectMaterialAlbedoTextureInfo final {
  ProjectAssetId material_id;
  ProjectRelativePath project_relative_path;
  std::filesystem::path absolute_path;
  std::shared_ptr<const ProjectMaterialTextureSnapshot> texture;
};

} // namespace quader
