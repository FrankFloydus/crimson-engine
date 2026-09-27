////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/session/project_session.hpp"
#include "editor/session/tool_session_defaults.hpp"
#include "editor/tools/extrude_tool_service.hpp"

#include "core/qstr.hpp"
#include "core/user_config_paths.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/session/project_asset_bootstrap_service.hpp"
#include "editor/session/project_autosave_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/scene_viewport_camera_persistence.hpp"
#include "editor/instances/project_clipboard_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/material/project_image_decoder.hpp"
#include "editor/material/project_material_texture_snapshot.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/material/project_material_deletion_service.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/tools/native_viewport_tool_coordinator.hpp"
#include "assets/default_material_catalog.hpp"
#include "io/project/project_material_provisioner.hpp"
#include "io/project/project_material_reference_resolver.hpp"
#include "io/project/project_bridge_notifications.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <limits>
#include <set>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace quader {
namespace {

static_assert(std::is_nothrow_move_assignable_v<SceneId>);
static_assert(
    std::is_nothrow_move_assignable_v<std::optional<ProjectRelativePath>>);
static_assert(std::is_nothrow_move_assignable_v<ProjectManifest>);
static_assert(std::is_nothrow_move_assignable_v<std::string>);
static_assert(std::is_nothrow_move_constructible_v<ProjectSessionResult>);

ProjectSessionResult failure(std::string message) {
  return {.ok = false, .changed = false, .message = std::move(message)};
}

ProjectSessionResult success(bool changed, std::string message,
                             std::optional<SceneSessionId> scene = {}) {
  return {
      .ok = true,
      .changed = changed,
      .message = std::move(message),
      .scene_session_id = scene,
  };
}

bool has_pending_scene_edits(const OpenSceneSession &scene) {
  if (!scene.session) return false;
  return scene.session->document_commands().property_edit_active() ||
         scene.session->native_tools().has_pending_history_preview() ||
         scene.session->modeling().can_cancel_active_preview_for_undo() ||
         scene.session->modeling().pending_operation_active() ||
         scene.session->modeling().curve_document_preview_active();
}

void append_bridge_notification_warning(
    ProjectSessionResult &result, const ProjectIoResult<void> &notification) {
  const std::string &warning = notification.ok()
                                   ? notification.warning()
                                   : notification.error().message;
  if (!warning.empty()) {
    result.warnings.push_back(warning);
    result.message += " " + warning;
  }
}

std::string path_text_utf8(const std::filesystem::path &path) {
  const std::u8string encoded = path.generic_u8string();
  return {encoded.begin(), encoded.end()};
}

std::string display_name_for_path(const ProjectRelativePath &path) {
  const std::string stem = path_text_utf8(path.native_path().stem());
  return stem.empty() ? std::string("Untitled Scene") : stem;
}

bool has_qdr_extension(const ProjectRelativePath &path) {
  return QStr::to_ascii_lower(path.native_path().extension().string()) ==
         ".qdr";
}

ProjectIoResult<std::filesystem::path> resolve_authored_scene_save_path(
    const ProjectFileSystem &file_system, const ProjectRelativePath &path) {
  static const ProjectRelativePath kScenesDirectory =
      ProjectRelativePath::parse_portable("Scenes").take_value();
  auto resolved =
      file_system.resolve_for_write_in_directory(path, kScenesDirectory);
  if (!resolved.ok()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        resolved.error().code,
        "Scene files must be saved in the current project's Scenes folder or "
        "one of its subfolders. " + resolved.error().message,
        resolved.error().path);
  }
  return resolved;
}

/// Projects resident saved tabs into manifest-restorable workspace state.
struct PersistedWorkspaceState final {
  std::vector<ProjectRelativePath> open_paths;
  std::optional<ProjectRelativePath> active_path;
};

PersistedWorkspaceState workspace_state_for(
    std::span<const std::unique_ptr<OpenSceneSession>> scenes,
    SceneSessionId active_scene_id) {
  PersistedWorkspaceState state;
  state.open_paths.reserve(scenes.size());
  for (const std::unique_ptr<OpenSceneSession> &scene : scenes) {
    if (!scene || !scene->path.has_value()) {
      continue;
    }
    state.open_paths.push_back(*scene->path);
    if (scene->session_id == active_scene_id) {
      state.active_path = *scene->path;
    }
  }
  return state;
}

std::filesystem::path default_runtime_resource_root() {
#if defined(_WIN32)
  std::wstring path(32'768u, L'\0');
  const DWORD count =
      ::GetModuleFileNameW(nullptr, path.data(),
                           static_cast<DWORD>(path.size()));
  if (count != 0u && count < path.size()) {
    path.resize(count);
    return std::filesystem::path(path).parent_path();
  }
#endif
  return std::filesystem::current_path();
}

std::optional<ProjectAssetId> default_project_material_id() {
  return ProjectAssetId::parse(kDefaultProjectMaterialId);
}

void append_project_material_reference(
    MaterialReference reference, std::set<ProjectAssetId> &ids) {
  if (reference.project_owned()) {
    ids.insert(reference.project_asset());
  }
}

std::set<ProjectAssetId>
project_material_references(const QdrProjectWriteSnapshot &snapshot) {
  std::set<ProjectAssetId> ids;
  for (const QdrProjectEntityWriteSnapshot &entity : snapshot.entities) {
    if (!entity.material.has_value()) {
      continue;
    }
    append_project_material_reference(entity.material->material, ids);
    for (const auto &[slot, material] : entity.material->material_slots) {
      (void)slot;
      append_project_material_reference(material, ids);
    }
  }
  return ids;
}

/// Normalizes saved and historical bindings without dropping dormant face slots.
bool canonicalize_material_component(MaterialComponent &component,
                                     const ProjectManifest &manifest) {
  bool changed = false;
  const auto canonicalize = [&](MaterialReference &reference) {
    if (!reference.project_owned()) {
      return;
    }
    const ProjectAssetId canonical =
        ProjectMaterialReferenceResolver::canonical_material_id(
            manifest, reference.project_asset());
    if (canonical != reference.project_asset()) {
      reference = MaterialReference::project(canonical);
      changed = true;
    }
  };
  canonicalize(component.material);
  for (auto &[slot, reference] : component.material_slots) {
    (void)slot;
    canonicalize(reference);
  }
  return changed;
}

/// Reports validated scene snapshot extraction before an atomic QDR write.
struct SceneWriteSnapshotResult final {
  bool ok = false;
  std::string message;
  QdrProjectWriteSnapshot snapshot;
};

SceneWriteSnapshotResult scene_write_snapshot(
    const SceneSession &session,
    const ProjectMaterialCatalog &materials,
    const ProjectManifest &manifest) {
  const SceneContentDocument &document = session.document();
  const SceneContentDocumentIdentityReport identity =
      document.validate_object_identity_invariants();
  if (!identity.ok()) {
    return {.message = identity.issues.empty()
                           ? "Scene object identity validation failed."
                           : identity.issues.front().message};
  }
  const SceneContentDocumentAssetReferenceReport references =
      document.validate_asset_references();
  if (!references.ok()) {
    return {.message = references.issues.empty()
                           ? "Scene asset reference validation failed."
                           : references.issues.front().message};
  }

  QdrProjectWriteSnapshotResult snapshot_result =
      qdr_project_write_snapshot_from_document(document);
  if (!snapshot_result.ok()) {
    return {.message = snapshot_result.error().message};
  }
  QdrProjectWriteSnapshot snapshot = snapshot_result.take_value();
  snapshot.minimum_product_release = std::max(
      session.minimum_product_release(), current_product_release());
  if (!manifest.deleted_material_ids().empty()) {
    for (QdrProjectEntityWriteSnapshot &entity : snapshot.entities) {
      if (entity.material.has_value()) {
        canonicalize_material_component(*entity.material, manifest);
      }
    }
  }
  append_qdr_viewport_camera_pose(session, snapshot);
  for (const ProjectAssetId &id : project_material_references(snapshot)) {
    const ProjectMaterialCatalogEntry *material = materials.find(id);
    if (material == nullptr) {
      return {.message = "Scene references missing project material " +
                         std::string(id.str()) + "."};
    }
    snapshot.project_asset_dependencies.push_back({
        .stable_id = id,
        .kind = ProjectAssetKind::Material,
        .path = material->path,
    });
  }
  return {
      .ok = true,
      .snapshot = std::move(snapshot),
  };
}

/// Publishes a scene together with a raised project requirement, retaining the
/// old scene until the manifest update has committed.
QdrProjectWriteResult write_scene_with_project_requirement(
    ProjectWorkspaceStorage &workspace, const std::filesystem::path &path,
    const QdrProjectWriteSnapshot &snapshot, const SceneId &scene_id) {
  if (workspace.manifest.minimum_product_release() >= snapshot.minimum_product_release) {
    return QdrDocumentWriter::write_project_document_atomic(
        path, snapshot, scene_id.bytes());
  }
  auto candidate = workspace.manifest;
  candidate.require_product_release(snapshot.minimum_product_release);
  quader_io::QdrAtomicFileTransaction transaction;
  const auto prepared = transaction.prepare(path);
  if (!prepared.ok) {
    return {.message = prepared.message};
  }
  auto written = QdrDocumentWriter::write_project_document_atomic(
      transaction.staging_path(), snapshot, scene_id.bytes());
  if (!written.ok) {
    return written;
  }
  const auto published = transaction.publish();
  if (!published.ok) {
    return {.message = published.message};
  }
  const auto persisted = ProjectManifestStore::save_atomic(
      workspace.file_system, workspace.manifest_path, candidate);
  if (!persisted.ok()) {
    return {.message = persisted.error().message};
  }
  const auto committed = transaction.commit();
  if (!committed.ok) {
    const auto restored = ProjectManifestStore::save_atomic(workspace);
    if (!restored.ok()) {
      (void)transaction.preserve_published_for_recovery();
      workspace.manifest = std::move(candidate);
      return {.message = committed.message + "; previous project manifest could not be restored: " +
                         restored.error().message};
    }
    return {.message = committed.message};
  }
  workspace.manifest = std::move(candidate);
  if (!committed.message.empty()) {
    if (!written.message.empty()) {
      written.message += "; ";
    }
    written.message += committed.message;
  }
  return written;
}

std::optional<std::string> validate_scene_project_dependencies(
    const SceneContentDocument &document,
    std::span<const ProjectAssetReference> dependencies,
    const ProjectMaterialCatalog &materials,
    const ProjectFileSystem &file_system) {
  std::set<ProjectAssetId> declared_materials;
  ProjectAssetResolver resolver(file_system);
  for (const ProjectAssetReference &dependency : dependencies) {
    if (dependency.kind == ProjectAssetKind::Material) {
      if (materials.find(dependency.stable_id) == nullptr) {
        return "Scene dependency references missing project material " +
               std::string(dependency.stable_id.str()) + ".";
      }
      declared_materials.insert(dependency.stable_id);
      continue;
    }
    const ProjectIoResult<std::filesystem::path> resolved =
        resolver.resolve_existing(dependency);
    if (!resolved.ok()) {
      return "Scene project dependency is unavailable: " +
             resolved.error().message;
    }
  }

  QdrProjectWriteSnapshotResult snapshot_result =
      qdr_project_write_snapshot_from_document(document);
  if (!snapshot_result.ok()) {
    return snapshot_result.error().message;
  }
  const QdrProjectWriteSnapshot &snapshot = snapshot_result.value();
  for (const ProjectAssetId &id : project_material_references(snapshot)) {
    if (!declared_materials.contains(id)) {
      return "Scene project material reference has no dependency record: " +
             std::string(id.str()) + ".";
    }
    if (materials.find(id) == nullptr) {
      return "Scene references missing project material " +
             std::string(id.str()) + ".";
    }
  }
  return std::nullopt;
}

/// Applies permanent project aliases before checking persisted dependency records.
std::optional<std::string> canonicalize_scene_project_materials(
    QdrProjectReadResult &read, const ProjectManifest &manifest,
    const ProjectMaterialCatalog &materials) {
  if (manifest.deleted_material_ids().empty()) {
    return std::nullopt;
  }
  SceneDocument &scene = read.document.scene();
  for (const EntityId entity : scene.entities()) {
    const MaterialComponent *existing = scene.material(entity);
    if (existing == nullptr) {
      continue;
    }
    MaterialComponent component = *existing;
    if (canonicalize_material_component(component, manifest) &&
        !scene.replace_existing_material_reference_component(
            entity, std::move(component))) {
      return "Could not replace a deleted project material reference.";
    }
  }

  for (ProjectAssetReference &dependency : read.project_asset_dependencies) {
    if (dependency.kind != ProjectAssetKind::Material) {
      continue;
    }
    const ProjectAssetId canonical =
        ProjectMaterialReferenceResolver::canonical_material_id(
            manifest, dependency.stable_id);
    if (canonical != dependency.stable_id) {
      dependency.stable_id = canonical;
      if (const ProjectMaterialCatalogEntry *entry = materials.find(canonical)) {
        dependency.path = entry->path;
      }
    }
  }
  std::set<ProjectAssetId> canonical_dependencies;
  std::erase_if(read.project_asset_dependencies,
                [&](const ProjectAssetReference &dependency) {
    return dependency.kind == ProjectAssetKind::Material &&
           !canonical_dependencies.insert(dependency.stable_id).second;
  });
  return std::nullopt;
}

void rollback_new_project(
    const ProjectWorkspaceStorage &workspace,
    const ProjectMaterialProvisioningChanges &material_changes) {
  ProjectMaterialProvisioner::rollback(material_changes);
  std::error_code ignored;
  std::filesystem::remove(workspace.manifest_path, ignored);
  ignored.clear();
  std::filesystem::remove(workspace.file_system.root() / "Scenes", ignored);
}

} // namespace

