////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_file_system.hpp"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace quader {
namespace {

std::wstring case_key(const std::filesystem::path &path) {
  std::wstring key = path.generic_wstring();
  std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
    return static_cast<wchar_t>(std::towlower(value));
  });
  return key;
}

bool same_portable_component(const std::filesystem::path &left,
                             const std::filesystem::path &right) {
  return case_key(left) == case_key(right);
}

bool path_is_within(const std::filesystem::path &candidate,
                    const std::filesystem::path &root) {
  auto candidate_part = candidate.begin();
  for (auto root_part = root.begin(); root_part != root.end(); ++root_part) {
    if (candidate_part == candidate.end() ||
        !same_portable_component(*candidate_part, *root_part)) {
      return false;
    }
    ++candidate_part;
  }
  return true;
}

std::string filesystem_error_message(std::string_view action,
                                     const std::error_code &error) {
  if (!error) {
    return std::string(action);
  }
  return std::string(action) + ": " + error.message();
}

} // namespace

/// Shares directory-name indexes across one batch of project path resolutions.
struct ProjectFileSystem::ResolutionBatchCache final {
  /// Holds exact and case-folded names observed in one project directory.
  struct DirectoryCaseIndex final {
    bool is_directory = false;
    std::unordered_set<std::wstring> exact_names;
    std::unordered_set<std::wstring> folded_names;
  };

  std::unordered_map<std::wstring, DirectoryCaseIndex> directories;
  ProjectFileSystemWorkStats *work_stats = nullptr;
};

ProjectIoResult<ProjectFileSystem>
ProjectFileSystem::open(const std::filesystem::path &root) {
  if (root.empty()) {
    return ProjectIoResult<ProjectFileSystem>::failure(
        ProjectIoErrorCode::InvalidArgument, "Project root is empty.", root);
  }

  std::error_code error;
  const std::filesystem::path absolute_root =
      std::filesystem::absolute(root, error);
  if (error) {
    return ProjectIoResult<ProjectFileSystem>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not resolve project root", error),
        root);
  }
  if (!std::filesystem::exists(absolute_root, error)) {
    return ProjectIoResult<ProjectFileSystem>::failure(
        ProjectIoErrorCode::RootNotFound,
        filesystem_error_message("Project root does not exist", error),
        absolute_root);
  }
  if (error || !std::filesystem::is_directory(absolute_root, error)) {
    return ProjectIoResult<ProjectFileSystem>::failure(
        ProjectIoErrorCode::RootNotDirectory,
        filesystem_error_message("Project root is not a directory", error),
        absolute_root);
  }

  const std::filesystem::path canonical_root =
      std::filesystem::canonical(absolute_root, error);
  if (error) {
    return ProjectIoResult<ProjectFileSystem>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not canonicalize project root", error),
        absolute_root);
  }
  return ProjectIoResult<ProjectFileSystem>::success(
      ProjectFileSystem(canonical_root.lexically_normal()));
}

ProjectFileSystem ProjectFileSystem::begin_resolution_batch(
    ProjectFileSystemWorkStats *work_stats) const {
  ProjectFileSystem batch(root_);
  batch.resolution_batch_cache_ = std::make_shared<ResolutionBatchCache>();
  batch.resolution_batch_cache_->work_stats = work_stats;
  if (work_stats != nullptr) {
    *work_stats = {};
  }
  return batch;
}

ProjectIoResult<std::filesystem::path>
ProjectFileSystem::resolve(const ProjectRelativePath &path) const {
  if (!valid()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Project filesystem has no root.");
  }
  if (!path.valid()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Project-relative path is invalid.");
  }

  const std::filesystem::path candidate =
      (root_ / path.native_path()).lexically_normal();
  std::error_code error;
  const std::filesystem::path resolved =
      std::filesystem::weakly_canonical(candidate, error);
  if (error) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not resolve project path", error),
        candidate);
  }
  if (!path_is_within(resolved, root_)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::SymlinkEscape,
        "Project path resolves outside the project root.", candidate);
  }

  const ProjectIoResult<void> case_result =
      validate_existing_component_case(path);
  if (!case_result.ok()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        case_result.error().code, case_result.error().message,
        case_result.error().path);
  }
  return ProjectIoResult<std::filesystem::path>::success(candidate);
}

ProjectIoResult<std::filesystem::path>
ProjectFileSystem::resolve_existing_file(
    const ProjectRelativePath &path) const {
  ProjectIoResult<std::filesystem::path> resolved = resolve(path);
  if (!resolved.ok()) {
    return resolved;
  }

  std::error_code error;
  if (!std::filesystem::exists(resolved.value(), error)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::FileNotFound,
        filesystem_error_message("Project file does not exist", error),
        resolved.value());
  }
  if (error || !std::filesystem::is_regular_file(resolved.value(), error)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::FileNotRegular,
        filesystem_error_message("Project path is not a regular file", error),
        resolved.value());
  }
  return resolved;
}

