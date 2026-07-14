////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_types.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace quader {

inline constexpr std::uintmax_t kMaximumProjectTextureEncodedBytes =
    64u * 1024u * 1024u;
inline constexpr std::uint32_t kMaximumProjectTextureDimension = 8192u;

/// Immutable RGBA8 image projection consumed by renderer-neutral snapshots.
struct ProjectDecodedImage final {
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::vector<std::byte> rgba8;
  std::string detected_format;
};

/// Validated external texture import while its original encoded bytes remain intact.
struct ProjectImportedImage final {
  std::filesystem::path source_path;
  std::string source_stem;
  std::string source_extension;
  std::vector<std::byte> encoded_bytes;
  ProjectDecodedImage decoded;
};

/// Validates and decodes still-image texture imports without changing files.
class ProjectImageDecoder final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectDecodedImage>
  decode(std::span<const std::byte> encoded_bytes);

  [[nodiscard]] static ProjectIoResult<ProjectImportedImage>
  read_import(const std::filesystem::path &source_path);
};

} // namespace quader