ProjectSession::ProjectSession(std::filesystem::path recent_scene_storage)
    : path_array_defaults_(std::make_shared<PathArraySessionDefaults>()),
      extrude_defaults_(std::make_shared<ExtrudeSessionDefaults>()),
      tool_defaults_(std::make_shared<ToolSessionDefaults>()),
      fallback_scene_(path_array_defaults_, extrude_defaults_, tool_defaults_),
      recent_scene_store_(recent_scene_storage.empty()
                              ? quader_user_config_directory({}) /
                                    "recent-scenes.json"
                              : std::move(recent_scene_storage)) {
  resource_root_ = default_runtime_resource_root();
  (void)ProjectAssetBootstrapService{}.bootstrap_default_assets(fallback_scene_);
  fallback_scene_.mark_clean();
  standard_material_editor_ =
      std::make_unique<StandardMaterialEditSessionService>(*this);
  clipboard_ = std::make_unique<ProjectClipboardService>();
  autosave_ = std::make_unique<ProjectAutosaveService>();
  dependencies_ = std::make_unique<ProjectAssetDependencyService>(*this);
  material_usage_ = std::make_unique<ProjectMaterialUsageService>(*this);
}

ProjectSession::~ProjectSession() = default;

ProjectAssetDependencyService &ProjectSession::dependencies() noexcept {
  return *dependencies_;
}

const ProjectAssetDependencyService &ProjectSession::dependencies() const noexcept {
  return *dependencies_;
}

ProjectMaterialUsageService &ProjectSession::material_usage() noexcept {
  return *material_usage_;
}

const ProjectMaterialUsageService &ProjectSession::material_usage() const noexcept {
  return *material_usage_;
}

ProjectAutosaveService &ProjectSession::autosave() noexcept { return *autosave_; }

const ProjectAutosaveService &ProjectSession::autosave() const noexcept {
  return *autosave_;
}

ProjectSessionResult ProjectSession::write_scene_recovery(
    SceneSessionId id, const ProjectRelativePath &path) const {
  const OpenSceneSession *scene = find(id);
  if (!workspace_ || scene == nullptr || scene->session == nullptr ||
      !is_project_autosave_path(path) || !has_qdr_extension(path)) {
    return failure("Autosave requires an open scene and a recovery destination.");
  }
  const SceneSession &session = *scene->session;
  if (session.document_commands().property_edit_active() ||
      session.native_tools().has_pending_history_preview() ||
      session.native_tools().pointer_dragging() ||
      session.modeling().can_cancel_active_tool_or_preview() ||
      session.modeling().curve_document_preview_active()) {
    return success(false, "Autosave deferred until the active edit finishes.", id);
  }
  const auto absolute = workspace_->file_system.resolve_for_write(path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  std::error_code error;
  if (std::filesystem::exists(absolute.value(), error) || error) {
    return failure("Autosave destination already exists or is unavailable.");
  }
  SceneWriteSnapshotResult snapshot = scene_write_snapshot(
      session, material_catalog_, workspace_->manifest);
  if (!snapshot.ok) {
    return failure(snapshot.message);
  }
  const QdrProjectWriteResult written =
      QdrDocumentWriter::write_project_document_atomic(
          absolute.value(), snapshot.snapshot, scene->scene_id.bytes());
  if (!written.ok) {
    return failure(written.message.empty() ? "Autosave failed." : written.message);
  }
  ProjectSessionResult result = success(true, "Recovery copy saved.", id);
  if (!written.message.empty()) {
    result.warnings.push_back(written.message);
  }
  return result;
}

ProjectSessionResult ProjectSession::recover_scene(
    const ProjectRelativePath &path, std::string display_name) {
  if (!workspace_ || !is_project_autosave_path(path) || !has_qdr_extension(path)) {
    return failure("Recovery requires a recovery copy in the open project.");
  }
  const auto absolute = workspace_->file_system.resolve_existing_file(path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  QdrProjectReadResult read = QdrDocumentReader::read_project_document(
          absolute.value(), current_product_release());
  if (!read.ok) {
    return failure(read.message.empty() ? "Recovery copy could not be read." : read.message);
  }
  if (const auto canonicalization_error = canonicalize_scene_project_materials(
          read, workspace_->manifest, material_catalog_)) {
    return failure(*canonicalization_error);
  }
  if (const auto dependency_error = validate_scene_project_dependencies(
          read.document, read.project_asset_dependencies, material_catalog_,
          workspace_->file_system)) {
    return failure(*dependency_error);
  }
  if (display_name.empty()) {
    display_name = "Recovered Scene";
  }
  auto scene = make_scene(SceneId::generate(), std::move(display_name), std::nullopt,
                          std::move(read.document),
                          viewport_camera_pose_from_qdr(read.viewport_camera_pose),
      read.minimum_product_release);
  scene->identity_dirty = true;
  ProjectSessionResult result =
      install_scene_and_activate(std::move(scene), workspace_->manifest);
  if (result.ok) {
    result.message = "Recovery copy opened as an unsaved scene. Use Save As to keep it.";
  }
  return result;
}

ProjectSessionResult ProjectSession::create(const std::filesystem::path &root,
                                            bool discard_existing_dirty) {
  if (has_project() && (has_dirty_scenes() || has_dirty_material()) &&
      !discard_existing_dirty) {
    return failure("Project contains unsaved document changes.");
  }
  ProjectIoResult<ProjectWorkspaceStorage> created =
      ProjectManifestStore::create_in_empty_directory(root);
  if (!created.ok()) {
    return failure(created.error().message);
  }
  ProjectWorkspaceStorage candidate = created.take_value();
  ProjectIoResult<ProjectMaterialCatalog> initial_catalog =
      ProjectMaterialCatalog::scan(candidate.file_system);
  if (!initial_catalog.ok()) {
    rollback_new_project(candidate, {});
    return failure(initial_catalog.error().message);
  }
  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          candidate.file_system, resource_root_, initial_catalog.value());
  if (!provisioned.ok()) {
    rollback_new_project(candidate, {});
    return failure(provisioned.error().message);
  }
  ProjectMaterialProvisioningChanges material_changes =
      provisioned.take_value();
  ProjectIoResult<ProjectMaterialCatalog> scanned =
      ProjectMaterialCatalog::scan(candidate.file_system);
  if (!scanned.ok()) {
    rollback_new_project(candidate, material_changes);
    return failure(scanned.error().message);
  }
  ProjectMaterialCatalog candidate_catalog = scanned.take_value();
  const std::optional<ProjectAssetId> default_material =
      default_project_material_id();
  if (!default_material.has_value() ||
      candidate_catalog.find(*default_material) == nullptr) {
    rollback_new_project(candidate, material_changes);
    return failure("Built-in default project material is unavailable.");
  }
  ProjectIoResult<void> manifest_saved =
      ProjectManifestStore::save_atomic(candidate);
  if (!manifest_saved.ok()) {
    rollback_new_project(candidate, material_changes);
    return failure(manifest_saved.error().message);
  }

  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    const CommandResult settled = scene
        ? settle_transient_state(*scene, discard_existing_dirty)
        : command_unchanged_result();
    if (!settled.ok) {
      rollback_new_project(candidate, material_changes);
      return failure(settled.message);
    }
  }
  if (!discard_existing_dirty) {
    const auto material = standard_material_editor().accept_base_color_preview();
    if (!material.ok) {
      rollback_new_project(candidate, material_changes);
      return failure(material.message);
    }
  }
  if (has_project() && (has_dirty_scenes() || has_dirty_material()) &&
      !discard_existing_dirty) {
    rollback_new_project(candidate, material_changes);
    return failure("Project contains unsaved scene changes.");
  }

  constexpr std::string_view kSceneName = "Untitled Scene";
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 0u,
      .name = std::string(kSceneName),
  });
  std::unique_ptr<OpenSceneSession> first_scene =
      make_scene(SceneId::generate(), std::string(kSceneName), std::nullopt,
                  std::move(document));
  const SceneSessionId first_id = first_scene->session_id;

  release_open_scene_ids();
  (void)standard_material_editor_->close(true);
  workspace_ = std::move(candidate);
  material_catalog_ = std::move(candidate_catalog);
  invalidate_material_textures();
  open_scenes_.push_back(std::move(first_scene));
  active_scene_id_ = first_id;
  ++activation_generation_;
  ProjectSessionResult result =
      success(true, "Quader project created.", first_id);
  result.warnings.assign(material_catalog_.warnings().begin(),
                         material_catalog_.warnings().end());
  return result;
}

