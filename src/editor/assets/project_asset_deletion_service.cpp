////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/assets/project_asset_deletion_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "core/release_features.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "io/project/project_bridge_notifications.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_binary_container.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

namespace quader {
namespace {

constexpr std::string_view kRecoveryRoot = ".quader/asset-deletions";

ProjectRelativePath relative_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  if (!parsed.ok()) {
    throw std::runtime_error(parsed.error().message);
  }
  return parsed.take_value();
}

bool inside(const ProjectRelativePath &path, const ProjectRelativePath &folder) {
  return path == folder ||
         path.portable_string().starts_with(folder.portable_string() + "/");
}

std::filesystem::path resolved_path(const ProjectFileSystem &fs,
                                    const ProjectRelativePath &path) {
  auto resolved = fs.resolve(path);
  const auto physical = fs.resolve_physical_location(path);
  if (!resolved.ok()) {
    throw std::runtime_error(resolved.error().message);
  }
  if (!physical.ok() ||
      physical.value().lexically_normal() != resolved.value().lexically_normal() ||
      std::filesystem::is_symlink(std::filesystem::symlink_status(resolved.value()))) {
    throw std::runtime_error("Asset deletion cannot traverse filesystem links.");
  }
  if (std::filesystem::is_regular_file(resolved.value()) &&
      std::filesystem::hard_link_count(resolved.value()) != 1u) {
    throw std::runtime_error("Asset deletion cannot modify hard-linked files.");
  }
  return resolved.take_value();
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Cannot read asset deletion source: " + path.string());
  }
  std::string bytes{std::istreambuf_iterator<char>(stream), {}};
  if (stream.bad()) {
    throw std::runtime_error("Cannot finish reading asset deletion source.");
  }
  return bytes;
}

std::uint64_t hash_bytes(std::string_view bytes) {
  return quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(bytes.data(), bytes.size())));
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream) {
    throw std::runtime_error("Cannot stage asset deletion data.");
  }
}

void write_journal(const std::filesystem::path &path,
                   const nlohmann::json &journal) {
  const auto result = quader_io::write_qdr_text_atomic(path, journal.dump(2));
  if (!result.ok) {
    throw std::runtime_error(result.message);
  }
}

void require_io(const ProjectIoResult<void> &result) {
  if (!result.ok()) {
    throw std::runtime_error(result.error().message);
  }
}

template <typename T>
bool contains(std::span<const T> values, const T &value) {
  return std::ranges::find(values, value) != values.end();
}

template <typename T>
bool contains(const std::vector<T> &values, const T &value) {
  return contains(std::span<const T>(values), value);
}

std::vector<ProjectRelativePath> selection_roots(
    std::span<const ProjectRelativePath> paths) {
  std::vector<ProjectRelativePath> sorted(paths.begin(), paths.end());
  std::ranges::sort(sorted, {}, &ProjectRelativePath::portable_string);
  std::vector<ProjectRelativePath> roots;
  for (const auto &path : sorted) {
    if (!std::ranges::any_of(roots, [&](const auto &root) { return inside(path, root); })) {
      roots.push_back(path);
    }
  }
  return roots;
}

struct SelectedSources final {
  std::vector<ProjectAssetEntry> entries;
  std::vector<ProjectRelativePath> directories;
};

SelectedSources inspect_selection(const ProjectFileSystem &fs,
                                  const ProjectAssetSnapshot &catalog,
                                  std::span<const ProjectRelativePath> roots) {
  std::map<std::string, const ProjectAssetEntry *> by_path;
  for (const auto &entry : catalog.entries) {
    by_path.emplace(entry.path.portable_string(), &entry);
  }
  SelectedSources selected;
  const auto inspect = [&](const ProjectRelativePath &path) {
    const auto found = by_path.find(path.portable_string());
    if (found == by_path.end() || is_project_asset_reserved_path(path)) {
      throw std::runtime_error("Selection contains unsupported or private project content: " +
                               path.portable_string());
    }
    const auto absolute = resolved_path(fs, path);
    if (std::filesystem::is_directory(absolute)) {
      selected.directories.push_back(path);
    } else if (std::filesystem::is_regular_file(absolute)) {
      if (found->second->asset_id == *ProjectAssetId::parse(kDefaultProjectMaterialId)) {
        throw std::runtime_error("Default Material cannot be deleted.");
      }
      selected.entries.push_back(*found->second);
    } else {
      throw std::runtime_error("Selected asset is no longer a regular file or directory.");
    }
  };
  for (const auto &root : roots) {
    inspect(root);
    const auto absolute = resolved_path(fs, root);
    if (std::filesystem::is_directory(absolute)) {
      for (const auto &child : std::filesystem::recursive_directory_iterator(absolute)) {
        const auto relative = fs.relative_from_absolute(child.path());
        if (!relative.ok()) {
          throw std::runtime_error(relative.error().message);
        }
        inspect(relative.value());
      }
    }
  }
  std::ranges::sort(selected.entries, {}, [](const auto &entry) {
    return entry.path.portable_string();
  });
  std::ranges::sort(selected.directories, {}, &ProjectRelativePath::portable_string);
  return selected;
}

