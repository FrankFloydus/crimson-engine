////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_deletion_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
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
    std::uint64_t graph_revision) {
  const std::string identity = preview.folder.portable_string() + ":" +
                               std::to_string(graph_revision) + ":" +
                               std::to_string(preview.folder_count);
  const auto hash = quader_io::qdr_binary_content_hash(
      std::as_bytes(std::span(identity.data(), identity.size())));
  return hash == 0u ? 1u : hash;
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
      if (material.draft && removed.contains(material.id)) {
        preview.discards_material_draft = material.dirty;
      }
    }
    std::set<std::string> affected;
    for (const auto &scene : graph->scenes) {
      if (contains_any(scene.material_ids, removed)) {
        affected.insert(scene.display_name +
                        (scene.live ? " (open)" : " (saved)"));
      }
    }
    preview.affected_scenes.assign(affected.begin(), affected.end());
    preview.revision = confirmation_revision(preview, graph->revision);
    preview.complete = graph->complete && preview.errors.empty();
  } catch (const std::exception &error) {
    preview.errors.push_back(error.what());
  }
  return preview;
}

ProjectSessionResult ProjectMaterialDeletionService::delete_folder(
    std::string_view folder, std::uint64_t preview_revision) const {
  const auto preview = preview_folder(folder);
  if (!preview.complete) {
    return {.message = preview.errors.empty() ? "Material dependencies could not be inspected."
                                             : preview.errors.front()};
  }
  if (preview_revision == 0u || preview.revision != preview_revision) {
    return {.message = "Material dependencies changed. Review a new deletion warning."};
  }

  auto &workspace = *project_->workspace_;
  const auto &file_system = workspace.file_system;
  std::vector<StagedFile> staged;
  std::vector<LiveReplacement> live;
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
    const auto marked = candidate.mark_materials_deleted(preview.material_ids);
    if (!marked.ok()) {
      throw std::runtime_error(marked.error().message);
    }
    transaction_relative = require_path(std::string(kTransactionRoot) + "/" +
                                        std::string(ProjectAssetId::generate().str()));
    transaction_directory = require_resolved(file_system, transaction_relative);
    std::filesystem::create_directories(transaction_directory);
    original_folder = require_resolved(file_system, preview.folder);
    quarantine = require_resolved(file_system,
                                 require_path(transaction_relative.portable_string() + "/folder"));
    journal = {{"version", 1}, {"project", workspace.manifest.id().str()},
               {"folder", preview.folder.portable_string()}, {"state", "prepared"},
               {"files", nlohmann::json::array()}};

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
    const auto final_preview = preview_folder(folder);
    if (!final_preview.complete || final_preview.revision != preview.revision) {
      throw std::runtime_error("Project changed while preparing deletion. Review a new warning.");
    }
    write_journal(transaction_directory / "journal.json", journal);
    journal_written = true;
    const auto moved = quader_io::move_project_entry_no_replace(original_folder, quarantine);
    if (!moved.ok) {
      throw std::runtime_error(moved.message);
    }
    folder_moved = true;
    validate_quarantined_folder(file_system, quarantine, preview,
                               project_->material_catalog_, *graph);
    auto scanned = ProjectMaterialCatalog::scan(file_system);
    if (!scanned.ok()) {
      throw std::runtime_error(scanned.error().message);
    }
    ProjectMaterialCatalog candidate_catalog = scanned.take_value();
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
    journal["state"] = "committed";
    write_journal(transaction_directory / "journal.json", journal);
    committed = true;
    workspace.manifest = std::move(candidate);
    project_->material_catalog_ = std::move(candidate_catalog);
    project_->invalidate_material_textures();
    project_->dependencies().invalidate();
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
    result = {.ok = true, .changed = true,
              .message = "Material folder permanently deleted; references use Default Material."};
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
      result.message = "Material folder deletion committed; cleanup will resume when the project opens.";
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
    if (rollback_errors.empty() && !transaction_directory.empty()) {
      try {
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
      if (journal.at("version") != 1 ||
          journal.at("project").get<std::string>() != workspace.manifest.id().str()) {
        throw std::runtime_error("Material deletion recovery identity is invalid.");
      }
      const auto folder = require_path(journal.at("folder").get<std::string>());
      if (folder == require_path("Materials") || !within_folder(folder, require_path("Materials"))) {
        throw std::runtime_error("Invalid folder in material deletion recovery.");
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
        const auto quarantine = require_resolved(
            file_system, require_path(relative.value().portable_string() + "/folder"));
        const auto destination = require_resolved(file_system, folder);
        const bool restore_folder = std::filesystem::exists(quarantine);
        if ((restore_folder && std::filesystem::exists(destination)) ||
            (!restore_folder && !std::filesystem::is_directory(destination))) {
          throw std::runtime_error("Material deletion recovery folder conflicts with current content.");
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
              quarantine, destination);
          if (!restored.ok) {
            throw std::runtime_error(restored.message);
          }
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
