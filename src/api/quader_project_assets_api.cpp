////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_project_assets_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_events_api.hpp"
#include "api/quader_events_api_internal_access.hpp"
#include "editor/material/material_presentation_service.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/material/project_material_import_service.hpp"
#include "editor/material/project_material_organization_service.hpp"
#include "editor/material/project_material_deletion_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <utility>

namespace quader {
namespace {

QuaderProjectMaterialKind to_public_kind(ProjectMaterialKind kind) {
  switch (kind) {
  case ProjectMaterialKind::Standard:
    return QuaderProjectMaterialKind::Standard;
  case ProjectMaterialKind::Graph:
    return QuaderProjectMaterialKind::Graph;
  }
  return QuaderProjectMaterialKind::Standard;
}

QuaderApiVoidResult status(QuaderApiSession session, bool ok, bool changed,
                           std::string message = {}) {
  return quader_api_void_status(
      QuaderApiSessionInternalAccess::scene_session(session), ok, changed,
      std::move(message),
      changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None);
}

QuaderProjectMaterialInfo
to_public_info(const ProjectMaterialCatalogEntry &entry) {
  const ProjectStandardMaterialDefinition *standard =
      entry.material.standard_material();
  return {
      .id = entry.material.id,
      .name = entry.material.name,
      .kind = to_public_kind(entry.material.kind),
      .project_relative_path = entry.path.portable_string(),
      .qmat_size_bytes = entry.file_size,
      .base_color = standard != nullptr
                        ? standard->base_color
                        : std::array<float, 3>{1.0f, 1.0f, 1.0f},
      .content_revision = entry.content_revision,
  };
}

QuaderMaterialPresentationKind
to_public_kind(MaterialPresentationKind kind) {
  switch (kind) {
  case MaterialPresentationKind::Embedded:
    return QuaderMaterialPresentationKind::Embedded;
  case MaterialPresentationKind::Standard:
    return QuaderMaterialPresentationKind::Standard;
  case MaterialPresentationKind::Graph:
    return QuaderMaterialPresentationKind::Graph;
  }
  return QuaderMaterialPresentationKind::Embedded;
}

QuaderMaterialReference to_public_reference(
    const MaterialReference &reference) {
  return reference.project_owned()
             ? QuaderMaterialReference::project(reference.project_asset())
             : QuaderMaterialReference::embedded(reference.embedded_asset());
}

QuaderMaterialPresentationRevision to_public_revision(
    const MaterialPresentationRevision &revision) {
  return {
      .project_activation_generation =
          revision.project_activation_generation,
      .project_material_revision = revision.project_material_revision,
      .active_scene_id = revision.active_scene_id,
      .embedded_material_revision = revision.embedded_material_revision,
  };
}

QuaderMaterialPresentationSnapshot to_public_snapshot(
    MaterialPresentationSnapshot snapshot) {
  QuaderMaterialPresentationSnapshot result{
      .revision = to_public_revision(snapshot.revision),
  };
  result.materials.reserve(snapshot.materials.size());
  for (MaterialPresentationItem &item : snapshot.materials) {
    result.materials.push_back(QuaderMaterialPresentationItem{
        .reference = to_public_reference(item.reference),
        .kind = to_public_kind(item.kind),
        .name = std::move(item.name),
        .project_relative_path = std::move(item.project_relative_path),
        .qmat_size_bytes = item.qmat_size_bytes,
        .encoded_albedo = std::move(item.encoded_albedo),
        .fallback_color = item.fallback_color,
        .content_revision = item.content_revision,
        .available = item.available,
        .editable = item.editable,
    });
  }
  return result;
}

QuaderProjectSceneMaterialDependencies to_public_scene(
    const ProjectSceneAssetDependencies &scene) {
  return {
      .scene_id = scene.scene_id,
      .session_id = scene.session_id,
      .project_relative_path =
          scene.path ? scene.path->portable_string() : std::string{},
      .name = scene.display_name,
      .live = scene.live,
      .dirty = scene.dirty,
      .material_ids = scene.material_ids,
  };
}

std::vector<QuaderProjectSceneMaterialDependencies> to_public_scenes(
    const std::vector<ProjectSceneAssetDependencies> &scenes) {
  std::vector<QuaderProjectSceneMaterialDependencies> result;
  result.reserve(scenes.size());
  std::ranges::transform(scenes, std::back_inserter(result), to_public_scene);
  return result;
}

void publish_material_deletion_events(
    QuaderEventsApi *events, ProjectSession &project,
    const std::optional<StandardMaterialEditSnapshot> &previous_draft) {
  if (events == nullptr) {
    return;
  }
  std::vector<QuaderEvent> notifications;
  const auto root_utf8 = project.root().u8string();
  const auto make_event = [&](std::string_view name) {
    return QuaderEvent{
        .name = std::string(name),
        .project_id = project.manifest()->id(),
        .project_name = project.manifest()->name(),
        .project_root = std::string(root_utf8.begin(), root_utf8.end()),
    };
  };
  notifications.push_back(make_event(quader_event::kProjectAssetsChanged));
  if (previous_draft &&
      !project.standard_material_editor().has_document()) {
    auto event = make_event(quader_event::kStandardMaterialClosed);
    event.project_asset_id = previous_draft->document.id;
    event.material_edit_session_id = previous_draft->session_id;
    event.material_name = previous_draft->document.name;
    if (previous_draft->path) {
      event.material_path = previous_draft->path->portable_string();
    }
    event.material_dirty = previous_draft->dirty;
    notifications.push_back(std::move(event));
  }
  for (QuaderEvent &event : notifications) {
    (void)QuaderEventsApiInternalAccess::publish(*events, std::move(event));
  }
}

} // namespace

QuaderProjectAssetsApi::QuaderProjectAssetsApi(QuaderApiSession session)
    : session_(session) {}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
QuaderProjectAssetsApi::materials() const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<
        std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false,
               "Project materials are unavailable without an open project."));
  }
  std::vector<QuaderProjectMaterialInfo> result;
  result.reserve(project->material_catalog().entries().size());
  for (const ProjectMaterialCatalogEntry &entry :
       project->material_catalog().entries()) {
    result.push_back(to_public_info(entry));
  }
  return quader_api_result_from_status(
      status(session_, true, false), std::move(result));
}