ProjectSessionResult ProjectSession::preflight_open(
    const std::filesystem::path &manifest_path,
    std::optional<ProjectRelativePath> required_scene) {
  auto opened = ProjectManifestStore::open(manifest_path);
  if (!opened.ok()) {
    return failure(opened.error().message);
  }
  const auto &candidate = opened.value();
  const auto required = candidate.manifest.minimum_product_release();
  if (!product_release_can_open(current_product_release(), required)) {
    return failure(product_release_unavailable_message(required));
  }
  if (current_product_release() == kInternalFullRelease) {
    return success(false, "Project release is compatible.");
  }
  std::vector<ProjectRelativePath> paths(
      candidate.manifest.open_scene_paths().begin(),
      candidate.manifest.open_scene_paths().end());
  if (required_scene && std::find(paths.begin(), paths.end(), *required_scene) == paths.end()) {
    paths.push_back(*required_scene);
  }
  for (const auto &path : paths) {
    const auto absolute = candidate.file_system.resolve_existing_file(path);
    if (!absolute.ok()) {
      continue; // Existing restoration reports missing scenes without opening them.
    }
    const auto candidate_scene = QdrDocumentReader::read_project_document(
        absolute.value(), current_product_release());
    // Any malformed existing candidate prevents compatibility from being
    // established. Reject before closing scenes or running recovery/provisioning;
    // limiting this to named release diagnostics misses earlier header failures.
    if (!candidate_scene.ok) {
      return failure(path.portable_string() + ": " + candidate_scene.message);
    }
  }
  return success(false, "Project release is compatible.");
}

ProjectSessionResult
ProjectSession::open(const std::filesystem::path &manifest_path,
                     bool discard_existing_dirty) {
  return open_workspace(manifest_path, discard_existing_dirty, std::nullopt, {});
}

ProjectSessionResult ProjectSession::open_scene_project(
    const std::filesystem::path &manifest_path, ProjectRelativePath scene_path,
    const std::function<bool()> &prepare_replacement) {
  if (!scene_path.valid() || !has_qdr_extension(scene_path) ||
      is_project_autosave_path(scene_path)) {
    return failure("Open Recent requires a saved project-relative .qdr scene.");
  }
  return open_workspace(manifest_path, false, std::move(scene_path),
                        prepare_replacement);
}

ProjectSessionResult ProjectSession::open_workspace(
    const std::filesystem::path &manifest_path, bool discard_existing_dirty,
    std::optional<ProjectRelativePath> required_scene,
    const std::function<bool()> &prepare_replacement) {
  if (has_project() && (has_dirty_scenes() || has_dirty_material()) &&
      !discard_existing_dirty && !prepare_replacement) {
    return failure("Project contains unsaved document changes.");
  }
  const auto compatible = preflight_open(manifest_path, required_scene);
  if (!compatible.ok) {
    return compatible;
  }
  ProjectIoResult<ProjectWorkspaceStorage> opened =
      ProjectManifestStore::open(manifest_path);
  if (!opened.ok()) {
    return failure(opened.error().message);
  }

  ProjectWorkspaceStorage candidate = opened.take_value();
  const auto recovered = ProjectMaterialDeletionService::recover(candidate);
  if (!recovered.ok()) {
    return failure(recovered.error().message);
  }
  const ProjectManifest original_manifest = candidate.manifest;
  ProjectIoResult<ProjectMaterialCatalog> initial_catalog =
      ProjectMaterialCatalog::scan(candidate.file_system);
  if (!initial_catalog.ok()) {
    return failure(initial_catalog.error().message);
  }
  ProjectIoResult<ProjectMaterialProvisioningChanges> provisioned =
      ProjectMaterialProvisioner::ensure_default(
          candidate.file_system, resource_root_, initial_catalog.value());
  if (!provisioned.ok()) {
    return failure(provisioned.error().message);
  }
  ProjectMaterialProvisioningChanges material_changes =
      provisioned.take_value();
  ProjectIoResult<ProjectMaterialCatalog> rescanned =
      ProjectMaterialCatalog::scan(candidate.file_system);
  if (!rescanned.ok()) {
    ProjectMaterialProvisioner::rollback(material_changes);
    return failure(rescanned.error().message);
  }
  ProjectMaterialCatalog candidate_catalog = rescanned.take_value();
  const std::optional<ProjectAssetId> default_material =
      default_project_material_id();
  if (!default_material.has_value() ||
      candidate_catalog.find(*default_material) == nullptr) {
    ProjectMaterialProvisioner::rollback(material_changes);
    return failure("Built-in default project material is unavailable.");
  }
  const std::optional<ProjectRelativePath> requested_active =
      required_scene.has_value() ? required_scene
                                 : candidate.manifest.active_scene_path();
  if (has_project() && (has_dirty_scenes() || has_dirty_material()) &&
      !discard_existing_dirty && !prepare_replacement) {
    ProjectMaterialProvisioner::rollback(material_changes);
    return failure("Project contains unsaved scene changes.");
  }
  std::vector<std::unique_ptr<OpenSceneSession>> scenes;
  std::vector<std::string> warnings(candidate_catalog.warnings().begin(),
                                    candidate_catalog.warnings().end());
  if (!recovered.warning().empty()) {
    warnings.push_back(recovered.warning());
  }
  std::vector<ProjectRelativePath> restore_paths(
      candidate.manifest.open_scene_paths().begin(),
      candidate.manifest.open_scene_paths().end());
  if (required_scene.has_value() &&
      std::find(restore_paths.begin(), restore_paths.end(), *required_scene) ==
          restore_paths.end()) {
    restore_paths.push_back(*required_scene);
  }
  scenes.reserve(restore_paths.size());
  for (const ProjectRelativePath &path : restore_paths) {
    ProjectIoResult<std::filesystem::path> absolute =
        candidate.file_system.resolve_existing_file(path);
    if (!absolute.ok()) {
      warnings.push_back(path.portable_string() + ": " +
                         absolute.error().message);
      continue;
    }
    QdrProjectReadResult read =
        QdrDocumentReader::read_project_document(
            absolute.value(), current_product_release());
    if (!read.ok) {
      warnings.push_back(path.portable_string() + ": " + read.message);
      continue;
    }
    if (const auto canonicalization_error = canonicalize_scene_project_materials(
            read, candidate.manifest, candidate_catalog)) {
      warnings.push_back(path.portable_string() + ": " +
                         *canonicalization_error);
      continue;
    }
    if (const std::optional<std::string> dependency_error =
            validate_scene_project_dependencies(
                read.document, read.project_asset_dependencies,
                candidate_catalog, candidate.file_system)) {
      warnings.push_back(path.portable_string() + ": " +
                         *dependency_error);
      continue;
    }
    const ProjectSceneCatalogEntry *catalog =
        candidate.manifest.find_scene(path);
    if (catalog == nullptr) {
      warnings.push_back(path.portable_string() +
                         ": scene is absent from the project catalog.");
      continue;
    }
    const SceneId embedded_id = SceneId::from_bytes(read.scene_uuid);
    const bool identity_mismatch = embedded_id != catalog->id;
    std::unique_ptr<OpenSceneSession> scene =
        make_scene(catalog->id, display_name_for_path(path), path,
                   std::move(read.document),
                   viewport_camera_pose_from_qdr(
                       read.viewport_camera_pose),
                   read.minimum_product_release);
    scene->identity_dirty = identity_mismatch;
    if (identity_mismatch) {
      warnings.push_back(
          path.portable_string() +
          ": file identity did not match the catalog and was reseeded.");
    }
    scenes.push_back(std::move(scene));
  }
  const auto abandon_staged = [&] {
    for (const auto &scene : scenes) {
      release_session_id(scene->session_id);
    }
    ProjectMaterialProvisioner::rollback(material_changes);
  };
  if (required_scene.has_value() &&
      std::none_of(scenes.begin(), scenes.end(), [&](const auto &scene) {
        return scene->path == required_scene;
      })) {
    abandon_staged();
    const std::string prefix = required_scene->portable_string() + ": ";
    const auto warning = std::find_if(
        warnings.begin(), warnings.end(), [&](const std::string &entry) {
          return entry.starts_with(prefix);
        });
    return failure(warning == warnings.end()
                       ? "The requested recent scene could not be opened."
                       : *warning);
  }
  ProjectSessionResult installed = install_workspace(
      std::move(candidate), std::move(candidate_catalog),
      std::move(scenes), requested_active, std::move(warnings),
      prepare_replacement, original_manifest, discard_existing_dirty);
  if (!installed.ok || !installed.changed) {
    ProjectMaterialProvisioner::rollback(material_changes);
  } else if (active_scene_id_.valid()) {
    remember_scene(active_scene_id_);
  }
  return installed;
}

