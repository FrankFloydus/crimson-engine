////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_manifest.hpp"

#include "assets/default_material_catalog.hpp"
#include "core/qstr.hpp"
#include "io/project/project_texture_catalog.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/shared_file_read.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cwctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::uintmax_t kMaximumManifestBytes = 4u * 1024u * 1024u;
constexpr std::size_t kMaximumManifestScenes = 100'000u;
constexpr std::size_t kMaximumDeletedMaterialIds = 100'000u;
constexpr std::size_t kMaximumTextureIdentities = 100'000u;
constexpr std::size_t kMaximumPortablePathBytes = 32'768u;

bool non_whitespace_name(std::string_view value) {
  return std::any_of(value.begin(), value.end(), [](char character) {
    return !std::isspace(static_cast<unsigned char>(character));
  });
}

bool same_portable_path(const ProjectRelativePath &left,
                        const ProjectRelativePath &right) {
  return left.portable_case_key() == right.portable_case_key();
}

bool has_qdr_extension(const ProjectRelativePath &path) {
  return QStr::to_ascii_lower(path.native_path().extension().string()) ==
         ".qdr";
}

ProjectIoResult<ProjectManifest> malformed_manifest(std::string message) {
  return ProjectIoResult<ProjectManifest>::failure(
      ProjectIoErrorCode::ManifestMalformed, std::move(message));
}

std::filesystem::path normalized_root_filename(
    const std::filesystem::path &root) {
  std::filesystem::path filename = root.filename();
  if (filename.empty()) {
    filename = root.parent_path().filename();
  }
  return filename;
}

std::filesystem::path expected_manifest_path(
    const ProjectFileSystem &file_system) {
  std::filesystem::path root_name =
      normalized_root_filename(file_system.root());
  root_name += ".qproj";
  return file_system.root() / root_name;
}

std::string utf8_project_name(const std::filesystem::path &root) {
  const auto name = normalized_root_filename(root).u8string();
  return {reinterpret_cast<const char *>(name.data()), name.size()};
}

bool same_portable_filesystem_path(const std::filesystem::path &left,
                                   const std::filesystem::path &right) {
  std::wstring left_key = left.lexically_normal().generic_wstring();
  std::wstring right_key = right.lexically_normal().generic_wstring();
  std::transform(left_key.begin(), left_key.end(), left_key.begin(),
                 [](wchar_t value) {
                   return static_cast<wchar_t>(std::towlower(value));
                 });
  std::transform(right_key.begin(), right_key.end(), right_key.begin(),
                 [](wchar_t value) {
                   return static_cast<wchar_t>(std::towlower(value));
                 });
  return left_key == right_key;
}

ProjectIoResult<void> validate_manifest_for_filesystem(
    const ProjectFileSystem &file_system, const ProjectManifest &manifest) {
  std::vector<ProjectRelativePath> paths;
  paths.reserve(manifest.scenes().size() + manifest.textures().size());
  for (const ProjectSceneCatalogEntry &scene : manifest.scenes()) {
    paths.push_back(scene.path);
  }
  for (const ProjectTextureCatalogEntry &texture : manifest.textures()) {
    paths.push_back(texture.path);
  }
  return file_system.validate_no_path_aliases(paths);
}

} // namespace

ProjectIoResult<ProjectManifest> ProjectManifest::create(ProjectId id,
                                                         std::string name) {
  if (!id.valid()) {
    return ProjectIoResult<ProjectManifest>::failure(
        ProjectIoErrorCode::InvalidUuid,
        "Project manifest requires a valid project UUID.");
  }
  if (!non_whitespace_name(name)) {
    return ProjectIoResult<ProjectManifest>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project manifest requires a non-empty project name.");
  }

  ProjectManifest manifest;
  manifest.id_ = std::move(id);
  manifest.name_ = std::move(name);
  manifest.minimum_product_release_ = current_product_release();
  return ProjectIoResult<ProjectManifest>::success(std::move(manifest));
}

