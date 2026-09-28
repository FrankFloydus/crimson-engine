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
#include "api/quader_standard_material_presentation.hpp"
#include "core/release_features.hpp"
#include "editor/assets/project_asset_deletion_service.hpp"
#include "editor/assets/project_asset_service.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QuaderApiVoidResult browser_status(QuaderApiSession session, bool ok,
                                   bool changed, std::string message = {}) {
  return quader_api_void_status(
      QuaderApiSessionInternalAccess::scene_session(session), ok, changed,
      std::move(message),
      changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None);
}

ProjectSession *open_project(QuaderApiSession session) {
  auto *project = QuaderApiSessionInternalAccess::project_session(session);
  return project != nullptr && project->has_project() ? project : nullptr;
}

template <typename T>
QuaderApiResult<T> browser_failure(QuaderApiSession session,
                                   std::string message) {
  return quader_api_result_from_status<T>(
      browser_status(session, false, false, std::move(message)));
}

QuaderProjectAssetInfo public_asset(const ProjectAssetEntry &entry) {
  QuaderProjectAssetKind kind = QuaderProjectAssetKind::Folder;
  switch (entry.kind) {
  case ProjectAssetKind::Scene: kind = QuaderProjectAssetKind::Scene; break;
  case ProjectAssetKind::Material: kind = QuaderProjectAssetKind::Material; break;
  case ProjectAssetKind::Texture: kind = QuaderProjectAssetKind::Texture; break;
  default: break;
  }
  return {.kind = kind, .asset_id = entry.asset_id, .scene_id = entry.scene_id,
          .name = entry.name, .project_relative_path = entry.path.portable_string(),
          .content_revision = entry.content_revision};
}

ProjectIoResult<std::vector<ProjectRelativePath>> parse_asset_paths(
    std::span<const std::string> paths) {
  std::vector<ProjectRelativePath> parsed;
  parsed.reserve(paths.size());
  for (const auto &path : paths) {
    auto item = ProjectRelativePath::parse_portable(path);
    if (!item.ok()) {
      return ProjectIoResult<std::vector<ProjectRelativePath>>::failure(
          item.error().code, item.error().message, item.error().path);
    }
    parsed.push_back(item.take_value());
  }
  return ProjectIoResult<std::vector<ProjectRelativePath>>::success(std::move(parsed));
}

std::string operation_message(const ProjectAssetOperationResult &operation) {
  std::string message = operation.message;
  for (const auto &warning : operation.warnings) {
    if (!message.empty()) message += '\n';
    message += warning;
  }
  return message;
}

QuaderApiResult<std::vector<QuaderProjectAssetInfo>> operation_assets(
    QuaderApiSession session, ProjectSession &project,
    const ProjectAssetOperationResult &operation) {
  auto result = quader_api_result_from_status<std::vector<QuaderProjectAssetInfo>>(
      browser_status(session, operation.ok, operation.changed,
                     operation_message(operation)));
  if (!operation.ok) return result;
  const auto &catalog = project.project_assets().snapshot();
  if (!catalog.ok()) {
    if (!result.message.empty()) result.message += '\n';
    result.message += catalog.error().message;
    return result;
  }
  for (const auto &path : operation.paths) {
    const auto found = std::ranges::find(catalog.value().entries, path,
                                       &ProjectAssetEntry::path);
    if (found != catalog.value().entries.end()) {
      result.value().push_back(public_asset(*found));
    }
  }
  return result;
}

QuaderProjectSceneMaterialDependencies public_scene_dependency(
    const ProjectSceneAssetDependencies &scene) {
  return {.scene_id = scene.scene_id, .session_id = scene.session_id,
          .project_relative_path = scene.path ? scene.path->portable_string() : "",
          .name = scene.display_name, .live = scene.live, .dirty = scene.dirty,
          .material_ids = scene.material_ids};
}

