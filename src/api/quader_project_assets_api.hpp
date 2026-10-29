////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_materials_api.hpp"
#include "api/quader_standard_material_documents_api.hpp"
#include "io/project/project_types.hpp"
#include "io/project/project_asset_recycle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderEventsApi;
class QuaderProjectAssetsApiInternalAccess;

/// Asset families exposed by the project browser; folders have no UUID.
enum class QuaderProjectAssetKind : std::uint8_t {
  Folder,
  Scene,
  Material,
  Texture,
  File,
};

/// Immutable metadata for one physical asset or folder in the current project.
struct QuaderProjectAssetInfo {
  QuaderProjectAssetKind kind = QuaderProjectAssetKind::Folder;
  ProjectAssetId asset_id;
  SceneId scene_id;
  std::string name;
  std::string project_relative_path;
  std::uint64_t content_revision = 0u;
};

/// Cheap owner observation used before copying the browser catalog.
struct QuaderProjectAssetRevision {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t revision = 0u;

  friend bool operator==(const QuaderProjectAssetRevision &,
                         const QuaderProjectAssetRevision &) = default;
};

/// Complete supported-asset projection; an empty folder path means project root.
struct QuaderProjectAssetSnapshot : QuaderProjectAssetRevision {
  std::vector<QuaderProjectAssetInfo> assets;
  std::vector<std::string> folders;
};

/// Saved material evaluation, independent of the material editor's live draft.
struct QuaderProjectMaterialThumbnailInfo {
  ProjectAssetId material_id;
  std::uint64_t content_revision = 0u;
  QuaderStandardMaterialDefinition material;
  std::array<std::optional<QuaderStandardMaterialPreviewTexture>, 4> textures;
  std::array<std::string, 4> texture_diagnostics;
};

/// Selects the public editor and payload family for a project material.
enum class QuaderProjectMaterialKind : std::uint8_t {
  Standard,
  Graph,
};

/// Public catalog metadata for one validated project material.
struct QuaderProjectMaterialInfo {
  ProjectAssetId id;
  std::string name;
  QuaderProjectMaterialKind kind = QuaderProjectMaterialKind::Standard;
  std::string project_relative_path;
  std::uintmax_t qmat_size_bytes = 0u;
  std::array<float, 3> base_color{1.0f, 1.0f, 1.0f};
  std::uint64_t content_revision = 0u;
};

/// Immutable encoded albedo preview returned by the project-assets API.
struct QuaderProjectMaterialPreview {
  std::vector<std::byte> encoded_bytes;
  std::uint64_t content_revision = 0u;
};

/// Identifies the semantic family presented by material catalog surfaces.
enum class QuaderMaterialPresentationKind : std::uint8_t {
  Embedded,
  Standard,
  Graph,
};

/// Cheap owner-derived stamp for the complete material presentation catalog.
struct QuaderMaterialPresentationRevision {
  std::uint64_t project_activation_generation = 0u;
  std::uint64_t project_material_revision = 0u;
  std::uint64_t project_texture_revision = 0u;
  std::uint64_t active_scene_id = 0u;
  std::uint64_t embedded_material_revision = 0u;

  friend bool operator==(const QuaderMaterialPresentationRevision &,
                         const QuaderMaterialPresentationRevision &) = default;
};

/// Renderer- and toolkit-neutral presentation of one available material.
struct QuaderMaterialPresentationItem final {
  QuaderMaterialReference reference;
  QuaderMaterialPresentationKind kind =
      QuaderMaterialPresentationKind::Embedded;
  std::string name;
  std::string project_relative_path;
  std::uintmax_t qmat_size_bytes = 0u;
  std::vector<std::byte> encoded_albedo;
  std::array<float, 3> fallback_color{1.0f, 1.0f, 1.0f};
  std::uint64_t content_revision = 0u;
  bool available = true;
  bool editable = false;
};

/// One immutable owner-derived catalog consumed by all retained UI surfaces.
struct QuaderMaterialPresentationSnapshot {
  QuaderMaterialPresentationRevision revision;
  std::vector<QuaderMaterialPresentationItem> materials;
  std::vector<std::string> material_folders;
};

/// Validated full-resolution albedo data for one project material.
struct QuaderProjectMaterialTextureInfo {
  ProjectAssetId material_id;
  ProjectAssetId texture_id;
  std::string name;
  std::string project_relative_path;
  std::filesystem::path absolute_path;
  std::string format;
  std::uintmax_t encoded_size_bytes = 0u;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  bool has_alpha_channel = false;
  std::shared_ptr<const std::vector<std::byte>> rgba8;
  std::uint64_t content_revision = 0u;
};

