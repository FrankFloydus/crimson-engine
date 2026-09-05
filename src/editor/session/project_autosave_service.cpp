////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_autosave_service.hpp"

#include "editor/session/project_session.hpp"
#include "io/qdr/qdr_atomic_write.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

namespace quader {
namespace {

using EntriesResult = ProjectIoResult<std::vector<ProjectAutosaveEntry>>;
using Json = nlohmann::ordered_json;
constexpr std::string_view kRecoveryDirectory = ".quader/autosave";
constexpr std::uintmax_t kMaximumMetadataBytes = 64u * 1024u;
constexpr std::int64_t kMaximumSavedAtUnixMs = 253402300799999;

EntriesResult entries_failure(std::string message) {
  return EntriesResult::failure(ProjectIoErrorCode::IoFailure, std::move(message));
}

ProjectRelativePath recovery_directory() {
  return ProjectRelativePath::parse_portable(kRecoveryDirectory).take_value();
}

std::string recovery_path_utf8(const std::filesystem::path &path) {
  const std::u8string text = path.generic_u8string();
  return std::string(reinterpret_cast<const char *>(text.data()), text.size());
}

// Bound the descriptive token, keeping UTF-8 characters whole. The timestamp and
// UUID suffix make device names (such as CON) safe without using names as identity.
std::string recovery_name_token(std::string_view name) {
  constexpr std::size_t kMaximumBytes = 64u;
  std::size_t length = std::min(name.size(), kMaximumBytes);
  while (length < name.size() && length > 0u &&
         (static_cast<unsigned char>(name[length]) & 0xc0u) == 0x80u) {
    --length;
  }
  std::string token(name.substr(0u, length));
  for (char &value : token) {
    if (static_cast<unsigned char>(value) < 32u ||
        std::string_view("<>:\"/\\|?*.").find(value) != std::string_view::npos) {
      value = '_';
    }
  }
  while (!token.empty() && token.back() == ' ') {
    token.pop_back();
  }
  return token.empty() ? "Scene" : token;
}

std::string recovery_filename_stem(std::string_view name, std::int64_t saved_at,
                                   std::string_view snapshot_id) {
  const std::chrono::sys_time<std::chrono::milliseconds> time{
      std::chrono::milliseconds(saved_at)};
  const auto day = std::chrono::floor<std::chrono::days>(time);
  const std::chrono::year_month_day date(day);
  const std::chrono::hh_mm_ss clock(time - day);
  char timestamp[32]{};
  std::snprintf(timestamp, sizeof(timestamp), "%04d-%02u-%02u_%02lld-%02lld-%02lld-%03lldZ",
                static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                static_cast<unsigned>(date.day()),
                static_cast<long long>(clock.hours().count()),
                static_cast<long long>(clock.minutes().count()),
                static_cast<long long>(clock.seconds().count()),
                static_cast<long long>(clock.subseconds().count()));
  return recovery_name_token(name) + "_" + timestamp + "_" + std::string(snapshot_id);
}

ProjectRelativePath recovery_copy_path(std::string_view stem,
                                       std::string_view extension) {
  return ProjectRelativePath::parse(
      recovery_directory().native_path() /
      std::filesystem::u8path(std::string(stem) + std::string(extension))).take_value();
}

std::optional<ProjectAutosaveEntry> read_metadata(
    const ProjectSession &project, const ProjectRelativePath &metadata_path) {
  const ProjectFileSystem &files = project.workspace()->file_system;
  const auto absolute = files.resolve_existing_file(metadata_path);
  if (!absolute.ok()) {
    return std::nullopt;
  }
  std::error_code error;
  const auto size = std::filesystem::file_size(absolute.value(), error);
  if (error || size == 0u || size > kMaximumMetadataBytes) {
    return std::nullopt;
  }
  std::ifstream input(absolute.value(), std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  try {
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
      return std::nullopt;
    }
    const Json json = Json::parse(bytes);
    if (!json.is_object() || json.value("format", "") != "quader.autosave" ||
        json.value("project_id", "") != project.manifest()->id().str()) {
      return std::nullopt;
    }
    const auto scene_id = SceneId::parse(json.at("scene_id").get<std::string>());
    const std::string name = json.at("name").get<std::string>();
    const std::int64_t saved_at = json.at("saved_at_unix_ms").get<std::int64_t>();
    if (!scene_id.ok() || name.empty() || name.size() > 4096u ||
        saved_at <= 0 || saved_at > kMaximumSavedAtUnixMs) {
      return std::nullopt;
    }
    const int version = json.value("version", 0);
    if (version == 1) {
      const std::string snapshot_id = recovery_path_utf8(metadata_path.native_path().stem());
      const auto expected_parent = recovery_directory().native_path() /
                                   std::string(scene_id.value().str());
      if (!SceneId::parse(snapshot_id).ok() ||
          metadata_path.native_path().parent_path() != expected_parent) {
        return std::nullopt;
      }
    } else if (version == 2) {
      const std::string snapshot_id = json.at("snapshot_id").get<std::string>();
      const auto parsed_snapshot = SceneId::parse(snapshot_id);
      if (!parsed_snapshot.ok() || parsed_snapshot.value().str() != snapshot_id ||
          metadata_path != recovery_copy_path(
              recovery_filename_stem(name, saved_at, snapshot_id), ".json")) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
    auto qdr_path = metadata_path.native_path();
    qdr_path.replace_extension(".qdr");
    auto relative = ProjectRelativePath::parse(qdr_path);
    if (!relative.ok() || !files.resolve_existing_file(relative.value()).ok()) {
      return std::nullopt;
    }
    std::optional<ProjectRelativePath> source_path;
    if (const auto source = json.find("source_path");
        source != json.end() && !source->is_null()) {
      auto parsed = ProjectRelativePath::parse_portable(source->get<std::string>());
      if (!parsed.ok() || is_project_autosave_path(parsed.value())) {
        return std::nullopt;
      }
      source_path = parsed.take_value();
    }
    return ProjectAutosaveEntry{
        .path = relative.take_value(),
        .scene_id = scene_id.value(),
        .scene_name = name,
        .source_path = std::move(source_path),
        .saved_at_unix_ms = saved_at,
    };
  } catch (const Json::exception &) {
    return std::nullopt;
  } catch (const std::system_error &) {
    return std::nullopt;
  }
}

ProjectIoResult<void> remove_copy(const ProjectSession &project,
                                  const ProjectAutosaveEntry &entry) {
  const ProjectFileSystem &files = project.workspace()->file_system;
  const auto qdr = files.resolve_existing_file(entry.path);
  if (!qdr.ok()) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                          qdr.error().message);
  }
  std::error_code error;
  std::filesystem::remove(qdr.value(), error);
  if (error) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                          error.message());
  }
  auto metadata = entry.path.native_path();
  metadata.replace_extension(".json");
  const auto relative = ProjectRelativePath::parse(metadata);
  const auto absolute = files.resolve_existing_file(relative.value());
  if (absolute.ok()) {
    std::filesystem::remove(absolute.value(), error);
    if (error) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                            error.message());
    }
  }
  return ProjectIoResult<void>::success();
}

} // namespace