std::shared_ptr<const ProjectAssetDependencySnapshot> deletion_dependencies(
    ProjectSession &project, const SelectedSources &selected,
    std::string_view manifest_bytes) {
  if (std::ranges::any_of(selected.entries, [](const auto &entry) {
        return entry.kind == ProjectAssetKind::Material || entry.kind == ProjectAssetKind::Texture;
      })) {
    return project.dependencies().snapshot(true);
  }
  // Scenes have no inbound scene references. Closing/recycling only scenes
  // needs their saved identities and resident drafts, not every QMAT and scene
  // dependency in the project. Exact selected file bytes are sealed below.
  auto graph = std::make_shared<ProjectAssetDependencySnapshot>();
  graph->project_id = project.manifest()->id();
  graph->project_generation = project.activation_generation();
  const auto persisted = ProjectManifestStore::deserialize(manifest_bytes);
  const auto &manifest = *project.manifest();
  if (!persisted.ok() || persisted.value().id() != manifest.id() ||
      persisted.value().texture_metadata_fingerprint() !=
          project.workspace()->persisted_texture_metadata_fingerprint ||
      !std::ranges::equal(persisted.value().scenes(), manifest.scenes()) ||
      !std::ranges::equal(persisted.value().deleted_material_ids(), manifest.deleted_material_ids())) {
    graph->errors.push_back("The project manifest changed on disk. Reopen the project before deleting project assets.");
    return graph;
  }
  std::string identity;
  for (const auto &entry : selected.entries) {
    const auto *registered = manifest.find_scene(entry.path);
    if (registered == nullptr) continue;
    const auto source = QdrDocumentReader::read_project_dependencies(
        resolved_path(project.workspace()->file_system, entry.path));
    const bool resident_reseed = std::ranges::any_of(project.open_scenes(), [&](const auto &open) {
      return open->identity_dirty && open->scene_id == registered->id && open->path == entry.path;
    });
    if (!source.ok || (SceneId::from_bytes(source.scene_uuid) != registered->id && !resident_reseed)) {
      graph->errors.push_back(entry.path.portable_string() + ": scene identity differs from the project manifest.");
      return graph;
    }
    graph->scenes.push_back({.scene_id = registered->id, .path = entry.path,
        .display_name = source.scene_name, .content_revision = source.source_content_hash});
    for (const auto &open : project.open_scenes()) {
      if (open->scene_id != registered->id) continue;
      const auto stamp = open->session->content_revision_stamp();
      identity += std::to_string(open->session_id.packed()) + ":" +
                  std::to_string(stamp.project_revision) + ":" +
                  std::to_string(stamp.scene_revision) + ":" +
                  std::to_string(stamp.asset_revision) + ":" +
                  std::to_string(stamp.modeling_content_revision) + ":" +
                  std::to_string(open->dirty());
      graph->scenes.push_back({.scene_id = open->scene_id, .session_id = open->session_id,
          .path = open->path, .display_name = open->display_name, .live = true,
          .dirty = open->dirty(), .content_revision = hash_bytes(identity)});
    }
  }
  graph->revision = hash_bytes(identity);
  graph->complete = true;
  return graph;
}

void normalize_component(MaterialComponent &component,
                         const ProjectManifest &manifest) {
  const auto normalize = [&](MaterialReference &reference) {
    if (reference.project_owned()) {
      reference = MaterialReference::project(
          ProjectMaterialReferenceResolver::canonical_material_id(
              manifest, reference.project_asset()));
    }
  };
  normalize(component.material);
  for (auto &[slot, material] : component.material_slots) {
    (void)slot;
    normalize(material);
  }
}

void normalize_snapshot(QdrProjectWriteSnapshot &snapshot,
                        const ProjectManifest &manifest,
                        const ProjectMaterialCatalog &catalog) {
  std::set<ProjectAssetId> ids;
  for (auto &entity : snapshot.entities) {
    if (!entity.material) {
      continue;
    }
    normalize_component(*entity.material, manifest);
    const auto add = [&](const MaterialReference &reference) {
      if (reference.project_owned()) {
        ids.insert(reference.project_asset());
      }
    };
    add(entity.material->material);
    for (const auto &[slot, material] : entity.material->material_slots) {
      (void)slot;
      add(material);
    }
  }
  std::erase_if(snapshot.project_asset_dependencies, [](const auto &reference) {
    return reference.kind == ProjectAssetKind::Material;
  });
  for (const auto id : ids) {
    const auto *entry = catalog.find(id);
    if (entry == nullptr) {
      throw std::runtime_error("Replacement scene references an unavailable material.");
    }
    snapshot.project_asset_dependencies.push_back(
        {.stable_id = id, .kind = ProjectAssetKind::Material, .path = entry->path});
  }
}

struct StagedDependency final {
  ProjectRelativePath path;
  std::unique_ptr<quader_io::QdrAtomicFileTransaction> transaction =
      std::make_unique<quader_io::QdrAtomicFileTransaction>();
  std::optional<SceneId> scene_id;
  std::optional<QdrProjectWriteSnapshot> scene_snapshot;
  std::optional<ProjectAssetId> material_id;
};

struct LiveReplacement final {
  OpenSceneSession *scene = nullptr;
  SceneModelingProjectionCheckpoint checkpoint;
  std::vector<std::pair<EntityId, MaterialComponent>> components;
  SceneContentRevisionStamp previous_stamp;
  bool applied = false;
};

void verify_bytes(std::string_view bytes, const nlohmann::json &entry,
                  bool replacement = false) {
  const char *size_key = replacement ? "replacement_size" : "size";
  const char *hash_key = replacement ? "replacement_hash" : "hash";
  if (bytes.size() != entry.at(size_key).get<std::uint64_t>() ||
      hash_bytes(bytes) != entry.at(hash_key).get<std::uint64_t>()) {
    throw std::runtime_error("Asset deletion source changed: " +
                             entry.at("path").get<std::string>());
  }
}

std::filesystem::path backup_path(const ProjectFileSystem &fs,
                                  const ProjectRelativePath &directory,
                                  const nlohmann::json &entry) {
  const auto name = entry.at("backup").get<std::string>();
  if (!name.starts_with("file-") || !name.ends_with(".old") ||
      name.find_first_of("/\\:") != std::string::npos) {
    throw std::runtime_error("Invalid asset deletion backup name.");
  }
  return resolved_path(fs, relative_path(directory.portable_string() + "/" + name));
}

std::filesystem::path private_path(const ProjectFileSystem &fs,
                                   const ProjectRelativePath &target,
                                   std::string_view text) {
  const auto path = relative_path(text);
  auto prefix = std::filesystem::path(".");
  prefix += target.native_path().filename();
  prefix += ".";
  if (path.native_path().parent_path() != target.native_path().parent_path() ||
      !path.native_path().filename().native().starts_with(prefix.native()) ||
      (path.native_path().extension() != ".rollback" &&
       path.native_path().extension() != ".stage")) {
    throw std::runtime_error("Invalid asset deletion private file location.");
  }
  return resolved_path(fs, path);
}

