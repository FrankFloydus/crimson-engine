////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_texture_catalog.hpp"

#include "core/qstr.hpp"
#include "io/project/project_material_catalog.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <unordered_map>

namespace quader {

bool project_asset_path_visible(const ProjectRelativePath &path) {
  if (!path.valid()) {
    return false;
  }
  for (const auto &component : path.native_path()) {
    const auto text = component.native();
    if (!text.empty() && text.front() == '.') {
      return false;
    }
  }
  return QStr::to_ascii_lower(path.native_path().extension().string()) != ".qproj";
}

bool project_texture_extension_supported(const std::filesystem::path &path) {
  static constexpr std::array<std::string_view, 18> kExtensions{
      ".png", ".jpg", ".jpeg", ".bmp", ".dib", ".gif", ".pbm", ".pgm",
      ".ppm", ".xbm", ".xpm", ".tga", ".webp", ".tif", ".tiff", ".ico",
      ".cur", ".jfif"};
  const auto extension = QStr::to_ascii_lower(path.extension().string());
  return std::ranges::find(kExtensions, extension) != kExtensions.end();
}

ProjectIoResult<ProjectManifest> ProjectTextureCatalog::discover(
    const ProjectFileSystem &file_system, const ProjectManifest &manifest,
    const ProjectMaterialCatalog &materials) {
  if (!file_system.valid() || !manifest.id().valid()) {
    return ProjectIoResult<ProjectManifest>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Cannot discover textures without a project workspace.");
  }
  auto scan_file_system = file_system.begin_resolution_batch();
  ProjectManifest candidate = manifest;
  std::map<std::wstring, ProjectTextureCatalogEntry> discovered;
  std::unordered_map<ProjectAssetId, ProjectRelativePath, ProjectAssetIdHash>
      legacy_paths;
  const auto fail = [](const ProjectIoError &error) {
    return ProjectIoResult<ProjectManifest>::failure(
        error.code, error.message, error.path);
  };
  for (const auto &entry : materials.entries()) {
    const auto *standard = entry.material.standard_material();
    if (standard == nullptr || manifest.is_material_deleted(entry.material.id)) {
      continue;
    }
    for (const auto slot_kind : {StandardMaterialTextureSlot::Albedo,
                                 StandardMaterialTextureSlot::Roughness,
                                 StandardMaterialTextureSlot::Metallic,
                                 StandardMaterialTextureSlot::Normal}) {
      const auto *slot = standard->texture(slot_kind);
      if (slot == nullptr || !slot->has_value() ||
          manifest.is_texture_deleted((*slot)->id) ||
          manifest.find_texture((*slot)->id) != nullptr) {
        continue;
      }
      auto path = ProjectRelativePath::parse_portable((*slot)->project_relative_path);
      if (!path.ok()) {
        return fail(path.error());
      }
      if (!project_asset_path_visible(path.value())) {
        return ProjectIoResult<ProjectManifest>::failure(
            ProjectIoErrorCode::InvalidRelativePath,
            "Texture reference points into project metadata.",
            path.value().native_path());
      }
      auto resolved = scan_file_system.resolve(path.value());
      if (!resolved.ok()) {
        return fail(resolved.error());
      }
      const auto [existing, inserted] = legacy_paths.emplace((*slot)->id, path.value());
      if (!inserted && existing->second.portable_case_key() != path.value().portable_case_key()) {
        return ProjectIoResult<ProjectManifest>::failure(
            ProjectIoErrorCode::DuplicateAsset,
            "Legacy texture UUID identifies multiple project files: " +
                std::string((*slot)->id.str()) + ".", path.value().native_path());
      }
      auto [group, new_path] = discovered.try_emplace(
          path.value().portable_case_key(), ProjectTextureCatalogEntry{
              .id = (*slot)->id, .path = path.value(), .aliases = {}});
      if (!new_path && group->second.id != (*slot)->id &&
          std::ranges::find(group->second.aliases, (*slot)->id) == group->second.aliases.end()) {
        group->second.aliases.push_back((*slot)->id);
      }
    }
  }
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      file_system.root(), std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    const auto &entry = *iterator;
    auto lexical = ProjectRelativePath::parse(
        entry.path().lexically_relative(file_system.root()));
    if (!lexical.ok()) {
      return fail(lexical.error());
    }
    if (!project_asset_path_visible(lexical.value())) {
      iterator.disable_recursion_pending();
      continue;
    }
    const bool directory = entry.is_directory(error);
    if (error) {
      break;
    }
    if (directory) {
      auto physical = scan_file_system.resolve_physical_location(lexical.value());
      if (!physical.ok()) {
        return fail(physical.error());
      }
      if (physical.value() != entry.path().lexically_normal()) {
        return ProjectIoResult<ProjectManifest>::failure(
            ProjectIoErrorCode::PathCaseCollision,
            "Asset folders cannot be symbolic links or directory aliases.", entry.path());
      }
      continue;
    }
    if (!project_texture_extension_supported(entry.path())) {
      continue;
    }
    auto resolved = scan_file_system.resolve_existing_file(lexical.value());
    if (!resolved.ok()) {
      return fail(resolved.error());
    }
    if (candidate.find_texture(lexical.value()) == nullptr) {
      const auto key = lexical.value().portable_case_key();
      discovered.try_emplace(key,
                             ProjectTextureCatalogEntry{
                                 .id = ProjectAssetId::generate(),
                                 .path = lexical.take_value(), .aliases = {}});
    }
  }
  if (error) {
    return ProjectIoResult<ProjectManifest>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not enumerate project textures: " + error.message(), file_system.root());
  }
  for (auto &[key, entry] : discovered) {
    (void)key;
    if (const auto *existing = candidate.find_texture(entry.path)) {
      ProjectTextureCatalogEntry merged = *existing;
      merged.aliases.push_back(entry.id);
      merged.aliases.insert(merged.aliases.end(), entry.aliases.begin(), entry.aliases.end());
      std::sort(merged.aliases.begin(), merged.aliases.end());
      merged.aliases.erase(std::unique(merged.aliases.begin(), merged.aliases.end()), merged.aliases.end());
      const auto removed = candidate.unregister_texture(existing->id);
      if (!removed.ok()) {
        return fail(removed.error());
      }
      entry = std::move(merged);
    }
    const auto registered = candidate.register_texture(std::move(entry));
    if (!registered.ok()) {
      return fail(registered.error());
    }
  }
  std::vector<ProjectRelativePath> paths;
  std::vector<ProjectRelativePath> existing_paths;
  paths.reserve(candidate.textures().size());
  for (const auto &entry : candidate.textures()) {
    paths.push_back(entry.path);
    const auto existing = scan_file_system.resolve_existing_file(entry.path);
    if (existing.ok()) {
      existing_paths.push_back(entry.path);
    } else if (existing.error().code != ProjectIoErrorCode::FileNotFound) {
      return fail(existing.error());
    }
  }
  const auto validated = scan_file_system.validate_no_path_aliases(paths);
  if (!validated.ok()) {
    return fail(validated.error());
  }
  const auto physical = validate_project_asset_file_aliases(scan_file_system, existing_paths);
  if (!physical.ok()) {
    return fail(physical.error());
  }
  return ProjectIoResult<ProjectManifest>::success(std::move(candidate));
}

} // namespace quader