/// One saved or live scene record affected by a material deletion.
struct QuaderProjectSceneMaterialDependencies {
  SceneId scene_id;
  std::optional<SceneSessionId> session_id;
  std::string project_relative_path;
  std::string name;
  bool live = false;
  bool dirty = false;
  std::vector<ProjectAssetId> material_ids;
};

/// Reviewed impact of recycling one project material folder.
struct QuaderMaterialFolderDeletionPreview {
  std::string project_relative_path;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> material_names;
  std::vector<QuaderProjectSceneMaterialDependencies> affected_scenes;
  std::vector<std::string> errors;
  std::size_t folder_count = 0u;
  bool discards_material_draft = false;
};

/// Reviewed impact of recycling an explicit material selection.
struct QuaderMaterialDeletionPreview {
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<ProjectAssetId> material_ids;
  std::vector<std::string> material_names;
  std::vector<QuaderProjectSceneMaterialDependencies> affected_scenes;
  std::vector<std::string> errors;
  bool discards_material_draft = false;
};

/// A material's saved or pending texture input, independent of any renderer.
struct QuaderProjectMaterialTextureDependency {
  std::string slot;
  std::optional<ProjectAssetId> texture_id;
  std::string project_relative_path;
  std::optional<std::filesystem::path> pending_source;
};

/// Material-to-texture edges for either its saved asset or open draft.
struct QuaderProjectMaterialDependencies {
  ProjectAssetId id;
  std::string project_relative_path;
  std::string name;
  bool draft = false;
  bool dirty = false;
  std::vector<QuaderProjectMaterialTextureDependency> textures;
};

/// Revision-bound impact of recycling a mixed asset or folder selection.
struct QuaderProjectAssetDeletionPreview {
  ProjectId project_id;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<std::string> paths;
  std::vector<SceneId> scene_ids;
  std::vector<ProjectAssetId> material_ids;
  std::vector<ProjectAssetId> texture_ids;
  std::vector<QuaderProjectSceneMaterialDependencies> affected_scenes;
  std::vector<QuaderProjectMaterialDependencies> affected_materials;
  std::vector<SceneSessionId> dirty_scene_sessions;
  std::vector<std::string> errors;
  bool discards_material_draft = false;
};

/// Immutable project-wide dependency information; errors prevent safe deletion.
struct QuaderProjectMaterialDependencySnapshot {
  ProjectId project_id;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<std::string> errors;
  std::vector<QuaderProjectSceneMaterialDependencies> scenes;
  std::vector<QuaderProjectMaterialDependencies> materials;
};

/// Cheap retained stamp for consumers that only need dependency invalidation.
struct QuaderProjectMaterialDependencyRevision {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t revision = 0u;
  bool complete = false;

  friend bool operator==(
      const QuaderProjectMaterialDependencyRevision &,
      const QuaderProjectMaterialDependencyRevision &) = default;
};

/// One source that prevents a reliable project-wide unused classification.
struct QuaderProjectMaterialUsageIssue final {
  SceneId scene_id;
  std::optional<SceneSessionId> session_id;
  std::string project_relative_path;
  std::string scene_name;
  std::string message;

  friend bool operator==(const QuaderProjectMaterialUsageIssue &,
                         const QuaderProjectMaterialUsageIssue &) = default;
};

/// Actual assigned face materials; live documents replace their saved versions.
struct QuaderProjectMaterialUsageSnapshot {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t revision = 0u;
  bool complete = false;
  // Embedded references are scoped to the currently active scene catalog.
  std::vector<QuaderMaterialReference> used_materials;
  std::vector<QuaderProjectMaterialUsageIssue> issues;
};

/// Cheap owner-gated usage stamp; unchanged reads perform no entity or file scans.
struct QuaderProjectMaterialUsageRevision {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t revision = 0u;
  bool complete = false;

  friend bool operator==(const QuaderProjectMaterialUsageRevision &,
                         const QuaderProjectMaterialUsageRevision &) = default;
};

/// One project material created from an imported texture source.
struct QuaderProjectMaterialImportSuccess {
  std::filesystem::path source_path;
  QuaderProjectMaterialInfo material;
};

/// One independently rejected texture source from a completed import batch.
struct QuaderProjectMaterialImportFailure {
  std::filesystem::path source_path;
  std::string error_message;
};

/// Per-source outcome of a completed texture-as-material import batch.
struct QuaderProjectMaterialImportReport {
  std::vector<QuaderProjectMaterialImportSuccess> imported;
  std::vector<QuaderProjectMaterialImportFailure> failed;
};

/// Public project-asset intent surface; standalone sessions are unavailable.
class QuaderProjectAssetsApi final {
public:
  explicit QuaderProjectAssetsApi(QuaderApiSession session);