ProjectIoResult<ProjectManifest> ProjectManifest::hydrate(
    ProjectId id, std::string name,
    std::vector<ProjectSceneCatalogEntry> scenes,
    std::vector<ProjectRelativePath> open_scene_paths,
    std::optional<ProjectRelativePath> active_scene_path,
    std::vector<ProjectAssetId> deleted_material_ids,
    ProductRelease minimum_product_release,
    std::vector<ProjectTextureCatalogEntry> textures,
    std::vector<ProjectAssetId> deleted_texture_ids) {
  if (minimum_product_release == 0u) {
    return malformed_manifest("Minimum product release must be nonzero.");
  }
  ProjectIoResult<ProjectManifest> created = create(std::move(id), std::move(name));
  if (!created.ok()) {
    return created;
  }
  ProjectManifest manifest = created.take_value();
  manifest.minimum_product_release_ = minimum_product_release;
  const auto retired = manifest.mark_textures_deleted(deleted_texture_ids);
  if (!retired.ok()) {
    return ProjectIoResult<ProjectManifest>::failure(
        retired.error().code, retired.error().message, retired.error().path);
  }
  for (auto &texture : textures) {
    const auto registered = manifest.register_texture(std::move(texture));
    if (!registered.ok()) {
      return ProjectIoResult<ProjectManifest>::failure(
          registered.error().code, registered.error().message,
          registered.error().path);
    }
  }
  for (ProjectSceneCatalogEntry &scene : scenes) {
    ProjectIoResult<void> registered =
        manifest.register_scene(std::move(scene));
    if (!registered.ok()) {
      return ProjectIoResult<ProjectManifest>::failure(
          registered.error().code, registered.error().message,
          registered.error().path);
    }
  }
  ProjectIoResult<void> workspace = manifest.set_workspace(
      std::move(open_scene_paths), std::move(active_scene_path));
  if (!workspace.ok()) {
    return ProjectIoResult<ProjectManifest>::failure(
        workspace.error().code, workspace.error().message,
        workspace.error().path);
  }
  const ProjectIoResult<void> deleted_materials =
      manifest.mark_materials_deleted(deleted_material_ids);
  if (!deleted_materials.ok()) {
    return ProjectIoResult<ProjectManifest>::failure(
        deleted_materials.error().code, deleted_materials.error().message,
        deleted_materials.error().path);
  }
  return ProjectIoResult<ProjectManifest>::success(std::move(manifest));
}

void ProjectManifest::require_product_release(ProductRelease release) noexcept {
  minimum_product_release_ = std::max(minimum_product_release_, release);
}

const ProjectSceneCatalogEntry *
ProjectManifest::find_scene(const SceneId &id) const {
  const auto found = scene_index_by_id_.find(id);
  return found == scene_index_by_id_.end()
             ? nullptr
             : &scenes_[found->second];
}

const ProjectSceneCatalogEntry *
ProjectManifest::find_scene(const ProjectRelativePath &path) const {
  const auto found = scene_index_by_path_.find(path.portable_case_key());
  return found == scene_index_by_path_.end()
             ? nullptr
             : &scenes_[found->second];
}

ProjectIoResult<void>
ProjectManifest::register_scene(ProjectSceneCatalogEntry scene) {
  if (!scene.id.valid()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidUuid,
        "Scene catalog entry requires a valid scene UUID.");
  }
  if (!scene.path.valid() || !has_qdr_extension(scene.path)) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Scene catalog entry must use a project-relative .qdr path.",
        scene.path.native_path());
  }
  if (find_scene(scene.id) != nullptr) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::DuplicateScene,
        "Scene catalog already contains this scene UUID.",
        scene.path.native_path());
  }
  if (find_scene(scene.path) != nullptr) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::PathCaseCollision,
        "Scene catalog already contains this path under portable casing.",
        scene.path.native_path());
  }

  const std::size_t index = scenes_.size();
  scene_index_by_id_.emplace(scene.id, index);
  scene_index_by_path_.emplace(scene.path.portable_case_key(), index);
  scenes_.push_back(std::move(scene));
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> ProjectManifest::unregister_scene(const SceneId &id) {
  const auto indexed = scene_index_by_id_.find(id);
  if (indexed == scene_index_by_id_.end()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::SceneNotFound,
        "Scene catalog does not contain this scene UUID.");
  }

  const auto found =
      scenes_.begin() + static_cast<std::ptrdiff_t>(indexed->second);
  const ProjectRelativePath removed_path = found->path;
  scenes_.erase(found);
  rebuild_scene_indexes();
  open_scene_paths_.erase(
      std::remove_if(open_scene_paths_.begin(), open_scene_paths_.end(),
                     [&](const ProjectRelativePath &path) {
                       return same_portable_path(path, removed_path);
                     }),
      open_scene_paths_.end());
  if (active_scene_path_.has_value() &&
      same_portable_path(*active_scene_path_, removed_path)) {
    if (open_scene_paths_.empty()) {
      active_scene_path_.reset();
    } else {
      active_scene_path_ = open_scene_paths_.back();
    }
  }
  return ProjectIoResult<void>::success();
}

void ProjectManifest::rebuild_scene_indexes() {
  scene_index_by_id_.clear();
  scene_index_by_path_.clear();
  scene_index_by_id_.reserve(scenes_.size());
  scene_index_by_path_.reserve(scenes_.size());
  for (std::size_t index = 0u; index < scenes_.size(); ++index) {
    scene_index_by_id_.emplace(scenes_[index].id, index);
    scene_index_by_path_.emplace(
        scenes_[index].path.portable_case_key(), index);
  }
}