QuaderApiResult<QuaderProjectMaterialPreview>
QuaderProjectAssetsApi::material_preview(ProjectAssetId material_id) const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialPreview>(
        status(session_, false, false,
               "Project materials are unavailable without an open project."));
  }
  ProjectIoResult<ProjectMaterialPreview> preview =
      project->material_preview(material_id);
  if (!preview.ok()) {
    return quader_api_result_from_status<QuaderProjectMaterialPreview>(
        status(session_, false, false, preview.error().message));
  }
  ProjectMaterialPreview value = preview.take_value();
  return quader_api_result_from_status(
      status(session_, true, false),
      QuaderProjectMaterialPreview{
          .encoded_bytes = std::move(value.encoded_bytes),
          .content_revision = value.content_revision,
      });
}

QuaderApiResult<QuaderMaterialPresentationRevision>
QuaderProjectAssetsApi::material_presentation_revision() const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialPresentationRevision>(
        status(session_, false, false,
               "Material presentations are unavailable without an open "
               "project."));
  }
  return quader_api_result_from_status(
      status(session_, true, false),
      to_public_revision(MaterialPresentationService::revision(
          *project,
          QuaderApiSessionInternalAccess::active_scene_session(session_))));
}

QuaderApiResult<QuaderMaterialPresentationSnapshot>
QuaderProjectAssetsApi::material_presentations() const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialPresentationSnapshot>(
        status(session_, false, false,
               "Material presentations are unavailable without an open "
               "project."));
  }
  SceneSession *scene =
      QuaderApiSessionInternalAccess::active_scene_session(session_);
  auto snapshot = to_public_snapshot(
      MaterialPresentationService::snapshot(*project, scene));
  for (const ProjectRelativePath &folder : project->material_catalog().folders()) {
    snapshot.material_folders.push_back(folder.portable_string());
  }
  return quader_api_result_from_status(status(session_, true, false),
                                       std::move(snapshot));
}