void remove_recovery_directory(const ProjectFileSystem &fs,
                               const ProjectRelativePath &directory) {
  const auto root = relative_path(kRecoveryRoot);
  if (directory == root || directory.native_path().parent_path() != root.native_path()) {
    throw std::runtime_error("Invalid asset deletion recovery directory.");
  }
  const auto absolute = resolved_path(fs, directory);
  for (const auto &entry : std::filesystem::directory_iterator(absolute)) {
    const auto relative = fs.relative_from_absolute(entry.path());
    if (!relative.ok() || !std::filesystem::is_regular_file(
                              resolved_path(fs, relative.value()))) {
      throw std::runtime_error("Unexpected content in asset deletion recovery directory.");
    }
  }
  std::filesystem::remove_all(absolute);
}

/// Validate every source before restoring any; never replace unrelated new data.
void restore_journal(const ProjectFileSystem &fs,
                     const ProjectRelativePath &directory,
                     const nlohmann::json &journal) {
  struct Restore final {
    ProjectRelativePath path;
    std::string bytes;
    bool replace = false;
  };
  std::vector<Restore> restores;
  std::vector<std::filesystem::path> private_files;
  std::set<std::string> unique;
  for (const auto &entry : journal.at("files")) {
    const auto path = relative_path(entry.at("path").get<std::string>());
    if (!unique.insert(path.portable_string()).second ||
        inside(path, relative_path(".quader"))) {
      throw std::runtime_error("Invalid duplicate or private recovery destination.");
    }
    const auto target = resolved_path(fs, path);
    auto bytes = read_bytes(backup_path(fs, directory, entry));
    verify_bytes(bytes, entry);
    const bool exists = std::filesystem::exists(target);
    bool unchanged = false;
    if (exists) {
      const auto current = read_bytes(target);
      unchanged = current == bytes;
      if (!unchanged) {
        if (!entry.contains("replacement_size")) {
          throw std::runtime_error("Newer asset content prevents recovery: " + path.portable_string());
        }
        verify_bytes(current, entry, true);
      }
    }
    if (entry.contains("rollback")) {
      const auto rollback = private_path(fs, path, entry.at("rollback").get<std::string>());
      if (std::filesystem::exists(rollback)) {
        verify_bytes(read_bytes(rollback), entry);
        private_files.push_back(rollback);
      }
    }
    if (entry.contains("stage")) {
      const auto stage = private_path(fs, path, entry.at("stage").get<std::string>());
      if (std::filesystem::exists(stage)) {
        verify_bytes(read_bytes(stage), entry, true);
        private_files.push_back(stage);
      }
    }
    if (!unchanged) {
      restores.push_back({path, std::move(bytes), exists});
    }
  }
  std::vector<std::filesystem::path> directories;
  for (const auto &entry : journal.at("directories")) {
    const auto path = relative_path(entry.get<std::string>());
    if (is_project_asset_reserved_path(path)) {
      throw std::runtime_error("Recovery contains a reserved directory.");
    }
    const auto absolute = resolved_path(fs, path);
    if (std::filesystem::exists(absolute) && !std::filesystem::is_directory(absolute)) {
      throw std::runtime_error("New content prevents restoring an asset folder.");
    }
    directories.push_back(absolute);
  }
  for (const auto &directory_path : directories) {
    std::filesystem::create_directories(directory_path);
  }
  // Revalidate immediately before each publication as external writers may race
  // the preflight. Atomic replacement itself retains displaced bytes.
  for (const auto &restore : restores) {
    const auto target = resolved_path(fs, restore.path);
    const auto found = std::ranges::find_if(journal.at("files"), [&](const auto &entry) {
      return entry.at("path").template get<std::string>() == restore.path.portable_string();
    });
    if (std::filesystem::exists(target)) {
      verify_bytes(read_bytes(target), *found, true);
    } else if (restore.replace) {
      throw std::runtime_error("Recovery destination changed during validation.");
    }
    quader_io::QdrAtomicFileTransaction transaction;
    auto result = transaction.prepare(target, restore.replace
        ? quader_io::QdrAtomicWriteDisposition::ReplaceExisting
        : quader_io::QdrAtomicWriteDisposition::CreateNew);
    if (result.ok) {
      write_bytes(transaction.staging_path(), restore.bytes);
      result = transaction.publish();
    }
    if (!result.ok) {
      throw std::runtime_error(result.message);
    }
    if (restore.replace) {
      try {
        verify_bytes(read_bytes(transaction.backup_path()), *found, true);
      } catch (...) {
        (void)transaction.rollback();
        throw;
      }
    }
    result = transaction.commit();
    if (!result.ok) {
      (void)transaction.preserve_published_for_recovery();
      throw std::runtime_error(result.message);
    }
  }
  for (const auto &path : private_files) {
    std::filesystem::remove(path);
  }
}

bool retry_committed_notifications(const ProjectWorkspaceStorage &workspace,
                                   const nlohmann::json &journal,
                                   std::string &warnings) {
  const auto &fs = workspace.file_system;
  auto catalog = ProjectMaterialCatalog::scan(fs, nullptr, &workspace.manifest);
  if (!catalog.ok()) {
    warnings += " " + catalog.error().message;
    return false;
  }
  bool complete = true;
  for (const auto &file : journal.at("files")) {
    if (!file.contains("scene_id") && !file.contains("material_id")) {
      continue;
    }
    const auto path = relative_path(file.at("path").get<std::string>());
    const auto absolute = resolved_path(fs, path);
    if (!std::filesystem::is_regular_file(absolute)) {
      warnings += " A dependency was moved after committed asset deletion; its newer state was preserved.";
      continue;
    }
    const auto bytes = read_bytes(absolute);
    if (bytes.size() != file.at("replacement_size").get<std::uint64_t>() ||
        hash_bytes(bytes) != file.at("replacement_hash").get<std::uint64_t>()) {
      warnings += " A dependency changed after committed asset deletion; its newer content was preserved.";
      continue;
    }
    if (file.contains("scene_id")) {
      const auto parsed = SceneId::parse(file.at("scene_id").get<std::string>());
      const auto *scene = workspace.manifest.find_scene(path);
      if (!parsed.ok() || scene == nullptr || scene->id != parsed.value()) {
        warnings += " A scene identity changed after committed asset deletion; its newer state was preserved.";
        continue;
      }
      auto read = QdrDocumentReader::read_project_document(absolute);
      if (!read.ok || SceneId::from_bytes(read.scene_uuid) != scene->id ||
          read.source_byte_count != bytes.size() || read.source_content_hash != hash_bytes(bytes)) {
        warnings += " Could not validate a committed scene notification.";
        complete = false;
        continue;
      }
      auto extracted = qdr_project_write_snapshot_from_document(read.document);
      if (!extracted.ok()) {
        warnings += " " + extracted.error().message;
        complete = false;
        continue;
      }
      auto snapshot = extracted.take_value();
      snapshot.minimum_product_release = read.minimum_product_release;
      snapshot.viewport_camera_pose = read.viewport_camera_pose;
      snapshot.project_asset_dependencies = std::move(read.project_asset_dependencies);
      const auto notified = ProjectBridgeNotifications::publish_scene(
          fs, workspace.manifest.id(), scene->id, path,
          read.source_byte_count, read.source_content_hash, snapshot, catalog.value());
      if (!notified.ok()) {
        warnings += " " + notified.error().message;
        complete = false;
      }
    } else {
      const auto id = ProjectAssetId::parse(file.at("material_id").get<std::string>());
      const auto *material = id ? catalog.value().find(*id) : nullptr;
      if (material == nullptr || material->path != path) {
        warnings += " A material identity changed after committed asset deletion; its newer state was preserved.";
        continue;
      }
      const auto notified = ProjectBridgeNotifications::publish_material(fs, workspace.manifest.id(), *material);
      if (!notified.ok()) {
        warnings += " " + notified.error().message;
        complete = false;
      }
    }
  }
  return complete;
}

} // namespace

