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
#include "core/release_features.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/material_presentation_service.hpp"
#include "editor/material/project_asset_dependency_service.hpp"
#include "editor/material/project_material_usage_service.hpp"
#include "editor/material/project_material_import_service.hpp"
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
      .project_texture_revision = revision.project_texture_revision,
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

ProjectIoResult<std::vector<ProjectRelativePath>> material_paths(
    ProjectSession &project, std::span<const ProjectAssetId> ids) {
  std::vector<ProjectRelativePath> result;
  result.reserve(ids.size());
  for (const auto id : ids) {
    const auto *entry = project.material_catalog().find(id);
    if (entry == nullptr) {
      return ProjectIoResult<std::vector<ProjectRelativePath>>::failure(
          ProjectIoErrorCode::AssetNotFound,
          "A selected material is no longer available.");
    }
    if (std::ranges::find(result, entry->path) != result.end()) {
      return ProjectIoResult<std::vector<ProjectRelativePath>>::failure(
          ProjectIoErrorCode::DuplicateAsset,
          "The material selection contains duplicate identities.");
    }
    result.push_back(entry->path);
  }
  return ProjectIoResult<std::vector<ProjectRelativePath>>::success(std::move(result));
}

ProjectIoResult<ProjectRelativePath> material_folder_path(
    ProjectSession &project, std::string_view folder) {
  const auto parsed = ProjectRelativePath::parse_portable(folder);
  if (!parsed.ok()) {
    return parsed;
  }
  const auto &catalog = project.project_assets().snapshot();
  if (!catalog.ok()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        catalog.error().code, catalog.error().message);
  }
  const auto found = std::ranges::find(catalog.value().entries, parsed.value(),
                                      &ProjectAssetEntry::path);
  if (found == catalog.value().entries.end() ||
      found->kind != ProjectAssetKind::Folder) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::DirectoryNotFound,
        "The selected project folder is no longer available.");
  }
  return parsed;
}

std::vector<std::string> portable_paths(
    std::span<const ProjectRelativePath> paths) {
  std::vector<std::string> result;
  result.reserve(paths.size());
  for (const auto &path : paths) {
    result.push_back(path.portable_string());
  }
  return result;
}

std::vector<std::string> material_names(
    const ProjectSession &project, std::span<const ProjectAssetId> ids) {
  std::vector<std::string> result;
  result.reserve(ids.size());
  for (const auto id : ids) {
    const auto *entry = project.material_catalog().find(id);
    result.push_back(entry != nullptr ? entry->material.name : std::string(id.str()));
  }
  return result;
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
                           const ProjectAssetOperationResult &result) {
  std::string message = result.message;
  for (const auto &warning : result.warnings) {
    if (!message.empty()) message += '\n';
    message += warning;
  }
  return status(session, result.ok, result.changed, std::move(message));
}

QuaderApiVoidResult QuaderProjectAssetsApi::create_material_folder(
    std::string_view parent_folder, std::string_view name) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  return create_folder(parent_folder, name);
}

QuaderApiVoidResult QuaderProjectAssetsApi::rename_material_folder(
    std::string_view folder, std::string_view name) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before renaming material folders.");
  }
  const auto path = material_folder_path(*project, folder);
  if (!path.ok()) return status(session_, false, false, path.error().message);
  return rename_asset(path.value().portable_string(), name,
                      project->manifest()->id(), project->project_assets().revision());
}

QuaderApiVoidResult QuaderProjectAssetsApi::move_material_folder(
    std::string_view folder, std::string_view destination_folder) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before moving material folders.");
  }
  const auto path = material_folder_path(*project, folder);
  if (!path.ok()) return status(session_, false, false, path.error().message);
  const std::array paths{path.value().portable_string()};
  return move_assets(paths, destination_folder, project->manifest()->id(),
                     project->project_assets().revision());
}

QuaderApiVoidResult QuaderProjectAssetsApi::move_materials(
    std::span<const ProjectAssetId> material_ids, std::string_view destination_folder) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before moving materials.");
  }
  const auto paths = material_paths(*project, material_ids);
  if (!paths.ok()) return status(session_, false, false, paths.error().message);
  const auto &catalog = project->project_assets().snapshot();
  if (!catalog.ok()) return status(session_, false, false, catalog.error().message);
  return move_assets(portable_paths(paths.value()), destination_folder,
                     catalog.value().project_id, catalog.value().revision);
}

