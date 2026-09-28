////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_deletion_service.hpp"

#include "core/release_features.hpp"
#include "assets/default_material_catalog.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "io/project/project_bridge_notifications.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/qdr/qdr_project_write_snapshot.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace quader {
namespace {

constexpr std::string_view kTransactionRoot = ".quader/material-deletions";

bool within_folder(const ProjectRelativePath &path,
                   const ProjectRelativePath &folder) {
  return path == folder ||
         path.portable_string().starts_with(folder.portable_string() + "/");
}

ProjectRelativePath require_path(std::string_view text) {
  auto parsed = ProjectRelativePath::parse_portable(text);
  if (!parsed.ok()) {
    throw std::runtime_error(parsed.error().message);
  }
  return parsed.take_value();
}

std::filesystem::path require_resolved(const ProjectFileSystem &file_system,
                                     const ProjectRelativePath &path) {
  auto resolved = file_system.resolve(path);
  if (!resolved.ok()) {
    throw std::runtime_error(resolved.error().message);
  }
  const auto physical = file_system.resolve_physical_location(path);
  if (!physical.ok() ||
      physical.value().lexically_normal() != resolved.value().lexically_normal()) {
    throw std::runtime_error("Material deletion paths cannot traverse filesystem links.");
  }
  return resolved.take_value();
}

ProjectAssetId default_id() {
  return *ProjectAssetId::parse(kDefaultProjectMaterialId);
}

std::string read_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("Cannot read deletion transaction source.");
  }
  std::string bytes{std::istreambuf_iterator<char>(stream), {}};
  if (stream.bad()) {
    throw std::runtime_error("Cannot finish reading deletion transaction source.");
  }
  return bytes;
}

void write_bytes(const std::filesystem::path &path, std::string_view bytes) {
  std::ofstream stream(path, std::ios::binary | std::ios::trunc);
  stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  stream.close();
  if (!stream) {
    throw std::runtime_error("Cannot stage deletion transaction data.");
  }
}

void write_journal(const std::filesystem::path &path,
                   const nlohmann::json &journal) {
  const auto result = quader_io::write_qdr_text_atomic(path, journal.dump(2));
  if (!result.ok) {
    throw std::runtime_error(result.message);
  }
}

std::filesystem::path require_private_rollback_path(
    const ProjectFileSystem &file_system, const ProjectRelativePath &target,
    std::string_view portable_path) {
  const auto rollback = require_path(portable_path);
  const auto name = rollback.native_path().filename().native();
  std::filesystem::path prefix(".");
  prefix += target.native_path().filename();
  prefix += ".";
  if (rollback.native_path().parent_path() != target.native_path().parent_path() ||
      !name.starts_with(prefix.native()) ||
      (rollback.native_path().extension() != ".rollback" &&
       rollback.native_path().extension() != ".stage")) {
    throw std::runtime_error("Invalid private rollback location in material recovery.");
  }
  return require_resolved(file_system, rollback);
}

// The caller passes only a freshly resolved child of the dedicated journal root.
void remove_transaction_directory(const ProjectFileSystem &file_system,
                                  const ProjectRelativePath &path) {
  const auto base = require_path(kTransactionRoot);
  if (path == base || !within_folder(path, base)) {
    throw std::runtime_error("Invalid material deletion recovery directory.");
  }
  const auto resolved = require_resolved(file_system, path);
  if (std::filesystem::is_symlink(std::filesystem::symlink_status(resolved))) {
    throw std::runtime_error("Material deletion recovery directory is a link.");
  }
  std::filesystem::remove_all(resolved);
}

bool contains_any(std::span<const ProjectAssetId> ids,
                  const std::unordered_set<ProjectAssetId, ProjectAssetIdHash>
                      &removed) {
  return std::ranges::any_of(ids, [&](ProjectAssetId id) {
    return removed.contains(id);
  });
}

std::uint64_t confirmation_revision(
    const ProjectMaterialFolderDeletionPreview &preview,
    const ProjectAssetDependencySnapshot &graph) {
  const std::string identity =
      std::string(graph.project_id.str()) + ":" +
      std::to_string(graph.project_generation) + ":" +
      preview.folder.portable_string() + ":" +
      std::to_string(graph.revision) + ":" +
      std::to_string(preview.folder_count);
  const auto hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(identity.data(), identity.size())));
  return hash == 0u ? 1u : hash;
}

std::uint64_t confirmation_revision(
    std::span<const ProjectAssetId> material_ids,
    const ProjectAssetDependencySnapshot &graph) {
  std::vector<ProjectAssetId> sorted(material_ids.begin(), material_ids.end());
  std::ranges::sort(sorted);
  std::string identity = std::string(graph.project_id.str()) + ":" +
                         std::to_string(graph.project_generation) +
                         ":materials:" + std::to_string(graph.revision);
  for (const ProjectAssetId id : sorted) {
    identity += ":";
    identity += id.str();
  }
  const auto hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(identity.data(), identity.size())));
  return hash == 0u ? 1u : hash;
}

template <typename Preview>
void populate_dependency_effects(
    Preview &preview, const ProjectAssetDependencySnapshot &graph,
    const std::unordered_set<ProjectAssetId, ProjectAssetIdHash> &removed) {
  for (const auto &material : graph.materials) {
    if (material.draft && removed.contains(material.id)) {
      preview.discards_material_draft =
          preview.discards_material_draft || material.dirty;
    }
  }
  std::vector<ProjectSceneAssetDependencies> affected;
  std::vector<const ProjectSceneAssetDependencies *> candidates;
  candidates.reserve(graph.scenes.size());
  for (const auto &scene : graph.scenes) {
    if (contains_any(scene.material_ids, removed)) {
      candidates.push_back(&scene);
    }
  }
  affected.reserve(candidates.size());
  std::vector<bool> consumed(candidates.size(), false);
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    const ProjectSceneAssetDependencies &saved = *candidates[index];
    if (saved.live || !saved.path.has_value()) {
      continue;
    }
    std::optional<std::size_t> matching_live;
    std::size_t saved_matches = 0u;
    std::size_t live_matches = 0u;
    for (std::size_t candidate_index = 0u;
         candidate_index < candidates.size(); ++candidate_index) {
      const ProjectSceneAssetDependencies &candidate =
          *candidates[candidate_index];
      if (candidate.scene_id != saved.scene_id ||
          candidate.path != saved.path) {
        continue;
      }
      if (candidate.live) {
        ++live_matches;
        matching_live = candidate_index;
      } else {
        ++saved_matches;
      }
    }
    if (saved_matches == 1u && live_matches == 1u &&
        matching_live.has_value()) {
      const ProjectSceneAssetDependencies &live =
          *candidates[*matching_live];
      if (live.session_id.has_value() && !live.dirty &&
          live.material_ids == saved.material_ids) {
        affected.push_back(live);
        consumed[index] = true;
        consumed[*matching_live] = true;
      }
    }
  }
  for (std::size_t index = 0u; index < candidates.size(); ++index) {
    if (!consumed[index]) {
      affected.push_back(*candidates[index]);
    }
  }
  std::ranges::sort(affected, [](const auto &left, const auto &right) {
    const std::string left_path =
        left.path.has_value() ? left.path->portable_string() : std::string{};
    const std::string right_path = right.path.has_value()
                                       ? right.path->portable_string()
                                       : std::string{};
    return std::tie(left.display_name, left.live, left_path, left.session_id,
                    left.scene_id) <
           std::tie(right.display_name, right.live, right_path,
                    right.session_id, right.scene_id);
  });
  preview.affected_scenes = std::move(affected);
}