ProjectSessionResult ProjectSession::close(bool discard_dirty) {
  if (!workspace_) {
    return success(false, "No project is open.");
  }
  if (!discard_dirty && (has_dirty_scenes() || has_dirty_material())) {
    return failure("Project contains unsaved document changes.");
  }
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    const CommandResult settled = scene
        ? settle_transient_state(*scene, discard_dirty)
        : command_unchanged_result();
    if (!settled.ok) {
      return failure(settled.message);
    }
  }
  if (!discard_dirty) {
    const auto material = standard_material_editor().accept_base_color_preview();
    if (!material.ok) return failure(material.message);
    if (has_dirty_scenes() || has_dirty_material()) {
      return failure("Project contains unsaved document changes.");
    }
  }
  release_open_scene_ids();
  (void)standard_material_editor_->close(true);
  workspace_.reset();
  material_catalog_ = {};
  invalidate_material_textures();
  clipboard_->clear();
  ++activation_generation_;
  return success(true, "Project closed.");
}

ProjectSessionResult ProjectSession::new_scene() {
  if (!workspace_) {
    return failure("Open or create a project before creating a scene.");
  }
  const SceneId scene_id = SceneId::generate();
  const std::string name = unique_untitled_name();
  SceneContentDocument document(SceneContentDocumentRecord{
      .id = 0u,
      .name = name,
  });
  std::unique_ptr<OpenSceneSession> scene =
      make_scene(scene_id, name, std::nullopt, std::move(document));
  ProjectSessionResult installed =
      install_scene_and_activate(std::move(scene), workspace_->manifest);
  if (installed.ok) {
    installed.message = "New scene created.";
  }
  return installed;
}