ProjectIoResult<void> ProjectManifest::set_workspace(
    std::vector<ProjectRelativePath> open_scene_paths,
    std::optional<ProjectRelativePath> active_scene_path) {
  std::unordered_set<std::wstring> seen;
  std::vector<ProjectRelativePath> canonical_paths;
  canonical_paths.reserve(open_scene_paths.size());
  for (const ProjectRelativePath &path : open_scene_paths) {
    const ProjectSceneCatalogEntry *scene = find_scene(path);
    if (scene == nullptr) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::SceneNotFound,
          "Open scene path is not present in the scene catalog.",
          path.native_path());
    }
    if (!seen.insert(scene->path.portable_case_key()).second) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidWorkspaceState,
          "Open scene tab order contains a duplicate path.",
          scene->path.native_path());
    }
    canonical_paths.push_back(scene->path);
  }

  std::optional<ProjectRelativePath> canonical_active;
  if (active_scene_path.has_value()) {
    const ProjectSceneCatalogEntry *scene = find_scene(*active_scene_path);
    if (scene == nullptr ||
        !seen.contains(scene->path.portable_case_key())) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidWorkspaceState,
          "Active scene must be present in the saved open tab order.",
          active_scene_path->native_path());
    }
    canonical_active = scene->path;
  }

  open_scene_paths_ = std::move(canonical_paths);
  active_scene_path_ = std::move(canonical_active);
  return ProjectIoResult<void>::success();
}

bool ProjectManifest::is_material_deleted(
    const ProjectAssetId &material_id) const noexcept {
  return std::binary_search(deleted_material_ids_.begin(),
                            deleted_material_ids_.end(), material_id);
}

ProjectIoResult<void> ProjectManifest::mark_materials_deleted(
    std::span<const ProjectAssetId> material_ids) {
  for (const ProjectAssetId &id : material_ids) {
    if (!id.valid()) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidUuid,
          "Deleted project materials require valid UUIDs.");
    }
    if (id.str() == kDefaultProjectMaterialId) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::InvalidArgument,
          "The reserved Default Material cannot be deleted.");
    }
  }
  std::vector<ProjectAssetId> candidate = deleted_material_ids_;
  candidate.insert(candidate.end(), material_ids.begin(), material_ids.end());
  std::sort(candidate.begin(), candidate.end());
  candidate.erase(std::unique(candidate.begin(), candidate.end()),
                  candidate.end());
  if (candidate.size() > kMaximumDeletedMaterialIds) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Deleted project material identities exceed the manifest size limit.");
  }
  deleted_material_ids_ = std::move(candidate);
  return ProjectIoResult<void>::success();
}

const ProjectTextureCatalogEntry *
ProjectManifest::find_texture(const ProjectAssetId &id) const {
  const auto found = texture_index_by_id_.find(id);
  return found == texture_index_by_id_.end() ? nullptr : &textures_[found->second];
}

const ProjectTextureCatalogEntry *
ProjectManifest::find_texture(const ProjectRelativePath &path) const {
  const auto found = texture_index_by_path_.find(path.portable_case_key());
  return found == texture_index_by_path_.end() ? nullptr
                                             : &textures_[found->second];
}

bool ProjectManifest::is_texture_deleted(const ProjectAssetId &id) const noexcept {
  return std::binary_search(deleted_texture_ids_.begin(),
                            deleted_texture_ids_.end(), id);
}

ProjectIoResult<void>
ProjectManifest::register_texture(ProjectTextureCatalogEntry texture) {
  if (!project_asset_path_visible(texture.path) ||
      !project_texture_extension_supported(texture.path.native_path())) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidRelativePath,
                                          "Texture requires a project path.");
  }
  if (find_texture(texture.path) != nullptr || find_scene(texture.path) != nullptr) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::PathCaseCollision,
        "Texture catalog already contains this path.", texture.path.native_path());
  }
  std::sort(texture.aliases.begin(), texture.aliases.end());
  if (std::adjacent_find(texture.aliases.begin(), texture.aliases.end()) !=
      texture.aliases.end()) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::DuplicateAsset,
                                          "Texture aliases contain duplicates.");
  }
  const auto validate_id = [&](const ProjectAssetId &id) {
    return id.valid() && !is_texture_deleted(id) && find_texture(id) == nullptr;
  };
  if (!validate_id(texture.id) ||
      !std::ranges::all_of(texture.aliases, [&](const ProjectAssetId &alias) {
        return alias != texture.id && validate_id(alias);
      })) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::DuplicateAsset,
        "Texture UUID or alias is invalid, duplicated, or permanently deleted.",
        texture.path.native_path());
  }
  if (texture_index_by_id_.size() + texture.aliases.size() + 1u >
      kMaximumTextureIdentities) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidArgument,
                                          "Texture catalog exceeds its size limit.");
  }
  const std::size_t index = textures_.size();
  texture_index_by_id_.emplace(texture.id, index);
  for (const auto &alias : texture.aliases) {
    texture_index_by_id_.emplace(alias, index);
  }
  texture_index_by_path_.emplace(texture.path.portable_case_key(), index);
  textures_.push_back(std::move(texture));
  return ProjectIoResult<void>::success();
}