QuaderApiResult<QuaderProjectMaterialImportReport>
QuaderProjectAssetsApi::import_textures_as_materials(
    std::span<const std::filesystem::path> source_paths,
    std::string_view destination_folder) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialImportReport>(
        status(session_, false, false,
               "Project materials are unavailable without an open project."));
  }
  if (source_paths.empty()) {
    return quader_api_result_from_status<QuaderProjectMaterialImportReport>(
        status(session_, false, false,
               "Choose at least one texture to import."));
  }

  ProjectMaterialImportService importer(*project);
  ProjectMaterialImportReport imported =
      importer.import_textures_as_materials(source_paths, destination_folder);

  QuaderProjectMaterialImportReport result;
  result.imported.reserve(imported.imported.size());
  result.failed.reserve(imported.failed.size());
  for (ProjectMaterialImportSuccess &success : imported.imported) {
    result.imported.push_back(QuaderProjectMaterialImportSuccess{
        .source_path = std::move(success.source_path),
        .material = to_public_info(success.catalog_entry),
    });
  }
  for (ProjectMaterialImportFailure &failure : imported.failed) {
    result.failed.push_back(QuaderProjectMaterialImportFailure{
        .source_path = std::move(failure.source_path),
        .error_message = std::move(failure.error_message),
    });
  }

  const bool changed = imported.ok && !result.imported.empty();
  QuaderApiResult<QuaderProjectMaterialImportReport> api_result =
      quader_api_result_from_status(status(session_, imported.ok, changed,
                                           std::move(imported.message)),
                                    std::move(result));
  if (changed) {
    publish_assets_changed();
  }
  return api_result;
}

QuaderApiResult<QuaderProjectMaterialTextureInfo>
QuaderProjectAssetsApi::material_albedo_texture_info(
    ProjectAssetId material_id) const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialTextureInfo>(
        status(session_, false, false,
               "Project textures are unavailable without an open project."));
  }
  auto inspected = project->material_albedo_texture_info(material_id);
  if (!inspected.ok()) {
    return quader_api_result_from_status<QuaderProjectMaterialTextureInfo>(
        status(session_, false, false, inspected.error().message));
  }
  ProjectMaterialAlbedoTextureInfo value = inspected.take_value();
  const auto filename = value.project_relative_path.native_path()
                            .filename()
                            .u8string();
  std::string format = value.texture->detected_format;
  std::ranges::transform(format, format.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return quader_api_result_from_status(
      status(session_, true, false),
      QuaderProjectMaterialTextureInfo{
          .material_id = value.material_id,
          .texture_id = value.texture->texture_id,
          .name = std::string(filename.begin(), filename.end()),
          .project_relative_path =
              value.project_relative_path.portable_string(),
          .absolute_path = std::move(value.absolute_path),
          .format = std::move(format),
          .encoded_size_bytes = value.texture->encoded_byte_size,
          .width = value.texture->width,
          .height = value.texture->height,
          .has_alpha_channel = value.texture->has_alpha_channel,
          .rgba8 = value.texture->decoded,
          .content_revision = value.texture->content_revision,
      });
}

QuaderApiVoidResult status(QuaderApiSession session,
                           const ProjectSessionResult &result) {
  std::string message = result.message;
  for (const std::string &warning : result.warnings) {
    if (!warning.empty()) {
      message += message.empty() ? warning : " " + warning;
    }
  }
  return status(session, result.ok, result.changed, std::move(message));
}

