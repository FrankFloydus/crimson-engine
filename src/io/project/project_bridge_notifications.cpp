////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_bridge_notifications.hpp"

#include "io/project/project_material_catalog.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::uintmax_t kMaximumBridgeRecordBytes = 64u * 1024u;

ProjectIoResult<void> notification_failure(std::string message,
                                         std::filesystem::path path = {}) {
  return ProjectIoResult<void>::failure(
      ProjectIoErrorCode::IoFailure,
      "Godot synchronization notification failed: " + std::move(message),
      std::move(path));
}

std::string hash_text(std::uint64_t hash) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result(16u, '0');
  for (std::size_t index = result.size(); index != 0u; --index) {
    result[index - 1u] = digits[hash & 0xfu];
    hash >>= 4u;
  }
  return result;
}

ProjectIoResult<std::filesystem::path> record_path(
    const ProjectFileSystem &file_system, std::string_view category,
    std::string_view document_id) {
  const auto relative = ProjectRelativePath::parse_portable(
      ".quader/bridge/" + std::string(category) + "/" +
      std::string(document_id) + ".json");
  if (!relative.ok()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        relative.error().code, relative.error().message);
  }
  return file_system.resolve(relative.value());
}

Json record_fields(const ProjectId &project_id, std::string_view document_id,
                   const ProjectRelativePath &path, std::uint64_t byte_count,
                   std::uint64_t content_hash) {
  return Json{
      {"version", 1},
      {"project_id", project_id.str()},
      {"document_id", document_id},
      {"relative_path", path.portable_string()},
      {"byte_count", std::to_string(byte_count)},
      {"content_hash", hash_text(content_hash)},
  };
}

bool record_matches(const std::filesystem::path &path,
                    const Json &expected_fields) {
  std::error_code error;
  const std::uintmax_t byte_count = std::filesystem::file_size(path, error);
  if (error || byte_count == 0u || byte_count > kMaximumBridgeRecordBytes) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  std::string bytes(static_cast<std::size_t>(byte_count), '\0');
  input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (input.gcount() != static_cast<std::streamsize>(bytes.size())) {
    return false;
  }
  const Json record = Json::parse(bytes, nullptr, false);
  if (!record.is_object() || !record.contains("save_token") ||
      !record["save_token"].is_string() ||
      !SceneId::parse(record["save_token"].get_ref<const std::string &>()).ok()) {
    return false;
  }
  for (const auto &[key, value] : expected_fields.items()) {
    const auto found = record.find(key);
    if (found == record.end() || *found != value) {
      return false;
    }
  }
  return true;
}

ProjectIoResult<void> write_record(
    const ProjectFileSystem &file_system, const ProjectId &project_id,
    std::string_view category, std::string_view document_id,
    const ProjectRelativePath &path, std::uint64_t byte_count,
    std::uint64_t content_hash) {
  if (!project_id.valid() || !path.valid() || byte_count == 0u ||
      !SceneId::parse(document_id).ok()) {
    return notification_failure("Committed document identity is invalid.");
  }
  auto absolute = record_path(file_system, category, document_id);
  if (!absolute.ok()) {
    return notification_failure(absolute.error().message,
                                absolute.error().path);
  }
  std::error_code error;
  std::filesystem::create_directories(absolute.value().parent_path(), error);
  if (error) {
    return notification_failure("Could not create bridge notification folder: " +
                                    error.message(),
                                absolute.value().parent_path());
  }
  // Resolve again after creation to preserve the project containment contract.
  absolute = record_path(file_system, category, document_id);
  if (!absolute.ok()) {
    return notification_failure(absolute.error().message,
                                absolute.error().path);
  }
  Json record = record_fields(project_id, document_id, path, byte_count,
                              content_hash);
  record["save_token"] = SceneId::generate().str();
  const auto written =
      quader_io::write_qdr_text_atomic(absolute.value(), record.dump(2) + "\n");
  if (!written.ok) {
    return notification_failure(written.message, absolute.value());
  }
  return ProjectIoResult<void>::success(
      written.message.empty()
          ? std::string{}
          : "Godot synchronization notification warning: " + written.message);
}