void ProjectManifest::rebuild_texture_indexes() {
  texture_index_by_id_.clear();
  texture_index_by_path_.clear();
  for (std::size_t index = 0u; index < textures_.size(); ++index) {
    const auto &entry = textures_[index];
    texture_index_by_id_.emplace(entry.id, index);
    for (const auto &alias : entry.aliases) {
      texture_index_by_id_.emplace(alias, index);
    }
    texture_index_by_path_.emplace(entry.path.portable_case_key(), index);
  }
}

ProjectIoResult<void> ProjectManifest::relocate_texture(
    const ProjectAssetId &id, ProjectRelativePath path) {
  const auto found = texture_index_by_id_.find(id);
  if (found == texture_index_by_id_.end()) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::AssetNotFound,
                                          "Texture is not registered.");
  }
  if (!project_asset_path_visible(path) ||
      !project_texture_extension_supported(path.native_path())) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidRelativePath,
                                          "Texture destination is invalid.");
  }
  const auto *existing = find_texture(path);
  if ((existing != nullptr && existing->id != textures_[found->second].id) ||
      find_scene(path) != nullptr) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::PathCaseCollision,
                                          "Texture destination is registered.");
  }
  textures_[found->second].path = std::move(path);
  rebuild_texture_indexes();
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void>
ProjectManifest::unregister_texture(const ProjectAssetId &id) {
  const auto found = texture_index_by_id_.find(id);
  if (found == texture_index_by_id_.end()) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::AssetNotFound,
                                          "Texture is not registered.");
  }
  textures_.erase(textures_.begin() + static_cast<std::ptrdiff_t>(found->second));
  rebuild_texture_indexes();
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> ProjectManifest::mark_textures_deleted(
    std::span<const ProjectAssetId> texture_ids) {
  std::vector<ProjectAssetId> retired = deleted_texture_ids_;
  for (const auto &id : texture_ids) {
    if (!id.valid()) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidUuid,
                                            "Deleted texture UUID is invalid.");
    }
    retired.push_back(id);
    if (const auto *entry = find_texture(id)) {
      retired.push_back(entry->id);
      retired.insert(retired.end(), entry->aliases.begin(), entry->aliases.end());
    }
  }
  std::sort(retired.begin(), retired.end());
  retired.erase(std::unique(retired.begin(), retired.end()), retired.end());
  if (retired.size() > kMaximumTextureIdentities) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::InvalidArgument,
                                          "Deleted texture catalog exceeds its size limit.");
  }
  deleted_texture_ids_ = std::move(retired);
  std::erase_if(textures_, [&](const ProjectTextureCatalogEntry &entry) {
    return is_texture_deleted(entry.id);
  });
  rebuild_texture_indexes();
  return ProjectIoResult<void>::success();
}

std::uint64_t ProjectManifest::texture_metadata_fingerprint() const noexcept {
  std::uint64_t hash = 14695981039346656037ull;
  const auto number = [&hash](std::uint64_t value) {
    for (std::size_t index = 0u; index < sizeof(value); ++index) {
      hash ^= static_cast<unsigned char>(value >> (index * 8u));
      hash *= 1099511628211ull;
    }
  };
  const auto text = [&number](const auto &value) {
    number(value.size());
    for (const auto character : value) number(static_cast<std::uint64_t>(character));
  };
  number(textures_.size());
  for (const auto &texture : textures_) {
    text(texture.id.str());
    text(texture.path.native_path().native());
    number(texture.aliases.size());
    for (const auto &alias : texture.aliases) text(alias.str());
  }
  number(deleted_texture_ids_.size());
  for (const auto &id : deleted_texture_ids_) text(id.str());
  return hash == 0u ? 1u : hash;
}