QuaderApiVoidResult status(
    QuaderApiSession session,
    const ProjectMaterialOrganizationResult &result) {
  return status(session, result.ok, result.changed, result.message);
}

QuaderApiVoidResult QuaderProjectAssetsApi::create_material_folder(
    std::string_view parent_folder, std::string_view name) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before creating material folders.");
  }
  const auto result = ProjectMaterialOrganizationService(*project).create_folder(parent_folder, name);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_assets_changed();
  }
  return api_result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::rename_material_folder(
    std::string_view folder, std::string_view name) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before renaming material folders.");
  }
  const auto result = ProjectMaterialOrganizationService(*project).rename_folder(folder, name);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_assets_changed();
  }
  return api_result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::move_material_folder(
    std::string_view folder, std::string_view destination_folder) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before moving material folders.");
  }
  const auto result = ProjectMaterialOrganizationService(*project).move_folder(folder, destination_folder);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_assets_changed();
  }
  return api_result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::move_materials(
    std::span<const ProjectAssetId> material_ids, std::string_view destination_folder) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before moving materials.");
  }
  const auto result = ProjectMaterialOrganizationService(*project).move_materials(material_ids, destination_folder);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_assets_changed();
  }
  return api_result;
}

QuaderApiResult<QuaderProjectMaterialInfo>
QuaderProjectAssetsApi::rename_material(ProjectAssetId material_id,
                                        std::string_view new_name) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false,
               "Open a project before renaming materials."));
  }
  const auto renamed =
      ProjectMaterialOrganizationService(*project).rename_material(
          material_id, new_name);
  const QuaderApiVoidResult operation_status = status(session_, renamed);
  if (!renamed.ok) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        operation_status);
  }
  const ProjectMaterialCatalogEntry *entry =
      project->material_catalog().find(material_id);
  if (entry == nullptr) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false,
               "The renamed material is unavailable after publication."));
  }
  if (renamed.changed) {
    publish_assets_changed();
  }
  return quader_api_result_from_status(operation_status,
                                       to_public_info(*entry));
}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
QuaderProjectAssetsApi::duplicate_materials(
    std::span<const ProjectAssetId> material_ids) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<
        std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false,
               "Open a project before duplicating materials."));
  }
  const auto duplicated =
      ProjectMaterialOrganizationService(*project).duplicate_materials(
          material_ids);
  const QuaderApiVoidResult operation_status = status(session_, duplicated);
  if (!duplicated.ok) {
    return quader_api_result_from_status<
        std::vector<QuaderProjectMaterialInfo>>(operation_status);
  }
  std::vector<QuaderProjectMaterialInfo> result;
  result.reserve(duplicated.material_ids.size());
  for (const ProjectAssetId id : duplicated.material_ids) {
    const ProjectMaterialCatalogEntry *entry =
        project->material_catalog().find(id);
    if (entry == nullptr) {
      return quader_api_result_from_status<
          std::vector<QuaderProjectMaterialInfo>>(
          status(session_, false, false,
                 "A duplicated material is unavailable after publication."));
    }
    result.push_back(to_public_info(*entry));
  }
  if (duplicated.changed) {
    publish_assets_changed();
  }
  return quader_api_result_from_status(operation_status, std::move(result));
}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
QuaderProjectAssetsApi::copy_materials(
    std::span<const ProjectAssetId> material_ids,
    std::string_view destination_folder) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<
        std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false,
               "Open a project before copying materials."));
  }
  const auto copied = ProjectMaterialOrganizationService(*project)
                          .copy_materials(material_ids, destination_folder);
  const QuaderApiVoidResult operation_status = status(session_, copied);
  if (!copied.ok) {
    return quader_api_result_from_status<
        std::vector<QuaderProjectMaterialInfo>>(operation_status);
  }
  std::vector<QuaderProjectMaterialInfo> result;
  result.reserve(copied.material_ids.size());
  for (const ProjectAssetId id : copied.material_ids) {
    const ProjectMaterialCatalogEntry *entry =
        project->material_catalog().find(id);
    if (entry == nullptr) {
      return quader_api_result_from_status<
          std::vector<QuaderProjectMaterialInfo>>(
          status(session_, false, false,
                 "A copied material is unavailable after publication."));
    }
    result.push_back(to_public_info(*entry));
  }
  if (copied.changed) {
    publish_assets_changed();
  }
  return quader_api_result_from_status(operation_status, std::move(result));
}