void normalize_reference(MaterialReference &reference,
                         const ProjectManifest &manifest) {
  if (reference.project_owned()) {
    reference = MaterialReference::project(
        ProjectMaterialReferenceResolver::canonical_material_id(
            manifest, reference.project_asset()));
  }
}

void normalize_component(MaterialComponent &component,
                         const ProjectManifest &manifest) {
  normalize_reference(component.material, manifest);
  for (auto &[slot, material] : component.material_slots) {
    (void)slot;
    normalize_reference(material, manifest);
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
    const auto append = [&](const MaterialReference &reference) {
      if (reference.project_owned()) {
        ids.insert(reference.project_asset());
      }
    };
    append(entity.material->material);
    for (const auto &[slot, material] : entity.material->material_slots) {
      (void)slot;
      append(material);
    }
  }
  std::erase_if(snapshot.project_asset_dependencies,
                [](const ProjectAssetReference &reference) {
                  return reference.kind == ProjectAssetKind::Material;
                });
  for (const ProjectAssetId id : ids) {
    const auto *entry = catalog.find(id);
    if (entry == nullptr) {
      throw std::runtime_error("Replacement scene references an unavailable material.");
    }
    snapshot.project_asset_dependencies.push_back({
        .stable_id = id, .kind = ProjectAssetKind::Material, .path = entry->path});
  }
}

/// Retains reversible publication and its independent crash-recovery baseline.
struct StagedFile final {
  ProjectRelativePath path;
  std::unique_ptr<quader_io::QdrAtomicFileTransaction> transaction =
      std::make_unique<quader_io::QdrAtomicFileTransaction>();
  std::optional<SceneId> scene_id;
  std::optional<QdrProjectWriteSnapshot> snapshot;
  std::uint64_t byte_count = 0u;
  std::uint64_t content_hash = 0u;
  std::uint64_t original_byte_count = 0u;
  std::uint64_t original_content_hash = 0u;
};

/// Keeps exact scene-owner compensation without touching history or selection.
struct LiveReplacement final {
  OpenSceneSession *scene = nullptr;
  SceneModelingProjectionCheckpoint checkpoint;
  std::vector<std::pair<EntityId, MaterialComponent>> replacements;
  SceneContentRevisionStamp previous_stamp;
  bool applied = false;
};

/// One material file moved under a durable transaction before publication.
struct QuarantinedMaterialFile final {
  ProjectRelativePath path;
  std::filesystem::path original;
  std::filesystem::path quarantine;
  std::uint64_t byte_count = 0u;
  std::uint64_t content_hash = 0u;
  bool moved = false;
};

void validate_quarantined_folder(
    const ProjectFileSystem &file_system,
    const std::filesystem::path &quarantine,
    const ProjectMaterialFolderDeletionPreview &preview,
    const ProjectMaterialCatalog &catalog,
    const ProjectAssetDependencySnapshot &graph) {
  std::set<std::string> expected_files;
  std::set<std::string> expected_folders;
  for (const auto &entry : catalog.entries()) {
    if (within_folder(entry.path, preview.folder)) {
      expected_files.insert(entry.path.portable_string());
    }
  }
  for (const auto &folder : catalog.folders()) {
    if (folder != preview.folder && within_folder(folder, preview.folder)) {
      expected_folders.insert(folder.portable_string());
    }
  }
  for (const auto &entry : std::filesystem::recursive_directory_iterator(quarantine)) {
    const auto physical_relative = file_system.relative_from_absolute(entry.path());
    if (!physical_relative.ok()) {
      throw std::runtime_error(physical_relative.error().message);
    }
    (void)require_resolved(file_system, physical_relative.value());
    const auto suffix = ProjectRelativePath::parse(
        entry.path().lexically_relative(quarantine));
    if (!suffix.ok()) {
      throw std::runtime_error(suffix.error().message);
    }
    const auto original_path = require_path(
        preview.folder.portable_string() + "/" + suffix.value().portable_string());
    if (entry.is_directory()) {
      if (expected_folders.erase(original_path.portable_string()) != 1u) {
        throw std::runtime_error("Material folders changed during deletion staging.");
      }
      continue;
    }
    if (!entry.is_regular_file() ||
        expected_files.erase(original_path.portable_string()) != 1u) {
      throw std::runtime_error("Unmanaged content appeared during deletion staging.");
    }
    const auto source = std::ranges::find_if(graph.sources, [&](const auto &value) {
      return value.path == original_path;
    });
    const auto bytes = read_bytes(entry.path());
    const auto hash = quader_io::qdr_binary_content_hash(
        std::as_bytes(std::span(bytes.data(), bytes.size())));
    if (source == graph.sources.end() || source->byte_count != bytes.size() ||
        source->content_hash != hash) {
      throw std::runtime_error("Material content changed during deletion staging.");
    }
  }
  if (!expected_files.empty() || !expected_folders.empty()) {
    throw std::runtime_error("Material folder contents changed during deletion staging.");
  }
}

void validate_material_source(
    const ProjectFileSystem &file_system,
    const ProjectAssetDependencySnapshot &graph,
    QuarantinedMaterialFile &file) {
  file.original = require_resolved(file_system, file.path);
  const auto status = std::filesystem::symlink_status(file.original);
  if (!std::filesystem::is_regular_file(status) ||
      std::filesystem::is_symlink(status)) {
    throw std::runtime_error("A selected material file is missing or is a filesystem link.");
  }
  std::error_code link_error;
  const auto links = std::filesystem::hard_link_count(file.original, link_error);
  if (link_error || links != 1u) {
    throw std::runtime_error("Linked or aliased material files cannot be deleted.");
  }
  const auto source = std::ranges::find_if(graph.sources, [&](const auto &value) {
    return value.path == file.path;
  });
  const auto bytes = read_bytes(file.original);
  const auto hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(bytes.data(), bytes.size())));
  if (source == graph.sources.end() || source->byte_count != bytes.size() ||
      source->content_hash != hash) {
    throw std::runtime_error("Material content changed after the deletion warning.");
  }
  file.byte_count = bytes.size();
  file.content_hash = hash;
}

void validate_quarantined_materials(
    std::span<const QuarantinedMaterialFile> materials) {
  for (const auto &material : materials) {
    if (!material.moved || std::filesystem::exists(material.original)) {
      throw std::runtime_error("Material files changed during deletion staging.");
    }
    const auto status = std::filesystem::symlink_status(material.quarantine);
    if (!std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
      throw std::runtime_error("A quarantined material is missing or is a filesystem link.");
    }
    std::error_code link_error;
    if (std::filesystem::hard_link_count(material.quarantine, link_error) != 1u ||
        link_error) {
      throw std::runtime_error(
          "A quarantined material became linked or aliased.");
    }
    const auto bytes = read_bytes(material.quarantine);
    const auto hash = quader_io::qdr_binary_content_hash(
        std::as_bytes(std::span(bytes.data(), bytes.size())));
    if (bytes.size() != material.byte_count || hash != material.content_hash) {
      throw std::runtime_error("Material content changed during deletion staging.");
    }
  }
}