ProjectIoResult<std::filesystem::path>
ProjectAutosaveService::directory(const ProjectSession &project) const {
  using DirectoryResult = ProjectIoResult<std::filesystem::path>;
  if (!project.has_project()) {
    return DirectoryResult::failure(ProjectIoErrorCode::InvalidWorkspaceState,
                                    "Open a project to browse its recovery copies.");
  }
  const ProjectFileSystem &files = project.workspace()->file_system;
  const auto resolved = files.resolve(recovery_directory());
  if (!resolved.ok()) {
    return resolved;
  }
  std::error_code error;
  std::filesystem::create_directories(resolved.value(), error);
  if (error) {
    return DirectoryResult::failure(ProjectIoErrorCode::IoFailure,
        "Could not create autosave storage: " + error.message(), resolved.value());
  }
  return files.resolve(recovery_directory());
}

ProjectIoResult<std::vector<ProjectAutosaveEntry>>
ProjectAutosaveService::entries(const ProjectSession &project) const {
  if (!project.has_project()) {
    return entries_failure("Open a project to browse its recovery copies.");
  }
  const ProjectFileSystem &files = project.workspace()->file_system;
  const auto directory = files.resolve(recovery_directory());
  if (!directory.ok()) {
    return entries_failure(directory.error().message);
  }
  std::error_code error;
  const bool exists = std::filesystem::exists(directory.value(), error);
  if (error) {
    return entries_failure(error.message());
  }
  if (!exists) {
    return EntriesResult::success({});
  }
  std::vector<ProjectAutosaveEntry> result;
  std::filesystem::directory_iterator scenes(directory.value(), error);
  for (; !error && scenes != std::filesystem::directory_iterator{};
       scenes.increment(error)) {
    if (scenes->path().extension() == ".json") {
      const auto relative = files.relative_from_absolute(scenes->path());
      if (relative.ok()) {
        if (auto entry = read_metadata(project, relative.value())) {
          result.push_back(std::move(*entry));
        }
      }
      continue;
    }
    if (!scenes->is_directory(error) || error ||
        !SceneId::parse(recovery_path_utf8(scenes->path().filename())).ok()) {
      continue;
    }
    const auto scene_directory = files.relative_from_absolute(scenes->path());
    if (!scene_directory.ok() || !files.resolve(scene_directory.value()).ok()) {
      continue;
    }
    std::error_code entry_error;
    std::filesystem::directory_iterator metadata(scenes->path(), entry_error);
    for (; !entry_error && metadata != std::filesystem::directory_iterator{};
         metadata.increment(entry_error)) {
      if (metadata->path().extension() != ".json") {
        continue;
      }
      const auto relative = files.relative_from_absolute(metadata->path());
      if (relative.ok()) {
        if (auto entry = read_metadata(project, relative.value())) {
          result.push_back(std::move(*entry));
        }
      }
    }
    if (entry_error) {
      return entries_failure("Could not read scene recovery copies: " +
                             entry_error.message());
    }
  }
  if (error) {
    return entries_failure("Could not read recovery storage: " + error.message());
  }
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    return left.saved_at_unix_ms != right.saved_at_unix_ms
               ? left.saved_at_unix_ms > right.saved_at_unix_ms
               : left.path > right.path;
  });
  return EntriesResult::success(std::move(result));
}