ProjectIoResult<ProjectWorkspaceStorage>
ProjectManifestStore::create_in_empty_directory(
    const std::filesystem::path &root) {
  ProjectIoResult<ProjectFileSystem> opened_file_system =
      ProjectFileSystem::open(root);
  if (!opened_file_system.ok()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        opened_file_system.error().code, opened_file_system.error().message,
        opened_file_system.error().path);
  }
  ProjectFileSystem file_system = opened_file_system.take_value();

  std::error_code error;
  std::filesystem::directory_iterator iterator(file_system.root(), error);
  if (error) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not inspect project folder: " + error.message(),
        file_system.root());
  }
  if (iterator != std::filesystem::directory_iterator{}) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::RootNotEmpty,
        "A new Quader project requires a completely empty folder.",
        file_system.root());
  }

  const std::filesystem::path project_name =
      normalized_root_filename(file_system.root());
  if (project_name.empty()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project folder must have a usable name.", file_system.root());
  }

  ProjectIoResult<ProjectManifest> created_manifest =
      ProjectManifest::create(ProjectId::generate(),
                              utf8_project_name(file_system.root()));
  if (!created_manifest.ok()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        created_manifest.error().code, created_manifest.error().message,
        created_manifest.error().path);
  }

  const std::filesystem::path scenes_directory =
      file_system.root() / "Scenes";
  if (!std::filesystem::create_directory(scenes_directory, error) || error) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not create project Scenes directory: " + error.message(),
        scenes_directory);
  }

  ProjectWorkspaceStorage workspace{
      .file_system = std::move(file_system),
      .manifest = created_manifest.take_value(),
  };
  workspace.manifest_path = expected_manifest_path(workspace.file_system);
  ProjectIoResult<void> saved = save_atomic(workspace);
  if (!saved.ok()) {
    std::error_code ignored;
    std::filesystem::remove(scenes_directory, ignored);
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        saved.error().code, saved.error().message, saved.error().path);
  }
  workspace.persisted_texture_metadata_fingerprint =
      workspace.manifest.texture_metadata_fingerprint();
  return ProjectIoResult<ProjectWorkspaceStorage>::success(
      std::move(workspace));
}

ProjectIoResult<ProjectWorkspaceStorage>
ProjectManifestStore::open(const std::filesystem::path &manifest_path) {
  auto opened = open_metadata(manifest_path);
  if (!opened.ok()) {
    return opened;
  }
  const auto &workspace = opened.value();
  const auto validated =
      validate_manifest_for_filesystem(workspace.file_system, workspace.manifest);
  if (!validated.ok()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        validated.error().code, validated.error().message,
        validated.error().path);
  }
  return opened;
}

ProjectIoResult<ProjectWorkspaceStorage>
ProjectManifestStore::open_metadata(const std::filesystem::path &manifest_path) {
  if (manifest_path.empty()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::InvalidArgument, "Project manifest path is empty.");
  }

  std::error_code error;
  if (!std::filesystem::is_regular_file(manifest_path, error)) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::FileNotFound,
        "Project manifest does not exist or is not a regular file.",
        manifest_path);
  }
  const std::filesystem::path canonical_manifest =
      std::filesystem::canonical(manifest_path, error);
  if (error) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not canonicalize project manifest: " + error.message(),
        manifest_path);
  }

  ProjectIoResult<ProjectFileSystem> opened_file_system =
      ProjectFileSystem::open(canonical_manifest.parent_path());
  if (!opened_file_system.ok()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        opened_file_system.error().code, opened_file_system.error().message,
        opened_file_system.error().path);
  }
  ProjectFileSystem file_system = opened_file_system.take_value();
  const std::filesystem::path expected = expected_manifest_path(file_system);
  if (!same_portable_filesystem_path(canonical_manifest, expected)) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::ManifestMalformed,
        "Project manifest must be named after its containing folder.",
        canonical_manifest);
  }

  const quader_io::SharedFileReadResult source =
      quader_io::read_file_bytes_shared(canonical_manifest, kMaximumManifestBytes);
  if (!source.ok) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        source.error == quader_io::SharedFileReadError::Limit ||
                source.error == quader_io::SharedFileReadError::Size
            ? ProjectIoErrorCode::ManifestMalformed
            : ProjectIoErrorCode::IoFailure,
        "Could not read project manifest: " + source.message,
        canonical_manifest);
  }
  const std::string text(
      source.bytes.empty() ? ""
                           : reinterpret_cast<const char *>(source.bytes.data()),
      source.bytes.size());

  ProjectIoResult<ProjectManifest> decoded = deserialize(text);
  if (!decoded.ok()) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        decoded.error().code, decoded.error().message, canonical_manifest);
  }
  ProjectManifest manifest = decoded.take_value();
  if (QStr::to_ascii_lower(manifest.name()) !=
      QStr::to_ascii_lower(
          utf8_project_name(file_system.root()))) {
    return ProjectIoResult<ProjectWorkspaceStorage>::failure(
        ProjectIoErrorCode::ManifestMalformed,
        "Project name must match its containing folder name.",
        canonical_manifest);
  }

  const auto texture_metadata_fingerprint = manifest.texture_metadata_fingerprint();
  return ProjectIoResult<ProjectWorkspaceStorage>::success(
      ProjectWorkspaceStorage{
          .file_system = std::move(file_system),
          .manifest = std::move(manifest),
          .manifest_path = canonical_manifest,
          .persisted_texture_metadata_fingerprint = texture_metadata_fingerprint,
      });
}

