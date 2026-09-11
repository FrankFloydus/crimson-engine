////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_material_document.hpp"
#include "io/project/project_types.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/// Identifies the exact on-disk source version inspected for dependencies.
struct ProjectAssetDependencySource final {
  ProjectRelativePath path;
  std::uint64_t byte_count = 0u;
  std::uint64_t content_hash = 0u;

  friend bool operator==(const ProjectAssetDependencySource &,
                         const ProjectAssetDependencySource &) = default;
};

/// Preserves saved and live provenance separately, including unsaved scene tabs.
struct ProjectSceneAssetDependencies final {
  SceneId scene_id;
  std::optional<SceneSessionId> session_id;
  std::optional<ProjectRelativePath> path;
  std::string display_name;
  bool live = false;
  bool dirty = false;
  std::uint64_t content_revision = 0u;
  std::vector<ProjectAssetId> material_ids;

  friend bool operator==(const ProjectSceneAssetDependencies &,
                         const ProjectSceneAssetDependencies &) = default;
};

/// One saved or pending texture dependency of a material slot.
struct ProjectMaterialTextureDependency final {
  StandardMaterialTextureSlot slot = StandardMaterialTextureSlot::Albedo;
  std::optional<ProjectAssetId> texture_id;
  std::optional<ProjectRelativePath> path;
  std::optional<std::filesystem::path> pending_source;

  friend bool operator==(const ProjectMaterialTextureDependency &,
                         const ProjectMaterialTextureDependency &) = default;
};

/// Saved material dependencies and material-draft dependencies remain distinct.
struct ProjectMaterialAssetDependencies final {
  ProjectAssetId id;
  std::optional<ProjectRelativePath> path;
  std::string name;
  bool draft = false;
  bool dirty = false;
  std::uint64_t content_revision = 0u;
  std::vector<ProjectMaterialTextureDependency> textures;

  friend bool operator==(const ProjectMaterialAssetDependencies &,
                         const ProjectMaterialAssetDependencies &) = default;
};

/// Immutable derived project graph; incomplete inspection forbids deletion.
struct ProjectAssetDependencySnapshot final {
  ProjectId project_id;
  std::uint64_t project_generation = 0u;
  std::uint64_t catalog_revision = 0u;
  std::uint64_t revision = 0u;
  bool complete = false;
  std::vector<std::string> errors;
  std::vector<ProjectSceneAssetDependencies> scenes;
  std::vector<ProjectMaterialAssetDependencies> materials;
  std::vector<ProjectAssetDependencySource> sources;
};

/// Counts expensive work so retained consumers can verify their revision gates.
struct ProjectAssetDependencyWorkStats final {
  std::uint64_t snapshot_build_count = 0u;
  std::uint64_t saved_scene_read_count = 0u;
  std::uint64_t live_scene_scan_count = 0u;
  std::uint64_t material_catalog_scan_count = 0u;
  std::uint64_t fingerprint_read_count = 0u;

  friend bool operator==(const ProjectAssetDependencyWorkStats &,
                         const ProjectAssetDependencyWorkStats &) = default;
};

} // namespace quader
