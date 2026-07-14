////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_types.hpp"

#include <filesystem>
#include <span>
#include <utility>

namespace quader {

/**
 * Resolves all project-owned paths against one canonical root. This is the
 * storage authority for containment, portable case uniqueness, and link escape
 * validation; callers must not concatenate project paths directly.
 */
class ProjectFileSystem final {
public:
  ProjectFileSystem() = default;

  [[nodiscard]] static ProjectIoResult<ProjectFileSystem>
  open(const std::filesystem::path &root);

  [[nodiscard]] bool valid() const noexcept { return !root_.empty(); }
  [[nodiscard]] const std::filesystem::path &root() const noexcept {
    return root_;
  }

  /** Resolves a path without requiring its final entry to exist. */
  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve(const ProjectRelativePath &path) const;

  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve_existing_file(const ProjectRelativePath &path) const;

  /** Resolves a writable file path and requires an existing parent folder. */
  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve_for_write(const ProjectRelativePath &path) const;

  /**
   * Resolves the physical target after following in-project symlinks and
   * junctions. The final entry does not need to exist.
   */
  [[nodiscard]] ProjectIoResult<std::filesystem::path>
  resolve_physical_location(const ProjectRelativePath &path) const;

  /**
   * Reports whether two project paths name the same physical target. This
   * follows directory links and also recognizes existing hard-linked files.
   */
  [[nodiscard]] ProjectIoResult<bool>
  refers_to_same_location(const ProjectRelativePath &left,
                          const ProjectRelativePath &right) const;

  /** Converts an absolute dialog result into a validated project path. */
  [[nodiscard]] ProjectIoResult<ProjectRelativePath>
  relative_from_absolute(const std::filesystem::path &path) const;

  /** Enforces Windows-portable case uniqueness even on case-sensitive hosts. */
  [[nodiscard]] ProjectIoResult<void> validate_no_case_collisions(
      std::span<const ProjectRelativePath> paths) const;

  /** Rejects portable-case and in-project link aliases in one path set. */
  [[nodiscard]] ProjectIoResult<void> validate_no_path_aliases(
      std::span<const ProjectRelativePath> paths) const;

private:
  explicit ProjectFileSystem(std::filesystem::path root)
      : root_(std::move(root)) {}

  [[nodiscard]] ProjectIoResult<void>
  validate_existing_component_case(const ProjectRelativePath &path) const;

  std::filesystem::path root_;
};

} // namespace quader