ProjectIoResult<void> ProjectManifestStore::save_atomic(
    const ProjectWorkspaceStorage &workspace) {
  return save_atomic(workspace.file_system, workspace.manifest_path,
                     workspace.manifest);
}

ProjectIoResult<void> ProjectManifestStore::save_atomic(
    const ProjectFileSystem &file_system,
    const std::filesystem::path &manifest_path,
    const ProjectManifest &manifest) {
  if (!file_system.valid() || !manifest.id().valid()) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Cannot save an invalid project workspace.", manifest_path);
  }
  if (!same_portable_filesystem_path(manifest_path,
                                     expected_manifest_path(file_system))) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::PathOutsideProject,
        "Project manifest path is not the project root manifest.",
        manifest_path);
  }
  const ProjectIoResult<void> validated =
      validate_manifest_for_filesystem(file_system, manifest);
  if (!validated.ok()) {
    return validated;
  }

  const std::string encoded = serialize(manifest);
  if (encoded.size() > kMaximumManifestBytes) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::ManifestMalformed,
        "Project manifest exceeds its encoded size limit.", manifest_path);
  }
  quader_io::QdrAtomicWriteResult write_result =
      quader_io::write_qdr_text_atomic(manifest_path, encoded);
  if (!write_result.ok) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                          write_result.message,
                                          manifest_path);
  }
  return ProjectIoResult<void>::success(std::move(write_result.message));
}

std::string ProjectManifestStore::serialize(const ProjectManifest &manifest) {
  Json scenes = Json::array();
  scenes.get_ref<Json::array_t &>().reserve(manifest.scenes().size());
  for (const ProjectSceneCatalogEntry &scene : manifest.scenes()) {
    scenes.push_back(Json{
        {"id", std::string(scene.id.str())},
        {"path", scene.path.portable_string()},
    });
  }

  Json open_scenes = Json::array();
  open_scenes.get_ref<Json::array_t &>().reserve(
      manifest.open_scene_paths().size());
  for (const ProjectRelativePath &path : manifest.open_scene_paths()) {
    open_scenes.push_back(path.portable_string());
  }

  Json active_scene = nullptr;
  if (manifest.active_scene_path().has_value()) {
    active_scene = manifest.active_scene_path()->portable_string();
  }
  Json root{
      {"format", std::string(kQuaderProjectManifestFormat)},
      {"version", kQuaderProjectManifestVersion},
      {"minimumProductRelease", manifest.minimum_product_release()},
      {"project",
       Json{
           {"id", std::string(manifest.id().str())},
           {"name", manifest.name()},
       }},
      {"scenes", std::move(scenes)},
      {"workspace",
       Json{
           {"openScenes", std::move(open_scenes)},
           {"activeScene", std::move(active_scene)},
       }},
  };
  {
    Json deleted_material_ids = Json::array();
    deleted_material_ids.get_ref<Json::array_t &>().reserve(
        manifest.deleted_material_ids().size());
    for (const ProjectAssetId &id : manifest.deleted_material_ids()) {
      deleted_material_ids.push_back(std::string(id.str()));
    }
    root["deletedMaterialIds"] = std::move(deleted_material_ids);
  }
  Json textures = Json::array();
  for (const auto &entry : manifest.textures()) {
    Json aliases = Json::array();
    for (const auto &alias : entry.aliases) {
      aliases.push_back(std::string(alias.str()));
    }
    textures.push_back(Json{{"id", std::string(entry.id.str())},
                            {"path", entry.path.portable_string()},
                            {"aliases", std::move(aliases)}});
  }
  root["textures"] = std::move(textures);
  Json retired_textures = Json::array();
  for (const auto &id : manifest.deleted_texture_ids()) {
    retired_textures.push_back(std::string(id.str()));
  }
  root["deletedTextureIds"] = std::move(retired_textures);
  return root.dump(2);
}