QuaderProjectMaterialDependencies public_material_dependency(
    const ProjectMaterialAssetDependencies &material) {
  QuaderProjectMaterialDependencies result{
      .id = material.id,
      .project_relative_path = material.path ? material.path->portable_string() : "",
      .name = material.name, .draft = material.draft, .dirty = material.dirty};
  constexpr std::array kNames{"Albedo", "Roughness", "Metallic", "Normal"};
  for (const auto &texture : material.textures) {
    result.textures.push_back({
        .slot = kNames[static_cast<std::size_t>(texture.slot)],
        .texture_id = texture.texture_id,
        .project_relative_path = texture.path ? texture.path->portable_string() : "",
        .pending_source = texture.pending_source});
  }
  return result;
}

} // namespace

QuaderApiResult<QuaderProjectAssetRevision>
QuaderProjectAssetsApi::asset_revision() const {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<QuaderProjectAssetRevision>(
      session_, "Open a project before browsing assets.");
  return quader_api_result_from_status(browser_status(session_, true, false),
      QuaderProjectAssetRevision{.project_id = project->manifest()->id(),
          .project_generation = project->project_generation(),
          .revision = project->project_assets().revision()});
}

QuaderApiResult<QuaderProjectAssetSnapshot>
QuaderProjectAssetsApi::assets(bool refresh_files) const {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<QuaderProjectAssetSnapshot>(
      session_, "Open a project before browsing assets.");
  if (refresh_files) {
    const auto refreshed = project->refresh_asset_catalog();
    if (!refreshed.ok) return browser_failure<QuaderProjectAssetSnapshot>(
        session_, refreshed.message);
  }
  const auto &source = refresh_files ? project->project_assets().refresh()
                                    : project->project_assets().snapshot();
  if (!source.ok()) return browser_failure<QuaderProjectAssetSnapshot>(
      session_, source.error().message);
  QuaderProjectAssetSnapshot result;
  result.project_id = source.value().project_id;
  result.project_generation = project->project_generation();
  result.revision = source.value().revision;
  result.folders.push_back({});
  for (const auto &entry : source.value().entries) {
    if ((entry.kind == ProjectAssetKind::Material ||
         entry.kind == ProjectAssetKind::Texture) &&
        !release_has_feature(ReleaseFeature::Materials)) continue;
    result.assets.push_back(public_asset(entry));
    if (entry.kind == ProjectAssetKind::Folder) {
      result.folders.push_back(entry.path.portable_string());
    }
  }
  return quader_api_result_from_status(browser_status(session_, true, false),
                                       std::move(result));
}

QuaderApiResult<QuaderProjectAssetInfo>
QuaderProjectAssetsApi::create_scene_asset(std::string_view folder,
                                           std::string_view name) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<QuaderProjectAssetInfo>(
      session_, "Open a project before creating assets.");
  const auto operation = project->project_assets().create_scene(folder, name);
  auto result = operation_assets(session_, *project, operation);
  auto created = quader_api_result_from_status<QuaderProjectAssetInfo>(result);
  if (!result.value().empty()) created.value = result.value().front();
  if (operation.changed) publish_assets_changed();
  return created;
}

QuaderApiResult<QuaderProjectAssetInfo>
QuaderProjectAssetsApi::create_material_asset(std::string_view folder,
                                              std::string_view name) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<QuaderProjectAssetInfo>(
      session_, "Open a project before creating assets.");
  const auto operation = project->project_assets().create_material(folder, name);
  auto result = operation_assets(session_, *project, operation);
  auto created = quader_api_result_from_status<QuaderProjectAssetInfo>(result);
  if (!result.value().empty()) created.value = result.value().front();
  if (operation.changed) publish_assets_changed();
  return created;
}

