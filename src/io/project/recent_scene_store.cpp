////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/recent_scene_store.hpp"

#include "io/project/project_file_system.hpp"
#include "io/qdr/qdr_atomic_write.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;
using EntriesResult = ProjectIoResult<std::vector<RecentSceneEntry>>;

constexpr std::string_view kFormat = "quader.recent-scenes";
constexpr std::uint32_t kVersion = 1u;
constexpr std::uintmax_t kMaximumStorageBytes = 1024u * 1024u;
constexpr std::size_t kMaximumDecodedEntries = 1000u;

std::filesystem::path normalized_manifest_path(const std::filesystem::path &path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error && !canonical.empty()) {
    return canonical;
  }
  error.clear();
  const auto absolute = std::filesystem::absolute(path, error);
  return error ? path.lexically_normal() : absolute.lexically_normal();
}

std::wstring manifest_path_key(const std::filesystem::path &path) {
  std::wstring key = normalized_manifest_path(path).generic_wstring();
  std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) {
    return static_cast<wchar_t>(std::towlower(value));
  });
  return key;
}

bool same_project(const RecentSceneEntry &left, const RecentSceneEntry &right) {
  if (manifest_path_key(left.manifest_path) ==
      manifest_path_key(right.manifest_path)) {
    return true;
  }
  std::error_code error;
  return std::filesystem::equivalent(left.manifest_path, right.manifest_path,
                                     error) && !error;
}

bool same_scene(const RecentSceneEntry &left, const RecentSceneEntry &right) {
  if (!same_project(left, right)) {
    return false;
  }
  if (left.scene_id == right.scene_id ||
      left.scene_path.portable_case_key() ==
          right.scene_path.portable_case_key()) {
    return true;
  }
  const auto left_files =
      ProjectFileSystem::open(left.manifest_path.parent_path());
  const auto right_files =
      ProjectFileSystem::open(right.manifest_path.parent_path());
  if (!left_files.ok() || !right_files.ok()) {
    return false;
  }
  const auto left_file = left_files.value().resolve_existing_file(left.scene_path);
  const auto right_file =
      right_files.value().resolve_existing_file(right.scene_path);
  if (!left_file.ok() || !right_file.ok()) {
    return false;
  }
  std::error_code error;
  return std::filesystem::equivalent(left_file.value(), right_file.value(),
                                     error) && !error;
}

bool valid_scene_path(const ProjectRelativePath &path) {
  return path.valid() && !is_project_autosave_path(path) &&
         path.portable_case_key().ends_with(L".qdr");
}

bool valid_label(std::string_view text) {
  return !text.empty() && text.find('\0') == std::string_view::npos;
}

bool scene_available(const RecentSceneEntry &entry) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(entry.manifest_path, error) || error) {
    return false;
  }
  const auto files = ProjectFileSystem::open(entry.manifest_path.parent_path());
  if (!files.ok()) {
    return false;
  }
  const auto scene = files.value().resolve_existing_file(entry.scene_path);
  if (!scene.ok()) {
    return false;
  }
  const auto physical = files.value().resolve_physical_location(entry.scene_path);
  if (!physical.ok()) {
    return false;
  }
  const auto relative = files.value().relative_from_absolute(physical.value());
  return relative.ok() && !is_project_autosave_path(relative.value());
}

bool contains_required_strings(const Json &entry) {
  if (!entry.is_object()) {
    return false;
  }
  for (const char *field : {"sceneId", "sceneName", "projectName",
                            "manifestPath", "scenePath"}) {
    const auto found = entry.find(field);
    if (found == entry.end() || !found->is_string() ||
        !valid_label(found->get_ref<const std::string &>())) {
      return false;
    }
  }
  return true;
}

} // namespace

