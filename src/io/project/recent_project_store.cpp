////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/recent_project_store.hpp"

#include "io/qdr/qdr_atomic_write.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::string_view kRecentProjectsFormat = "quader.recent-projects";
constexpr std::uint32_t kRecentProjectsVersion = 1u;
constexpr std::uintmax_t kMaximumRecentProjectsBytes = 1024u * 1024u;
constexpr std::size_t kMaximumDecodedEntries = 1000u;

std::wstring recent_path_key(const std::filesystem::path &path) {
  std::error_code error;
  std::filesystem::path normalized;
  if (std::filesystem::exists(path, error) && !error) {
    normalized = std::filesystem::weakly_canonical(path, error);
  }
  if (normalized.empty() || error) {
    error.clear();
    normalized = std::filesystem::absolute(path, error);
  }
  if (normalized.empty() || error) {
    normalized = path;
  }
  normalized = normalized.lexically_normal();

  std::wstring key = normalized.generic_wstring();
  std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
    return static_cast<wchar_t>(std::towlower(value));
  });
  return key;
}

std::filesystem::path normalized_recent_path(
    const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::path canonical =
      std::filesystem::weakly_canonical(path, error);
  if (!error && !canonical.empty()) {
    return canonical;
  }
  error.clear();
  const std::filesystem::path absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

std::uint64_t unix_time_milliseconds() {
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

} // namespace

ProjectIoResult<std::vector<RecentProjectEntry>>
RecentProjectStore::load() const {
  if (storage_path_.empty()) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Recent project storage path is empty.");
  }

  std::error_code error;
  if (!std::filesystem::exists(storage_path_, error)) {
    if (error) {
      return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
          ProjectIoErrorCode::IoFailure,
          "Could not inspect recent project storage: " + error.message(),
          storage_path_);
    }
    return ProjectIoResult<std::vector<RecentProjectEntry>>::success({});
  }
  if (!std::filesystem::is_regular_file(storage_path_, error)) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::FileNotRegular,
        "Recent project storage is not a regular file.", storage_path_);
  }

  const std::uintmax_t size = std::filesystem::file_size(storage_path_, error);
  if (error || size > kMaximumRecentProjectsBytes) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::ManifestMalformed,
        error ? "Could not inspect recent project storage: " + error.message()
              : "Recent project storage exceeds the supported size limit.",
        storage_path_);
  }
  std::ifstream stream(storage_path_, std::ios::binary);
  if (!stream.is_open()) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not open recent project storage.", storage_path_);
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (!stream.good() && !stream.eof()) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not read recent project storage.", storage_path_);
  }

  try {
    const Json root = Json::parse(text.begin(), text.end());
    if (!root.is_object() || !root.contains("format") ||
        !root["format"].is_string() ||
        root["format"].get<std::string>() != kRecentProjectsFormat ||
        !root.contains("version") ||
        !root["version"].is_number_unsigned() ||
        root["version"].get<std::uint32_t>() != kRecentProjectsVersion ||
        !root.contains("projects") || !root["projects"].is_array() ||
        root["projects"].size() > kMaximumDecodedEntries) {
      return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
          ProjectIoErrorCode::ManifestMalformed,
          "Recent project storage has an unsupported or malformed root.",
          storage_path_);
    }

    std::vector<RecentProjectEntry> entries;
    entries.reserve(std::min(root["projects"].size(), kMaximumEntries));
    std::unordered_set<std::wstring> path_keys;
    std::unordered_set<ProjectId, ProjectIdHash> project_ids;
    for (const Json &entry_json : root["projects"]) {
      if (entries.size() == kMaximumEntries) {
        break;
      }
      if (!entry_json.is_object() || !entry_json.contains("projectId") ||
          !entry_json["projectId"].is_string() ||
          !entry_json.contains("name") || !entry_json["name"].is_string() ||
          !entry_json.contains("manifestPath") ||
          !entry_json["manifestPath"].is_string() ||
          !entry_json.contains("lastOpenedUnixMs") ||
          !entry_json["lastOpenedUnixMs"].is_number_unsigned()) {
        return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
            ProjectIoErrorCode::ManifestMalformed,
            "Recent project entry is malformed.", storage_path_);
      }

      ProjectIoResult<ProjectId> id =
          ProjectId::parse(entry_json["projectId"].get<std::string>());
      const std::filesystem::path manifest_path =
          std::filesystem::u8path(entry_json["manifestPath"].get<std::string>());
      if (!id.ok() || manifest_path.empty() || !manifest_path.is_absolute()) {
        return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
            ProjectIoErrorCode::ManifestMalformed,
            "Recent project entry contains an invalid identity or path.",
            storage_path_);
      }

      ProjectId parsed_id = id.take_value();
      const std::wstring path_key = recent_path_key(manifest_path);
      if (!path_keys.insert(path_key).second ||
          !project_ids.insert(parsed_id).second) {
        continue;
      }
      std::error_code availability_error;
      const bool available =
          std::filesystem::is_regular_file(manifest_path, availability_error) &&
          !availability_error;
      std::optional<std::uint64_t> manifest_last_modified_unix_ms;
      std::optional<std::uintmax_t> manifest_size_bytes;
      std::string last_saved_app_version;
      if (available) {
        const auto metadata = ProjectManifestStore::open_metadata(manifest_path);
        if (metadata.ok() && metadata.value().manifest.id() == parsed_id) {
          last_saved_app_version = metadata.value().manifest.last_saved_app_version();
        }
        std::error_code metadata_error;
        const std::uintmax_t manifest_size =
            std::filesystem::file_size(manifest_path, metadata_error);
        if (!metadata_error) {
          manifest_size_bytes = manifest_size;
        }

        metadata_error.clear();
        const std::filesystem::file_time_type last_modified =
            std::filesystem::last_write_time(manifest_path, metadata_error);
        if (!metadata_error) {
          const auto file_clock_now =
              std::filesystem::file_time_type::clock::now();
          const auto system_clock_now = std::chrono::system_clock::now();
          const auto system_time =
              last_modified - file_clock_now + system_clock_now;
          const auto stable_seconds =
              std::chrono::round<std::chrono::seconds>(system_time);
          const std::int64_t unix_milliseconds =
              std::chrono::duration_cast<std::chrono::milliseconds>(
                  stable_seconds.time_since_epoch())
                  .count();
          if (unix_milliseconds >= 0) {
            manifest_last_modified_unix_ms =
                static_cast<std::uint64_t>(unix_milliseconds);
          }
        }
      }
      entries.push_back(RecentProjectEntry{
          .project_id = std::move(parsed_id),
          .name = entry_json["name"].get<std::string>(),
          .manifest_path = normalized_recent_path(manifest_path),
          .last_opened_unix_ms =
              entry_json["lastOpenedUnixMs"].get<std::uint64_t>(),
          .available = available,
          .manifest_last_modified_unix_ms =
              manifest_last_modified_unix_ms,
          .manifest_size_bytes = manifest_size_bytes,
          .last_saved_app_version = std::move(last_saved_app_version),
      });
    }
    return ProjectIoResult<std::vector<RecentProjectEntry>>::success(
        std::move(entries));
  } catch (const std::exception &exception) {
    return ProjectIoResult<std::vector<RecentProjectEntry>>::failure(
        ProjectIoErrorCode::ManifestMalformed,
        std::string("Could not parse recent project storage: ") +
            exception.what(),
        storage_path_);
  }
}