ProjectIoResult<std::filesystem::path>
ProjectFileSystem::resolve_for_write(const ProjectRelativePath &path) const {
  ProjectIoResult<std::filesystem::path> resolved = resolve(path);
  if (!resolved.ok()) {
    return resolved;
  }

  std::error_code error;
  const std::filesystem::path parent = resolved.value().parent_path();
  if (!std::filesystem::is_directory(parent, error)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::DirectoryNotFound,
        filesystem_error_message(
            "Project output directory does not exist", error),
        parent);
  }
  if (std::filesystem::exists(resolved.value(), error) &&
      !std::filesystem::is_regular_file(resolved.value(), error)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::FileNotRegular,
        filesystem_error_message("Project output path is not a file", error),
        resolved.value());
  }
  if (error) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not inspect project output path", error),
        resolved.value());
  }
  return resolved;
}

ProjectIoResult<std::filesystem::path>
ProjectFileSystem::resolve_physical_location(
    const ProjectRelativePath &path) const {
  ProjectIoResult<std::filesystem::path> resolved = resolve(path);
  if (!resolved.ok()) {
    return resolved;
  }

  std::error_code error;
  const std::filesystem::path physical =
      std::filesystem::weakly_canonical(resolved.value(), error);
  if (error) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not resolve physical project path",
                                 error),
        resolved.value());
  }
  if (!path_is_within(physical, root_)) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::SymlinkEscape,
        "Project path resolves outside the project root.", resolved.value());
  }
  return ProjectIoResult<std::filesystem::path>::success(
      physical.lexically_normal());
}

ProjectIoResult<bool> ProjectFileSystem::refers_to_same_location(
    const ProjectRelativePath &left, const ProjectRelativePath &right) const {
  ProjectIoResult<std::filesystem::path> left_physical =
      resolve_physical_location(left);
  if (!left_physical.ok()) {
    return ProjectIoResult<bool>::failure(
        left_physical.error().code, left_physical.error().message,
        left_physical.error().path);
  }
  ProjectIoResult<std::filesystem::path> right_physical =
      resolve_physical_location(right);
  if (!right_physical.ok()) {
    return ProjectIoResult<bool>::failure(
        right_physical.error().code, right_physical.error().message,
        right_physical.error().path);
  }
  if (case_key(left_physical.value()) == case_key(right_physical.value())) {
    return ProjectIoResult<bool>::success(true);
  }

  std::error_code error;
  const bool left_exists =
      std::filesystem::exists(left_physical.value(), error);
  if (error) {
    return ProjectIoResult<bool>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not inspect project path", error),
        left_physical.value());
  }
  const bool right_exists =
      std::filesystem::exists(right_physical.value(), error);
  if (error) {
    return ProjectIoResult<bool>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not inspect project path", error),
        right_physical.value());
  }
  if (!left_exists || !right_exists) {
    return ProjectIoResult<bool>::success(false);
  }

  const bool equivalent = std::filesystem::equivalent(
      left_physical.value(), right_physical.value(), error);
  if (error) {
    return ProjectIoResult<bool>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not compare project paths", error),
        right_physical.value());
  }
  return ProjectIoResult<bool>::success(equivalent);
}

ProjectIoResult<ProjectRelativePath>
ProjectFileSystem::relative_from_absolute(
    const std::filesystem::path &path) const {
  if (!valid() || path.empty() || !path.is_absolute()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "An absolute path is required for project path conversion.", path);
  }

  std::error_code error;
  const std::filesystem::path absolute_path =
      std::filesystem::absolute(path, error).lexically_normal();
  if (error) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not resolve absolute path", error),
        path);
  }
  const std::filesystem::path resolved =
      std::filesystem::weakly_canonical(absolute_path, error);
  if (error) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::IoFailure,
        filesystem_error_message("Could not canonicalize absolute path", error),
        path);
  }
  if (!path_is_within(resolved, root_)) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::PathOutsideProject,
        "Selected path is outside the project root.", path);
  }

  // Dialog paths can reach the canonical root through a Windows short name or
  // another ancestor alias. Keep in-project spelling when it is already
  // relative to this root, including intentional internal links and casing.
  std::filesystem::path relative = absolute_path.lexically_relative(root_);
  if (relative.empty() || *relative.begin() == "..") {
    relative = resolved.lexically_relative(root_);
  }
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse(relative);
  if (!parsed.ok()) {
    return parsed;
  }
  const ProjectIoResult<std::filesystem::path> validated =
      resolve(parsed.value());
  if (!validated.ok()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        validated.error().code, validated.error().message,
        validated.error().path);
  }
  return parsed;
}