  [[nodiscard]] QuaderApiResult<QuaderProjectAssetRevision>
  asset_revision() const;
  [[nodiscard]] QuaderApiResult<QuaderProjectAssetSnapshot>
  assets(bool refresh_files = false) const;
  /// Discovers ordinary external changes while retaining unchanged asset content.
  [[nodiscard]] QuaderApiResult<QuaderProjectAssetSnapshot>
  assets_if_changed() const;
  [[nodiscard]] QuaderApiResult<QuaderProjectAssetInfo> create_scene_asset(
      std::string_view folder, std::string_view name = "New Scene");
  [[nodiscard]] QuaderApiResult<QuaderProjectAssetInfo> create_material_asset(
      std::string_view folder, std::string_view name = "New Material");
  QuaderApiVoidResult create_folder(std::string_view parent,
                                   std::string_view name);
  QuaderApiVoidResult rename_asset(std::string_view path,
                                  std::string_view name,
                                  ProjectId expected_project = {},
                                  std::uint64_t expected_revision = 0u);
  QuaderApiVoidResult move_assets(
      std::span<const std::string> paths, std::string_view destination,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectAssetInfo>> copy_assets(
      std::span<const std::string> paths, std::string_view destination,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectAssetInfo>>
  duplicate_assets(std::span<const std::string> paths,
                   ProjectId expected_project = {},
                   std::uint64_t expected_revision = 0u);
  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectAssetInfo>>
  import_textures(std::span<const std::filesystem::path> sources,
                  std::string_view destination);
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialThumbnailInfo>
  material_thumbnail_info(ProjectAssetId material_id) const;
  [[nodiscard]] QuaderApiResult<QuaderProjectAssetDeletionPreview>
  preview_delete_assets(std::span<const std::string> paths) const;
  QuaderApiVoidResult delete_assets(
      std::span<const std::string> paths, std::uint64_t preview_revision,
      std::span<const SceneSessionId> approved_dirty_scenes = {});

  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
  materials() const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialPreview>
  material_preview(ProjectAssetId material_id) const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialTextureInfo>
  material_albedo_texture_info(ProjectAssetId material_id) const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialTextureInfo>
  texture_info(ProjectAssetId texture_id) const;
  [[nodiscard]] QuaderApiResult<QuaderMaterialPresentationRevision>
  material_presentation_revision() const;
  [[nodiscard]] QuaderApiResult<QuaderMaterialPresentationSnapshot>
  material_presentations() const;
  /// Completes valid sources independently and reports per-file rejections.
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialImportReport>
  import_textures_as_materials(
      std::span<const std::filesystem::path> source_paths,
      std::string_view destination_folder = "Materials");

  QuaderApiVoidResult create_material_folder(std::string_view parent_folder,
                                             std::string_view name);
  QuaderApiVoidResult rename_material_folder(std::string_view folder,
                                             std::string_view name);
  QuaderApiVoidResult move_material_folder(std::string_view folder,
                                           std::string_view destination_folder);
  QuaderApiVoidResult move_materials(std::span<const ProjectAssetId> material_ids,
                                     std::string_view destination_folder);
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialInfo>
  rename_material(ProjectAssetId material_id, std::string_view new_name);
  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
  duplicate_materials(std::span<const ProjectAssetId> material_ids);
  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
  copy_materials(std::span<const ProjectAssetId> material_ids,
                 std::string_view destination_folder);
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialDependencyRevision>
  material_dependency_revision() const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialDependencySnapshot>
  material_dependencies(bool refresh_files = false) const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialUsageRevision>
  material_usage_revision() const;
  /// Inspection failures are source issues, not failed material assignments.
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialUsageSnapshot>
  material_usage(bool refresh_files = false) const;
  [[nodiscard]] QuaderApiResult<QuaderMaterialFolderDeletionPreview>
  preview_delete_material_folder(std::string_view folder) const;
  /// Rejects an obsolete preview instead of applying an unreviewed deletion.
  QuaderApiVoidResult delete_material_folder(std::string_view folder,
                                             std::uint64_t expected_revision);
  [[nodiscard]] QuaderApiResult<QuaderMaterialDeletionPreview>
  preview_delete_materials(std::span<const ProjectAssetId> material_ids) const;
  /// Rejects an obsolete preview instead of applying an unreviewed deletion.
  QuaderApiVoidResult delete_materials(
      std::span<const ProjectAssetId> material_ids,
      std::uint64_t expected_revision);

  void rebind_session(QuaderApiSession session) { session_ = session; }

private:
  void attach_events(QuaderEventsApi &events) { events_ = &events; }
  void publish_assets_changed();

  friend class QuaderProjectAssetsApiInternalAccess;
  QuaderApiSession session_;
  QuaderEventsApi *events_ = nullptr;
  ProjectAssetRecyclePort recycle_;
  std::function<void(QuaderApiSession)> binding_changed_;
};

} // namespace quader
