////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_material_document.hpp"
#include "io/project/project_file_system.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace quader {

inline constexpr std::string_view kQuaderMaterialFormat = "quader.material";
inline constexpr std::uint32_t kQuaderMaterialLegacyVersion = 1u;
inline constexpr std::uint32_t kQuaderMaterialVersion = 2u;
inline constexpr std::uintmax_t kMaximumQmatBytes = 4u * 1024u * 1024u;

/// Combines decoded material data with file metadata used by the catalog.
struct QmatDecodedFile final {
  ProjectMaterialDocument material;
  std::uintmax_t file_size = 0u;
  std::uint64_t content_revision = 0u;
};

/// Deterministic JSON codec and durable file adapter for typed `.qmat` files.
class QmatCodec final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectMaterialDocument>
  decode(std::string_view text);
  [[nodiscard]] static ProjectIoResult<std::string>
  encode(const ProjectMaterialDocument &material);

  [[nodiscard]] static ProjectIoResult<QmatDecodedFile>
  read(const ProjectFileSystem &file_system,
       const ProjectRelativePath &path);
  [[nodiscard]] static ProjectIoResult<void>
  write_atomic(const ProjectFileSystem &file_system,
               const ProjectRelativePath &path,
               const ProjectMaterialDocument &material);
  /// Atomically creates a QMAT and fails rather than replacing any existing file.
  [[nodiscard]] static ProjectIoResult<void>
  create_atomic(const ProjectFileSystem &file_system,
                const ProjectRelativePath &path,
                const ProjectMaterialDocument &material);
};

} // namespace quader
