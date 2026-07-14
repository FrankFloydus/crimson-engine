////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_types.hpp"

#include <stduuid/uuid.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <random>
#include <string>
#include <utility>

namespace quader {
namespace {

std::mt19937 &project_uuid_random_engine() {
  thread_local std::mt19937 engine = [] {
    std::random_device random_device;
    std::array<std::uint32_t, 8> seed_data{};
    for (std::uint32_t &value : seed_data) {
      value = random_device();
    }
    std::seed_seq seed(seed_data.begin(), seed_data.end());
    return std::mt19937(seed);
  }();
  return engine;
}

std::string generate_uuid_string() {
  uuids::uuid_random_generator generator{project_uuid_random_engine()};
  return uuids::to_string(generator());
}

ProjectIoResult<std::string> normalize_uuid(std::string_view text) {
  const std::optional<uuids::uuid> parsed = uuids::uuid::from_string(text);
  if (!parsed.has_value()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::InvalidUuid, "Value is not a valid UUID.");
  }
  return ProjectIoResult<std::string>::success(uuids::to_string(*parsed));
}

bool invalid_portable_component(const std::filesystem::path &component) {
  const std::wstring native = component.generic_wstring();
  return std::any_of(native.begin(), native.end(), [](wchar_t value) {
    return value == L':' || value < wchar_t{32};
  });
}

} // namespace

ProjectId ProjectId::generate() { return ProjectId(generate_uuid_string()); }

ProjectIoResult<ProjectId> ProjectId::parse(std::string_view text) {
  ProjectIoResult<std::string> parsed = normalize_uuid(text);
  if (!parsed.ok()) {
    return ProjectIoResult<ProjectId>::failure(
        parsed.error().code, parsed.error().message, parsed.error().path);
  }
  return ProjectIoResult<ProjectId>::success(
      ProjectId(parsed.take_value()));
}

SceneId SceneId::generate() { return SceneId(generate_uuid_string()); }

SceneId SceneId::from_bytes(const std::array<std::byte, 16> &bytes) {
  std::array<std::uint8_t, 16> values{};
  std::transform(bytes.begin(), bytes.end(), values.begin(),
                 [](std::byte value) {
                   return std::to_integer<std::uint8_t>(value);
                 });
  return SceneId(uuids::to_string(uuids::uuid(values)));
}

ProjectIoResult<SceneId> SceneId::parse(std::string_view text) {
  ProjectIoResult<std::string> parsed = normalize_uuid(text);
  if (!parsed.ok()) {
    return ProjectIoResult<SceneId>::failure(
        parsed.error().code, parsed.error().message, parsed.error().path);
  }
  return ProjectIoResult<SceneId>::success(SceneId(parsed.take_value()));
}

std::array<std::byte, 16> SceneId::bytes() const {
  std::array<std::byte, 16> result{};
  const std::optional<uuids::uuid> parsed = uuids::uuid::from_string(value_);
  if (!parsed.has_value()) {
    return result;
  }
  const auto bytes = parsed->as_bytes();
  std::copy(bytes.begin(), bytes.end(), result.begin());
  return result;
}

ProjectIoResult<ProjectRelativePath>
ProjectRelativePath::parse(const std::filesystem::path &path) {
  if (path.empty()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Project-relative path is empty.", path);
  }
  if (path.is_absolute() || path.has_root_name() ||
      path.has_root_directory()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Project-relative path must not be rooted.", path);
  }

  std::filesystem::path normalized;
  for (const std::filesystem::path &component : path) {
    if (component.empty() || component == ".") {
      continue;
    }
    if (component == "..") {
      return ProjectIoResult<ProjectRelativePath>::failure(
          ProjectIoErrorCode::InvalidRelativePath,
          "Project-relative path must not contain parent traversal.", path);
    }
    if (invalid_portable_component(component)) {
      return ProjectIoResult<ProjectRelativePath>::failure(
          ProjectIoErrorCode::InvalidRelativePath,
          "Project-relative path contains an unsupported character.", path);
    }
    normalized /= component;
  }

  if (normalized.empty() || normalized == ".") {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Project-relative path does not identify a project entry.", path);
  }
  return ProjectIoResult<ProjectRelativePath>::success(
      ProjectRelativePath(normalized.lexically_normal()));
}

ProjectIoResult<ProjectRelativePath>
ProjectRelativePath::parse_portable(std::string_view path) {
  return parse(std::filesystem::path(std::string(path)));
}

std::string ProjectRelativePath::portable_string() const {
  return path_.generic_string();
}

std::wstring ProjectRelativePath::portable_case_key() const {
  std::wstring key = path_.generic_wstring();
  std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
    return static_cast<wchar_t>(std::towlower(value));
  });
  return key;
}

} // namespace quader
