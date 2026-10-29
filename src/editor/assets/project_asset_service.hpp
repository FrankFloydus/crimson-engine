////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_asset_resolver.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {

class ProjectSession;

/// One physical project entry; only scene entries use scene_id.
struct ProjectAssetEntry final {
  ProjectAssetKind kind = ProjectAssetKind::Unknown;
  ProjectRelativePath path;
  std::string name;
  SceneId scene_id;
  ProjectAssetId asset_id;
  std::uint64_t content_revision = 0u;
};

/// Retained catalog stamp also used to reject stale clipboard operations.
struct ProjectAssetSnapshot final {
  ProjectId project_id;
  std::uint64_t revision = 0u;
  std::vector<ProjectAssetEntry> entries;
};

/// Reports a committed asset operation and the new/relocated top-level paths.
struct ProjectAssetOperationResult final {
  bool ok = false;
  bool changed = false;
  std::string message;
  std::vector<ProjectRelativePath> paths;
  std::vector<std::string> warnings;
};

/// Counts physical discovery so retained browser reads can verify their gates.
struct ProjectAssetDiscoveryWorkStats final {
  std::uint64_t scans = 0u;
  std::uint64_t inspected_entries = 0u;
  std::uint64_t metadata_checks = 0u;
};

/// Accepted physical roots, shared with additional project asset domains.
enum class ProjectAssetMutationKind { Move, Copy, Delete };
struct ProjectAssetPathMapping final {
  ProjectRelativePath source;
  std::optional<ProjectRelativePath> destination;
};
struct ProjectAssetMutation final {
  ProjectAssetMutationKind kind = ProjectAssetMutationKind::Move;
  std::vector<ProjectAssetPathMapping> paths;
};
/// An existing metadata replacement, or a copied file's identity rewrite.
/// The filesystem owner checks exact baseline bytes before publication and
/// includes the replacement in its existing recovery journal.
struct ProjectAssetMutationWrite final {
  ProjectRelativePath path;
  std::string before;
  std::string after;
  bool created_by_copy = false;
  bool create_new = false;
};
struct ProjectAssetMutationPreparation final {
  std::uint64_t revision = 0u;
  std::vector<ProjectAssetMutationWrite> writes;
  std::function<ProjectIoResult<void>()> validate;
  std::function<void()> committed;
};
using ProjectAssetMutationPrepare = std::function<
    ProjectIoResult<ProjectAssetMutationPreparation>(const ProjectAssetMutation &)>;

/// Owns mixed project folder discovery and transactional organization.
class ProjectAssetService final {
public:
  explicit ProjectAssetService(ProjectSession &project) : project_(&project) {}

  /// A retained read: physical IO occurs only after invalidation or refresh.
  [[nodiscard]] const ProjectIoResult<ProjectAssetSnapshot> &snapshot() const;
  [[nodiscard]] const ProjectIoResult<ProjectAssetSnapshot> &refresh() const;
  /// Discovers external changes without reparsing unchanged files or images.
  [[nodiscard]] const ProjectIoResult<ProjectAssetSnapshot> &refresh_if_changed() const;
  /// Freshly validates only selected roots and their descendants for deletion.
  [[nodiscard]] ProjectIoResult<ProjectAssetSnapshot>
  inspect(std::span<const ProjectRelativePath> roots) const;
  [[nodiscard]] std::uint64_t revision() const noexcept;
  [[nodiscard]] const ProjectAssetDiscoveryWorkStats &work_stats() const noexcept { return work_; }
  void invalidate() noexcept;
  /// Bind the active additional asset domain; clear before its owner closes.
  void set_mutation_prepare(ProjectAssetMutationPrepare prepare) {
    mutation_prepare_ = std::move(prepare);
  }
  [[nodiscard]] ProjectIoResult<ProjectAssetMutationPreparation>
  prepare_mutation(const ProjectAssetMutation &mutation) const;
  /// Repairs interrupted organization before project catalogs are opened.
  [[nodiscard]] static ProjectIoResult<void> recover_pending_operations(
      const std::filesystem::path &manifest_path);

  /// Empty parent/destination denotes the project root, never a rooted path.
  [[nodiscard]] ProjectAssetOperationResult create_folder(
      std::string_view parent, std::string_view name,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult create_material(
      std::string_view parent, std::string_view name = "Material",
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult create_scene(
      std::string_view parent, std::string_view name = "Scene",
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult import_textures(
      std::span<const std::filesystem::path> sources,
      std::string_view destination, ProjectId expected_project = {},
      std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult rename(
      const ProjectRelativePath &path, std::string_view name,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult move(
      std::span<const ProjectRelativePath> paths, std::string_view destination,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult copy(
      std::span<const ProjectRelativePath> paths, std::string_view destination,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);
  [[nodiscard]] ProjectAssetOperationResult duplicate(
      std::span<const ProjectRelativePath> paths,
      ProjectId expected_project = {}, std::uint64_t expected_revision = 0u);

private:
  friend class ProjectAssetDeletionService;
  friend class ProjectSession;
  /// Metadata hints fence passive discovery; explicit refresh still reads bytes.
  struct SourceStamp final {
    std::uint64_t entries = 0u;
    std::uint64_t folders = 0u;
    std::uint64_t materials = 0u;
    std::uint64_t textures = 0u;
    friend bool operator==(const SourceStamp &, const SourceStamp &) = default;
  };
  [[nodiscard]] std::optional<SourceStamp> inspect_source_stamp() const;
  /// Certifies only a catalog scan whose surrounding metadata stayed stable.
  void retain_refreshed_catalog_stamp(std::optional<SourceStamp> before) const;
  [[nodiscard]] ProjectIoResult<ProjectAssetSnapshot>
  scan(std::span<const ProjectRelativePath> roots = {}) const;
  [[nodiscard]] ProjectIoResult<ProjectAssetSnapshot> preflight(
      const ProjectId &expected_project, std::uint64_t expected_revision) const;
  [[nodiscard]] ProjectAssetOperationResult organize(
      std::span<const ProjectRelativePath> paths, std::string_view destination,
      bool clone, bool keep_parent, std::optional<std::string_view> name,
      const ProjectId &expected_project, std::uint64_t expected_revision);
  void retain_committed_changes(ProjectAssetSnapshot previous,
                                std::span<const ProjectRelativePath> removed,
                                std::span<const ProjectRelativePath> changed);

  ProjectSession *project_ = nullptr;
  ProjectAssetMutationPrepare mutation_prepare_;
  mutable std::optional<ProjectIoResult<ProjectAssetSnapshot>> cached_;
  mutable ProjectId cached_project_;
  mutable std::uint64_t cached_material_revision_ = 0u;
  mutable std::uint64_t cached_project_generation_ = 0u;
  mutable std::optional<SourceStamp> cached_source_stamp_;
  mutable std::optional<SourceStamp> verified_source_stamp_;
  mutable ProjectId verified_project_;
  mutable std::uint64_t verified_project_generation_ = 0u;
  mutable ProjectAssetDiscoveryWorkStats work_;
};

/// Shared physical folder validation for the asset browser and legacy callers.
[[nodiscard]] ProjectIoResult<std::filesystem::path>
resolve_project_asset_folder(const ProjectFileSystem &file_system,
                             std::string_view folder);

/// Excludes recovery, authored project metadata, and unsupported source files.
[[nodiscard]] bool is_project_asset_reserved_path(
    const ProjectRelativePath &path);

} // namespace quader