QuaderApiVoidResult QuaderProjectAssetsApi::create_folder(
    std::string_view parent, std::string_view name) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_status(session_, false, false,
                                               "Open a project before creating folders.");
  const auto operation = project->project_assets().create_folder(parent, name);
  const auto result = browser_status(session_, operation.ok, operation.changed,
                                     operation_message(operation));
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::rename_asset(
    std::string_view path, std::string_view name, ProjectId expected_project,
    std::uint64_t expected_revision) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_status(session_, false, false,
                                               "Open a project before renaming assets.");
  const auto parsed = ProjectRelativePath::parse_portable(path);
  if (!parsed.ok()) return browser_status(session_, false, false, parsed.error().message);
  const auto operation = project->project_assets().rename(
      parsed.value(), name, expected_project, expected_revision);
  const auto result = browser_status(session_, operation.ok, operation.changed,
                                     operation_message(operation));
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiVoidResult QuaderProjectAssetsApi::move_assets(
    std::span<const std::string> paths, std::string_view destination,
    ProjectId expected_project, std::uint64_t expected_revision) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_status(session_, false, false,
                                               "Open a project before moving assets.");
  const auto parsed = parse_asset_paths(paths);
  if (!parsed.ok()) return browser_status(session_, false, false, parsed.error().message);
  const auto operation = project->project_assets().move(
      parsed.value(), destination, expected_project, expected_revision);
  const auto result = browser_status(session_, operation.ok, operation.changed,
                                     operation_message(operation));
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<std::vector<QuaderProjectAssetInfo>>
QuaderProjectAssetsApi::copy_assets(std::span<const std::string> paths,
    std::string_view destination, ProjectId expected_project,
    std::uint64_t expected_revision) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<std::vector<QuaderProjectAssetInfo>>(
      session_, "Open a project before copying assets.");
  const auto parsed = parse_asset_paths(paths);
  if (!parsed.ok()) return browser_failure<std::vector<QuaderProjectAssetInfo>>(
      session_, parsed.error().message);
  const auto operation = project->project_assets().copy(
      parsed.value(), destination, expected_project, expected_revision);
  auto result = operation_assets(session_, *project, operation);
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<std::vector<QuaderProjectAssetInfo>>
QuaderProjectAssetsApi::duplicate_assets(std::span<const std::string> paths,
    ProjectId expected_project, std::uint64_t expected_revision) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<std::vector<QuaderProjectAssetInfo>>(
      session_, "Open a project before duplicating assets.");
  const auto parsed = parse_asset_paths(paths);
  if (!parsed.ok()) return browser_failure<std::vector<QuaderProjectAssetInfo>>(
      session_, parsed.error().message);
  const auto operation = project->project_assets().duplicate(
      parsed.value(), expected_project, expected_revision);
  auto result = operation_assets(session_, *project, operation);
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<std::vector<QuaderProjectAssetInfo>>
QuaderProjectAssetsApi::import_textures(
    std::span<const std::filesystem::path> sources, std::string_view destination) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<std::vector<QuaderProjectAssetInfo>>(
      session_, "Open a project before importing textures.");
  const auto operation = project->project_assets().import_textures(sources, destination);
  auto result = operation_assets(session_, *project, operation);
  if (operation.changed) publish_assets_changed();
  return result;
}

QuaderApiResult<QuaderProjectMaterialTextureInfo>
QuaderProjectAssetsApi::texture_info(ProjectAssetId texture_id) const {
  auto *project = open_project(session_);
  if (project == nullptr || !release_has_feature(ReleaseFeature::Materials)) {
    return browser_failure<QuaderProjectMaterialTextureInfo>(
        session_, "Project textures are unavailable.");
  }
  const auto *entry = project->manifest()->find_texture(texture_id);
  if (entry == nullptr) return browser_failure<QuaderProjectMaterialTextureInfo>(
      session_, "The selected texture is unavailable.");
  const auto image = project->texture_image({.id = entry->id,
      .project_relative_path = entry->path.portable_string()});
  if (image == nullptr) return browser_failure<QuaderProjectMaterialTextureInfo>(
      session_, "The selected texture could not be decoded.");
  const auto absolute = project->workspace()->file_system.resolve_existing_file(entry->path);
  if (!absolute.ok()) return browser_failure<QuaderProjectMaterialTextureInfo>(
      session_, absolute.error().message);
  const auto filename = entry->path.native_path().filename().u8string();
  const auto &value = *image;
  return quader_api_result_from_status(browser_status(session_, true, false),
      QuaderProjectMaterialTextureInfo{.texture_id = entry->id,
          .name = std::string(filename.begin(), filename.end()),
          .project_relative_path = entry->path.portable_string(),
          .absolute_path = absolute.value(), .format = value.detected_format,
          .encoded_size_bytes = value.encoded_byte_size, .width = value.width,
          .height = value.height, .has_alpha_channel = value.has_alpha_channel,
          .rgba8 = value.decoded, .content_revision = value.content_revision});
}