ProjectAssetDeletionPreview ProjectAssetDeletionService::preview(
    std::span<const ProjectRelativePath> paths) const {
  ProjectAssetDeletionPreview result;
  try {
    if (!project_->has_project() || paths.empty()) {
      throw std::runtime_error("Select project assets to delete.");
    }
    const auto &workspace = *project_->workspace();
    result.project_id = workspace.manifest.id();
    result.paths = selection_roots(paths);
    ProjectAssetService assets(*project_);
    const auto catalog = assets.inspect(result.paths);
    if (!catalog.ok()) {
      throw std::runtime_error(catalog.error().message);
    }
    const auto selected = inspect_selection(workspace.file_system, catalog.value(), result.paths);
    const auto manifest_bytes = read_bytes(workspace.manifest_path);
    const auto graph = deletion_dependencies(*project_, selected, manifest_bytes);
    if (!graph || !graph->complete) {
      result.errors = graph ? graph->errors : std::vector<std::string>{"Dependency inspection failed."};
      return result;
    }
    std::string identity = std::string(result.project_id.str()) + ":" +
                           std::to_string(graph->project_generation) + ":" +
                           std::to_string(graph->revision);
    for (const auto &root : result.paths) {
      identity += ":root:" + root.portable_string();
    }
    for (const auto &directory : selected.directories) {
      identity += ":dir:" + directory.portable_string();
    }
    for (const auto &entry : selected.entries) {
      if ((entry.kind == ProjectAssetKind::Material || entry.kind == ProjectAssetKind::Texture) &&
          !release_has_feature(ReleaseFeature::Materials)) {
        throw std::runtime_error(release_feature_unavailable_message());
      }
      const auto bytes = read_bytes(resolved_path(workspace.file_system, entry.path));
      identity += ":file:" + entry.path.portable_string() + ":" +
                   std::to_string(bytes.size()) + ":" + std::to_string(hash_bytes(bytes));
      if (entry.kind == ProjectAssetKind::Scene) {
        // Physical copies may retain another scene's embedded UUID. Only the
        // project manifest authorizes removal of a resident/catalog identity.
        if (const auto *scene = workspace.manifest.find_scene(entry.path)) {
          result.scene_ids.push_back(scene->id);
        }
      } else if (entry.kind == ProjectAssetKind::Material) {
        result.material_ids.push_back(entry.asset_id);
      } else if (entry.kind == ProjectAssetKind::Texture) {
        if (entry.path == relative_path(kDefaultProjectAlbedoTexturePath)) {
          throw std::runtime_error("Default Material's reserved texture cannot be deleted.");
        }
        if (entry.asset_id.valid()) {
          result.texture_ids.push_back(entry.asset_id);
        }
        if (const auto *texture = workspace.manifest.find_texture(entry.asset_id)) {
          result.texture_ids.insert(result.texture_ids.end(), texture->aliases.begin(), texture->aliases.end());
        }
      }
    }
    if ((!result.material_ids.empty() || !result.texture_ids.empty()) &&
        !release_has_feature(ReleaseFeature::Materials)) {
      throw std::runtime_error(release_feature_unavailable_message());
    }
    std::ranges::sort(result.texture_ids);
    result.texture_ids.erase(std::unique(result.texture_ids.begin(), result.texture_ids.end()), result.texture_ids.end());
    if (contains(result.texture_ids, *ProjectAssetId::parse(kDefaultProjectAlbedoTextureId))) {
      throw std::runtime_error("Default Material's reserved texture cannot be deleted.");
    }
    identity += ":manifest:" + std::to_string(hash_bytes(manifest_bytes));
    for (const auto &scene : graph->scenes) {
      if (contains(result.scene_ids, scene.scene_id) ||
          std::ranges::any_of(scene.material_ids, [&](auto id) { return contains(result.material_ids, id); })) {
        result.affected_scenes.push_back(scene);
      }
      if (scene.live && scene.session_id && contains(result.scene_ids, scene.scene_id)) {
        const bool pending = project_->scene_has_pending_edits(*scene.session_id);
        const auto *open = project_->find(*scene.session_id);
        identity += ":pending:" + std::to_string(pending) + ":" +
                    std::to_string(open->session->modeling().tool_preview_source_revision());
        if (scene.dirty || pending) {
          result.dirty_scene_sessions.push_back(*scene.session_id);
        }
      }
    }
    // A clean resident copy represents the same dependency as its saved file.
    // Dirty/pending copies remain separate because deletion affects both states.
    std::vector<ProjectSceneAssetDependencies> displayed_scenes;
    for (const auto &scene : result.affected_scenes) {
      bool represented_by_live = false;
      if (!scene.live && scene.path) {
        const auto saved_count = std::ranges::count_if(result.affected_scenes, [&](const auto &other) {
          return !other.live && other.scene_id == scene.scene_id && other.path == scene.path;
        });
        const auto live_count = std::ranges::count_if(result.affected_scenes, [&](const auto &other) {
          return other.live && other.scene_id == scene.scene_id && other.path == scene.path;
        });
        represented_by_live = saved_count == 1 && live_count == 1 &&
            std::ranges::any_of(result.affected_scenes, [&](const auto &other) {
              return other.live && !other.dirty && other.session_id &&
                     !project_->scene_has_pending_edits(*other.session_id) &&
                     other.scene_id == scene.scene_id && other.path == scene.path &&
                     other.material_ids == scene.material_ids;
            });
      }
      if (!represented_by_live) {
        displayed_scenes.push_back(scene);
      }
    }
    result.affected_scenes = std::move(displayed_scenes);
    for (const auto &material : graph->materials) {
      if (contains(result.material_ids, material.id)) {
        result.discards_material_draft |= material.draft && material.dirty;
      } else if (std::ranges::any_of(material.textures, [&](const auto &texture) {
                   return texture.texture_id && contains(result.texture_ids, *texture.texture_id);
                 })) {
        result.affected_materials.push_back(material);
      }
    }
    result.revision = hash_bytes(identity);
    if (result.revision == 0u) {
      result.revision = 1u;
    }
    result.complete = true;
  } catch (const std::exception &error) {
    result.errors.push_back(error.what());
  }
  return result;
}

