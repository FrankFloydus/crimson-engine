////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace quader {

/// Categorizes project filesystem and manifest failures.
enum class ProjectIoErrorCode {
  None,
  InvalidArgument,
  InvalidUuid,
  InvalidRelativePath,
  PathOutsideProject,
  PathCaseCollision,
  SymlinkEscape,
  RootNotFound,
  RootNotDirectory,
  RootNotEmpty,
  FileNotFound,
  FileNotRegular,
  DirectoryNotFound,
  ManifestMalformed,
  MaterialMalformed,
  UnsupportedFormat,
  DuplicateScene,
  DuplicateAsset,
  SceneNotFound,
  AssetNotFound,
  InvalidWorkspaceState,
  IoFailure,
};

/// Carries a structured project I/O failure and its relevant path.
struct ProjectIoError {
  ProjectIoErrorCode code = ProjectIoErrorCode::None;
  std::string message;
  std::filesystem::path path;
};

template <typename T> class ProjectIoResult final {
public:
  [[nodiscard]] static ProjectIoResult success(T value) {
    ProjectIoResult result;
    result.value_ = std::move(value);
    return result;
  }

  [[nodiscard]] static ProjectIoResult failure(
      ProjectIoErrorCode code, std::string message,
      std::filesystem::path path = {}) {
    ProjectIoResult result;
    result.error_ = ProjectIoError{
        .code = code,
        .message = std::move(message),
        .path = std::move(path),
    };
    return result;
  }

  [[nodiscard]] bool ok() const noexcept { return value_.has_value(); }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }

  [[nodiscard]] T &value() { return value_.value(); }
  [[nodiscard]] const T &value() const { return value_.value(); }
  [[nodiscard]] T take_value() { return std::move(value_).value(); }
  [[nodiscard]] const ProjectIoError &error() const noexcept { return error_; }

private:
  std::optional<T> value_;
  ProjectIoError error_;
};

template <> class ProjectIoResult<void> final {
public:
  [[nodiscard]] static ProjectIoResult success(std::string warning = {}) {
    ProjectIoResult result;
    result.ok_ = true;
    result.warning_ = std::move(warning);
    return result;
  }

  [[nodiscard]] static ProjectIoResult failure(
      ProjectIoErrorCode code, std::string message,
      std::filesystem::path path = {}) {
    ProjectIoResult result;
    result.error_ = ProjectIoError{
        .code = code,
        .message = std::move(message),
        .path = std::move(path),
    };
    return result;
  }

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
  [[nodiscard]] const ProjectIoError &error() const noexcept { return error_; }
  /** Returns a non-fatal persistence warning carried by a successful result. */
  [[nodiscard]] const std::string &warning() const noexcept { return warning_; }
  /** Moves the non-fatal persistence warning out of this successful result. */
  [[nodiscard]] std::string take_warning() noexcept {
    return std::move(warning_);
  }

private:
  bool ok_ = false;
  ProjectIoError error_;
  std::string warning_;
};

/// Identifies one folder-backed Quader project persistently.
class ProjectId final {
public:
  ProjectId() = default;

  [[nodiscard]] static ProjectId generate();
  [[nodiscard]] static ProjectIoResult<ProjectId> parse(std::string_view text);

  [[nodiscard]] bool valid() const noexcept { return !value_.empty(); }
  [[nodiscard]] std::string_view str() const noexcept { return value_; }

  friend bool operator==(const ProjectId &, const ProjectId &) = default;
  friend auto operator<=>(const ProjectId &, const ProjectId &) = default;

private:
  explicit ProjectId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

/// Identifies one saved scene persistently and independently of its path.
class SceneId final {
public:
  SceneId() = default;

  [[nodiscard]] static SceneId generate();
  [[nodiscard]] static ProjectIoResult<SceneId> parse(std::string_view text);
  [[nodiscard]] static SceneId
  from_bytes(const std::array<std::byte, 16> &bytes);

  [[nodiscard]] bool valid() const noexcept { return !value_.empty(); }
  [[nodiscard]] std::string_view str() const noexcept { return value_; }
  [[nodiscard]] std::array<std::byte, 16> bytes() const;

  friend bool operator==(const SceneId &, const SceneId &) = default;
  friend auto operator<=>(const SceneId &, const SceneId &) = default;

private:
  explicit SceneId(std::string value) : value_(std::move(value)) {}