QuaderApiResult<QuaderProjectMaterialThumbnailInfo>
QuaderProjectAssetsApi::material_thumbnail_info(ProjectAssetId material_id) const {
  auto *project = open_project(session_);
  if (project == nullptr || !release_has_feature(ReleaseFeature::Materials)) {
    return browser_failure<QuaderProjectMaterialThumbnailInfo>(
        session_, "Project material previews are unavailable.");
  }
  const auto *entry = project->material_catalog().find(material_id);
  const auto *material = entry ? entry->material.standard_material() : nullptr;
  if (material == nullptr) return browser_failure<QuaderProjectMaterialThumbnailInfo>(
      session_, "The selected Standard Material is unavailable.");
  QuaderProjectMaterialThumbnailInfo result{
      .material_id = material_id, .content_revision = entry->content_revision,
      .material = quader_standard_material_definition(*material)};
  for (std::size_t index = 0; index != result.textures.size(); ++index) {
    const auto slot = static_cast<StandardMaterialTextureSlot>(index);
    const auto *reference = material->texture(slot);
    if (reference == nullptr || !reference->has_value()) continue;
    const auto image = project->material_texture_image(material_id, slot);
    if (image == nullptr) {
      result.texture_diagnostics[index] = "The material texture could not be decoded.";
      continue;
    }
    const auto &value = *image;
    result.textures[index] = QuaderStandardMaterialPreviewTexture{
        .id = value.texture_id,
        .color_space = reference->value().color_space == ProjectTextureColorSpace::Linear
                           ? QuaderProjectTextureColorSpace::Linear
                           : QuaderProjectTextureColorSpace::Srgb,
        .content_revision = value.content_revision, .rgba8 = value.decoded,
        .width = value.width, .height = value.height,
        .detected_format = value.detected_format,
        .encoded_byte_size = value.encoded_byte_size,
        .has_alpha_channel = value.has_alpha_channel};
  }
  return quader_api_result_from_status(browser_status(session_, true, false),
                                       std::move(result));
}

QuaderApiResult<QuaderProjectAssetDeletionPreview>
QuaderProjectAssetsApi::preview_delete_assets(std::span<const std::string> paths) const {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_failure<QuaderProjectAssetDeletionPreview>(
      session_, "Open a project before deleting assets.");
  const auto parsed = parse_asset_paths(paths);
  if (!parsed.ok()) return browser_failure<QuaderProjectAssetDeletionPreview>(
      session_, parsed.error().message);
  const auto source = ProjectAssetDeletionService(*project, recycle_).preview(parsed.value());
  QuaderProjectAssetDeletionPreview result{.project_id = source.project_id,
      .revision = source.revision, .complete = source.complete,
      .scene_ids = source.scene_ids, .material_ids = source.material_ids,
      .texture_ids = source.texture_ids, .dirty_scene_sessions = source.dirty_scene_sessions,
      .errors = source.errors, .discards_material_draft = source.discards_material_draft};
  for (const auto &path : source.paths) result.paths.push_back(path.portable_string());
  for (const auto &scene : source.affected_scenes) {
    result.affected_scenes.push_back(public_scene_dependency(scene));
  }
  for (const auto &material : source.affected_materials) {
    result.affected_materials.push_back(public_material_dependency(material));
  }
  return quader_api_result_from_status(browser_status(session_, true, false),
                                       std::move(result));
}