ProjectSessionResult ProjectAssetDeletionService::erase(
    std::span<const ProjectRelativePath> paths, std::uint64_t preview_revision,
    std::span<const SceneSessionId> approved_dirty_scenes) const {
  const auto checked = preview(paths);
  if (!checked.complete || checked.revision != preview_revision) {
    return {.message = checked.errors.empty()
        ? "Project assets changed. Review a new deletion warning."
        : checked.errors.front()};
  }
  if (!recycle_) {
    return {.message = "The project asset Recycle Bin service is unavailable."};
  }
  for (const auto id : checked.dirty_scene_sessions) {
    if (!contains(approved_dirty_scenes, id)) {
      return {.message = "Save a copy or approve discarding each dirty scene before deleting it."};
    }
  }
  auto &workspace = *project_->workspace_;
  const auto fs = workspace.file_system;
  auto &browser_assets = project_->project_assets();
  std::optional<ProjectAssetSnapshot> browser_snapshot;
  const auto material_revision = project_->material_catalog().material_revision();
  if (browser_assets.cached_ && browser_assets.cached_->ok() &&
      browser_assets.cached_project_ == workspace.manifest.id() &&
      browser_assets.cached_material_revision_ == project_->material_catalog().content_revision() &&
      browser_assets.cached_project_generation_ == project_->project_generation()) {
    browser_snapshot = browser_assets.cached_->value();
  }
  ProjectRelativePath transaction_relative;
  std::filesystem::path transaction_directory;
  std::vector<StagedDependency> staged;
  std::vector<LiveReplacement> live;
  nlohmann::json journal;
  bool journal_written = false;
  bool recycle_attempted = false;
  bool committed = false;
  ProjectSessionResult result;
  try {
    ProjectAssetService assets(*project_);
    const auto catalog = assets.inspect(checked.paths);
    if (!catalog.ok()) {
      throw std::runtime_error(catalog.error().message);
    }
    const auto selected = inspect_selection(fs, catalog.value(), checked.paths);
    const auto dependency_graph = checked.material_ids.empty()
        ? std::shared_ptr<const ProjectAssetDependencySnapshot>{}
        : project_->dependencies().snapshot();
    struct ResidentStamp final {
      SceneSessionId id;
      SceneContentRevisionStamp content;
      std::uint64_t preview_revision = 0u;
      bool pending = false;
    };
    std::vector<ResidentStamp> resident_stamps;
    for (const auto &open : project_->open_scenes()) {
      resident_stamps.push_back({open->session_id, open->session->content_revision_stamp(),
          open->session->modeling().tool_preview_source_revision(),
          project_->scene_has_pending_edits(open->session_id)});
    }
    const auto *draft_before = project_->standard_material_editor().current();
    const auto draft_session = draft_before != nullptr ? draft_before->session_id : MaterialEditSessionId{};
    const auto draft_revision = draft_before != nullptr ? draft_before->draft_revision : 0u;
    const auto material_preview_revision = project_->standard_material_editor().scene_preview_revision();
    auto candidate = workspace.manifest;
    candidate.require_product_release(current_product_release());
    require_io(candidate.mark_materials_deleted(checked.material_ids));
    require_io(candidate.mark_textures_deleted(checked.texture_ids));
    for (const auto &id : checked.scene_ids) {
      if (candidate.find_scene(id) != nullptr) {
        require_io(candidate.unregister_scene(id));
      }
    }
    const auto prepared_manifest = project_->prepare_asset_manifest(candidate, checked.scene_ids);
    if (!prepared_manifest.ok) {
      throw std::runtime_error(prepared_manifest.message);
    }
    transaction_relative = relative_path(std::string(kRecoveryRoot) + "/" +
                                         std::string(ProjectAssetId::generate().str()));
    transaction_directory = resolved_path(fs, transaction_relative);
    std::filesystem::create_directories(transaction_directory);
    journal = {{"version", 1}, {"project", workspace.manifest.id().str()},
               {"state", "prepared"}, {"files", nlohmann::json::array()},
               {"directories", nlohmann::json::array()}};
    for (const auto &directory : selected.directories) {
      journal["directories"].push_back(directory.portable_string());
    }
    const auto backup = [&](const ProjectRelativePath &path) -> std::size_t {
      const auto bytes = read_bytes(resolved_path(fs, path));
      const std::string name = "file-" + std::to_string(journal["files"].size()) + ".old";
      const auto saved = quader_io::write_qdr_text_atomic(transaction_directory / name, bytes,
          {}, quader_io::QdrAtomicWriteDisposition::CreateNew);
      if (!saved.ok) {
        throw std::runtime_error(saved.message);
      }
      const auto index = journal["files"].size();
      journal["files"].push_back({{"path", path.portable_string()}, {"backup", name},
                                  {"size", bytes.size()}, {"hash", hash_bytes(bytes)}});
      verify_bytes(read_bytes(transaction_directory / name), journal["files"][index]);
      return index;
    };
    for (const auto &entry : selected.entries) {
      (void)backup(entry.path);
    }
    std::vector<std::size_t> staged_indices;
    const auto stage = [&](const ProjectRelativePath &path, std::string_view bytes,
                           std::optional<SceneId> scene_id = std::nullopt,
                           std::optional<QdrProjectWriteSnapshot> snapshot = std::nullopt,
                           std::optional<ProjectAssetId> material_id = std::nullopt) {
      const auto index = backup(path);
      auto &entry = journal["files"][index];
      entry["replacement_size"] = bytes.size();
      entry["replacement_hash"] = hash_bytes(bytes);
      if (scene_id) {
        entry["scene_id"] = std::string(scene_id->str());
      }
      if (material_id) {
        entry["material_id"] = std::string(material_id->str());
      }
      StagedDependency file;
      file.path = path;
      file.scene_id = scene_id;
      file.scene_snapshot = std::move(snapshot);
      file.material_id = material_id;
      const auto prepared = file.transaction->prepare(resolved_path(fs, path));
      if (!prepared.ok) {
        throw std::runtime_error(prepared.message);
      }
      write_bytes(file.transaction->staging_path(), bytes);
      const auto stage_path = fs.relative_from_absolute(file.transaction->staging_path());
      if (!stage_path.ok()) {
        throw std::runtime_error(stage_path.error().message);
      }
      entry["stage"] = stage_path.value().portable_string();
      staged_indices.push_back(index);
      staged.push_back(std::move(file));
    };
    const std::span<const ProjectSceneAssetDependencies> saved_dependencies = dependency_graph
        ? std::span<const ProjectSceneAssetDependencies>(dependency_graph->scenes)
        : std::span<const ProjectSceneAssetDependencies>{};
    for (const auto &scene : saved_dependencies) {
      if (scene.live || !scene.path || contains(checked.scene_ids, scene.scene_id) ||
          !std::ranges::any_of(scene.material_ids, [&](auto id) {
            return contains(checked.material_ids, id);
          })) {
        continue;
      }
      auto read = QdrDocumentReader::read_project_document(resolved_path(fs, *scene.path));
      const auto *registered = workspace.manifest.find_scene(*scene.path);
      if (!read.ok || registered == nullptr || registered->id != scene.scene_id) {
        throw std::runtime_error("Cannot prepare saved scene dependency replacement.");
      }
      auto extracted = qdr_project_write_snapshot_from_document(read.document);
      if (!extracted.ok()) {
        throw std::runtime_error(extracted.error().message);
      }
      auto snapshot = extracted.take_value();
      snapshot.minimum_product_release = read.minimum_product_release;
      snapshot.viewport_camera_pose = read.viewport_camera_pose;
      snapshot.project_asset_dependencies = std::move(read.project_asset_dependencies);
      normalize_snapshot(snapshot, candidate, project_->material_catalog());
      const auto bytes = QdrDocumentWriter::serialize_project_document_bytes(snapshot, scene.scene_id.bytes());
      stage(*scene.path, std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()),
            scene.scene_id, std::move(snapshot));
    }
    for (const auto &dependency : checked.affected_materials) {
      if (dependency.draft || !dependency.path) {
        continue;
      }
      auto decoded = QmatCodec::read(fs, *dependency.path);
      if (!decoded.ok() || decoded.value().material.id != dependency.id) {
        throw std::runtime_error("Cannot prepare saved material dependency replacement.");
      }
      auto document = decoded.take_value().material;
      auto *standard = document.standard_material();
      if (standard == nullptr) {
        throw std::runtime_error("Cannot rewrite unsupported material dependencies.");
      }
      for (const auto slot : {StandardMaterialTextureSlot::Albedo, StandardMaterialTextureSlot::Roughness,
                              StandardMaterialTextureSlot::Metallic, StandardMaterialTextureSlot::Normal}) {
        auto *texture = standard->texture(slot);
        if (texture->has_value() && contains(checked.texture_ids, (*texture)->id)) {
          texture->reset();
        }
      }
      const auto encoded = QmatCodec::encode(document);
      if (!encoded.ok()) {
        throw std::runtime_error(encoded.error().message);
      }
      stage(*dependency.path, encoded.value(), std::nullopt, std::nullopt, dependency.id);
    }
    const auto manifest_path = fs.relative_from_absolute(workspace.manifest_path);
    if (!manifest_path.ok()) {
      throw std::runtime_error(manifest_path.error().message);
    }
    stage(manifest_path.value(), ProjectManifestStore::serialize(candidate));
    for (const auto &open : project_->open_scenes()) {
      if (contains(checked.scene_ids, open->scene_id)) {
        continue;
      }
      LiveReplacement replacement;
      replacement.scene = open.get();
      replacement.previous_stamp = open->session->content_revision_stamp();
      std::vector<EntityId> participants;
      for (const auto entity : open->session->scene().entities()) {
        const auto *component = open->session->scene().material(entity);
        if (component == nullptr) {
          continue;
        }
        auto normalized = *component;
        normalize_component(normalized, candidate);
        if (normalized != *component) {
          participants.push_back(entity);
          replacement.components.emplace_back(entity, std::move(normalized));
        }
      }
      if (!participants.empty()) {
        std::set<EntityId> enrolled(participants.begin(), participants.end());
        for (std::size_t index = 0u; index < participants.size(); ++index) {
          if (const auto *hierarchy = open->session->scene().hierarchy(participants[index])) {
            for (const auto child : hierarchy->children) {
              if (enrolled.insert(child).second) {
                participants.push_back(child);
              }
            }
          }
        }
        replacement.checkpoint = open->session->scene().capture_modeling_projection_checkpoint(
            participants, SceneModelingProjectionDomain::Structure |
                              SceneModelingProjectionDomain::MeshBindings |
                              SceneModelingProjectionDomain::CurveBindings |
                              SceneModelingProjectionDomain::Selection);
        if (!replacement.checkpoint.valid()) {
          throw std::runtime_error("Cannot prepare live material reference compensation.");
        }
        live.push_back(std::move(replacement));
      }
    }
    const auto final = preview(paths);
    if (!final.complete || final.revision != checked.revision) {
      throw std::runtime_error("Project changed while preparing deletion. Review a new warning.");
    }
    write_journal(transaction_directory / "journal.json", journal);
    journal_written = true;
    for (std::size_t index = 0u; index < staged.size(); ++index) {
      auto &file = staged[index];
      auto &entry = journal["files"][staged_indices[index]];
      verify_bytes(read_bytes(file.transaction->target_path()), entry);
      const auto published = file.transaction->publish([&](const auto &rollback) {
        try {
          const auto path = fs.relative_from_absolute(rollback);
          if (!path.ok()) {
            return quader_io::QdrAtomicWriteResult{false, path.error().message};
          }
          entry["rollback"] = path.value().portable_string();
          write_journal(transaction_directory / "journal.json", journal);
          return quader_io::QdrAtomicWriteResult{true, {}};
        } catch (const std::exception &error) {
          return quader_io::QdrAtomicWriteResult{false, error.what()};
        }
      });
      if (!published.ok) {
        throw std::runtime_error(published.message);
      }
      verify_bytes(read_bytes(file.transaction->backup_path()), entry);
    }
    if (hooks_.after_dependency_publication) {
      hooks_.after_dependency_publication();
    }
    for (const auto &entry : journal["files"]) {
      const auto path = relative_path(entry.at("path").get<std::string>());
      verify_bytes(read_bytes(resolved_path(fs, path)), entry, entry.contains("replacement_size"));
    }
    // Reinspect folders after staging; private stage/rollback files live only
    // beside dependencies outside the deletion set and must never be recycled.
    (void)inspect_selection(fs, catalog.value(), checked.paths);
    std::vector<std::filesystem::path> originals;
    for (const auto &path : checked.paths) {
      originals.push_back(resolved_path(fs, path));
    }
    journal["state"] = "recycling";
    write_journal(transaction_directory / "journal.json", journal);
    recycle_attempted = true;
    const auto recycled = recycle_(originals);
    if (!recycled.ok || recycled.aborted || recycled.items.size() != originals.size()) {
      throw std::runtime_error(recycled.message.empty() ? "The asset recycling operation was incomplete." : recycled.message);
    }
    for (const auto &path : originals) {
      const auto reported = std::ranges::find_if(recycled.items, [&](const auto &item) {
        return item.original_path == path && item.recycled;
      });
      if (reported == recycled.items.end() || std::filesystem::exists(path)) {
        throw std::runtime_error("Windows did not confirm recycling every original asset path.");
      }
    }
    if (hooks_.after_recycle) {
      hooks_.after_recycle();
    }
    if (!project_->has_project() || project_->manifest()->id() != checked.project_id ||
        project_->open_scenes().size() != resident_stamps.size()) {
      throw std::runtime_error("The project workspace changed during asset recycling.");
    }
    for (const auto &stamp : resident_stamps) {
      const auto *open = project_->find(stamp.id);
      if (open == nullptr || open->session->content_revision_stamp() != stamp.content ||
          open->session->modeling().tool_preview_source_revision() != stamp.preview_revision ||
          project_->scene_has_pending_edits(stamp.id) != stamp.pending) {
        throw std::runtime_error("A live scene changed during asset recycling.");
      }
    }
    const auto *draft_after = project_->standard_material_editor().current();
    if ((draft_after != nullptr ? draft_after->session_id : MaterialEditSessionId{}) != draft_session ||
        (draft_after != nullptr ? draft_after->draft_revision : 0u) != draft_revision ||
        project_->standard_material_editor().scene_preview_revision() != material_preview_revision) {
      throw std::runtime_error("The material draft changed during asset recycling.");
    }
    auto candidate_catalog = ProjectMaterialCatalog::scan(fs, nullptr, &candidate);
    if (!candidate_catalog.ok()) {
      throw std::runtime_error(candidate_catalog.error().message);
    }
    for (const auto &original : project_->material_catalog().entries()) {
      const auto *current = candidate_catalog.value().find(original.material.id);
      if (contains(checked.material_ids, original.material.id)) {
        if (current != nullptr) {
          throw std::runtime_error("A deleted material reappeared during recycling.");
        }
      } else if (current == nullptr) {
        throw std::runtime_error("An unrelated material disappeared during recycling.");
      }
    }
    for (auto &replacement : live) {
      if (replacement.scene->session->content_revision_stamp() != replacement.previous_stamp) {
        throw std::runtime_error("A live scene changed during asset recycling.");
      }
      replacement.applied = true;
      for (const auto &[entity, component] : replacement.components) {
        if (!replacement.scene->session->scene().replace_existing_material_reference_component(entity, component)) {
          throw std::runtime_error("A live material binding changed during asset deletion.");
        }
      }
    }
    for (const auto &file : staged) {
      const auto verified = file.transaction->verify_published();
      if (!verified.ok) {
        throw std::runtime_error(verified.message);
      }
    }
    journal["state"] = "committed";
    write_journal(transaction_directory / "journal.json", journal);
    committed = true;
    project_->publish_asset_manifest(std::move(candidate), checked.scene_ids);
    const bool material_content_changed = !checked.material_ids.empty() || !checked.texture_ids.empty() ||
        project_->material_catalog().material_revision() != candidate_catalog.value().material_revision();
    project_->publish_material_catalog(candidate_catalog.take_value(), material_content_changed);
    project_->dependencies().invalidate();
    project_->material_usage().invalidate();
    for (auto &replacement : live) {
      if (replacement.scene->saved()) {
        replacement.scene->session->mark_clean_after_external_reference_update(replacement.previous_stamp);
      }
    }
    auto &editor = project_->standard_material_editor();
    if (const auto *draft = editor.current()) {
      if (contains(checked.material_ids, draft->document.id)) {
        (void)editor.close(true);
      } else {
        const auto *entry = project_->material_catalog().find(draft->document.id);
        editor.reconcile_deleted_textures(checked.texture_ids,
                                         entry != nullptr ? entry->content_revision : 0u);
      }
    }
    result = {.ok = true, .changed = true, .message = "Assets moved to the Recycle Bin."};
    bool cleanup_complete = true;
    for (std::size_t index = 0u; index < staged.size(); ++index) {
      auto &file = staged[index];
      const auto finalized = file.transaction->commit();
      if (!finalized.ok) {
        cleanup_complete = false;
        (void)file.transaction->preserve_published_for_recovery();
        result.warnings.push_back(finalized.message);
      }
      if (file.scene_id && file.scene_snapshot) {
        const auto &entry = journal["files"][staged_indices[index]];
        const auto notified = ProjectBridgeNotifications::publish_scene(
            fs, workspace.manifest.id(), *file.scene_id, file.path,
            entry.at("replacement_size").get<std::uint64_t>(),
            entry.at("replacement_hash").get<std::uint64_t>(),
            *file.scene_snapshot, project_->material_catalog());
        if (!notified.ok()) {
          cleanup_complete = false;
          result.warnings.push_back(notified.error().message);
        }
      } else if (file.material_id) {
        if (const auto *material = project_->material_catalog().find(*file.material_id)) {
          const auto notified = ProjectBridgeNotifications::publish_material(
              fs, workspace.manifest.id(), *material);
          if (!notified.ok()) {
            cleanup_complete = false;
            result.warnings.push_back(notified.error().message);
          }
        }
      }
    }
    if (cleanup_complete) {
      remove_recovery_directory(fs, transaction_relative);
    }
    if (browser_snapshot && material_revision == project_->material_catalog().material_revision()) {
      browser_assets.retain_committed_changes(std::move(*browser_snapshot), checked.paths, {});
    }
    return result;
  } catch (const std::exception &error) {
    if (committed) {
      for (auto &file : staged) {
        (void)file.transaction->preserve_published_for_recovery();
      }
      return {.ok = true, .changed = true,
              .message = "Asset deletion committed; recovery cleanup will resume when the project opens.",
              .warnings = {error.what()}};
    }
    std::string failure = error.what();
    bool restored = true;
    for (auto iterator = live.rbegin(); iterator != live.rend(); ++iterator) {
      if (iterator->applied && !iterator->scene->session->scene().restore_modeling_projection_checkpoint(iterator->checkpoint)) {
        restored = false;
        failure += " Live scene reference compensation failed.";
      }
    }
    for (auto iterator = staged.rbegin(); iterator != staged.rend(); ++iterator) {
      const auto rolled_back = iterator->transaction->rollback();
      if (!rolled_back.ok) {
        restored = false;
        failure += " " + rolled_back.message;
        (void)iterator->transaction->preserve_published_for_recovery();
      }
    }
    if (journal_written && restored) {
      try {
        restore_journal(fs, transaction_relative, journal);
      } catch (const std::exception &restore_error) {
        restored = false;
        failure += " " + std::string(restore_error.what());
      }
    }
    if (restored && !transaction_directory.empty()) {
      try {
        remove_recovery_directory(fs, transaction_relative);
      } catch (const std::exception &cleanup_error) {
        restored = false;
        failure += " " + std::string(cleanup_error.what());
      }
    }
    if (!restored) {
      failure += " Recovery backups remain under .quader/asset-deletions; newer content was not overwritten.";
    }
    ProjectSessionResult failed{.message = std::move(failure)};
    if (recycle_attempted) {
      failed.warnings.push_back("The Recycle Bin may contain extra copies of assets restored to the project.");
    }
    return failed;
  }
}