void validate_material_quarantine_entries(
    const std::filesystem::path &directory,
    std::span<const std::filesystem::path> expected_paths) {
  const auto status = std::filesystem::symlink_status(directory);
  if (!std::filesystem::exists(status)) {
    return;
  }
  if (!std::filesystem::is_directory(status) ||
      std::filesystem::is_symlink(status)) {
    throw std::runtime_error(
        "Material deletion quarantine is not a regular directory.");
  }
  std::set<std::filesystem::path> expected;
  for (const auto &path : expected_paths) {
    expected.insert(path.lexically_normal());
  }
  for (const auto &entry : std::filesystem::directory_iterator(directory)) {
    const auto entry_status = entry.symlink_status();
    if (!std::filesystem::is_regular_file(entry_status) ||
        std::filesystem::is_symlink(entry_status) ||
        expected.erase(entry.path().lexically_normal()) != 1u) {
      throw std::runtime_error(
          "Material deletion quarantine contains an unjournaled entry; "
          "recovery preserved it for conflict resolution.");
    }
  }
}

void remove_validated_material_quarantine(
    const std::filesystem::path &directory,
    std::span<const std::filesystem::path> expected_paths) {
  validate_material_quarantine_entries(directory, expected_paths);
  for (const auto &path : expected_paths) {
    if (!std::filesystem::exists(path)) {
      continue;
    }
    std::error_code error;
    if (!std::filesystem::remove(path, error) || error) {
      throw std::runtime_error(
          "Cannot destroy a validated quarantined material.");
    }
  }
  if (!std::filesystem::exists(directory)) {
    return;
  }
  std::error_code error;
  if (!std::filesystem::remove(directory, error) || error) {
    throw std::runtime_error(
        "Material deletion quarantine changed during cleanup; recovery "
        "preserved it for conflict resolution.");
  }
}

std::vector<std::filesystem::path> quarantine_paths(
    std::span<const QuarantinedMaterialFile> materials) {
  std::vector<std::filesystem::path> paths;
  paths.reserve(materials.size());
  std::ranges::transform(materials, std::back_inserter(paths),
                         &QuarantinedMaterialFile::quarantine);
  return paths;
}

void validate_catalog_after_quarantine(
    const ProjectMaterialCatalog &reviewed,
    const ProjectMaterialCatalog &scanned,
    const std::unordered_set<ProjectAssetId, ProjectAssetIdHash> &removed,
    const ProjectRelativePath *removed_folder) {
  const auto removed_entry_count = std::ranges::count_if(
      reviewed.entries(), [&](const ProjectMaterialCatalogEntry &entry) {
        return removed.contains(entry.material.id);
      });
  if (removed_entry_count != removed.size() ||
      scanned.entries().size() + removed_entry_count !=
          reviewed.entries().size()) {
    throw std::runtime_error(
        "Material catalog changed during deletion staging.");
  }
  for (const auto &before : reviewed.entries()) {
    if (removed.contains(before.material.id)) {
      if (scanned.find(before.material.id) != nullptr) {
        throw std::runtime_error(
            "A selected material remained in the catalog after quarantine.");
      }
      continue;
    }
    const auto *after = scanned.find(before.material.id);
    if (after == nullptr || *after != before) {
      throw std::runtime_error(
          "Material catalog content changed during deletion staging.");
    }
  }
  std::vector<ProjectRelativePath> expected_folders;
  expected_folders.reserve(reviewed.folders().size());
  std::ranges::copy_if(
      reviewed.folders(), std::back_inserter(expected_folders),
      [&](const ProjectRelativePath &folder) {
        return removed_folder == nullptr ||
               !within_folder(folder, *removed_folder);
      });
  if (!std::ranges::equal(expected_folders, scanned.folders())) {
    throw std::runtime_error(
        "Material folders changed during deletion staging.");
  }
}

} // namespace

ProjectMaterialFolderDeletionPreview
ProjectMaterialDeletionService::preview_folder(std::string_view folder) const {
  ProjectMaterialFolderDeletionPreview preview;
  try {
    if (!project_->workspace()) {
      throw std::runtime_error("Open a project before deleting material folders.");
    }
    preview.folder = require_path(folder);
    const auto materials_root = require_path("Materials");
    if (preview.folder == materials_root ||
        !within_folder(preview.folder, materials_root)) {
      throw std::runtime_error("All and the Materials root cannot be deleted.");
    }
    const auto &file_system = project_->workspace()->file_system;
    const auto absolute = require_resolved(file_system, preview.folder);
    if (!std::filesystem::is_directory(absolute) ||
        std::filesystem::is_symlink(std::filesystem::symlink_status(absolute))) {
      throw std::runtime_error("Material folder is missing or is a filesystem link.");
    }
    const auto graph = project_->dependencies().snapshot(true);
    preview.errors = graph->errors;
    std::set<std::string> managed_files;
    std::unordered_set<ProjectAssetId, ProjectAssetIdHash> removed;
    for (const auto &entry : project_->material_catalog().entries()) {
      if (!within_folder(entry.path, preview.folder)) {
        continue;
      }
      if (entry.material.id == default_id()) {
        throw std::runtime_error("The reserved Default Material cannot be deleted.");
      }
      preview.material_ids.push_back(entry.material.id);
      preview.material_names.push_back(entry.material.name);
      removed.insert(entry.material.id);
      managed_files.insert(entry.path.portable_string());
    }
    preview.folder_count = 1u;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(absolute)) {
      const auto relative = file_system.relative_from_absolute(entry.path());
      if (!relative.ok()) {
        throw std::runtime_error(relative.error().message);
      }
      (void)require_resolved(file_system, relative.value());
      if (entry.is_symlink()) {
        throw std::runtime_error("Folders containing filesystem links cannot be deleted.");
      }
      if (entry.is_directory()) {
        ++preview.folder_count;
      } else if (!entry.is_regular_file() ||
                 !managed_files.contains(relative.value().portable_string())) {
        throw std::runtime_error("Folder contains unmanaged files or resources: " +
                                 relative.value().portable_string());
      }
    }
    for (const auto &material : graph->materials) {
      for (const auto &texture : material.textures) {
        if (texture.path && within_folder(*texture.path, preview.folder)) {
          throw std::runtime_error("Folder contains a referenced texture resource.");
        }
        if (texture.pending_source) {
          const auto relative = file_system.relative_from_absolute(*texture.pending_source);
          if (relative.ok() && within_folder(relative.value(), preview.folder)) {
            throw std::runtime_error("Folder contains a pending material texture source.");
          }
        }
      }
    }
    populate_dependency_effects(preview, *graph, removed);
    preview.revision = confirmation_revision(preview, *graph);
    preview.complete = graph->complete && preview.errors.empty();
  } catch (const std::exception &error) {
    preview.errors.push_back(error.what());
  }
  return preview;
}