ProjectIoResult<std::vector<RecentSceneEntry>> RecentSceneStore::load(
    const std::filesystem::path &project_manifest) const {
  if (storage_path_.empty()) {
    return EntriesResult::failure(ProjectIoErrorCode::InvalidArgument,
                                  "Recent scene storage path is empty.");
  }
  std::error_code error;
  if (!std::filesystem::exists(storage_path_, error)) {
    if (error) {
      return EntriesResult::failure(ProjectIoErrorCode::IoFailure,
          "Could not inspect recent scene storage: " + error.message(),
          storage_path_);
    }
    return EntriesResult::success({});
  }
  if (!std::filesystem::is_regular_file(storage_path_, error) || error) {
    return EntriesResult::failure(ProjectIoErrorCode::FileNotRegular,
        "Recent scene storage is not a regular file.", storage_path_);
  }
  const auto size = std::filesystem::file_size(storage_path_, error);
  if (error || size > kMaximumStorageBytes) {
    return EntriesResult::failure(ProjectIoErrorCode::ManifestMalformed,
        "Recent scene storage could not be read within its size limit.",
        storage_path_);
  }
  std::ifstream stream(storage_path_, std::ios::binary);
  if (!stream.is_open()) {
    return EntriesResult::failure(ProjectIoErrorCode::IoFailure,
        "Could not open recent scene storage.", storage_path_);
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  stream.read(text.data(), static_cast<std::streamsize>(text.size()));
  if (stream.gcount() != static_cast<std::streamsize>(text.size())) {
    return EntriesResult::failure(ProjectIoErrorCode::IoFailure,
        "Could not read recent scene storage completely.", storage_path_);
  }

  try {
    const Json root = Json::parse(text.begin(), text.end());
    if (!root.is_object() || !root.contains("format") ||
        !root["format"].is_string() ||
        root["format"].get<std::string>() != kFormat ||
        !root.contains("version") || !root["version"].is_number_unsigned() ||
        root["version"] != kVersion || !root.contains("scenes") ||
        !root["scenes"].is_array() ||
        root["scenes"].size() > kMaximumDecodedEntries) {
      return EntriesResult::failure(ProjectIoErrorCode::ManifestMalformed,
          "Recent scene storage has an unsupported or malformed root.",
          storage_path_);
    }

    std::vector<RecentSceneEntry> entries;
    entries.reserve(project_manifest.empty()
                        ? root["scenes"].size()
                        : std::min(root["scenes"].size(), kMaximumEntries));
    const RecentSceneEntry requested_project{.manifest_path = project_manifest};
    for (const Json &encoded : root["scenes"]) {
      if (!contains_required_strings(encoded)) {
        return EntriesResult::failure(ProjectIoErrorCode::ManifestMalformed,
            "Recent scene entry is malformed.", storage_path_);
      }
      auto id = SceneId::parse(encoded["sceneId"].get<std::string>());
      auto path = ProjectRelativePath::parse_portable(
          encoded["scenePath"].get<std::string>());
      const std::filesystem::path manifest = std::filesystem::u8path(
          encoded["manifestPath"].get<std::string>());
      if (!id.ok() || !path.ok() || !valid_scene_path(path.value()) ||
          manifest.empty() || !manifest.is_absolute()) {
        return EntriesResult::failure(ProjectIoErrorCode::ManifestMalformed,
            "Recent scene entry contains an invalid identity or path.",
            storage_path_);
      }
      RecentSceneEntry entry{
          .scene_id = id.take_value(),
          .scene_name = encoded["sceneName"].get<std::string>(),
          .project_name = encoded["projectName"].get<std::string>(),
          .manifest_path = normalized_manifest_path(manifest),
          .scene_path = path.take_value(),
      };
      if ((!project_manifest.empty() &&
           !same_project(entry, requested_project)) ||
          std::count_if(entries.begin(), entries.end(),
                        [&](const RecentSceneEntry &existing) {
                          return same_project(existing, entry);
                        }) >= kMaximumEntries ||
          std::any_of(entries.begin(), entries.end(),
                      [&](const RecentSceneEntry &existing) {
                        return same_scene(existing, entry);
                      })) {
        continue;
      }
      entry.available = scene_available(entry);
      entries.push_back(std::move(entry));
    }
    return EntriesResult::success(std::move(entries));
  } catch (const std::exception &exception) {
    return EntriesResult::failure(ProjectIoErrorCode::ManifestMalformed,
        std::string("Could not parse recent scene storage: ") + exception.what(),
        storage_path_);
  }
}

ProjectIoResult<void> RecentSceneStore::remember(
    const ProjectManifest &manifest, const std::filesystem::path &manifest_path,
    const SceneId &scene_id, std::string scene_name,
    const ProjectRelativePath &scene_path) const {
  if (!manifest.id().valid() || !scene_id.valid() ||
      !valid_label(manifest.name()) || !valid_label(scene_name) ||
      manifest_path.empty() || !manifest_path.is_absolute() ||
      !valid_scene_path(scene_path)) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidArgument,
        "A valid project, saved scene and absolute manifest path are required.",
        manifest_path);
  }
  auto loaded = load();
  if (!loaded.ok()) {
    return ProjectIoResult<void>::failure(loaded.error().code,
        loaded.error().message, loaded.error().path);
  }
  try {
    RecentSceneEntry entry{
        .scene_id = scene_id,
        .scene_name = std::move(scene_name),
        .project_name = manifest.name(),
        .manifest_path = normalized_manifest_path(manifest_path),
        .scene_path = scene_path,
    };
    auto entries = loaded.take_value();
    if (!entries.empty()) {
      const auto &first = entries.front();
      if (same_project(first, entry) && first.scene_id == entry.scene_id &&
          first.scene_path == entry.scene_path &&
          first.scene_name == entry.scene_name &&
          first.project_name == entry.project_name) {
        return ProjectIoResult<void>::success();
      }
    }
    std::erase_if(entries, [&](const RecentSceneEntry &existing) {
      return same_scene(existing, entry);
    });
    entries.insert(entries.begin(), std::move(entry));
    std::size_t project_count = 0u;
    std::erase_if(entries, [&](const RecentSceneEntry &existing) {
      return same_project(existing, entries.front()) &&
             ++project_count > kMaximumEntries;
    });
    if (entries.size() > kMaximumDecodedEntries) {
      entries.resize(kMaximumDecodedEntries);
    }
    return save(entries);
  } catch (const std::exception &exception) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
        std::string("Could not persist recent scene storage: ") +
            exception.what(), storage_path_);
  }
}