  std::string value_;
};

/// Identifies one open scene tab with a runtime-only generation.
class SceneSessionId {
public:
  SceneSessionId() = default;

  [[nodiscard]] static SceneSessionId from_parts(std::uint32_t slot,
                                                 std::uint32_t generation) {
    return SceneSessionId(slot, generation);
  }

  [[nodiscard]] bool valid() const noexcept {
    return slot_ != invalid_slot() && generation_ != 0u;
  }
  [[nodiscard]] std::uint32_t slot() const noexcept { return slot_; }
  [[nodiscard]] std::uint32_t generation() const noexcept {
    return generation_;
  }
  [[nodiscard]] std::uint64_t packed() const noexcept {
    return (static_cast<std::uint64_t>(generation_) << 32u) |
           static_cast<std::uint64_t>(slot_);
  }
  [[nodiscard]] static SceneSessionId from_packed(std::uint64_t value) {
    return from_parts(static_cast<std::uint32_t>(value),
                      static_cast<std::uint32_t>(value >> 32u));
  }

  friend bool operator==(const SceneSessionId &, const SceneSessionId &) =
      default;
  friend auto operator<=>(const SceneSessionId &, const SceneSessionId &) =
      default;

private:
  static constexpr std::uint32_t invalid_slot() noexcept {
    return std::numeric_limits<std::uint32_t>::max();
  }

  SceneSessionId(std::uint32_t slot, std::uint32_t generation)
      : slot_(slot), generation_(generation) {}

  std::uint32_t slot_ = invalid_slot();
  std::uint32_t generation_ = 0u;
};

/// Identifies the current Standard Material draft with a runtime generation.
class MaterialEditSessionId final {
public:
  MaterialEditSessionId() = default;

  [[nodiscard]] static MaterialEditSessionId
  from_generation(std::uint32_t generation) noexcept {
    return MaterialEditSessionId(generation);
  }

  [[nodiscard]] bool valid() const noexcept { return generation_ != 0u; }
  [[nodiscard]] std::uint32_t generation() const noexcept {
    return generation_;
  }

  friend bool operator==(const MaterialEditSessionId &,
                         const MaterialEditSessionId &) = default;
  friend auto operator<=>(const MaterialEditSessionId &,
                          const MaterialEditSessionId &) = default;

private:
  explicit MaterialEditSessionId(std::uint32_t generation)
      : generation_(generation) {}

  std::uint32_t generation_ = 0u;
};

/**
 * A normalized, portable path that is always relative to a project root.
 * The type rejects rooted paths and any parent traversal before filesystem IO.
 */
class ProjectRelativePath final {
public:
  ProjectRelativePath() = default;

  [[nodiscard]] static ProjectIoResult<ProjectRelativePath>
  parse(const std::filesystem::path &path);
  [[nodiscard]] static ProjectIoResult<ProjectRelativePath>
  parse_portable(std::string_view path);

  [[nodiscard]] bool valid() const noexcept { return !path_.empty(); }
  [[nodiscard]] const std::filesystem::path &native_path() const noexcept {
    return path_;
  }
  [[nodiscard]] std::string portable_string() const;
  [[nodiscard]] std::wstring portable_case_key() const;

  friend bool operator==(const ProjectRelativePath &,
                         const ProjectRelativePath &) = default;
  friend auto operator<=>(const ProjectRelativePath &,
                          const ProjectRelativePath &) = default;

private:
  explicit ProjectRelativePath(std::filesystem::path path)
      : path_(std::move(path)) {}

  std::filesystem::path path_;
};

/// Hashes persistent project identities for unordered containers.
struct ProjectIdHash {
  [[nodiscard]] std::size_t operator()(const ProjectId &id) const noexcept {
    return std::hash<std::string_view>{}(id.str());
  }
};

/// Hashes persistent scene identities for unordered containers.
struct SceneIdHash {
  [[nodiscard]] std::size_t operator()(const SceneId &id) const noexcept {
    return std::hash<std::string_view>{}(id.str());
  }
};

/// Hashes runtime scene-session identities for unordered containers.
struct SceneSessionIdHash {
  [[nodiscard]] std::size_t
  operator()(const SceneSessionId &id) const noexcept {
    const std::uint64_t packed =
        (static_cast<std::uint64_t>(id.generation()) << 32u) | id.slot();
    return std::hash<std::uint64_t>{}(packed);
  }
};

} // namespace quader