ProjectIoResult<void> ensure_material_baseline(
    const ProjectFileSystem &file_system, const ProjectId &project_id,
    const ProjectMaterialCatalogEntry &material) {
  const auto absolute =
      record_path(file_system, "materials", material.material.id.str());
  if (!absolute.ok()) {
    return notification_failure(absolute.error().message,
                                absolute.error().path);
  }
  if (record_matches(absolute.value(),
                     record_fields(project_id, material.material.id.str(),
                                   material.path, material.file_size,
                                   material.content_revision))) {
    return ProjectIoResult<void>::success();
  }
  // A catalog may outlive external file changes. Only establish a missing
  // baseline when the actual decoded bytes still represent that saved catalog.
  const auto decoded = QmatCodec::read(file_system, material.path);
  if (!decoded.ok()) {
    return notification_failure(decoded.error().message, decoded.error().path);
  }
  if (decoded.value().material.id != material.material.id ||
      decoded.value().file_size != material.file_size ||
      decoded.value().content_revision != material.content_revision) {
    return notification_failure(
        "Saved material changed on disk; reopen it before synchronizing: " +
        material.path.portable_string());
  }
  return write_record(file_system, project_id, "materials",
                      material.material.id.str(), material.path,
                      decoded.value().file_size,
                      decoded.value().content_revision);
}

} // namespace

ProjectIoResult<void> ProjectBridgeNotifications::publish_scene(
    const ProjectFileSystem &file_system, const ProjectId &project_id,
    const SceneId &scene_id, const ProjectRelativePath &path,
    std::uint64_t source_byte_count, std::uint64_t source_content_hash,
    const QdrProjectWriteSnapshot &snapshot,
    const ProjectMaterialCatalog &materials) {
  try {
    std::string warning;
    for (const ProjectAssetReference &dependency :
         snapshot.project_asset_dependencies) {
      if (dependency.kind != ProjectAssetKind::Material) {
        continue;
      }
      const auto *material = materials.find(dependency.stable_id);
      if (material == nullptr || material->path != dependency.path) {
        return notification_failure(
            "Committed scene material is missing from the installed catalog.");
      }
      const auto baseline =
          ensure_material_baseline(file_system, project_id, *material);
      if (!baseline.ok()) {
        return baseline;
      }
      if (!baseline.warning().empty()) {
        warning += warning.empty() ? baseline.warning()
                                   : "; " + baseline.warning();
      }
    }
    const auto scene = write_record(file_system, project_id, "scenes",
                                    scene_id.str(), path, source_byte_count,
                                    source_content_hash);
    if (!scene.ok()) {
      return scene;
    }
    if (!scene.warning().empty()) {
      warning += warning.empty() ? scene.warning() : "; " + scene.warning();
    }
    return ProjectIoResult<void>::success(std::move(warning));
  } catch (const std::exception &exception) {
    return notification_failure(exception.what());
  } catch (...) {
    return notification_failure("Unknown bridge notification error.");
  }
}

ProjectIoResult<void> ProjectBridgeNotifications::publish_material(
    const ProjectFileSystem &file_system, const ProjectId &project_id,
    const ProjectMaterialCatalogEntry &material) {
  try {
    // Catalog installation precedes the outer file-transaction commit. Recheck
    // the committed QMAT without publishing a marker for a replaced/recovered
    // destination that no longer represents the catalog's decoded bytes.
    const auto decoded = QmatCodec::read(file_system, material.path);
    if (!decoded.ok()) {
      return notification_failure(decoded.error().message, decoded.error().path);
    }
    if (decoded.value().material.id != material.material.id ||
        decoded.value().file_size != material.file_size ||
        decoded.value().content_revision != material.content_revision) {
      return notification_failure(
          "Committed material no longer matches the saved catalog: " +
          material.path.portable_string());
    }
    return write_record(file_system, project_id, "materials",
                        material.material.id.str(), material.path,
                        decoded.value().file_size,
                        decoded.value().content_revision);
  } catch (const std::exception &exception) {
    return notification_failure(exception.what());
  } catch (...) {
    return notification_failure("Unknown bridge notification error.");
  }
}

} // namespace quader