ProjectSessionResult ProjectSession::open_scene(ProjectRelativePath path) {
  if (!workspace_) {
    return failure("Open or create a project before opening a scene.");
  }
  if (!path.valid() || !has_qdr_extension(path)) {
    return failure("Scene path must be a project-relative .qdr file.");
  }
  if (is_project_autosave_path(path)) {
    return failure("Use Recover Auto Save to open a recovery copy safely.");
  }
  ProjectIoResult<std::filesystem::path> absolute =
      workspace_->file_system.resolve_existing_file(path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  SceneSessionId existing;
  if (path_is_open(path, &existing)) {
    ProjectSessionResult activated = activate(existing);
    if (activated.ok) {
      activated.message = "Scene is already open.";
      remember_scene(existing);
    }
    return activated;
  }
  QdrProjectReadResult read =
      QdrDocumentReader::read_project_document(
          absolute.value(), current_product_release());
  if (!read.ok) {
    return failure(read.message.empty() ? "Scene open failed." : read.message);
  }
  if (const auto canonicalization_error = canonicalize_scene_project_materials(
          read, workspace_->manifest, material_catalog_)) {
    return failure(*canonicalization_error);
  }
  if (const std::optional<std::string> dependency_error =
          validate_scene_project_dependencies(
              read.document, read.project_asset_dependencies,
              material_catalog_, workspace_->file_system)) {
    return failure(*dependency_error);
  }

  const ProjectSceneCatalogEntry *catalog = find_catalog_scene(path);
  const ProjectRelativePath session_path =
      catalog != nullptr ? catalog->path : path;
  const SceneId embedded_id = SceneId::from_bytes(read.scene_uuid);
  SceneId scene_id = catalog != nullptr ? catalog->id : embedded_id;
  const bool identity_mismatch =
      catalog != nullptr && embedded_id != catalog->id;
  bool reseeded_duplicate = false;
  const bool catalog_identity_collision =
      catalog == nullptr && workspace_->manifest.find_scene(scene_id) != nullptr;
  if (catalog_identity_collision ||
      scene_id_is_open(scene_id, &session_path)) {
    scene_id = SceneId::generate();
    reseeded_duplicate = true;
  }
  ProjectManifest candidate_manifest = workspace_->manifest;
  if (catalog == nullptr || reseeded_duplicate) {
    ProjectIoResult<void> registered = candidate_manifest.register_scene(
        {.id = scene_id, .path = session_path});
    if (!registered.ok()) {
      return failure(registered.error().message);
    }
  }
  std::unique_ptr<OpenSceneSession> scene = make_scene(
      scene_id, display_name_for_path(session_path), session_path,
      std::move(read.document),
      viewport_camera_pose_from_qdr(read.viewport_camera_pose),
      read.minimum_product_release);
  if (reseeded_duplicate || identity_mismatch) {
    scene->identity_dirty = true;
  }
  ProjectSessionResult result = install_scene_and_activate(
      std::move(scene), std::move(candidate_manifest));
  if (!result.ok) {
    return result;
  }
  result.message = "Scene opened.";
  remember_scene(active_scene_id_);
  if (reseeded_duplicate) {
    result.warnings.push_back(
        "The scene UUID duplicated another open scene and was reseeded. Save "
        "the scene to persist its new identity.");
  } else if (identity_mismatch) {
    result.warnings.push_back(
        "The scene UUID did not match its project catalog entry. Save the "
        "scene to persist the catalog identity.");
  }
  return result;
}

ProjectSessionResult ProjectSession::activate(SceneSessionId id) {
  OpenSceneSession *target = find(id);
  if (target == nullptr) {
    return failure("Scene tab is closed or stale.");
  }
  if (active_scene_id_ == id) {
    return success(false, "Scene is already active.", id);
  }
  if (OpenSceneSession *current = active_scene()) {
    const CommandResult settled = settle_transient_state(*current);
    if (!settled.ok) {
      return failure(settled.message);
    }
  }
  const ModelingCommandResult hover_cleared =
      target->session->selection().clear_hover();
  if (!hover_cleared.ok) {
    return failure(hover_cleared.message.empty()
                       ? "Could not clear scene hover state."
                       : hover_cleared.message);
  }
  const SceneSessionId previous_active = active_scene_id_;
  const std::uint64_t previous_activation_generation = activation_generation_;
  active_scene_id_ = id;
  ++activation_generation_;
  ProjectSessionResult persisted = persist_workspace();
  if (!persisted.ok) {
    active_scene_id_ = previous_active;
    activation_generation_ = previous_activation_generation;
    return persisted;
  }
  (void)standard_material_editor().accept_base_color_preview();
  return success(true, "Active scene changed.", id);
}

ProjectSessionResult ProjectSession::settle_scene_edits(SceneSessionId id) {
  OpenSceneSession *scene = find(id);
  if (scene == nullptr) return failure("Scene tab is closed or stale.");
  const CommandResult settled = settle_transient_state(*scene);
  return settled.ok ? success(settled.changed, settled.message, id)
                    : failure(settled.message);
}

ProjectSessionResult ProjectSession::preflight_save(SceneSessionId id) const {
  const OpenSceneSession *scene = find(id);
  if (scene == nullptr) {
    return failure("Scene tab is closed or stale.");
  }
  if (!scene->path.has_value()) {
    return failure("Unsaved scene requires Save As.");
  }
  if (!workspace_) {
    return failure("No project is open.");
  }
  ProjectIoResult<std::filesystem::path> absolute =
      resolve_authored_scene_save_path(workspace_->file_system, *scene->path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  return success(false, {}, id);
}

ProjectSessionResult ProjectSession::save(SceneSessionId id) {
  OpenSceneSession *scene = find(id);
  if (scene == nullptr) {
    return failure("Scene tab is closed or stale.");
  }
  if (!scene->path.has_value()) {
    return failure("Unsaved scene requires Save As.");
  }
  if (!workspace_) {
    return failure("No project is open.");
  }
  ProjectIoResult<std::filesystem::path> absolute =
      resolve_authored_scene_save_path(workspace_->file_system, *scene->path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  const CommandResult settled = settle_transient_state(*scene);
  if (!settled.ok) {
    return failure(settled.message);
  }
  const SceneContentRevisionStamp stamp = scene->session->content_revision_stamp();
  SceneWriteSnapshotResult snapshot = scene_write_snapshot(
      *scene->session, material_catalog_, workspace_->manifest);
  if (!snapshot.ok) {
    return failure(snapshot.message);
  }
  ProjectSessionResult success_result = success(true, "Scene saved.", id);
  success_result.warnings.reserve(1u);
  QdrProjectWriteResult written = write_scene_with_project_requirement(
      *workspace_, absolute.value(), snapshot.snapshot, scene->scene_id);
  if (!written.ok) {
    return failure(written.message.empty() ? "Scene save failed."
                                           : written.message);
  }
  if (scene->session->content_revision_stamp() == stamp) {
    scene->session->mark_clean();
  }
  scene->session->set_minimum_product_release(
      snapshot.snapshot.minimum_product_release);
  scene->identity_dirty = false;
  if (!written.message.empty()) {
    success_result.warnings.push_back(std::move(written.message));
  }
  append_bridge_notification_warning(
      success_result, ProjectBridgeNotifications::publish_scene(
                          workspace_->file_system, workspace_->manifest.id(),
                          scene->scene_id, *scene->path,
                          written.source_byte_count,
                          written.source_content_hash, snapshot.snapshot,
                          material_catalog_));
  remember_scene(id);
  return success_result;
}

ProjectSessionResult ProjectSession::save_as(SceneSessionId id,
                                             ProjectRelativePath path) {
  OpenSceneSession *scene = find(id);
  if (scene == nullptr) {
    return failure("Scene tab is closed or stale.");
  }
  if (!workspace_ || !path.valid() || !has_qdr_extension(path)) {
    return failure("Save As requires a project-relative .qdr path.");
  }
  if (is_project_autosave_path(path)) {
    return failure("Recovery storage is reserved for automatic scene copies. "
                   "Save the scene in the current project's Scenes folder or "
                   "one of its subfolders.");
  }
  ProjectIoResult<std::filesystem::path> absolute =
      resolve_authored_scene_save_path(workspace_->file_system, path);
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  SceneSessionId existing;
  if (path_is_open(path, &existing)) {
    return failure("Another open scene already uses that path.");
  }
  if (find_catalog_scene(path) != nullptr) {
    return failure("A project scene already uses that path.");
  }
  const CommandResult settled = settle_transient_state(*scene);
  if (!settled.ok) {
    return failure(settled.message);
  }

  SceneId new_scene_id = SceneId::generate();
  const SceneContentRevisionStamp stamp = scene->session->content_revision_stamp();
  SceneWriteSnapshotResult snapshot = scene_write_snapshot(
      *scene->session, material_catalog_, workspace_->manifest);
  if (!snapshot.ok) {
    return failure(snapshot.message);
  }
  ProjectManifest candidate_manifest = workspace_->manifest;
  ProjectManifest recovery_manifest = candidate_manifest;
  candidate_manifest.require_product_release(snapshot.snapshot.minimum_product_release);
  PersistedWorkspaceState previous_workspace_state =
      workspace_state_for(open_scenes_, active_scene_id_);
  ProjectIoResult<void> recovery_workspace_set = recovery_manifest.set_workspace(
      std::move(previous_workspace_state.open_paths),
      std::move(previous_workspace_state.active_path));
  if (!recovery_workspace_set.ok()) {
    return failure(recovery_workspace_set.error().message);
  }
  ProjectIoResult<void> registered = candidate_manifest.register_scene(
      {.id = new_scene_id, .path = path});
  if (!registered.ok()) {
    return failure(registered.error().message);
  }

  SceneId installed_scene_id = new_scene_id;
  std::optional<ProjectRelativePath> installed_path(path);
  std::string installed_display_name = display_name_for_path(path);
  ProjectSessionResult success_result =
      success(true, "Scene saved as a new project file.", id);
  success_result.warnings.reserve(4u);
  success_result.warnings.emplace_back(
      "Scene saved, but obsolete destination cleanup failed.");

  std::vector<ProjectRelativePath> open_paths;
  open_paths.reserve(open_scenes_.size());
  std::optional<ProjectRelativePath> active_path;
  for (const std::unique_ptr<OpenSceneSession> &open_scene : open_scenes_) {
    if (!open_scene) {
      continue;
    }
    const std::optional<ProjectRelativePath> prospective_path =
        open_scene->session_id == id
            ? std::optional<ProjectRelativePath>(path)
            : open_scene->path;
    if (!prospective_path.has_value()) {
      continue;
    }
    open_paths.push_back(*prospective_path);
    if (open_scene->session_id == active_scene_id_) {
      active_path = *prospective_path;
    }
  }
  ProjectIoResult<void> candidate_workspace_set =
      candidate_manifest.set_workspace(std::move(open_paths),
                                       std::move(active_path));
  if (!candidate_workspace_set.ok()) {
    return failure(candidate_workspace_set.error().message);
  }

  quader_io::QdrAtomicFileTransaction scene_file_transaction;
  const quader_io::QdrAtomicWriteResult prepared =
      scene_file_transaction.prepare(absolute.value());
  if (!prepared.ok) {
    return failure(prepared.message.empty()
                       ? "Scene save transaction could not be prepared."
                       : prepared.message);
  }
  QdrProjectWriteResult written =
      QdrDocumentWriter::write_project_document_atomic(
          scene_file_transaction.staging_path(), snapshot.snapshot,
          new_scene_id.bytes());
  if (!written.ok) {
    const quader_io::QdrAtomicWriteResult rolled_back =
        scene_file_transaction.rollback();
    std::string message =
        written.message.empty() ? "Scene save failed." : written.message;
    if (!rolled_back.ok) {
      message += "; rollback failed: " + rolled_back.message;
    }
    return failure(std::move(message));
  }
  if (!written.message.empty()) {
    success_result.warnings.push_back(std::move(written.message));
  }
  const bool mark_clean_after_publish =
      scene->session->content_revision_stamp() == stamp;

  const quader_io::QdrAtomicWriteResult published =
      scene_file_transaction.publish();
  if (!published.ok) {
    const quader_io::QdrAtomicWriteResult rolled_back =
        scene_file_transaction.rollback();
    std::string message = published.message.empty()
                              ? "Scene publish failed."
                              : published.message;
    if (!rolled_back.ok) {
      message += "; rollback failed: " + rolled_back.message;
    }
    return failure(std::move(message));
  }
  if (!published.message.empty()) {
    success_result.warnings.push_back(published.message);
  }

  std::string preserved_scene_diagnostic =
      "Published scene remains at '" +
      path_text_utf8(scene_file_transaction.target_path()) + "'.";
  if (!scene_file_transaction.backup_path().empty()) {
    preserved_scene_diagnostic += " Prior destination backup remains at '" +
                                  path_text_utf8(scene_file_transaction.backup_path()) +
                                  "'.";
  }

  ProjectSessionResult persisted = persist_prepared_manifest(
      *workspace_, std::move(candidate_manifest));
  if (!persisted.ok) {
    const quader_io::QdrAtomicWriteResult rolled_back =
        scene_file_transaction.rollback();
    if (!rolled_back.ok) {
      persisted.message += persisted.message.empty() ? "" : "; ";
      persisted.message += "scene rollback failed: " + rolled_back.message;
    }
    return persisted;
  }

  const auto preserve_candidate_for_recovery = [&]() noexcept {
    if (!scene_file_transaction.preserve_published_for_recovery()) {
      return false;
    }
    scene->scene_id = std::move(installed_scene_id);
    scene->path = std::move(installed_path);
    scene->display_name = std::move(installed_display_name);
    scene->identity_dirty = true;
    return true;
  };

  const auto attach_recovery_warnings =
      [&](ProjectSessionResult &result,
          ProjectSessionResult *manifest_restored = nullptr) {
    result.warnings = std::move(persisted.warnings);
    if (manifest_restored != nullptr) {
      for (std::string &warning : manifest_restored->warnings) {
        result.warnings.push_back(std::move(warning));
      }
    }
  };

  const auto recover_after_persisted_reference =
      [&](std::string_view failure_prefix,
          std::string_view failure_detail) -> ProjectSessionResult {
    ProjectSessionResult manifest_restored;
    try {
      manifest_restored = persist_prepared_manifest(
          *workspace_, std::move(recovery_manifest));
    } catch (const std::exception &exception) {
      const bool preserved = preserve_candidate_for_recovery();
      std::string message(failure_prefix);
      if (!failure_detail.empty()) {
        message += ": ";
        message += failure_detail;
      }
      message += "; previous manifest restoration threw: ";
      message += exception.what();
      message += "; " + preserved_scene_diagnostic;
      if (!preserved) {
        message += "; published transaction preservation failed.";
      }
      ProjectSessionResult result = failure(std::move(message));
      attach_recovery_warnings(result);
      return result;
    } catch (...) {
      const bool preserved = preserve_candidate_for_recovery();
      std::string message(failure_prefix);
      if (!failure_detail.empty()) {
        message += ": ";
        message += failure_detail;
      }
      message += "; previous manifest restoration threw an unknown exception; " +
                 preserved_scene_diagnostic;
      if (!preserved) {
        message += "; published transaction preservation failed.";
      }
      ProjectSessionResult result = failure(std::move(message));
      attach_recovery_warnings(result);
      return result;
    }

    if (!manifest_restored.ok) {
      const bool preserved = preserve_candidate_for_recovery();
      std::string message(failure_prefix);
      if (!failure_detail.empty()) {
        message += ": ";
        message += failure_detail;
      }
      message += "; previous manifest restoration failed: " +
                 manifest_restored.message + "; " +
                 preserved_scene_diagnostic;
      if (!preserved) {
        message += "; published transaction preservation failed.";
      }
      ProjectSessionResult result = failure(std::move(message));
      attach_recovery_warnings(result, &manifest_restored);
      return result;
    }

    quader_io::QdrAtomicWriteResult rolled_back;
    try {
      rolled_back = scene_file_transaction.rollback();
    } catch (const std::exception &exception) {
      std::string message(failure_prefix);
      if (!failure_detail.empty()) {
        message += ": ";
        message += failure_detail;
      }
      message += "; scene rollback threw: ";
      message += exception.what();
      ProjectSessionResult result = failure(std::move(message));
      attach_recovery_warnings(result, &manifest_restored);
      return result;
    } catch (...) {
      std::string message(failure_prefix);
      if (!failure_detail.empty()) {
        message += ": ";
        message += failure_detail;
      }
      message += "; scene rollback threw an unknown exception.";
      ProjectSessionResult result = failure(std::move(message));
      attach_recovery_warnings(result, &manifest_restored);
      return result;
    }

    std::string message(failure_prefix);
    if (!failure_detail.empty()) {
      message += ": ";
      message += failure_detail;
    }
    if (!rolled_back.ok) {
      message += "; scene rollback failed: " + rolled_back.message;
    }
    ProjectSessionResult result = failure(std::move(message));
    attach_recovery_warnings(result, &manifest_restored);
    return result;
  };

  quader_io::QdrAtomicWriteResult verified;
  try {
    verified = scene_file_transaction.verify_published();
  } catch (const std::exception &exception) {
    return recover_after_persisted_reference(
        "Scene verification threw after manifest persistence",
        exception.what());
  } catch (...) {
    return recover_after_persisted_reference(
        "Scene verification threw after manifest persistence",
        "unknown exception");
  }
  if (!verified.ok) {
    return recover_after_persisted_reference(
        "Scene verification failed after manifest persistence",
        verified.message);
  }

  quader_io::QdrAtomicWriteResult committed;
  try {
    committed = scene_file_transaction.commit();
  } catch (const std::exception &exception) {
    return recover_after_persisted_reference(
        "Scene commit threw after manifest persistence", exception.what());
  } catch (...) {
    return recover_after_persisted_reference(
        "Scene commit threw after manifest persistence", "unknown exception");
  }
  if (!committed.ok) {
    return recover_after_persisted_reference(
        "Scene commit verification failed", committed.message);
  }
  if (committed.message.empty()) {
    success_result.warnings.erase(success_result.warnings.begin());
  } else {
    success_result.warnings.front() = std::move(committed.message);
  }
  for (std::string &warning : persisted.warnings) {
    success_result.warnings.push_back(std::move(warning));
  }

  scene->scene_id = std::move(installed_scene_id);
  scene->path = std::move(installed_path);
  scene->display_name = std::move(installed_display_name);
  if (mark_clean_after_publish) {
    scene->session->mark_clean_at(stamp);
  }
  scene->session->set_minimum_product_release(
      snapshot.snapshot.minimum_product_release);
  scene->identity_dirty = false;
  append_bridge_notification_warning(
      success_result, ProjectBridgeNotifications::publish_scene(
                          workspace_->file_system, workspace_->manifest.id(),
                          scene->scene_id, *scene->path,
                          written.source_byte_count,
                          written.source_content_hash, snapshot.snapshot,
                          material_catalog_));
  remember_scene(id);
  return success_result;
}

ProjectSessionResult ProjectSession::save_all() {
  if (!workspace_) {
    return failure("No project is open.");
  }
  bool changed = false;
  std::vector<std::string> warnings;
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (!scene || (!scene->dirty() && !has_pending_scene_edits(*scene))) {
      continue;
    }
    if (!scene->path.has_value()) {
      return failure("Save All requires paths for every dirty scene.");
    }
    ProjectSessionResult preflight = preflight_save(scene->session_id);
    if (!preflight.ok) {
      return preflight;
    }
  }
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (!scene || (!scene->dirty() && !has_pending_scene_edits(*scene))) {
      continue;
    }
    ProjectSessionResult saved = save(scene->session_id);
    if (!saved.ok) {
      saved.warnings.insert(saved.warnings.begin(), warnings.begin(),
                            warnings.end());
      for (const std::string &warning : warnings) {
        saved.message += " " + warning;
      }
      return saved;
    }
    warnings.insert(warnings.end(), saved.warnings.begin(),
                     saved.warnings.end());
    changed = true;
  }
  ProjectSessionResult result =
      success(changed, changed ? "All dirty scenes saved."
                               : "No dirty scenes to save.");
  result.warnings = std::move(warnings);
  for (const std::string &warning : result.warnings) {
    result.message += " " + warning;
  }
  return result;
}

ProjectSessionResult ProjectSession::close_scene(SceneSessionId id,
                                                 bool discard_dirty) {
  const auto found = std::find_if(
      open_scenes_.begin(), open_scenes_.end(),
      [&](const std::unique_ptr<OpenSceneSession> &scene) {
        return scene && scene->session_id == id;
      });
  if (found == open_scenes_.end()) {
    return failure("Scene tab is closed or stale.");
  }
  if ((*found)->dirty() && !discard_dirty) {
    return failure("Scene contains unsaved changes.");
  }
  const CommandResult settled = settle_transient_state(**found, discard_dirty);
  if (!settled.ok) {
    return failure(settled.message);
  }
  if ((*found)->dirty() && !discard_dirty) {
    return failure("Scene contains unsaved changes.");
  }
  const std::size_t index =
      static_cast<std::size_t>(std::distance(open_scenes_.begin(), found));
  const bool was_active = active_scene_id_ == id;
  SceneSessionId next_active;
  if (was_active && open_scenes_.size() > 1u) {
    const std::size_t next_index =
        index + 1u < open_scenes_.size() ? index + 1u : index - 1u;
    OpenSceneSession *target = open_scenes_[next_index].get();
    const ModelingCommandResult hover_cleared =
        target->session->selection().clear_hover();
    if (!hover_cleared.ok) {
      return failure(hover_cleared.message.empty()
                         ? "Could not clear scene hover state."
                         : hover_cleared.message);
    }
    next_active = target->session_id;
  }

  const SceneSessionId previous_active = active_scene_id_;
  const std::uint64_t previous_activation_generation = activation_generation_;
  std::unique_ptr<OpenSceneSession> removed = std::move(*found);
  open_scenes_.erase(found);
  if (was_active) {
    active_scene_id_ = next_active;
    ++activation_generation_;
  }
  ProjectSessionResult persisted = persist_workspace();
  if (!persisted.ok) {
    open_scenes_.insert(open_scenes_.begin() +
                            static_cast<std::ptrdiff_t>(index),
                        std::move(removed));
    active_scene_id_ = previous_active;
    activation_generation_ = previous_activation_generation;
    return persisted;
  }
  if (was_active) {
    (void)standard_material_editor().accept_base_color_preview();
  }
  release_session_id(id);
  return success(true, "Scene closed.", active_scene_id_.valid()
                                               ? std::optional(active_scene_id_)
                                               : std::nullopt);
}

ProjectSessionResult ProjectSession::install_material_catalog(
    ProjectMaterialCatalog material_catalog) {
  if (!workspace_) {
    return failure("Open or create a project before updating materials.");
  }
  const std::optional<ProjectAssetId> default_material =
      default_project_material_id();
  if (!default_material.has_value() ||
      material_catalog.find(*default_material) == nullptr) {
    return failure("Built-in default project material is unavailable.");
  }
  if (workspace_->manifest.minimum_product_release() < current_product_release()) {
    auto manifest = workspace_->manifest;
    manifest.require_product_release(current_product_release());
    auto persisted = persist_prepared_manifest(*workspace_, std::move(manifest));
    if (!persisted.ok) {
      return persisted;
    }
  }
  material_catalog_ = std::move(material_catalog);
  invalidate_material_textures();
  return success(true, "Project material catalog updated.");
}

void ProjectSession::set_resource_root(
    std::filesystem::path resource_root) {
  if (!resource_root.empty()) {
    resource_root_ = std::move(resource_root);
  }
}

bool ProjectSession::has_dirty_scenes() const {
  return std::any_of(open_scenes_.begin(), open_scenes_.end(),
                     [](const std::unique_ptr<OpenSceneSession> &scene) {
                       return scene && scene->dirty();
                     });
}

void ProjectSession::set_history_max_steps(std::size_t max_steps) {
  fallback_scene_.set_history_max_steps(max_steps);
  history_max_steps_ = fallback_scene_.history().max_steps();
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (scene && scene->session) {
      scene->session->set_history_max_steps(history_max_steps_);
    }
  }
}