ProjectMaterialDeletionPreview ProjectMaterialDeletionService::preview_materials(
    std::span<const ProjectAssetId> material_ids) const {
  ProjectMaterialDeletionPreview preview;
  try {
    if (!project_->workspace()) {
      throw std::runtime_error("Open a project before deleting materials.");
    }
    if (material_ids.empty()) {
      throw std::runtime_error("Select at least one material to delete.");
    }
    const auto graph = project_->dependencies().snapshot(true);
    preview.errors = graph->errors;
    std::unordered_set<ProjectAssetId, ProjectAssetIdHash> removed;
    removed.reserve(material_ids.size());
    for (const ProjectAssetId id : material_ids) {
      if (!id.valid()) {
        throw std::runtime_error("Material deletion requires valid UUIDs.");
      }
      if (!removed.insert(id).second) {
        throw std::runtime_error("The material deletion selection contains duplicate entries.");
      }
      if (id == default_id()) {
        throw std::runtime_error("The reserved Default Material cannot be deleted.");
      }
      const auto *entry = project_->material_catalog().find(id);
      if (entry == nullptr) {
        throw std::runtime_error("A selected material is unavailable or was already deleted.");
      }
      const auto absolute = require_resolved(project_->workspace()->file_system,
                                             entry->path);
      const auto status = std::filesystem::symlink_status(absolute);
      if (!std::filesystem::is_regular_file(status) ||
          std::filesystem::is_symlink(status)) {
        throw std::runtime_error(
            "A selected material file is missing or is a filesystem link.");
      }
      std::error_code link_error;
      if (std::filesystem::hard_link_count(absolute, link_error) != 1u ||
          link_error) {
        throw std::runtime_error(
            "Linked or aliased material files cannot be deleted.");
      }
      preview.material_ids.push_back(id);
      preview.material_names.push_back(entry->material.name);
    }
    populate_dependency_effects(preview, *graph, removed);
    preview.revision = confirmation_revision(preview.material_ids, *graph);
    preview.complete = graph->complete && preview.errors.empty();
  } catch (const std::exception &error) {
    preview.errors.push_back(error.what());
  }
  return preview;
}

ProjectSessionResult ProjectMaterialDeletionService::delete_folder(
    std::string_view folder, std::uint64_t preview_revision) const {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return {.message = release_feature_unavailable_message()};
  }

  const auto folder_preview = preview_folder(folder);
  if (!folder_preview.complete) {
    return {.message = folder_preview.errors.empty()
                           ? "Material dependencies could not be inspected."
                           : folder_preview.errors.front()};
  }
  if (preview_revision == 0u || folder_preview.revision != preview_revision) {
    return {.message = "Material dependencies changed. Review a new deletion warning."};
  }
  return delete_preview(
      {.revision = folder_preview.revision,
       .complete = folder_preview.complete,
       .material_ids = folder_preview.material_ids,
       .material_names = folder_preview.material_names,
       .affected_scenes = folder_preview.affected_scenes,
       .errors = folder_preview.errors,
       .discards_material_draft = folder_preview.discards_material_draft},
      &folder_preview);
}

ProjectSessionResult ProjectMaterialDeletionService::delete_materials(
    std::span<const ProjectAssetId> material_ids,
    std::uint64_t preview_revision) const {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return {.message = release_feature_unavailable_message()};
  }

  const auto preview = preview_materials(material_ids);
  if (!preview.complete) {
    return {.message = preview.errors.empty()
                           ? "Material dependencies could not be inspected."
                           : preview.errors.front()};
  }
  if (preview_revision == 0u || preview.revision != preview_revision) {
    return {.message =
                "Material dependencies changed. Review a new deletion warning."};
  }
  return delete_preview(preview, nullptr);
}