ProjectIoResult<void> RecentProjectStore::remember(
    const ProjectManifest &manifest,
    const std::filesystem::path &manifest_path) const {
  if (!manifest.id().valid() || manifest_path.empty() ||
      !manifest_path.is_absolute()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "A valid project and absolute manifest path are required.",
        manifest_path);
  }

  ProjectIoResult<std::vector<RecentProjectEntry>> loaded = load();
  if (!loaded.ok()) {
    return ProjectIoResult<void>::failure(
        loaded.error().code, loaded.error().message, loaded.error().path);
  }
  std::vector<RecentProjectEntry> entries = loaded.take_value();
  const std::wstring new_path_key = recent_path_key(manifest_path);
  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [&](const RecentProjectEntry &entry) {
                       return entry.project_id == manifest.id() ||
                              recent_path_key(entry.manifest_path) ==
                                  new_path_key;
                     }),
      entries.end());

  std::error_code error;
  const std::filesystem::path normalized_path =
      normalized_recent_path(manifest_path);
  entries.insert(entries.begin(), RecentProjectEntry{
                                      .project_id = manifest.id(),
                                      .name = manifest.name(),
                                      .manifest_path = normalized_path,
                                      .last_opened_unix_ms =
                                          unix_time_milliseconds(),
                                      .available = std::filesystem::is_regular_file(
                                          normalized_path, error) &&
                                                   !error,
                                  });
  if (entries.size() > kMaximumEntries) {
    entries.resize(kMaximumEntries);
  }
  return save(entries);
}

ProjectIoResult<void>
RecentProjectStore::remove(const std::filesystem::path &manifest_path) const {
  ProjectIoResult<std::vector<RecentProjectEntry>> loaded = load();
  if (!loaded.ok()) {
    return ProjectIoResult<void>::failure(
        loaded.error().code, loaded.error().message, loaded.error().path);
  }
  std::vector<RecentProjectEntry> entries = loaded.take_value();
  const std::wstring key = recent_path_key(manifest_path);
  entries.erase(
      std::remove_if(entries.begin(), entries.end(),
                     [&](const RecentProjectEntry &entry) {
                       return recent_path_key(entry.manifest_path) == key;
                     }),
      entries.end());
  return save(entries);
}

ProjectIoResult<void>
RecentProjectStore::save(std::span<const RecentProjectEntry> entries) const {
  if (storage_path_.empty()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Recent project storage path is empty.");
  }
  std::error_code error;
  const std::filesystem::path parent = storage_path_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::IoFailure,
          "Could not create recent project storage directory: " +
              error.message(),
          parent);
    }
  }

  Json projects = Json::array();
  const std::size_t count = std::min(entries.size(), kMaximumEntries);
  projects.get_ref<Json::array_t &>().reserve(count);
  for (std::size_t index = 0u; index < count; ++index) {
    const RecentProjectEntry &entry = entries[index];
    if (!entry.project_id.valid() || entry.name.empty() ||
        entry.manifest_path.empty() || !entry.manifest_path.is_absolute()) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidArgument,
          "Cannot persist an invalid recent project entry.",
          entry.manifest_path);
    }
    const std::u8string manifest_path_utf8 = entry.manifest_path.generic_u8string();
    projects.push_back(Json{
        {"projectId", std::string(entry.project_id.str())},
        {"name", entry.name},
        {"manifestPath", std::string(
            reinterpret_cast<const char *>(manifest_path_utf8.data()),
            manifest_path_utf8.size())},
        {"lastOpenedUnixMs", entry.last_opened_unix_ms},
    });
  }

  const std::string text = Json{
      {"format", std::string(kRecentProjectsFormat)},
      {"version", kRecentProjectsVersion},
      {"projects", std::move(projects)},
  }.dump(2);
  const quader_io::QdrAtomicWriteResult result =
      quader_io::write_qdr_text_atomic(storage_path_, text);
  if (!result.ok) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                          result.message, storage_path_);
  }
  return ProjectIoResult<void>::success();
}

} // namespace quader
