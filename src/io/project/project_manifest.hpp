////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/release_features.hpp"
#include "io/project/project_file_system.hpp"
#include "io/project/project_types.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

inline constexpr std::string_view kQuaderProjectManifestFormat =
    "quader.project";
inline constexpr std::uint32_t kQuaderProjectManifestVersion = 5u;
inline constexpr std::uint32_t kOldestQuaderProjectManifestVersion = 1u;

/// Maps a persistent scene identity to its project-relative file location.
struct ProjectSceneCatalogEntry {
  SceneId id;
  ProjectRelativePath path;

  friend bool operator==(const ProjectSceneCatalogEntry &,
                         const ProjectSceneCatalogEntry &) = default;
};

/// Identifies one source image; aliases retain legacy material-slot identities.
struct ProjectTextureCatalogEntry {
  ProjectAssetId id;
  ProjectRelativePath path;
  std::vector<ProjectAssetId> aliases;

  friend bool operator==(const ProjectTextureCatalogEntry &,
                         const ProjectTextureCatalogEntry &) = default;
};

/// Owns persistent project metadata without embedding scene payloads.
class ProjectManifest final {
public:
  ProjectManifest() = default;

  [[nodiscard]] static ProjectIoResult<ProjectManifest>
  create(ProjectId id, std::string name);

  [[nodiscard]] static ProjectIoResult<ProjectManifest> hydrate(
      ProjectId id, std::string name,
      std::vector<ProjectSceneCatalogEntry> scenes,
      std::vector<ProjectRelativePath> open_scene_paths,
      std::optional<ProjectRelativePath> active_scene_path,
      std::vector<ProjectAssetId> deleted_material_ids = {},
      ProductRelease minimum_product_release = kInternalFullRelease,
      std::vector<ProjectTextureCatalogEntry> textures = {},
      std::vector<ProjectAssetId> deleted_texture_ids = {});

  [[nodiscard]] const ProjectId &id() const noexcept { return id_; }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }
  /// Source format read from disk; serialization always writes the current format.
  [[nodiscard]] std::uint32_t loaded_version() const noexcept {
    return loaded_version_;
  }
  /// Records a successful current-format save on the retained workspace candidate.
  void mark_current_format() noexcept {
    loaded_version_ = kQuaderProjectManifestVersion;
  }
  [[nodiscard]] ProductRelease minimum_product_release() const noexcept {
    return minimum_product_release_;
  }
  /// Raises the persisted requirement without ever downgrading existing data.
  void require_product_release(ProductRelease release) noexcept;
  [[nodiscard]] std::span<const ProjectSceneCatalogEntry> scenes() const {
    return scenes_;
  }
  [[nodiscard]] std::span<const ProjectRelativePath>
  open_scene_paths() const {
    return open_scene_paths_;
  }
  [[nodiscard]] const std::optional<ProjectRelativePath> &
  active_scene_path() const noexcept {
    return active_scene_path_;
  }
  /// Sorted persistent identities whose effective material is always Default.
  [[nodiscard]] std::span<const ProjectAssetId>
  deleted_material_ids() const noexcept {
    return deleted_material_ids_;
  }
  /// Tests permanent deletion without consulting paths or the material catalog.
  [[nodiscard]] bool
  is_material_deleted(const ProjectAssetId &material_id) const noexcept;
  /// Permanently records validated material identities in one atomic edit.
  [[nodiscard]] ProjectIoResult<void>
  mark_materials_deleted(std::span<const ProjectAssetId> material_ids);

  [[nodiscard]] std::span<const ProjectTextureCatalogEntry>
  textures() const noexcept { return textures_; }
  [[nodiscard]] const ProjectTextureCatalogEntry *
  find_texture(const ProjectAssetId &id) const;
  [[nodiscard]] const ProjectTextureCatalogEntry *
  find_texture(const ProjectRelativePath &path) const;
  [[nodiscard]] ProjectIoResult<void>
  register_texture(ProjectTextureCatalogEntry texture);
  [[nodiscard]] ProjectIoResult<void>
  relocate_texture(const ProjectAssetId &id, ProjectRelativePath path);
  /// Removes catalog membership without retiring the identity (transaction use).
  [[nodiscard]] ProjectIoResult<void>
  unregister_texture(const ProjectAssetId &id);
  /// Retires canonical identities and aliases, removing their catalog entries.
  [[nodiscard]] ProjectIoResult<void>
  mark_textures_deleted(std::span<const ProjectAssetId> texture_ids);
  [[nodiscard]] bool is_texture_deleted(const ProjectAssetId &id) const noexcept;
  [[nodiscard]] std::span<const ProjectAssetId>
  deleted_texture_ids() const noexcept { return deleted_texture_ids_; }
  /// Allocation-free stamp of exact texture identities, paths and retirements.
  [[nodiscard]] std::uint64_t texture_metadata_fingerprint() const noexcept;

  [[nodiscard]] const ProjectSceneCatalogEntry *
  find_scene(const SceneId &id) const;
  [[nodiscard]] const ProjectSceneCatalogEntry *
  find_scene(const ProjectRelativePath &path) const;

  [[nodiscard]] ProjectIoResult<void>
  register_scene(ProjectSceneCatalogEntry scene);
  [[nodiscard]] ProjectIoResult<void> unregister_scene(const SceneId &id);

  /**
   * Replaces saved tab order and active scene in one invariant-preserving edit.
   * Every supplied path must already exist in the scene catalog.
   */
  [[nodiscard]] ProjectIoResult<void> set_workspace(
      std::vector<ProjectRelativePath> open_scene_paths,
      std::optional<ProjectRelativePath> active_scene_path);

