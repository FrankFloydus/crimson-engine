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
#include "editor/material/project_material_import_service.hpp"
#include "editor/session/project_session.hpp"

#include <algorithm>
#include <array>
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

QuaderApiResult<QuaderProjectMaterialInfo>
QuaderProjectAssetsApi::active_material() const {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false,
               "Project materials are unavailable without an open project."));
  }
  const std::optional<ProjectAssetId> active = project->active_material_id();
  const ProjectMaterialCatalogEntry *entry =
      active.has_value() ? project->material_catalog().find(*active) : nullptr;
  if (entry == nullptr) {
    return quader_api_result_from_status<QuaderProjectMaterialInfo>(
        status(session_, false, false,
               "The active project material is unavailable."));
  }
  return quader_api_result_from_status(status(session_, true, false),
                                       to_public_info(*entry));
}

QuaderApiVoidResult
QuaderProjectAssetsApi::set_active_material(ProjectAssetId material_id) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  if (project == nullptr || !project->has_project()) {
    return status(session_, false, false,
                  "Project materials are unavailable without an open project.");
  }
  const ProjectSessionResult changed = project->set_active_material(material_id);
  QuaderApiVoidResult result =
      status(session_, changed.ok, changed.changed, changed.message);
  if (changed.ok && changed.changed && events_ != nullptr) {
    (void)QuaderEventsApiInternalAccess::publish(
        *events_, QuaderEvent{
                      .name = std::string(
                          quader_event::kActiveProjectMaterialChanged),
                      .project_id = project->manifest()->id(),
                      .project_name = project->manifest()->name(),
                      .project_root = project->root().string(),
                      .project_asset_id = material_id,
                  });
  }
  return result;
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
  return quader_api_result_from_status(status(session_, true, false),
                                       to_public_snapshot(
                                           MaterialPresentationService::snapshot(
                                               *project, scene)));
}

QuaderApiResult<QuaderProjectMaterialImportReport>
QuaderProjectAssetsApi::import_textures_as_materials(
    std::span<const std::filesystem::path> source_paths) {
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
      importer.import_textures_as_materials(source_paths);
  if (!imported.ok) {
    return quader_api_result_from_status<QuaderProjectMaterialImportReport>(
        status(session_, false, false, std::move(imported.message)));
  }

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

  const bool changed = !result.imported.empty();
  QuaderApiResult<QuaderProjectMaterialImportReport> api_result =
      quader_api_result_from_status(status(session_, true, changed,
                                           std::move(imported.message)),
                                    std::move(result));
  if (changed) {
    publish_assets_changed();
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
    event.project_root = project->root().string();
    event.project_asset_id = project->active_material_id();
  }
  (void)QuaderEventsApiInternalAccess::publish(*events_, std::move(event));
}

} // namespace quader