QuaderApiResult<QuaderProjectMaterialInfo>
QuaderProjectAssetsApi::rename_material(ProjectAssetId material_id,
                                        std::string_view new_name) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false, release_feature_unavailable_message()));
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false, "Open a project before renaming materials."));
  }
  const auto paths = material_paths(*project, std::array{material_id});
  if (!paths.ok()) return quader_api_result_from_status<QuaderProjectMaterialInfo>(
      status(session_, false, false, paths.error().message));
  const auto &catalog = project->project_assets().snapshot();
  if (!catalog.ok()) return quader_api_result_from_status<QuaderProjectMaterialInfo>(
      status(session_, false, false, catalog.error().message));
  const auto operation = project->project_assets().rename(
      paths.value().front(), new_name, catalog.value().project_id,
      catalog.value().revision);
  auto result = quader_api_result_from_status<QuaderProjectMaterialInfo>(status(session_, operation));
  if (!operation.ok) return result;
  const auto *entry = project->material_catalog().find(material_id);
  if (entry != nullptr) result.value = to_public_info(*entry);
  // Construct the result before events can synchronously replace the project.
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>> material_operation_result(
    QuaderApiSession session, ProjectSession &project,
    const ProjectAssetOperationResult &operation) {
  auto result = quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
      status(session, operation));
  if (!operation.ok) return result;
  for (const auto &path : operation.paths) {
    const auto &entries = project.material_catalog().entries();
    const auto found = std::ranges::find(entries, path, &ProjectMaterialCatalogEntry::path);
    if (found != entries.end()) result.value().push_back(to_public_info(*found));
  }
  return result;
}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
QuaderProjectAssetsApi::duplicate_materials(std::span<const ProjectAssetId> material_ids) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false, release_feature_unavailable_message()));
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false, "Open a project before duplicating materials."));
  }
  const auto paths = material_paths(*project, material_ids);
  if (!paths.ok()) return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
      status(session_, false, false, paths.error().message));
  const auto &catalog = project->project_assets().snapshot();
  if (!catalog.ok()) return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
      status(session_, false, false, catalog.error().message));
  const auto operation = project->project_assets().duplicate(
      paths.value(), catalog.value().project_id, catalog.value().revision);
  auto result = material_operation_result(session_, *project, operation);
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
QuaderProjectAssetsApi::copy_materials(std::span<const ProjectAssetId> material_ids,
                                      std::string_view destination_folder) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false, release_feature_unavailable_message()));
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
        status(session_, false, false, "Open a project before copying materials."));
  }
  const auto paths = material_paths(*project, material_ids);
  if (!paths.ok()) return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
      status(session_, false, false, paths.error().message));
  const auto &catalog = project->project_assets().snapshot();
  if (!catalog.ok()) return quader_api_result_from_status<std::vector<QuaderProjectMaterialInfo>>(
      status(session_, false, false, catalog.error().message));
  const auto operation = project->project_assets().copy(
      paths.value(), destination_folder, catalog.value().project_id, catalog.value().revision);
  auto result = material_operation_result(session_, *project, operation);
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<QuaderMaterialFolderDeletionPreview>
QuaderProjectAssetsApi::preview_delete_material_folder(std::string_view folder) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialFolderDeletionPreview>(
        status(session_, false, false, "Open a project before deleting material folders."));
  }
  const auto path = material_folder_path(*project, folder);
  if (!path.ok()) {
    return quader_api_result_from_status(status(session_, true, false),
        QuaderMaterialFolderDeletionPreview{
            .project_relative_path = std::string(folder),
            .errors = {path.error().message}});
  }
  const auto preview = preview_delete_assets(std::array{path.value().portable_string()});
  if (!preview.ok()) return quader_api_result_from_status<QuaderMaterialFolderDeletionPreview>(preview);
  const auto &source = preview.value();
  QuaderMaterialFolderDeletionPreview result{
      .project_relative_path = path.value().portable_string(),
      .revision = source.revision,
      .complete = source.complete,
      .material_ids = source.material_ids,
      .material_names = material_names(*project, source.material_ids),
      .affected_scenes = source.affected_scenes,
      .errors = source.errors,
      .discards_material_draft = source.discards_material_draft,
  };
  if (!source.scene_ids.empty() || !source.texture_ids.empty()) {
    result.complete = false;
    result.errors.push_back("This folder contains scene or texture assets. Use the Asset Browser to review and delete its complete contents.");
  }
  const auto &catalog = project->project_assets().snapshot();
  if (catalog.ok()) {
    const std::string prefix = path.value().portable_string() + '/';
    result.folder_count = static_cast<std::size_t>(std::ranges::count_if(
        catalog.value().entries, [&](const ProjectAssetEntry &entry) {
          return entry.kind == ProjectAssetKind::Folder &&
                 (entry.path == path.value() || entry.path.portable_string().starts_with(prefix));
        }));
  }
  return quader_api_result_from_status(preview, std::move(result));
}

QuaderApiResult<QuaderMaterialDeletionPreview>
QuaderProjectAssetsApi::preview_delete_materials(std::span<const ProjectAssetId> material_ids) const {
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderMaterialDeletionPreview>(
        status(session_, false, false, "Open a project before deleting materials."));
  }
  const auto paths = material_paths(*project, material_ids);
  if (!paths.ok()) {
    return quader_api_result_from_status(status(session_, true, false),
        QuaderMaterialDeletionPreview{.errors = {paths.error().message}});
  }
  const auto preview = preview_delete_assets(portable_paths(paths.value()));
  if (!preview.ok()) return quader_api_result_from_status<QuaderMaterialDeletionPreview>(preview);
  const auto &source = preview.value();
  return quader_api_result_from_status(preview, QuaderMaterialDeletionPreview{
      .revision = source.revision,
      .complete = source.complete,
      .material_ids = source.material_ids,
      .material_names = material_names(*project, source.material_ids),
      .affected_scenes = source.affected_scenes,
      .errors = source.errors,
      .discards_material_draft = source.discards_material_draft,
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
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  const auto preview = preview_delete_material_folder(folder);
  if (!preview.ok()) return preview;
  if (!preview.value().complete) {
    return status(session_, false, false,
                  preview.value().errors.empty()
                      ? "The material folder cannot be deleted safely."
                      : preview.value().errors.front());
  }
  return delete_assets(std::array{preview.value().project_relative_path}, expected_revision);
}

QuaderApiVoidResult QuaderProjectAssetsApi::delete_materials(
    std::span<const ProjectAssetId> material_ids, std::uint64_t expected_revision) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return status(session_, false, false, release_feature_unavailable_message());
  }
  auto *project = QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false, "Open a project before deleting materials.");
  }
  const auto paths = material_paths(*project, material_ids);
  if (!paths.ok()) return status(session_, false, false, paths.error().message);
  return delete_assets(portable_paths(paths.value()), expected_revision);
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