private:
  friend class ProjectManifestStore;
  void rebuild_scene_indexes();
  void rebuild_texture_indexes();

  ProjectId id_;
  std::string name_;
  std::uint32_t loaded_version_ = kQuaderProjectManifestVersion;
  ProductRelease minimum_product_release_ = kInternalFullRelease;
  std::vector<ProjectSceneCatalogEntry> scenes_;
  std::unordered_map<SceneId, std::size_t, SceneIdHash>
      scene_index_by_id_;
  std::unordered_map<std::wstring, std::size_t> scene_index_by_path_;
  std::vector<ProjectRelativePath> open_scene_paths_;
  std::optional<ProjectRelativePath> active_scene_path_;
  std::vector<ProjectAssetId> deleted_material_ids_;
  std::vector<ProjectTextureCatalogEntry> textures_;
  std::unordered_map<ProjectAssetId, std::size_t, ProjectAssetIdHash>
      texture_index_by_id_;
  std::unordered_map<std::wstring, std::size_t> texture_index_by_path_;
  std::vector<ProjectAssetId> deleted_texture_ids_;
};

/// Bundles a validated project filesystem with its loaded manifest.
struct ProjectWorkspaceStorage {
  ProjectFileSystem file_system;
  ProjectManifest manifest;
  std::filesystem::path manifest_path;
  /// Last loaded/committed disk metadata, unchanged by read-only discovery.
  std::uint64_t persisted_texture_metadata_fingerprint = 0u;
};

/// Creates, reads, validates, and atomically writes `.qproj` manifests.
class ProjectManifestStore final {
public:
  [[nodiscard]] static ProjectIoResult<ProjectWorkspaceStorage>
  create_in_empty_directory(const std::filesystem::path &root);

  [[nodiscard]] static ProjectIoResult<ProjectWorkspaceStorage>
  open(const std::filesystem::path &manifest_path);

  /// Reads validated manifest metadata before transaction recovery. Asset paths
  /// still require a regular open after recovery has restored their spelling.
  [[nodiscard]] static ProjectIoResult<ProjectWorkspaceStorage>
  open_metadata(const std::filesystem::path &manifest_path);

  [[nodiscard]] static ProjectIoResult<void>
  save_atomic(const ProjectWorkspaceStorage &workspace);

  [[nodiscard]] static ProjectIoResult<void> save_atomic(
      const ProjectFileSystem &file_system,
      const std::filesystem::path &manifest_path,
      const ProjectManifest &manifest);

  [[nodiscard]] static std::string serialize(const ProjectManifest &manifest);
  [[nodiscard]] static ProjectIoResult<ProjectManifest>
  deserialize(std::string_view text);
};

} // namespace quader