bool ProjectSession::has_dirty_material() const {
  const StandardMaterialEditSnapshot *draft =
      standard_material_editor_ ? standard_material_editor_->current()
                                : nullptr;
  return draft != nullptr && draft->dirty;
}

StandardMaterialEditSessionService &
ProjectSession::standard_material_editor() {
  return *standard_material_editor_;
}

const StandardMaterialEditSessionService &
ProjectSession::standard_material_editor() const {
  return *standard_material_editor_;
}

ProjectClipboardService &ProjectSession::clipboard() { return *clipboard_; }

const ProjectClipboardService &ProjectSession::clipboard() const {
  return *clipboard_;
}

const std::filesystem::path &ProjectSession::root() const noexcept {
  return workspace_ ? workspace_->file_system.root() : empty_path_;
}

const std::filesystem::path &ProjectSession::manifest_path() const noexcept {
  return workspace_ ? workspace_->manifest_path : empty_path_;
}

ProjectAssetId ProjectSession::canonical_material_id(
    ProjectAssetId material_id) const {
  return workspace_
             ? ProjectMaterialReferenceResolver::canonical_material_id(
                   workspace_->manifest, material_id)
             : material_id;
}

ProjectIoResult<ProjectMaterialPreview>
ProjectSession::material_preview(ProjectAssetId material_id) const {
  if (!workspace_) {
    return ProjectIoResult<ProjectMaterialPreview>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "No project is open.");
  }
  material_id = canonical_material_id(material_id);
  return material_catalog_.read_preview(workspace_->file_system, material_id);
}