QuaderApiResult<QuaderMaterialFolderDeletionPreview>
QuaderProjectAssetsApi::preview_delete_material_folder(std::string_view folder) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialFolderDeletionPreview>(
        status(session_, false, false, "Open a project before deleting material folders."));
  }
  const auto preview = ProjectMaterialDeletionService(*project).preview_folder(folder);
  return quader_api_result_from_status(status(session_, true, false),
      QuaderMaterialFolderDeletionPreview{
          .project_relative_path = preview.folder.portable_string(),
          .revision = preview.revision,
          .complete = preview.complete,
          .material_ids = preview.material_ids,
          .material_names = preview.material_names,
          .affected_scenes = to_public_scenes(preview.affected_scenes),
          .errors = preview.errors,
          .folder_count = preview.folder_count,
          .discards_material_draft = preview.discards_material_draft,
      });
}

QuaderApiResult<QuaderMaterialDeletionPreview>
QuaderProjectAssetsApi::preview_delete_materials(
    std::span<const ProjectAssetId> material_ids) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialDeletionPreview>(
        status(session_, false, false,
               "Open a project before deleting materials."));
  }
  const auto preview =
      ProjectMaterialDeletionService(*project).preview_materials(material_ids);
  return quader_api_result_from_status(
      status(session_, true, false),
      QuaderMaterialDeletionPreview{
          .revision = preview.revision,
          .complete = preview.complete,
          .material_ids = preview.material_ids,
          .material_names = preview.material_names,
          .affected_scenes = to_public_scenes(preview.affected_scenes),
          .errors = preview.errors,
          .discards_material_draft = preview.discards_material_draft,
      });
}

QuaderApiResult<QuaderProjectMaterialDependencySnapshot>
QuaderProjectAssetsApi::material_dependencies(bool refresh_files) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialDependencySnapshot>(
        status(session_, false, false, "Open a project before inspecting material dependencies."));
  }
  const auto source = project->dependencies().snapshot(refresh_files);
  QuaderProjectMaterialDependencySnapshot result{
      .project_id = source->project_id,
      .revision = source->revision,
      .complete = source->complete,
      .errors = source->errors,
  };
  result.scenes = to_public_scenes(source->scenes);
  for (const auto &material : source->materials) {
    QuaderProjectMaterialDependencies item{
        .id = material.id,
        .project_relative_path = material.path ? material.path->portable_string() : std::string{},
        .name = material.name,
        .draft = material.draft,
        .dirty = material.dirty,
    };
    for (const auto &texture : material.textures) {
      std::string slot;
      switch (texture.slot) {
      case StandardMaterialTextureSlot::Albedo: slot = "albedo"; break;
      case StandardMaterialTextureSlot::Roughness: slot = "roughness"; break;
      case StandardMaterialTextureSlot::Metallic: slot = "metallic"; break;
      case StandardMaterialTextureSlot::Normal: slot = "normal"; break;
      }
      item.textures.push_back({
          .slot = std::move(slot),
          .texture_id = texture.texture_id,
          .project_relative_path = texture.path ? texture.path->portable_string() : std::string{},
          .pending_source = texture.pending_source,
      });
    }
    result.materials.push_back(std::move(item));
  }
  return quader_api_result_from_status(status(session_, true, false), std::move(result));
}

