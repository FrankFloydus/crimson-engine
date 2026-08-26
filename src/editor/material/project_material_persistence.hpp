////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_material_document.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/project/project_file_system.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/// One collision-checked UUID-backed destination for a project asset file.
struct ProjectMaterialAssetDestination final {
  ProjectAssetId id;
  ProjectRelativePath path;
};

[[nodiscard]] ProjectIoResult<ProjectMaterialAssetDestination>
make_unique_material_destination(const ProjectFileSystem &file_system,
                                 std::string_view source_stem);

[[nodiscard]] ProjectIoResult<ProjectMaterialAssetDestination>
make_unique_texture_destination(const ProjectFileSystem &file_system,
                                std::string_view source_stem,
                                std::string_view extension);

[[nodiscard]] ProjectIoResult<std::string>
read_project_material_file_bytes(const std::filesystem::path &path);

/**
 * Retains guarded material/texture publications until their owning catalog
 * update succeeds. Transactions roll back in reverse publication order and
 * keep their recovery state when a rollback attempt fails.
 */
class ProjectMaterialFileRollback final {
public:
  using Checkpoint = std::size_t;

  [[nodiscard]] Checkpoint checkpoint() const noexcept {
    return entries_.size();
  }

  [[nodiscard]] ProjectIoResult<void>
  create_bytes(const std::filesystem::path &path,
               std::span<const std::byte> bytes);
  [[nodiscard]] ProjectIoResult<void>
  create_qmat(const std::filesystem::path &path,
              const ProjectMaterialDocument &document);
  [[nodiscard]] ProjectIoResult<void>
  replace_qmat(const std::filesystem::path &path,
               const ProjectMaterialDocument &document);

  /// Returns an empty string on success or a combined rollback diagnostic.
  [[nodiscard]] std::string rollback_to(Checkpoint checkpoint);
  [[nodiscard]] std::string rollback() {
    return rollback_to(0u);
  }
  void commit() noexcept;

private:
  [[nodiscard]] ProjectIoResult<void> publish(
      const std::filesystem::path &path, std::string_view bytes,
      quader_io::QdrAtomicWriteValidator validator,
      quader_io::QdrAtomicWriteDisposition disposition);

  std::vector<std::unique_ptr<quader_io::QdrAtomicFileTransaction>> entries_;
};

} // namespace quader