QuaderApiVoidResult QuaderProjectAssetsApi::delete_assets(
    std::span<const std::string> paths, std::uint64_t preview_revision,
    std::span<const SceneSessionId> approved_dirty_scenes) {
  auto *project = open_project(session_);
  if (project == nullptr) return browser_status(session_, false, false,
                                               "Open a project before deleting assets.");
  const auto parsed = parse_asset_paths(paths);
  if (!parsed.ok()) return browser_status(session_, false, false, parsed.error().message);
  const auto previous_active = project->active_scene_id();
  const auto previous_generation = project->activation_generation();
  const auto *draft = project->standard_material_editor().current();
  std::optional<QuaderEvent> previous_material;
  if (draft != nullptr) {
    previous_material = QuaderEvent{
        .name = std::string(quader_event::kStandardMaterialClosed),
        .project_id = project->manifest()->id(),
        .project_asset_id = draft->document.id,
        .material_edit_session_id = draft->session_id,
        .material_name = draft->document.name,
        .material_path = draft->path ? std::optional(draft->path->portable_string()) : std::nullopt,
        .material_dirty = draft->dirty};
  }
  std::vector<QuaderEvent> previous_scenes;
  for (const auto &scene : project->open_scenes()) {
    previous_scenes.push_back({.name = std::string(quader_event::kSceneClosed),
        .project_id = project->manifest()->id(),
        .scene_session_id = scene->session_id, .scene_id = scene->scene_id,
        .scene_name = scene->display_name,
        .scene_path = scene->path ? std::optional(scene->path->portable_string()) : std::nullopt,
        .scene_dirty = scene->dirty(), .scene_active = scene->session_id == previous_active});
  }
  const auto operation = ProjectAssetDeletionService(*project, recycle_).erase(
      parsed.value(), preview_revision, approved_dirty_scenes);
  std::string message = operation.message;
  for (const auto &warning : operation.warnings) {
    if (!message.empty()) message += '\n';
    message += warning;
  }
  const auto result = browser_status(session_, operation.ok, operation.changed,
                                     std::move(message));
  if (!operation.ok || !operation.changed) return result;
  std::erase_if(previous_scenes, [&](const QuaderEvent &event) {
    return project->find(*event.scene_session_id) != nullptr;
  });
  if (previous_active != project->active_scene_id()) {
    QuaderEvent active{.name = std::string(quader_event::kActiveSceneChanged),
                       .project_id = project->manifest()->id()};
    if (const auto *scene = project->active_scene()) {
      active.scene_session_id = scene->session_id;
      active.scene_id = scene->scene_id;
      active.scene_name = scene->display_name;
      active.scene_active = true;
      active.scene_dirty = scene->dirty();
      if (scene->path) active.scene_path = scene->path->portable_string();
    }
    previous_scenes.push_back(std::move(active));
  }
  if (previous_material && !project->standard_material_editor().has_document()) {
    previous_scenes.push_back(std::move(*previous_material));
  }
  const auto committed_project = project->manifest()->id();
  const auto committed_generation = project->activation_generation();
  if (previous_generation != committed_generation && binding_changed_) {
    binding_changed_(session_);
  }
  if (!project->has_project() || project->manifest()->id() != committed_project ||
      project->activation_generation() != committed_generation) return result;
  publish_assets_changed();
  if (events_ != nullptr) {
    for (auto &event : previous_scenes) {
      if (!project->has_project() || project->manifest()->id() != committed_project ||
          project->activation_generation() != committed_generation) break;
      (void)QuaderEventsApiInternalAccess::publish(*events_, std::move(event));
    }
  }
  return result;
}

} // namespace quader