QuaderApiResult<QuaderProjectMaterialDependencyRevision>
QuaderProjectAssetsApi::material_dependency_revision() const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<
        QuaderProjectMaterialDependencyRevision>(
        status(session_, false, false,
               "Open a project before inspecting material dependencies."));
  }
  const auto source = project->dependencies().snapshot(false);
  return quader_api_result_from_status(
      status(session_, true, false),
      QuaderProjectMaterialDependencyRevision{
          .project_id = source->project_id,
          .project_generation = source->project_generation,
          .revision = source->revision,
          .complete = source->complete,
      });
}

QuaderApiResult<QuaderProjectMaterialUsageRevision>
QuaderProjectAssetsApi::material_usage_revision() const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialUsageRevision>(
        status(session_, false, false, "Open a project before inspecting material usage."));
  }
  const auto source = project->material_usage().snapshot();
  return quader_api_result_from_status(status(session_, true, false),
      QuaderProjectMaterialUsageRevision{
          .project_id = source->project_id,
          .project_generation = source->project_generation,
          .revision = source->revision,
          .complete = source->complete,
      });
}

QuaderApiResult<QuaderProjectMaterialUsageSnapshot>
QuaderProjectAssetsApi::material_usage(bool refresh_files) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialUsageSnapshot>(
        status(session_, false, false, "Open a project before inspecting material usage."));
  }
  const auto source = project->material_usage().snapshot(refresh_files);
  QuaderProjectMaterialUsageSnapshot result{
      .project_id = source->project_id,
      .project_generation = source->project_generation,
      .revision = source->revision,
      .complete = source->complete,
  };
  result.used_materials.reserve(source->material_ids.size() +
                                source->active_scene_embedded_material_ids.size());
  for (const auto id : source->material_ids) {
    result.used_materials.push_back(QuaderMaterialReference::project(id));
  }
  for (const auto id : source->active_scene_embedded_material_ids) {
    result.used_materials.push_back(QuaderMaterialReference::embedded(id));
  }
  result.issues.reserve(source->issues.size());
  for (const auto &issue : source->issues) {
    result.issues.push_back({issue.scene_id, issue.session_id,
        issue.project_relative_path, issue.scene_name, issue.message});
  }
  return quader_api_result_from_status(status(session_, true, false), std::move(result));
}

QuaderApiVoidResult QuaderProjectAssetsApi::delete_material_folder(
    std::string_view folder, std::uint64_t expected_revision) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before deleting material folders.");
  }
  const auto *draft = project->standard_material_editor().current();
  const std::optional<StandardMaterialEditSnapshot> previous_draft =
      draft != nullptr ? std::optional(*draft) : std::nullopt;
  const auto result = ProjectMaterialDeletionService(*project).delete_folder(folder, expected_revision);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_material_deletion_events(events_, *project, previous_draft);
  }
  return api_result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::delete_materials(
    std::span<const ProjectAssetId> material_ids,
    std::uint64_t expected_revision) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false,
                  "Open a project before deleting materials.");
  }
  const auto *draft = project->standard_material_editor().current();
  const std::optional<StandardMaterialEditSnapshot> previous_draft =
      draft != nullptr ? std::optional(*draft) : std::nullopt;
  const auto result = ProjectMaterialDeletionService(*project).delete_materials(
      material_ids, expected_revision);
  const auto api_result = status(session_, result);
  if (result.ok && result.changed) {
    publish_material_deletion_events(events_, *project, previous_draft);
  }
  return api_result;
}

void QuaderProjectAssetsApi::publish_assets_changed() {
  if (events_ == nullptr) {
    return;
  }
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  QuaderEvent event{
      .name = std::string(quader_event::kProjectAssetsChanged),
  };
  if (project != nullptr && project->has_project()) {
    event.project_id = project->manifest()->id();
    event.project_name = project->manifest()->name();
    const auto root_utf8 = project->root().u8string();
    event.project_root.emplace(root_utf8.begin(), root_utf8.end());
  }
  (void)QuaderEventsApiInternalAccess::publish(*events_, std::move(event));
}

} // namespace quader