ProjectAutosaveResult ProjectAutosaveService::poll(
    ProjectSession &project, const SessionPreferences &settings,
    std::chrono::steady_clock::time_point now, bool external_edit_active) {
  ProjectAutosaveResult result;
  if (!settings.valid()) {
    return {.ok = false, .warnings = {"Autosave settings are invalid."}};
  }
  if (!project.has_project()) {
    project_id_ = {};
    project_root_.clear();
    saved_states_.clear();
    next_attempt_.reset();
    return result;
  }
  const auto interval = std::chrono::minutes(settings.autosave_interval_minutes);
  const bool project_changed = project_id_ != project.manifest()->id() ||
                               project_root_ != project.root();
  const bool schedule_changed = !settings_.has_value() ||
      settings_->autosave_enabled != settings.autosave_enabled ||
      settings_->autosave_interval_minutes != settings.autosave_interval_minutes;
  if (!settings_.has_value() ||
      settings_->autosave_max_files != settings.autosave_max_files || project_changed) {
    retention_pending_ = true;
  }
  if (project_changed) {
    project_id_ = project.manifest()->id();
    project_root_ = project.root();
    saved_states_.clear();
  }
  settings_ = settings;
  if (project_changed || schedule_changed || !next_attempt_.has_value()) {
    next_attempt_ = now + interval;
  }
  // No filesystem traversal, snapshots or revision queries before this gate.
  if (!settings.autosave_enabled || now < *next_attempt_) {
    return result;
  }
  next_attempt_ = now + interval;
  if (external_edit_active) {
    result.deferred = 1u;
    next_attempt_ = now + std::chrono::seconds(1);
    return result;
  }

  std::erase_if(saved_states_, [&project](const auto &entry) {
    const OpenSceneSession *scene = project.find(SceneSessionId::from_packed(entry.first));
    return scene == nullptr || scene->scene_id != entry.second.scene_id;
  });
  const ProjectFileSystem &files = project.workspace()->file_system;
  if (retention_pending_) {
    auto available = entries(project);
    if (available.ok()) {
      retention_pending_ = false;
      std::unordered_map<SceneId, std::size_t, SceneIdHash> counts;
      for (const auto &entry : available.value()) {
        if (++counts[entry.scene_id] <=
            static_cast<std::size_t>(settings.autosave_max_files)) {
          continue;
        }
        const auto removed = remove_copy(project, entry);
        if (!removed.ok()) {
          retention_pending_ = true;
          result.warnings.push_back("Could not remove an old recovery copy: " +
                                    removed.error().message);
        }
      }
    } else {
      result.warnings.push_back(available.error().message);
    }
  }
  for (const auto &scene : project.open_scenes()) {
    if (!scene || !scene->session || !scene->dirty()) {
      continue;
    }
    const SceneContentRevisionStamp stamp = scene->session->content_revision_stamp();
    const auto previous = saved_states_.find(scene->session_id.packed());
    if (previous != saved_states_.end() && previous->second.scene_id == scene->scene_id &&
        previous->second.revision == stamp && previous->second.source_path == scene->path &&
        previous->second.scene_name == scene->display_name) {
      continue;
    }
    const auto storage = directory(project);
    if (!storage.ok()) {
      result.ok = false;
      result.warnings.push_back(storage.error().message);
      continue;
    }
    std::error_code error;
    const auto saved_at = std::max<std::int64_t>(last_saved_at_unix_ms_ + 1,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    const std::string snapshot_id(SceneId::generate().str());
    const std::string stem = recovery_filename_stem(scene->display_name, saved_at,
                                                    snapshot_id);
    const auto path = recovery_copy_path(stem, ".qdr");
    ProjectSessionResult written = project.write_scene_recovery(scene->session_id, path);
    if (!written.ok) {
      result.ok = false;
      result.warnings.push_back(scene->display_name + ": " + written.message);
      continue;
    }
    if (!written.changed) {
      ++result.deferred;
      continue;
    }
    const auto metadata_path = recovery_copy_path(stem, ".json");
    const auto metadata = files.resolve_for_write(metadata_path);
    const Json json{
        {"format", "quader.autosave"}, {"version", 2},
        {"project_id", project.manifest()->id().str()},
        {"snapshot_id", snapshot_id},
        {"scene_id", scene->scene_id.str()}, {"name", scene->display_name},
        {"source_path", scene->path ? Json(scene->path->portable_string()) : Json(nullptr)},
        {"saved_at_unix_ms", saved_at},
    };
    const auto published = metadata.ok()
        ? quader_io::write_qdr_text_atomic(metadata.value(), json.dump(2), {},
              quader_io::QdrAtomicWriteDisposition::CreateNew)
        : quader_io::QdrAtomicWriteResult{.message = metadata.error().message};
    if (!published.ok) {
      // This unique candidate has no published metadata. Previous copies stay intact.
      const auto orphan = files.resolve_existing_file(path);
      if (orphan.ok()) {
        std::filesystem::remove(orphan.value(), error);
      }
      result.ok = false;
      result.warnings.push_back("Could not publish autosave metadata: " + published.message);
      continue;
    }
    ++result.written;
    last_saved_at_unix_ms_ = saved_at;
    saved_states_[scene->session_id.packed()] = {
        .scene_id = scene->scene_id, .revision = stamp,
        .source_path = scene->path, .scene_name = scene->display_name,
    };
    result.warnings.insert(result.warnings.end(), written.warnings.begin(), written.warnings.end());
    if (!published.message.empty()) {
      result.warnings.push_back(published.message);
    }

    auto available = entries(project);
    if (!available.ok()) {
      result.warnings.push_back(available.error().message);
      continue;
    }
    // Keep the just-published copy even if the system clock moved backwards.
    std::size_t retained = 1u;
    for (const auto &entry : available.value()) {
      if (entry.scene_id != scene->scene_id || entry.path == path) {
        continue;
      }
      if (retained++ < static_cast<std::size_t>(settings.autosave_max_files)) {
        continue;
      }
      const auto removed = remove_copy(project, entry);
      if (!removed.ok()) {
        result.warnings.push_back("Could not remove an old recovery copy: " +
                                  removed.error().message);
      }
    }
  }
  if (result.deferred != 0u) {
    next_attempt_ = now + std::chrono::seconds(1);
  }
  return result;
}

ProjectSessionResult ProjectAutosaveService::recover(
    ProjectSession &project, const ProjectRelativePath &path) const {
  if (!is_project_autosave_path(path)) {
    return {.message = "Choose a recovery copy from the open project."};
  }
  auto available = entries(project);
  if (!available.ok()) {
    return {.message = available.error().message};
  }
  const auto found = std::find_if(available.value().begin(), available.value().end(),
      [&path](const auto &entry) { return entry.path == path; });
  if (found == available.value().end()) {
    return {.message = "The recovery copy is missing or its metadata is invalid."};
  }
  return project.recover_scene(found->path, found->scene_name + " (Recovered)");
}

} // namespace quader