ProjectIoResult<ProjectMaterialTextureData>
ProjectSession::material_texture(ProjectAssetId material_id,
                                 StandardMaterialTextureSlot slot) const {
  if (!workspace_.has_value()) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "No project is open.");
  }
  material_id = canonical_material_id(material_id);
  return material_catalog_.read_texture(workspace_->file_system, material_id,
                                      slot);
}

void ProjectSession::invalidate_material_textures() {
  material_usage_->invalidate();
  material_texture_images_.clear();
  ++material_texture_revision_;
}

std::shared_ptr<const ProjectMaterialTextureSnapshot>
ProjectSession::material_texture_image(
    ProjectAssetId material_id, StandardMaterialTextureSlot slot) const {
  if (!workspace_) {
    return {};
  }
  material_id = canonical_material_id(material_id);
  const ProjectMaterialCatalogEntry *entry = material_catalog_.find(material_id);
  const ProjectStandardMaterialDefinition *standard =
      entry != nullptr ? entry->material.standard_material() : nullptr;
  const auto *definition = standard != nullptr ? standard->texture(slot) : nullptr;
  if (definition == nullptr || !definition->has_value()) {
    return {};
  }
  const ProjectAssetId texture_id = definition->value().id;
  if (const auto cached = material_texture_images_.find(texture_id);
      cached != material_texture_images_.end()) {
    if (cached->second != nullptr &&
        (cached->second->color_space != definition->value().color_space ||
         cached->second->project_relative_path !=
             definition->value().project_relative_path)) {
      return {};
    }
    return cached->second;
  }

  std::shared_ptr<const ProjectMaterialTextureSnapshot> result;
  auto texture = material_texture(material_id, slot);
  if (texture.ok()) {
    auto decoded = ProjectImageDecoder::decode(texture.value().encoded_bytes);
    if (decoded.ok()) {
      auto texture_data = texture.take_value();
      auto image_data = decoded.take_value();
      result = std::make_shared<const ProjectMaterialTextureSnapshot>(
          ProjectMaterialTextureSnapshot{
              .texture_id = texture_data.id,
              .color_space = texture_data.color_space,
              .project_relative_path =
                  definition->value().project_relative_path,
              .content_revision = texture_data.content_revision,
              .encoded = std::make_shared<const std::vector<std::byte>>(
                  std::move(texture_data.encoded_bytes)),
              .decoded = std::make_shared<const std::vector<std::byte>>(
                  std::move(image_data.rgba8)),
              .width = image_data.width,
              .height = image_data.height,
              .detected_format = std::move(image_data.detected_format),
              .encoded_byte_size = image_data.encoded_byte_size,
              .has_alpha_channel = image_data.has_alpha_channel,
          });
    }
  }
  // Cache failures too: a missing or invalid image must not trigger repeated IO
  // while a selected face is presented. Catalog replacement retries resolution.
  material_texture_images_.emplace(texture_id, result);
  return result;
}

ProjectIoResult<ProjectMaterialAlbedoTextureInfo>
ProjectSession::material_albedo_texture_info(ProjectAssetId material_id) const {
  if (!workspace_) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState, "No project is open.");
  }
  material_id = canonical_material_id(material_id);
  const ProjectMaterialCatalogEntry *entry = material_catalog_.find(material_id);
  if (entry == nullptr) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::AssetNotFound,
        "Project material is not present in the catalog.");
  }
  const ProjectStandardMaterialDefinition *standard =
      entry->material.standard_material();
  if (standard == nullptr) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "Project material is not a Standard Material.",
        entry->path.native_path());
  }
  if (!standard->base_color_texture.has_value()) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::AssetNotFound,
        "Project Standard Material has no albedo texture.",
        entry->path.native_path());
  }
  const ProjectMaterialTextureSlot &definition =
      *standard->base_color_texture;
  ProjectIoResult<ProjectRelativePath> relative =
      ProjectRelativePath::parse_portable(definition.project_relative_path);
  if (!relative.ok()) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        relative.error().code, relative.error().message,
        relative.error().path);
  }
  ProjectIoResult<std::filesystem::path> absolute =
      workspace_->file_system.resolve_existing_file(relative.value());
  if (!absolute.ok()) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        absolute.error().code, absolute.error().message,
        absolute.error().path);
  }
  std::shared_ptr<const ProjectMaterialTextureSnapshot> texture =
      material_texture_image(material_id, StandardMaterialTextureSlot::Albedo);
  if (texture == nullptr) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Project material albedo texture could not be decoded.",
        absolute.value());
  }
  if (texture->texture_id != definition.id ||
      texture->color_space != definition.color_space ||
      texture->project_relative_path != definition.project_relative_path) {
    return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::failure(
        ProjectIoErrorCode::DuplicateAsset,
        "Project texture UUID resolves to conflicting texture metadata.",
        absolute.value());
  }
  return ProjectIoResult<ProjectMaterialAlbedoTextureInfo>::success({
      .material_id = material_id,
      .project_relative_path = relative.take_value(),
      .absolute_path = absolute.take_value(),
      .texture = std::move(texture),
  });
}

OpenSceneSession *ProjectSession::find(SceneSessionId id) {
  const auto found = std::find_if(
      open_scenes_.begin(), open_scenes_.end(),
      [&](const std::unique_ptr<OpenSceneSession> &scene) {
        return scene && scene->session_id == id;
      });
  return found == open_scenes_.end() ? nullptr : found->get();
}

const OpenSceneSession *ProjectSession::find(SceneSessionId id) const {
  const auto found = std::find_if(
      open_scenes_.begin(), open_scenes_.end(),
      [&](const std::unique_ptr<OpenSceneSession> &scene) {
        return scene && scene->session_id == id;
      });
  return found == open_scenes_.end() ? nullptr : found->get();
}

OpenSceneSession *ProjectSession::active_scene() {
  return find(active_scene_id_);
}

const OpenSceneSession *ProjectSession::active_scene() const {
  return find(active_scene_id_);
}

SceneSessionId ProjectSession::allocate_session_id() {
  std::uint32_t slot = 0u;
  if (!free_slots_.empty()) {
    slot = free_slots_.back();
    free_slots_.pop_back();
    free_slot_membership_.erase(slot);
    std::uint32_t &generation = slot_generations_[slot];
    ++generation;
    if (generation == 0u) {
      ++generation;
    }
  } else {
    slot = static_cast<std::uint32_t>(slot_generations_.size());
    slot_generations_.push_back(1u);
  }
  return SceneSessionId::from_parts(slot, slot_generations_[slot]);
}

void ProjectSession::release_session_id(SceneSessionId id) {
  if (id.valid() && id.slot() < slot_generations_.size() &&
      slot_generations_[id.slot()] == id.generation() &&
      free_slot_membership_.insert(id.slot()).second) {
    free_slots_.push_back(id.slot());
  }
}

void ProjectSession::release_open_scene_ids() {
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (scene) {
      release_session_id(scene->session_id);
    }
  }
  open_scenes_.clear();
  active_scene_id_ = {};
}

std::unique_ptr<OpenSceneSession> ProjectSession::make_scene(
    SceneId scene_id, std::string display_name,
    std::optional<ProjectRelativePath> path, SceneContentDocument document,
    std::optional<ViewportCameraPose> viewport_camera_pose,
    ProductRelease minimum_product_release) {
  auto session = std::make_unique<SceneSession>(path_array_defaults_,
                                                 std::move(document),
                                                 extrude_defaults_, tool_defaults_);
  if (const auto material = default_project_material_id()) {
    session->set_creation_material(MaterialReference::project(*material));
  }
  session->set_minimum_product_release(minimum_product_release);
  session->set_history_max_steps(history_max_steps_);
  (void)session->restore_persisted_viewport_camera_pose(
      viewport_camera_pose);
  session->mark_clean();
  const SceneSessionId id = allocate_session_id();
  const auto content_stamp = session->content_revision_stamp();
  session->set_content_settled_observer(
      [this, id](bool committed) { remember_edited_scene(id, committed); });
  return std::make_unique<OpenSceneSession>(OpenSceneSession{
      .session_id = id,
      .scene_id = std::move(scene_id),
      .display_name = std::move(display_name),
      .path = std::move(path),
      .session = std::move(session),
      .recent_content_stamp = content_stamp,
  });
}

std::vector<RecentSceneEntry> ProjectSession::recent_scenes() const {
  auto loaded = recent_scene_store_.load();
  return loaded.ok() ? loaded.take_value() : std::vector<RecentSceneEntry>{};
}

void ProjectSession::remember_edited_scene(SceneSessionId id, bool committed) {
  OpenSceneSession *scene = find(id);
  if (scene == nullptr || scene->session == nullptr) {
    return;
  }
  const auto stamp = scene->session->content_revision_stamp();
  if (stamp == scene->recent_content_stamp) {
    return;
  }
  scene->recent_content_stamp = stamp;
  if (committed) {
    remember_scene(id);
  }
}

void ProjectSession::remember_scene(SceneSessionId id) noexcept {
  // MRU bookkeeping must never turn an already committed edit/save into failure.
  try {
    OpenSceneSession *scene = find(id);
    if (!workspace_ || scene == nullptr || !scene->path.has_value() ||
        is_project_autosave_path(*scene->path)) {
      return;
    }
    scene->recent_content_stamp = scene->session->content_revision_stamp();
    if (recent_front_scene_ == id && recent_front_path_ == scene->path) {
      return;
    }
    const auto remembered = recent_scene_store_.remember(
        workspace_->manifest, workspace_->manifest_path, scene->scene_id,
        scene->display_name, *scene->path);
    if (remembered.ok()) {
      recent_front_scene_ = id;
      recent_front_path_ = scene->path;
    }
  } catch (...) {
    // The saved scene remains usable even when the optional recent-list fails.
  }
}