ProjectSessionResult ProjectMaterialDeletionService::delete_preview(
    const ProjectMaterialDeletionPreview &preview,
    const ProjectMaterialFolderDeletionPreview *folder_preview) const {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return {.message = release_feature_unavailable_message()};
  }

  auto &workspace = *project_->workspace_;
  const auto &file_system = workspace.file_system;
  std::vector<StagedFile> staged;
  std::vector<LiveReplacement> live;
  std::vector<QuarantinedMaterialFile> quarantined_materials;
  ProjectRelativePath transaction_relative;
  std::filesystem::path transaction_directory;
  std::filesystem::path original_folder;
  std::filesystem::path quarantine;
  bool folder_moved = false;
  bool committed = false;
  bool journal_written = false;
  nlohmann::json journal;
  ProjectSessionResult result;

  try {
    const auto graph = project_->dependencies().snapshot();
    ProjectManifest candidate = workspace.manifest;
    candidate.require_product_release(current_product_release());
    const auto marked = candidate.mark_materials_deleted(preview.material_ids);
    if (!marked.ok()) {
      throw std::runtime_error(marked.error().message);
    }
    transaction_relative = require_path(std::string(kTransactionRoot) + "/" +
                                        std::string(ProjectAssetId::generate().str()));
    transaction_directory = require_resolved(file_system, transaction_relative);
    std::filesystem::create_directories(transaction_directory);
    journal = {{"version", folder_preview != nullptr ? 1 : 2},
               {"project", workspace.manifest.id().str()},
               {"state", "prepared"},
               {"files", nlohmann::json::array()}};
    if (folder_preview != nullptr) {
      original_folder = require_resolved(file_system, folder_preview->folder);
      quarantine = require_resolved(
          file_system,
          require_path(transaction_relative.portable_string() + "/folder"));
      journal["folder"] = folder_preview->folder.portable_string();
    } else {
      journal["kind"] = "materials";
      journal["material_ids"] = nlohmann::json::array();
      journal["material_files"] = nlohmann::json::array();
      std::filesystem::create_directories(transaction_directory / "materials");
      quarantined_materials.reserve(preview.material_ids.size());
      for (std::size_t index = 0u; index < preview.material_ids.size(); ++index) {
        const auto *entry = project_->material_catalog_.find(
            preview.material_ids[index]);
        if (entry == nullptr) {
          throw std::runtime_error(
              "A selected material became unavailable before deletion.");
        }
        QuarantinedMaterialFile file{.path = entry->path};
        const auto quarantine_relative = require_path(
            transaction_relative.portable_string() + "/materials/material-" +
            std::to_string(index) + ".qmat");
        file.quarantine = require_resolved(file_system, quarantine_relative);
        validate_material_source(file_system, *graph, file);
        journal["material_ids"].push_back(
            std::string(preview.material_ids[index].str()));
        journal["material_files"].push_back(
            {{"path", file.path.portable_string()},
             {"byte_count", file.byte_count},
             {"content_hash", file.content_hash}});
        quarantined_materials.push_back(std::move(file));
      }
    }

    const auto stage = [&](ProjectRelativePath path, std::string_view bytes,
                           std::optional<SceneId> scene_id = std::nullopt,
                           std::optional<QdrProjectWriteSnapshot> snapshot = std::nullopt) {
      const auto target = require_resolved(file_system, path);
      const std::string backup_name = "file-" + std::to_string(staged.size()) + ".old";
      const auto baseline = read_bytes(target);
      const auto backup_saved = quader_io::write_qdr_text_atomic(
          transaction_directory / backup_name, baseline);
      if (!backup_saved.ok) {
        throw std::runtime_error(backup_saved.message);
      }
      StagedFile file;
      file.path = path;
      file.scene_id = scene_id;
      file.snapshot = std::move(snapshot);
      file.byte_count = bytes.size();
      file.content_hash = quader_io::qdr_binary_content_hash(
          std::as_bytes(std::span(bytes.data(), bytes.size())));
      const auto prepared = file.transaction->prepare(target);
      if (!prepared.ok) {
        throw std::runtime_error(prepared.message);
      }
      write_bytes(file.transaction->staging_path(), bytes);
      const auto baseline_hash = quader_io::qdr_binary_content_hash(
          std::as_bytes(std::span(baseline.data(), baseline.size())));
      file.original_byte_count = baseline.size();
      file.original_content_hash = baseline_hash;
      journal["files"].push_back({{"path", path.portable_string()}, {"backup", backup_name},
                                  {"byte_count", baseline.size()},
                                  {"content_hash", baseline_hash},
                                  {"replacement_byte_count", file.byte_count},
                                  {"replacement_content_hash", file.content_hash}});
      staged.push_back(std::move(file));
    };

    const std::unordered_set<ProjectAssetId, ProjectAssetIdHash> removed(
        preview.material_ids.begin(), preview.material_ids.end());
    for (const auto &scene : graph->scenes) {
      if (scene.live || !scene.path || !contains_any(scene.material_ids, removed)) {
        continue;
      }
      const auto path = require_resolved(file_system, *scene.path);
      auto read = QdrDocumentReader::read_project_document(path);
      if (!read.ok || SceneId::from_bytes(read.scene_uuid) != scene.scene_id) {
        throw std::runtime_error("Cannot prepare saved scene replacement: " +
                                 scene.path->portable_string() + ". " + read.message);
      }
      const auto fingerprint = std::ranges::find_if(graph->sources, [&](const auto &source) {
        return source.path == *scene.path;
      });
      if (fingerprint == graph->sources.end() ||
          fingerprint->byte_count != read.source_byte_count ||
          fingerprint->content_hash != read.source_content_hash) {
        throw std::runtime_error("Saved scene changed after the deletion warning.");
      }
      auto extracted = qdr_project_write_snapshot_from_document(read.document);
      if (!extracted.ok()) {
        throw std::runtime_error(extracted.error().message);
      }
      auto snapshot = extracted.take_value();
      snapshot.minimum_product_release = read.minimum_product_release;
      snapshot.viewport_camera_pose = read.viewport_camera_pose;
      snapshot.project_asset_dependencies = std::move(read.project_asset_dependencies);
      normalize_snapshot(snapshot, candidate, project_->material_catalog_);
      const auto encoded = QdrDocumentWriter::serialize_project_document_bytes(
          snapshot, read.scene_uuid);
      stage(*scene.path,
            std::string_view(reinterpret_cast<const char *>(encoded.data()), encoded.size()),
            scene.scene_id, std::move(snapshot));
    }
    const auto manifest_relative = file_system.relative_from_absolute(workspace.manifest_path);
    if (!manifest_relative.ok()) {
      throw std::runtime_error(manifest_relative.error().message);
    }
    stage(manifest_relative.value(), ProjectManifestStore::serialize(candidate));

    for (const auto &open : project_->open_scenes_) {
      LiveReplacement replacement;
      replacement.scene = open.get();
      replacement.previous_stamp = open->session->content_revision_stamp();
      std::vector<EntityId> participants;
      for (const EntityId entity : open->session->scene().entities()) {
        const auto *component = open->session->scene().material(entity);
        if (component == nullptr) {
          continue;
        }
        auto normalized = *component;
        normalize_component(normalized, candidate);
        if (normalized != *component) {
          participants.push_back(entity);
          replacement.replacements.emplace_back(entity, std::move(normalized));
        }
      }
      if (!participants.empty()) {
        // The structural checkpoint also protects mesh/curve bindings and
        // selection, and requires the complete descendant closure even though
        // this operation only rewrites existing material components.
        std::unordered_set<EntityId> enrolled(participants.begin(), participants.end());
        for (std::size_t index = 0u; index < participants.size(); ++index) {
          if (const auto *hierarchy = open->session->scene().hierarchy(participants[index])) {
            for (const EntityId child : hierarchy->children) {
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
          throw std::runtime_error("Cannot prepare live scene reference compensation.");
        }
        live.push_back(std::move(replacement));
      }
    }
    const std::uint64_t final_revision = [&] {
      if (folder_preview != nullptr) {
        const auto final = preview_folder(
            folder_preview->folder.portable_string());
        return final.complete ? final.revision : 0u;
      }
      const auto final = preview_materials(preview.material_ids);
      return final.complete ? final.revision : 0u;
    }();
    if (final_revision != preview.revision) {
      throw std::runtime_error(
          "Project changed while preparing deletion. Review a new warning.");
    }
    for (auto &material : quarantined_materials) {
      validate_material_source(file_system, *graph, material);
    }
    write_journal(transaction_directory / "journal.json", journal);
    journal_written = true;
    if (folder_preview != nullptr) {
      const auto moved = quader_io::move_project_entry_no_replace(
          original_folder, quarantine);
      if (!moved.ok) {
        throw std::runtime_error(moved.message);
      }
      folder_moved = true;
      validate_quarantined_folder(file_system, quarantine, *folder_preview,
                                  project_->material_catalog_, *graph);
    } else {
      for (auto &material : quarantined_materials) {
        const auto moved = quader_io::move_project_entry_no_replace(
            material.original, material.quarantine);
        if (!moved.ok) {
          throw std::runtime_error(moved.message);
        }
        material.moved = true;
      }
      validate_quarantined_materials(quarantined_materials);
    }
    if (hooks_.after_quarantine) {
      hooks_.after_quarantine();
    }
    auto scanned = ProjectMaterialCatalog::scan(file_system);
    if (!scanned.ok()) {
      throw std::runtime_error(scanned.error().message);
    }
    ProjectMaterialCatalog candidate_catalog = scanned.take_value();
    validate_catalog_after_quarantine(
        project_->material_catalog_, candidate_catalog, removed,
        folder_preview != nullptr ? &folder_preview->folder : nullptr);
    if (candidate_catalog.find(default_id()) == nullptr) {
      throw std::runtime_error("Default Material is unavailable after folder staging.");
    }
    std::size_t publishing_index = 0u;
    for (auto &file : staged) {
      const auto current = read_bytes(file.transaction->target_path());
      const auto current_hash = quader_io::qdr_binary_content_hash(
          std::as_bytes(std::span(current.data(), current.size())));
      if (current.size() != file.original_byte_count ||
          current_hash != file.original_content_hash) {
        throw std::runtime_error("A saved document changed before deletion publication: " +
                                 file.path.portable_string());
      }
      // An interruption before the actual displaced bytes are verified must
      // never authorize restoring the older warning-time baseline over them.
      journal["state"] = "publishing";
      journal["publishing_path"] = file.path.portable_string();
      write_journal(transaction_directory / "journal.json", journal);
      const auto published = file.transaction->publish(
          [&](const std::filesystem::path &backup) -> quader_io::QdrAtomicWriteResult {
            try {
              const auto rollback_path = file_system.relative_from_absolute(backup);
              if (!rollback_path.ok()) {
                return {false, rollback_path.error().message};
              }
              journal["files"][publishing_index]["rollback_path"] =
                  rollback_path.value().portable_string();
              write_journal(transaction_directory / "journal.json", journal);
              return {true, {}};
            } catch (const std::exception &error) {
              return {false, error.what()};
            }
          });
      if (!published.ok) {
        throw std::runtime_error(published.message);
      }
      const auto displaced = read_bytes(file.transaction->backup_path());
      const auto displaced_hash = quader_io::qdr_binary_content_hash(
          std::as_bytes(std::span(displaced.data(), displaced.size())));
      if (displaced.size() != file.original_byte_count ||
          displaced_hash != file.original_content_hash) {
        throw std::runtime_error("A concurrent saved document update was detected during deletion: " +
                                 file.path.portable_string());
      }
      journal["state"] = "prepared";
      journal.erase("publishing_path");
      write_journal(transaction_directory / "journal.json", journal);
      ++publishing_index;
    }
    for (auto &replacement : live) {
      replacement.applied = true;
      for (auto &[entity, material] : replacement.replacements) {
        if (!replacement.scene->session->scene().replace_existing_material_reference_component(
                entity, std::move(material))) {
          throw std::runtime_error("A live material binding changed during deletion.");
        }
      }
    }
    for (const auto &file : staged) {
      const auto verified = file.transaction->verify_published();
      if (!verified.ok) {
        throw std::runtime_error(verified.message);
      }
    }
    if (folder_preview == nullptr) {
      validate_quarantined_materials(quarantined_materials);
    }
    journal["state"] = "committed";
    write_journal(transaction_directory / "journal.json", journal);
    committed = true;
    workspace.persisted_texture_metadata_fingerprint =
        candidate.texture_metadata_fingerprint();
    workspace.manifest = std::move(candidate);
    project_->material_catalog_ = std::move(candidate_catalog);
    project_->invalidate_material_textures();
    project_->dependencies().invalidate();
    project_->material_usage().invalidate();
    for (auto &replacement : live) {
      if (replacement.scene->saved()) {
        replacement.scene->session->mark_clean_after_external_reference_update(
            replacement.previous_stamp);
      }
    }
    const auto *draft = project_->standard_material_editor().current();
    if (draft != nullptr && removed.contains(draft->document.id)) {
      (void)project_->standard_material_editor().close(true);
    }
    result = {
        .ok = true,
        .changed = true,
        .message = folder_preview != nullptr
                       ? "Material folder permanently deleted; references use Default Material."
                       : "Materials permanently deleted; references use Default Material."};
    bool cleanup_complete = true;
    for (auto &file : staged) {
      const auto finalized = file.transaction->commit();
      if (!finalized.ok) {
        cleanup_complete = false;
        (void)file.transaction->preserve_published_for_recovery();
      }
      if (!finalized.message.empty()) {
        result.warnings.push_back(finalized.message);
      }
      if (file.scene_id && file.snapshot) {
        const auto notified = ProjectBridgeNotifications::publish_scene(
            file_system, workspace.manifest.id(), *file.scene_id, file.path,
            file.byte_count, file.content_hash, *file.snapshot, project_->material_catalog_);
        if (!notified.ok()) {
          cleanup_complete = false;
          result.warnings.push_back(notified.error().message);
        }
      }
    }
    if (cleanup_complete) {
      if (folder_preview == nullptr) {
        const auto paths = quarantine_paths(quarantined_materials);
        remove_validated_material_quarantine(
            transaction_directory / "materials", paths);
      }
      remove_transaction_directory(file_system, transaction_relative);
    }
    return result;
  } catch (const std::exception &error) {
    if (committed) {
      for (auto &file : staged) {
        (void)file.transaction->preserve_published_for_recovery();
      }
      result.ok = true;
      result.changed = true;
      result.message =
          "Material deletion committed; cleanup will resume when the project opens.";
      result.warnings.push_back(error.what());
      return result;
    }
    std::string rollback_errors;
    for (auto iterator = live.rbegin(); iterator != live.rend(); ++iterator) {
      if (iterator->applied && !iterator->scene->session->scene().restore_modeling_projection_checkpoint(
                                   iterator->checkpoint)) {
        rollback_errors += " Live material compensation failed.";
      }
    }
    for (auto iterator = staged.rbegin(); iterator != staged.rend(); ++iterator) {
      const auto restored = iterator->transaction->rollback();
      if (!restored.ok) {
        rollback_errors += " " + restored.message;
        (void)iterator->transaction->preserve_published_for_recovery();
      }
    }
    if (folder_moved) {
      const auto restored = quader_io::move_project_entry_no_replace(quarantine, original_folder);
      if (!restored.ok) {
        rollback_errors += " " + restored.message;
      }
    }
    for (auto iterator = quarantined_materials.rbegin();
         iterator != quarantined_materials.rend(); ++iterator) {
      if (!iterator->moved) {
        continue;
      }
      const auto restored = quader_io::move_project_entry_no_replace(
          iterator->quarantine, iterator->original);
      if (!restored.ok) {
        rollback_errors += " " + restored.message;
      } else {
        iterator->moved = false;
      }
    }
    if (rollback_errors.empty() && !transaction_directory.empty()) {
      try {
        if (folder_preview == nullptr) {
          const auto paths = quarantine_paths(quarantined_materials);
          remove_validated_material_quarantine(
              transaction_directory / "materials", paths);
        }
        remove_transaction_directory(file_system, transaction_relative);
      } catch (const std::exception &cleanup_error) {
        rollback_errors += " " + std::string(cleanup_error.what());
      }
    }
    if (!rollback_errors.empty() && journal_written) {
      rollback_errors += " Recovery data was retained under .quader/material-deletions.";
    }
    return {.message = std::string(error.what()) + rollback_errors};
  }
}

ProjectIoResult<void>
ProjectMaterialDeletionService::recover(ProjectWorkspaceStorage &workspace) {
  try {
    const auto &file_system = workspace.file_system;
    const auto transaction_root = require_resolved(file_system, require_path(kTransactionRoot));
    if (!std::filesystem::exists(transaction_root)) {
      return ProjectIoResult<void>::success();
    }
    bool restored_manifest = false;
    std::string warnings;
    for (const auto &entry : std::filesystem::directory_iterator(transaction_root)) {
      if (!entry.is_directory() || entry.is_symlink() ||
          !ProjectAssetId::parse(entry.path().filename().string())) {
        throw std::runtime_error("Invalid material deletion recovery entry.");
      }
      const auto relative = file_system.relative_from_absolute(entry.path());
      if (!relative.ok()) {
        throw std::runtime_error(relative.error().message);
      }
      const auto journal_path = require_resolved(
          file_system, require_path(relative.value().portable_string() + "/journal.json"));
      if (!std::filesystem::exists(journal_path)) {
        // No durable journal means publication never started.
        remove_transaction_directory(file_system, relative.value());
        continue;
      }
      const auto journal = nlohmann::json::parse(read_bytes(journal_path));
      const int journal_version = journal.at("version").get<int>();
      if ((journal_version != 1 && journal_version != 2) ||
          journal.at("project").get<std::string>() !=
              workspace.manifest.id().str()) {
        throw std::runtime_error("Material deletion recovery identity is invalid.");
      }
      std::optional<ProjectRelativePath> folder;
      struct RecoveryMaterialFile final {
        ProjectRelativePath path;
        std::filesystem::path original;
        std::filesystem::path quarantine;
        std::uint64_t byte_count = 0u;
        std::uint64_t content_hash = 0u;
      };
      std::vector<RecoveryMaterialFile> material_files;
      std::vector<ProjectAssetId> material_ids;
      std::filesystem::path material_quarantine_directory;
      std::vector<std::filesystem::path> material_quarantine_paths;
      if (journal_version == 1) {
        folder = require_path(journal.at("folder").get<std::string>());
        if (*folder == require_path("Materials") ||
            !within_folder(*folder, require_path("Materials"))) {
          throw std::runtime_error("Invalid folder in material deletion recovery.");
        }
      } else {
        if (journal.at("kind").get<std::string>() != "materials" ||
            !journal.at("material_ids").is_array() ||
            journal.at("material_ids").empty() ||
            !journal.at("material_files").is_array() ||
            journal.at("material_files").empty() ||
            journal.at("material_ids").size() !=
                journal.at("material_files").size()) {
          throw std::runtime_error("Invalid material-file deletion recovery data.");
        }
        std::set<ProjectAssetId> recovered_ids;
        material_ids.reserve(journal.at("material_ids").size());
        for (const auto &encoded_id : journal.at("material_ids")) {
          if (!encoded_id.is_string()) {
            throw std::runtime_error(
                "Invalid material UUID in deletion recovery.");
          }
          const auto id = ProjectAssetId::parse(
              encoded_id.get<std::string>());
          if (!id.has_value() || *id == default_id() ||
              !recovered_ids.insert(*id).second) {
            throw std::runtime_error(
                "Invalid material UUID in deletion recovery.");
          }
          material_ids.push_back(*id);
        }
        std::set<std::string> recovered_paths;
        std::size_t material_index = 0u;
        material_quarantine_directory = require_resolved(
            file_system,
            require_path(relative.value().portable_string() + "/materials"));
        for (const auto &file : journal.at("material_files")) {
          auto path = require_path(file.at("path").get<std::string>());
          std::string portable = path.portable_string();
          std::ranges::transform(portable, portable.begin(), [](unsigned char value) {
            return value >= 'A' && value <= 'Z'
                       ? static_cast<char>(value - 'A' + 'a')
                       : static_cast<char>(value);
          });
          if (!within_folder(path, require_path("Materials")) ||
              !portable.ends_with(".qmat") ||
              !recovered_paths.insert(portable).second) {
            throw std::runtime_error(
                "Invalid material path in deletion recovery.");
          }
          const auto quarantine_path = require_path(
              relative.value().portable_string() + "/materials/material-" +
              std::to_string(material_index) + ".qmat");
          material_files.push_back({
              .path = path,
              .original = require_resolved(file_system, path),
              .quarantine = require_resolved(file_system, quarantine_path),
              .byte_count = file.at("byte_count").get<std::uint64_t>(),
              .content_hash = file.at("content_hash").get<std::uint64_t>(),
          });
          material_quarantine_paths.push_back(
              material_files.back().quarantine);
          ++material_index;
        }
        validate_material_quarantine_entries(
            material_quarantine_directory, material_quarantine_paths);
      }
      const auto state = journal.at("state").get<std::string>();
      if (state != "prepared" && state != "publishing" && state != "committed") {
        throw std::runtime_error("Unknown material deletion recovery state.");
      }
      std::vector<std::filesystem::path> obsolete_backups;
      for (const auto &file : journal.at("files")) {
        const auto target = require_path(file.at("path").get<std::string>());
        const auto manifest_relative = file_system.relative_from_absolute(workspace.manifest_path);
        if (!manifest_relative.ok() ||
            (target != manifest_relative.value() && workspace.manifest.find_scene(target) == nullptr)) {
          throw std::runtime_error("Invalid saved document in material deletion recovery.");
        }
        if (!file.contains("rollback_path")) {
          continue;
        }
        const auto backup = require_private_rollback_path(
            file_system, target, file.at("rollback_path").get<std::string>());
        if (!std::filesystem::exists(backup)) {
          continue;
        }
        const auto bytes = read_bytes(backup);
        const auto hash = quader_io::qdr_binary_content_hash(
            std::as_bytes(std::span(bytes.data(), bytes.size())));
        if (file.at("byte_count").get<std::uint64_t>() != bytes.size() ||
            file.at("content_hash").get<std::uint64_t>() != hash) {
          throw std::runtime_error("Atomic rollback data contains newer or damaged content; "
                                   "recovery preserved it for conflict resolution.");
        }
        obsolete_backups.push_back(backup);
      }
      if (state != "committed") {
        struct RecoveryReplacement final {
          std::filesystem::path target;
          std::string baseline;
          std::string expected_current;
        };
        std::vector<RecoveryReplacement> replacements;
        std::set<std::string> targets;
        const std::string publishing_path = state == "publishing"
            ? journal.at("publishing_path").get<std::string>() : std::string{};
        bool found_publishing_path = state != "publishing";
        for (const auto &file : journal.at("files")) {
          const auto target = require_path(file.at("path").get<std::string>());
          if (!targets.insert(target.portable_string()).second) {
            throw std::runtime_error("Duplicate document in material deletion recovery.");
          }
          const auto manifest_path = file_system.relative_from_absolute(workspace.manifest_path);
          if (!manifest_path.ok() ||
              (target != manifest_path.value() && workspace.manifest.find_scene(target) == nullptr)) {
            throw std::runtime_error("Invalid saved document in material deletion recovery.");
          }
          const auto backup_name = file.at("backup").get<std::string>();
          if (!backup_name.starts_with("file-") || !backup_name.ends_with(".old") ||
              backup_name.find_first_of("/\\") != std::string::npos) {
            throw std::runtime_error("Invalid deletion recovery backup path.");
          }
          const auto backup = require_resolved(
              file_system, require_path(relative.value().portable_string() + "/" + backup_name));
          const auto baseline = read_bytes(backup);
          const auto hash = quader_io::qdr_binary_content_hash(
              std::as_bytes(std::span(baseline.data(), baseline.size())));
          if (file.at("byte_count").get<std::uint64_t>() != baseline.size() ||
              file.at("content_hash").get<std::uint64_t>() != hash) {
            throw std::runtime_error("Material deletion recovery backup is corrupt.");
          }
          const auto target_path = require_resolved(file_system, target);
          const auto current = read_bytes(target_path);
          const auto current_hash = quader_io::qdr_binary_content_hash(
              std::as_bytes(std::span(current.data(), current.size())));
          if (target.portable_string() == publishing_path) {
            found_publishing_path = true;
            if (current != baseline) {
              if (!file.contains("rollback_path")) {
                throw std::runtime_error("Interrupted replacement has no verified rollback "
                                         "location; recovery data was retained for conflict resolution.");
              }
              const auto rollback_absolute = require_private_rollback_path(
                  file_system, target, file.at("rollback_path").get<std::string>());
              const auto displaced = read_bytes(rollback_absolute);
              if (displaced != baseline) {
                throw std::runtime_error("Interrupted replacement displaced newer document "
                                         "content; recovery data was retained for conflict resolution.");
              }
            }
          }
          if (current == baseline) {
            continue;
          }
          if (file.at("replacement_byte_count").get<std::uint64_t>() != current.size() ||
              file.at("replacement_content_hash").get<std::uint64_t>() != current_hash) {
            throw std::runtime_error("A saved document changed after interrupted deletion; "
                                     "recovery will not overwrite newer content: " +
                                     target.portable_string());
          }
          replacements.push_back({target_path, baseline, current});
        }
        if (!found_publishing_path) {
          throw std::runtime_error("Interrupted material publication identifies no saved document.");
        }
        std::filesystem::path folder_quarantine;
        std::filesystem::path folder_destination;
        bool restore_folder = false;
        std::vector<std::size_t> restore_materials;
        if (folder.has_value()) {
          folder_quarantine = require_resolved(
              file_system,
              require_path(relative.value().portable_string() + "/folder"));
          folder_destination = require_resolved(file_system, *folder);
          restore_folder = std::filesystem::exists(folder_quarantine);
          if ((restore_folder && std::filesystem::exists(folder_destination)) ||
              (!restore_folder &&
               !std::filesystem::is_directory(folder_destination))) {
            throw std::runtime_error(
                "Material deletion recovery folder conflicts with current content.");
          }
        } else {
          for (std::size_t index = 0u; index < material_files.size(); ++index) {
            const auto &material = material_files[index];
            const bool quarantined = std::filesystem::exists(material.quarantine);
            const bool original = std::filesystem::exists(material.original);
            if (quarantined == original) {
              throw std::runtime_error(
                  "Material deletion recovery file locations conflict with current content.");
            }
            const auto &source = quarantined ? material.quarantine
                                             : material.original;
            const auto source_status = std::filesystem::symlink_status(source);
            if (!std::filesystem::is_regular_file(source_status) ||
                std::filesystem::is_symlink(source_status)) {
              throw std::runtime_error(
                  "Material deletion recovery source is not a regular file.");
            }
            std::error_code link_error;
            if (std::filesystem::hard_link_count(source, link_error) != 1u ||
                link_error) {
              throw std::runtime_error(
                  "Material deletion recovery source is linked or aliased.");
            }
            const auto bytes = read_bytes(source);
            const auto hash = quader_io::qdr_binary_content_hash(
                std::as_bytes(std::span(bytes.data(), bytes.size())));
            if (bytes.size() != material.byte_count ||
                hash != material.content_hash) {
              throw std::runtime_error(
                  "A material changed after interrupted deletion; recovery "
                  "preserved it for conflict resolution.");
            }
            if (quarantined) {
              restore_materials.push_back(index);
            }
          }
        }
        // Validate every baseline and destination before the first restoration.
        std::vector<std::unique_ptr<quader_io::QdrAtomicFileTransaction>> restore_files;
        for (const auto &replacement : replacements) {
          auto transaction = std::make_unique<quader_io::QdrAtomicFileTransaction>();
          const auto prepared = transaction->prepare(replacement.target);
          if (!prepared.ok) {
            throw std::runtime_error(prepared.message);
          }
          write_bytes(transaction->staging_path(), replacement.baseline);
          restore_files.push_back(std::move(transaction));
        }
        for (std::size_t index = 0u; index < restore_files.size(); ++index) {
          const auto &transaction = restore_files[index];
          if (read_bytes(transaction->target_path()) != replacements[index].expected_current) {
            throw std::runtime_error("A saved document changed while staging deletion recovery.");
          }
          const auto published = transaction->publish();
          if (!published.ok) {
            throw std::runtime_error(published.message);
          }
          if (read_bytes(transaction->backup_path()) != replacements[index].expected_current) {
            throw std::runtime_error("A concurrent saved document update interrupted deletion recovery.");
          }
        }
        if (restore_folder) {
          const auto restored = quader_io::move_project_entry_no_replace(
              folder_quarantine, folder_destination);
          if (!restored.ok) {
            throw std::runtime_error(restored.message);
          }
        }
        std::vector<std::size_t> restored_materials;
        for (const std::size_t index : restore_materials) {
          const auto &material = material_files[index];
          const auto restored = quader_io::move_project_entry_no_replace(
              material.quarantine, material.original);
          if (!restored.ok) {
            std::string compensation;
            for (auto iterator = restored_materials.rbegin();
                 iterator != restored_materials.rend(); ++iterator) {
              const auto &completed = material_files[*iterator];
              const auto reversed = quader_io::move_project_entry_no_replace(
                  completed.original, completed.quarantine);
              if (!reversed.ok) {
                compensation += " " + reversed.message;
              }
            }
            throw std::runtime_error(restored.message + compensation);
          }
          restored_materials.push_back(index);
        }
        for (const auto &transaction : restore_files) {
          const auto finalized = transaction->commit();
          if (!finalized.ok) {
            (void)transaction->preserve_published_for_recovery();
            throw std::runtime_error(finalized.message);
          }
        }
        restored_manifest = true;
      } else {
        if (journal_version == 2) {
          auto reopened = ProjectManifestStore::open(workspace.manifest_path);
          if (!reopened.ok() ||
              reopened.value().manifest.id() != workspace.manifest.id()) {
            throw std::runtime_error(
                "Committed material deletion recovery cannot validate the "
                "current project manifest.");
          }
          workspace.persisted_texture_metadata_fingerprint =
              reopened.value().persisted_texture_metadata_fingerprint;
          workspace.manifest = reopened.take_value().manifest;
          if (std::ranges::any_of(
                  material_ids, [&](const ProjectAssetId id) {
                    return !workspace.manifest.is_material_deleted(id);
                  })) {
            throw std::runtime_error(
                "Committed material deletion is absent from the current manifest; "
                "recovery preserved quarantined materials.");
          }
        }
        for (const auto &material : material_files) {
          if (!std::filesystem::exists(material.quarantine)) {
            continue;
          }
          const auto status = std::filesystem::symlink_status(
              material.quarantine);
          if (!std::filesystem::is_regular_file(status) ||
              std::filesystem::is_symlink(status)) {
            throw std::runtime_error(
                "Committed material quarantine contains an invalid entry.");
          }
          std::error_code link_error;
          if (std::filesystem::hard_link_count(material.quarantine,
                                               link_error) != 1u ||
              link_error) {
            throw std::runtime_error(
                "Committed material quarantine became linked or aliased.");
          }
          const auto bytes = read_bytes(material.quarantine);
          const auto hash = quader_io::qdr_binary_content_hash(
              std::as_bytes(std::span(bytes.data(), bytes.size())));
          if (bytes.size() != material.byte_count ||
              hash != material.content_hash) {
            throw std::runtime_error(
                "Committed material quarantine changed; recovery preserved it.");
          }
        }
        bool notifications_complete = true;
        auto catalog = ProjectMaterialCatalog::scan(file_system);
        if (!catalog.ok()) {
          throw std::runtime_error(catalog.error().message);
        }
        for (const auto &file : journal.at("files")) {
          const auto target = require_path(file.at("path").get<std::string>());
          const auto *scene = workspace.manifest.find_scene(target);
          if (scene == nullptr) {
            continue;
          }
          auto read = QdrDocumentReader::read_project_document(require_resolved(file_system, target));
          if (!read.ok || SceneId::from_bytes(read.scene_uuid) != scene->id ||
              read.source_byte_count != file.at("replacement_byte_count").get<std::uint64_t>() ||
              read.source_content_hash != file.at("replacement_content_hash").get<std::uint64_t>()) {
            warnings += " A scene changed after committed material deletion; its newer content was preserved.";
            continue;
          }
          auto extracted = qdr_project_write_snapshot_from_document(read.document);
          if (!extracted.ok()) {
            throw std::runtime_error(extracted.error().message);
          }
          auto snapshot = extracted.take_value();
          snapshot.minimum_product_release = read.minimum_product_release;
          snapshot.project_asset_dependencies = std::move(read.project_asset_dependencies);
          snapshot.viewport_camera_pose = read.viewport_camera_pose;
          const auto notified = ProjectBridgeNotifications::publish_scene(
              file_system, workspace.manifest.id(), scene->id, target,
              read.source_byte_count, read.source_content_hash, snapshot, catalog.value());
          if (!notified.ok()) {
            warnings += " " + notified.error().message;
            notifications_complete = false;
          }
        }
        if (!notifications_complete) {
          continue;
        }
      }
      if (journal_version == 2) {
        remove_validated_material_quarantine(
            material_quarantine_directory, material_quarantine_paths);
      }
      for (const auto &backup : obsolete_backups) {
        std::filesystem::remove(backup);
      }
      remove_transaction_directory(file_system, relative.value());
    }
    if (restored_manifest) {
      auto opened = ProjectManifestStore::open(workspace.manifest_path);
      if (!opened.ok()) {
        throw std::runtime_error(opened.error().message);
      }
      workspace.persisted_texture_metadata_fingerprint =
          opened.value().persisted_texture_metadata_fingerprint;
      workspace.manifest = opened.take_value().manifest;
    }
    return ProjectIoResult<void>::success(std::move(warnings));
  } catch (const std::exception &error) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::IoFailure,
        "Cannot recover interrupted material deletion: " + std::string(error.what()));
  }
}

} // namespace quader