ProjectIoResult<ProjectManifest>
ProjectManifestStore::deserialize(std::string_view text) {
  if (text.empty() || text.size() > kMaximumManifestBytes) {
    return malformed_manifest("Project manifest is empty or too large.");
  }

  try {
    const Json root = Json::parse(text.begin(), text.end());
    if (!root.is_object()) {
      return malformed_manifest("Project manifest root must be an object.");
    }
    if (!root.contains("format") || !root["format"].is_string() ||
        root["format"].get<std::string>() != kQuaderProjectManifestFormat) {
      return ProjectIoResult<ProjectManifest>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "File is not a Quader project manifest.");
    }
    if (!root.contains("version") ||
        !root["version"].is_number_unsigned()) {
      return ProjectIoResult<ProjectManifest>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "Project manifest version is not supported.");
    }
    const std::uint64_t version =
        root["version"].get<std::uint64_t>();
    if (version < kOldestQuaderProjectManifestVersion ||
        version > kQuaderProjectManifestVersion) {
      return ProjectIoResult<ProjectManifest>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "Project manifest version is not supported.");
    }
    ProductRelease minimum_product_release = kInternalFullRelease;
    if (version >= 4u) {
      if (!root.contains("minimumProductRelease") ||
          !root["minimumProductRelease"].is_number_unsigned() ||
          root["minimumProductRelease"].get<ProductRelease>() == 0u) {
        return malformed_manifest(
            "Minimum product release must be a nonzero unsigned integer.");
      }
      minimum_product_release =
          root["minimumProductRelease"].get<ProductRelease>();
    }
    if (!root.contains("project") || !root["project"].is_object() ||
        !root["project"].contains("id") ||
        !root["project"]["id"].is_string() ||
        !root["project"].contains("name") ||
        !root["project"]["name"].is_string()) {
      return malformed_manifest("Project manifest identity is malformed.");
    }

    ProjectIoResult<ProjectId> project_id =
        ProjectId::parse(root["project"]["id"].get<std::string>());
    if (!project_id.ok()) {
      return malformed_manifest("Project manifest contains an invalid UUID.");
    }

    if (!root.contains("scenes") || !root["scenes"].is_array() ||
        root["scenes"].size() > kMaximumManifestScenes) {
      return malformed_manifest("Project scene catalog is malformed or too large.");
    }
    std::vector<ProjectSceneCatalogEntry> scenes;
    scenes.reserve(root["scenes"].size());
    for (const Json &scene_json : root["scenes"]) {
      if (!scene_json.is_object() || !scene_json.contains("id") ||
          !scene_json["id"].is_string() || !scene_json.contains("path") ||
          !scene_json["path"].is_string()) {
        return malformed_manifest("Project scene catalog entry is malformed.");
      }
      const std::string scene_path = scene_json["path"].get<std::string>();
      if (scene_path.size() > kMaximumPortablePathBytes) {
        return malformed_manifest("Project scene path exceeds the size limit.");
      }
      ProjectIoResult<SceneId> scene_id =
          SceneId::parse(scene_json["id"].get<std::string>());
      ProjectIoResult<ProjectRelativePath> path =
          ProjectRelativePath::parse_portable(scene_path);
      if (!scene_id.ok() || !path.ok()) {
        return malformed_manifest("Project scene identity or path is invalid.");
      }
      scenes.push_back(ProjectSceneCatalogEntry{
          .id = scene_id.take_value(),
          .path = path.take_value(),
      });
    }

    if (!root.contains("workspace") || !root["workspace"].is_object() ||
        !root["workspace"].contains("openScenes") ||
        !root["workspace"]["openScenes"].is_array() ||
        !root["workspace"].contains("activeScene")) {
      return malformed_manifest("Project workspace state is malformed.");
    }
    std::vector<ProjectRelativePath> open_scenes;
    open_scenes.reserve(root["workspace"]["openScenes"].size());
    for (const Json &path_json : root["workspace"]["openScenes"]) {
      if (!path_json.is_string()) {
        return malformed_manifest("Open scene path must be a string.");
      }
      ProjectIoResult<ProjectRelativePath> path =
          ProjectRelativePath::parse_portable(path_json.get<std::string>());
      if (!path.ok()) {
        return malformed_manifest("Open scene path is invalid.");
      }
      open_scenes.push_back(path.take_value());
    }

    std::optional<ProjectRelativePath> active_scene;
    const Json &active_json = root["workspace"]["activeScene"];
    if (!active_json.is_null()) {
      if (!active_json.is_string()) {
        return malformed_manifest("Active scene path must be a string or null.");
      }
      ProjectIoResult<ProjectRelativePath> path =
          ProjectRelativePath::parse_portable(active_json.get<std::string>());
      if (!path.ok()) {
        return malformed_manifest("Active scene path is invalid.");
      }
      active_scene = path.take_value();
    }

    // Legacy selection is validated for compatibility and deliberately discarded.
    if (version >= 2u) {
      const Json &workspace_json = root["workspace"];
      if (workspace_json.contains("activeMaterial")) {
        const Json &material_json = workspace_json["activeMaterial"];
        if (!material_json.is_null()) {
          if (!material_json.is_string()) {
            return malformed_manifest(
                "Active material UUID must be a string or null.");
          }
          if (!ProjectAssetId::parse(material_json.get<std::string>())
                   .has_value()) {
            return malformed_manifest(
                "Active material UUID is invalid.");
          }
        }
      }
    }

    std::vector<ProjectAssetId> deleted_material_ids;
    if (version < 3u && root.contains("deletedMaterialIds")) {
      return malformed_manifest(
          "Deleted material identities require project manifest version 3.");
    }
    if (version >= 3u) {
      if (!root.contains("deletedMaterialIds") ||
          !root["deletedMaterialIds"].is_array() ||
          root["deletedMaterialIds"].size() > kMaximumDeletedMaterialIds) {
        return malformed_manifest(
            "Deleted material identities are malformed or too numerous.");
      }
      const Json &deleted_json = root["deletedMaterialIds"];
      deleted_material_ids.reserve(deleted_json.size());
      std::unordered_set<ProjectAssetId, ProjectAssetIdHash> seen;
      seen.reserve(deleted_json.size());
      for (const Json &id_json : deleted_json) {
        if (!id_json.is_string()) {
          return malformed_manifest("Deleted material UUID must be a string.");
        }
        const std::optional<ProjectAssetId> id =
            ProjectAssetId::parse(id_json.get<std::string>());
        if (!id.has_value() || id->str() == kDefaultProjectMaterialId ||
            !seen.insert(*id).second) {
          return malformed_manifest(
              "Deleted material UUID is invalid, reserved, or duplicated.");
        }
        deleted_material_ids.push_back(*id);
      }
    }

    std::vector<ProjectTextureCatalogEntry> textures;
    std::vector<ProjectAssetId> deleted_texture_ids;
    if (version < 5u &&
        (root.contains("textures") || root.contains("deletedTextureIds"))) {
      return malformed_manifest("Texture catalogs require project manifest version 5.");
    }
    if (version >= 5u) {
      if (!root.contains("textures") || !root["textures"].is_array() ||
          root["textures"].size() > kMaximumTextureIdentities ||
          !root.contains("deletedTextureIds") ||
          !root["deletedTextureIds"].is_array() ||
          root["deletedTextureIds"].size() > kMaximumTextureIdentities) {
        return malformed_manifest("Texture catalog or deleted identities are malformed.");
      }
      std::size_t identity_count = 0u;
      for (const auto &entry : root["textures"]) {
        if (!entry.is_object() || !entry.contains("id") ||
            !entry["id"].is_string() || !entry.contains("path") ||
            !entry["path"].is_string() || !entry.contains("aliases") ||
            !entry["aliases"].is_array()) {
          return malformed_manifest("Texture catalog entry is malformed.");
        }
        identity_count += 1u + entry["aliases"].size();
        const auto id = ProjectAssetId::parse(entry["id"].get<std::string>());
        const std::string path_text = entry["path"].get<std::string>();
        auto path = ProjectRelativePath::parse_portable(path_text);
        if (!id.has_value() || !path.ok() ||
            path_text.size() > kMaximumPortablePathBytes ||
            identity_count > kMaximumTextureIdentities) {
          return malformed_manifest("Texture identity or path is invalid.");
        }
        std::vector<ProjectAssetId> aliases;
        for (const auto &alias_json : entry["aliases"]) {
          if (!alias_json.is_string()) {
            return malformed_manifest("Texture alias must be a UUID string.");
          }
          const auto alias = ProjectAssetId::parse(alias_json.get<std::string>());
          if (!alias.has_value()) {
            return malformed_manifest("Texture alias is invalid.");
          }
          aliases.push_back(*alias);
        }
        textures.push_back({*id, path.take_value(), std::move(aliases)});
      }
      std::unordered_set<ProjectAssetId, ProjectAssetIdHash> seen;
      for (const auto &id_json : root["deletedTextureIds"]) {
        if (!id_json.is_string()) {
          return malformed_manifest("Deleted texture UUID must be a string.");
        }
        const auto id = ProjectAssetId::parse(id_json.get<std::string>());
        if (!id.has_value() || !seen.insert(*id).second) {
          return malformed_manifest("Deleted texture UUID is invalid or duplicated.");
        }
        deleted_texture_ids.push_back(*id);
      }
    }

    auto hydrated = ProjectManifest::hydrate(
        project_id.take_value(), root["project"]["name"].get<std::string>(),
        std::move(scenes), std::move(open_scenes), std::move(active_scene),
        std::move(deleted_material_ids), minimum_product_release,
        std::move(textures), std::move(deleted_texture_ids));
    if (hydrated.ok()) {
      hydrated.value().loaded_version_ = static_cast<std::uint32_t>(version);
    }
    return hydrated;
  } catch (const std::exception &exception) {
    return malformed_manifest(std::string("Could not parse project manifest: ") +
                              exception.what());
  }
}

} // namespace quader