ProjectIoResult<void> ProjectAssetDeletionService::recover(
    ProjectWorkspaceStorage &workspace) {
  try {
    const auto &fs = workspace.file_system;
    const auto root = resolved_path(fs, relative_path(kRecoveryRoot));
    if (!std::filesystem::exists(root)) {
      return ProjectIoResult<void>::success();
    }
    bool restored_recycling = false;
    std::string warnings;
    for (const auto &entry : std::filesystem::directory_iterator(root)) {
      const auto relative = fs.relative_from_absolute(entry.path());
      if (!relative.ok()) {
        throw std::runtime_error(relative.error().message);
      }
      const auto directory = resolved_path(fs, relative.value());
      if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("Unexpected asset deletion recovery entry.");
      }
      const auto journal_path = resolved_path(fs, relative_path(relative.value().portable_string() + "/journal.json"));
      if (!std::filesystem::exists(journal_path)) {
        // A prepared directory without a durable journal never authorized mutation.
        continue;
      }
      const auto journal = nlohmann::json::parse(read_bytes(journal_path));
      if (journal.at("version").get<unsigned>() != 1u ||
          journal.at("project").get<std::string>() != workspace.manifest.id().str()) {
        throw std::runtime_error("Asset deletion recovery journal does not match this project.");
      }
      const auto state = journal.at("state").get<std::string>();
      if (state != "committed") {
        if (state != "prepared" && state != "recycling") {
          throw std::runtime_error("Invalid asset deletion recovery state.");
        }
        restore_journal(fs, relative.value(), journal);
        restored_recycling |= state == "recycling";
      } else {
        // A committed journal is only cleanup work. Never restore deleted files
        // or discard unrelated data authored after the committed operation.
        if (!retry_committed_notifications(workspace, journal, warnings)) {
          continue;
        }
        for (const auto &file : journal.at("files")) {
          const auto target = relative_path(file.at("path").get<std::string>());
          for (const auto *key : {"rollback", "stage"}) {
            if (file.contains(key)) {
              const auto path = private_path(fs, target, file.at(key).get<std::string>());
              if (std::filesystem::exists(path)) {
                verify_bytes(read_bytes(path), file, std::string_view(key) == "stage");
                std::filesystem::remove(path);
              }
            }
          }
        }
      }
      remove_recovery_directory(fs, relative.value());
    }
    auto reopened = ProjectManifestStore::open(workspace.manifest_path);
    if (!reopened.ok()) {
      throw std::runtime_error(reopened.error().message);
    }
    workspace.persisted_texture_metadata_fingerprint =
        reopened.value().persisted_texture_metadata_fingerprint;
    workspace.manifest = std::move(reopened.value().manifest);
    if (restored_recycling) {
      warnings += " Interrupted asset deletion was recovered. The Recycle Bin may contain extra copies of restored assets.";
    }
    return ProjectIoResult<void>::success(std::move(warnings));
  } catch (const std::exception &error) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
        std::string(error.what()) + " Asset deletion recovery data was retained.");
  }
}

} // namespace quader