ProjectIoResult<void>
RecentSceneStore::synchronize_project(
    const ProjectManifest &manifest,
    const std::filesystem::path &manifest_path) const {
  auto loaded = load();
  if (!loaded.ok()) {
    return ProjectIoResult<void>::failure(loaded.error().code,
        loaded.error().message, loaded.error().path);
  }
  auto entries = loaded.take_value();
  const RecentSceneEntry project{.manifest_path = manifest_path};
  bool changed = false;
  for (auto &entry : entries) {
    if (!same_project(entry, project)) continue;
    const auto *scene = manifest.find_scene(entry.scene_id);
    if (scene == nullptr) continue;
    const auto stem = scene->path.native_path().stem().generic_u8string();
    const std::string name(stem.begin(), stem.end());
    if (entry.scene_path != scene->path || entry.scene_name != name ||
        entry.project_name != manifest.name()) {
      entry.scene_path = scene->path;
      entry.scene_name = name;
      entry.project_name = manifest.name();
      changed = true;
    }
  }
  std::erase_if(entries, [&](const RecentSceneEntry &entry) {
    const bool removed = same_project(entry, project) &&
                         manifest.find_scene(entry.scene_id) == nullptr;
    changed |= removed;
    return removed;
  });
  return changed ? save(entries) : ProjectIoResult<void>::success();
}

ProjectIoResult<void>
RecentSceneStore::save(std::span<const RecentSceneEntry> entries) const {
  Json scenes = Json::array();
  for (const auto &entry :
       entries.first(std::min(entries.size(), kMaximumDecodedEntries))) {
    const auto manifest_utf8 = entry.manifest_path.generic_u8string();
    scenes.push_back(Json{
        {"sceneId", std::string(entry.scene_id.str())},
        {"sceneName", entry.scene_name},
        {"projectName", entry.project_name},
        {"manifestPath", std::string(manifest_utf8.begin(), manifest_utf8.end())},
        {"scenePath", entry.scene_path.portable_string()},
    });
  }
  const std::string text = Json{
      {"format", kFormat}, {"version", kVersion}, {"scenes", std::move(scenes)},
  }.dump(2);
  if (text.size() > kMaximumStorageBytes) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidArgument,
        "Recent scene entries exceed the supported storage size.", storage_path_);
  }
  std::error_code error;
  if (!storage_path_.parent_path().empty()) {
    std::filesystem::create_directories(storage_path_.parent_path(), error);
    if (error) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
          "Could not create recent scene storage directory: " + error.message(),
          storage_path_);
    }
  }
  const auto written = quader_io::write_qdr_text_atomic(storage_path_, text);
  if (!written.ok) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
        written.message, storage_path_);
  }
  return ProjectIoResult<void>::success();
}

} // namespace quader