ProjectSessionResult ProjectSession::install_scene_and_activate(
    std::unique_ptr<OpenSceneSession> scene, ProjectManifest manifest) {
  const SceneSessionId id = scene->session_id;
  ProjectManifest previous_manifest = workspace_->manifest;
  open_scenes_.push_back(std::move(scene));
  const auto reject_candidate = [this]() {
    std::unique_ptr<OpenSceneSession> rejected =
        std::move(open_scenes_.back());
    open_scenes_.pop_back();
    release_session_id(rejected->session_id);
  };

  // Keep the outgoing scene bound while candidate persistence can still fail.
  // A failed create/open must not discard its authored tool draft or SDK preview.
  PersistedWorkspaceState state = workspace_state_for(open_scenes_, id);
  ProjectSessionResult persisted = persist_manifest(
      *workspace_, std::move(manifest), std::move(state.open_paths),
      std::move(state.active_path));
  if (!persisted.ok) {
    reject_candidate();
    return persisted;
  }

  const CommandResult settled = active_scene() != nullptr
      ? settle_transient_state(*active_scene()) : command_unchanged_result();
  if (!settled.ok) {
    reject_candidate();
    ProjectSessionResult restored =
        persist_prepared_manifest(*workspace_, previous_manifest);
    ProjectSessionResult result =
        failure(settled.message);
    if (!restored.ok) {
      // The old tab remains authoritative even when a second I/O failure stops
      // its manifest rollback. Report the durable mismatch rather than retaining
      // a failed candidate in the live workspace.
      workspace_->manifest = std::move(previous_manifest);
      result.message += " Could not restore the workspace manifest: " +
                        restored.message;
    }
    return result;
  }

  (void)standard_material_editor().accept_base_color_preview();
  active_scene_id_ = id;
  ++activation_generation_;
  ProjectSessionResult result = success(true, "Scene activated.", id);
  result.warnings = std::move(persisted.warnings);
  return result;
}

ProjectSessionResult ProjectSession::install_workspace(
    ProjectWorkspaceStorage workspace,
    ProjectMaterialCatalog material_catalog,
    std::vector<std::unique_ptr<OpenSceneSession>> scenes,
    std::optional<ProjectRelativePath> requested_active,
    std::vector<std::string> warnings,
    const std::function<bool()> &prepare_replacement,
    std::optional<ProjectManifest> original_manifest,
    bool discard_existing_dirty) {
  SceneSessionId candidate_active;
  if (requested_active.has_value()) {
    for (const std::unique_ptr<OpenSceneSession> &scene : scenes) {
      if (scene && scene->path == requested_active) {
        candidate_active = scene->session_id;
        break;
      }
    }
  }
  if (!candidate_active.valid() && !scenes.empty()) {
    candidate_active = scenes.front()->session_id;
  }

  PersistedWorkspaceState state =
      workspace_state_for(scenes, candidate_active);
  ProjectSessionResult persisted =
      persist_manifest(workspace, workspace.manifest,
                       std::move(state.open_paths), std::move(state.active_path));
  if (!persisted.ok) {
    for (const std::unique_ptr<OpenSceneSession> &scene : scenes) {
      if (scene) {
        release_session_id(scene->session_id);
      }
    }
    return persisted;
  }

  const auto abandon_workspace = [&](ProjectSessionResult result) {
    for (const auto &scene : scenes) {
      release_session_id(scene->session_id);
    }
    if (original_manifest.has_value()) {
      const auto restored = ProjectManifestStore::save_atomic(
          workspace.file_system, workspace.manifest_path, *original_manifest);
      if (!restored.ok()) {
        result.ok = false;
        result.message += " Target workspace restoration failed: " +
                          restored.error().message;
      }
    }
    return result;
  };
  // Resolve candidate I/O and any requested save destinations before settling.
  // The host includes pending edits in its dirty prompts without accepting them.
  if (prepare_replacement && !prepare_replacement()) {
    return abandon_workspace(success(false, "Open Recent canceled."));
  }
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    const CommandResult settled = scene
        ? settle_transient_state(*scene, discard_existing_dirty ||
                                          static_cast<bool>(prepare_replacement))
        : command_unchanged_result();
    if (!settled.ok) return abandon_workspace(failure(settled.message));
  }
  if (!discard_existing_dirty) {
    const auto material = standard_material_editor().accept_base_color_preview();
    if (!material.ok) return abandon_workspace(failure(material.message));
    if (!prepare_replacement && (has_dirty_scenes() || has_dirty_material())) {
      return abandon_workspace(failure("Project contains unsaved document changes."));
    }
  }
  release_open_scene_ids();
  (void)standard_material_editor_->close(true);
  clipboard_->clear();
  workspace_ = std::move(workspace);
  material_catalog_ = std::move(material_catalog);
  invalidate_material_textures();
  open_scenes_ = std::move(scenes);
  active_scene_id_ = candidate_active;
  ++activation_generation_;
  ProjectSessionResult result = success(true, "Quader project opened.",
                                        active_scene_id_.valid()
                                            ? std::optional(active_scene_id_)
                                            : std::nullopt);
  result.warnings = std::move(warnings);
  return result;
}

ProjectSessionResult ProjectSession::persist_workspace() {
  if (!workspace_) {
    return success(false, {});
  }
  PersistedWorkspaceState state =
      workspace_state_for(open_scenes_, active_scene_id_);
  return persist_manifest(*workspace_, workspace_->manifest,
                          std::move(state.open_paths),
                          std::move(state.active_path));
}

ProjectSessionResult ProjectSession::persist_manifest(
    ProjectWorkspaceStorage &workspace, ProjectManifest manifest,
    std::vector<ProjectRelativePath> open_paths,
    std::optional<ProjectRelativePath> active_path) {
  ProjectIoResult<void> updated = manifest.set_workspace(
      std::move(open_paths), std::move(active_path));
  if (!updated.ok()) {
    return failure(updated.error().message);
  }
  return persist_prepared_manifest(workspace, std::move(manifest));
}

ProjectSessionResult ProjectSession::persist_prepared_manifest(
    ProjectWorkspaceStorage &workspace, ProjectManifest manifest) {
  ProjectSessionResult result = success(false, {});
  result.warnings.reserve(1u);
  ProjectIoResult<void> saved = ProjectManifestStore::save_atomic(
      workspace.file_system, workspace.manifest_path, manifest);
  if (!saved.ok()) {
    return failure(saved.error().message);
  }
  workspace.manifest = std::move(manifest);
  if (!saved.warning().empty()) {
    result.warnings.push_back(saved.take_warning());
  }
  return result;
}

CommandResult ProjectSession::settle_transient_state(
    OpenSceneSession &scene, bool discard) {
  if (!scene.session) {
    return command_unchanged_result();
  }
  const CommandResult property_edit =
      discard ? scene.session->document_commands().cancel_active_property_edit()
              : scene.session->document_commands().accept_active_property_edit();
  if (!property_edit.ok) {
    return property_edit;
  }
  CommandResult native = discard
      ? scene.session->native_tools().cancel_transient_state()
      : scene.session->native_tools().settle_transient_state();
  native.changed = native.changed || property_edit.changed;
  return native;
}

bool ProjectSession::path_is_open(const ProjectRelativePath &path,
                                  SceneSessionId *id) const {
  const std::wstring key = path.portable_case_key();
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (!scene || !scene->path.has_value()) {
      continue;
    }
    bool matches = scene->path->portable_case_key() == key;
    if (!matches && workspace_) {
      const ProjectIoResult<bool> same_location =
          workspace_->file_system.refers_to_same_location(path, *scene->path);
      matches = same_location.ok() && same_location.value();
    }
    if (matches) {
      if (id != nullptr) {
        *id = scene->session_id;
      }
      return true;
    }
  }
  return false;
}

const ProjectSceneCatalogEntry *
ProjectSession::find_catalog_scene(const ProjectRelativePath &path) const {
  if (!workspace_) {
    return nullptr;
  }
  if (const ProjectSceneCatalogEntry *exact =
          workspace_->manifest.find_scene(path)) {
    return exact;
  }
  for (const ProjectSceneCatalogEntry &scene : workspace_->manifest.scenes()) {
    const ProjectIoResult<bool> same_location =
        workspace_->file_system.refers_to_same_location(path, scene.path);
    if (same_location.ok() && same_location.value()) {
      return &scene;
    }
  }
  return nullptr;
}

bool ProjectSession::scene_id_is_open(
    const SceneId &scene_id, const ProjectRelativePath *except_path) const {
  for (const std::unique_ptr<OpenSceneSession> &scene : open_scenes_) {
    if (!scene || scene->scene_id != scene_id) {
      continue;
    }
    if (except_path != nullptr && scene->path.has_value()) {
      bool matches =
          scene->path->portable_case_key() == except_path->portable_case_key();
      if (!matches && workspace_) {
        const ProjectIoResult<bool> same_location =
            workspace_->file_system.refers_to_same_location(*except_path,
                                                             *scene->path);
        matches = same_location.ok() && same_location.value();
      }
      if (matches) {
        continue;
      }
    }
    return true;
  }
  return false;
}

std::string ProjectSession::unique_untitled_name() const {
  constexpr std::string_view kBase = "Untitled Scene";
  const auto used = [&](std::string_view name) {
    return std::any_of(open_scenes_.begin(), open_scenes_.end(),
                       [&](const std::unique_ptr<OpenSceneSession> &scene) {
                         return scene && scene->display_name == name;
                       });
  };
  if (!used(kBase)) {
    return std::string(kBase);
  }
  for (std::uint32_t suffix = 2u;
       suffix != std::numeric_limits<std::uint32_t>::max(); ++suffix) {
    std::string candidate =
        std::string(kBase) + " " + std::to_string(suffix);
    if (!used(candidate)) {
      return candidate;
    }
  }
  return std::string(kBase);
}

} // namespace quader