ProjectIoResult<void> ProjectFileSystem::validate_no_case_collisions(
    std::span<const ProjectRelativePath> paths) const {
  std::unordered_set<std::wstring> keys;
  keys.reserve(paths.size());
  for (const ProjectRelativePath &path : paths) {
    if (!path.valid()) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidRelativePath,
          "Project path collection contains an invalid path.");
    }
    if (!keys.insert(path.portable_case_key()).second) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision,
          "Project paths collide under portable case-insensitive matching.",
          path.native_path());
    }
  }
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> ProjectFileSystem::validate_no_path_aliases(
    std::span<const ProjectRelativePath> paths) const {
  const ProjectFileSystem *resolver = this;
  std::optional<ProjectFileSystem> local_batch;
  if (resolution_batch_cache_ == nullptr) {
    local_batch.emplace(begin_resolution_batch());
    resolver = &*local_batch;
  }

  const ProjectIoResult<void> portable =
      resolver->validate_no_case_collisions(paths);
  if (!portable.ok()) {
    return portable;
  }

  std::unordered_set<std::wstring> physical_keys;
  physical_keys.reserve(paths.size());
  for (const ProjectRelativePath &path : paths) {
    ProjectIoResult<std::filesystem::path> physical =
        resolver->resolve_physical_location(path);
    if (!physical.ok()) {
      return ProjectIoResult<void>::failure(
          physical.error().code, physical.error().message,
          physical.error().path);
    }
    if (!physical_keys.insert(case_key(physical.value())).second) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision,
          "Project paths resolve to the same physical location.",
          path.native_path());
    }
  }
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> ProjectFileSystem::validate_existing_component_case(
    const ProjectRelativePath &path) const {
  std::filesystem::path current = root_;
  for (const std::filesystem::path &component : path.native_path()) {
    if (resolution_batch_cache_ != nullptr) {
      ResolutionBatchCache &cache = *resolution_batch_cache_;
      if (cache.work_stats != nullptr) {
        ++cache.work_stats->component_case_lookups;
      }

      const std::wstring directory_key =
          current.lexically_normal().generic_wstring();
      auto indexed = cache.directories.find(directory_key);
      if (indexed == cache.directories.end()) {
        ResolutionBatchCache::DirectoryCaseIndex directory;
        std::error_code error;
        directory.is_directory =
            std::filesystem::is_directory(current, error);
        if (error) {
          return ProjectIoResult<void>::failure(
              ProjectIoErrorCode::IoFailure,
              filesystem_error_message(
                  "Could not inspect project path component", error),
              current);
        }

        if (directory.is_directory) {
          if (cache.work_stats != nullptr) {
            ++cache.work_stats->directory_case_enumerations;
          }
          std::filesystem::directory_iterator iterator(current, error);
          const std::filesystem::directory_iterator end;
          for (; !error && iterator != end; iterator.increment(error)) {
            const std::wstring exact =
                iterator->path().filename().generic_wstring();
            directory.exact_names.insert(exact);
            directory.folded_names.insert(
                case_key(iterator->path().filename()));
            if (cache.work_stats != nullptr) {
              ++cache.work_stats->directory_case_entries_visited;
            }
          }
          if (error) {
            return ProjectIoResult<void>::failure(
                ProjectIoErrorCode::IoFailure,
                filesystem_error_message(
                    "Could not enumerate project directory", error),
                current);
          }
        }
        indexed =
            cache.directories.emplace(directory_key, std::move(directory))
                .first;
      }

      const ResolutionBatchCache::DirectoryCaseIndex &directory =
          indexed->second;
      if (!directory.is_directory) {
        break;
      }
      const std::wstring exact = component.generic_wstring();
      const bool exact_match = directory.exact_names.contains(exact);
      const bool folded_match =
          directory.folded_names.contains(case_key(component));
      if (!exact_match && folded_match) {
        return ProjectIoResult<void>::failure(
            ProjectIoErrorCode::PathCaseCollision,
            "Project path casing collides with an existing filesystem entry.",
            current / component);
      }
      current /= component;
      if (!exact_match) {
        break;
      }
      continue;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(current, error)) {
      if (error) {
        return ProjectIoResult<void>::failure(
            ProjectIoErrorCode::IoFailure,
            filesystem_error_message(
                "Could not inspect project path component", error),
            current);
      }
      break;
    }

    bool exact_match = false;
    bool folded_match = false;
    std::filesystem::directory_iterator iterator(current, error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
      const std::filesystem::path existing = iterator->path().filename();
      if (existing.generic_wstring() == component.generic_wstring()) {
        exact_match = true;
        break;
      }
      if (same_portable_component(existing, component)) {
        folded_match = true;
      }
    }
    if (error) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::IoFailure,
          filesystem_error_message("Could not enumerate project directory",
                                   error),
          current);
    }
    if (!exact_match && folded_match) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision,
          "Project path casing collides with an existing filesystem entry.",
          current / component);
    }
    current /= component;
    if (!exact_match) {
      break;
    }
  }
  return ProjectIoResult<void>::success();
}

} // namespace quader
