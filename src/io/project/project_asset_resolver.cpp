////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_asset_resolver.hpp"
#include "io/project/project_manifest.hpp"

namespace quader {

ProjectIoResult<ProjectRelativePath> resolve_project_texture_path(
    const ProjectManifest &manifest, ProjectAssetId id,
    const ProjectRelativePath &path_hint) {
  if (!id.valid()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidUuid, "Texture reference requires a UUID.");
  }
  if (manifest.is_texture_deleted(id)) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::AssetNotFound, "Project texture was deleted.");
  }
  if (const auto *entry = manifest.find_texture(id)) {
    return ProjectIoResult<ProjectRelativePath>::success(entry->path);
  }
  if (!path_hint.valid()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::AssetNotFound,
        "Texture identity is not registered and has no usable location.");
  }
  // Retain pre-catalog references until the next authored asset transaction.
  return ProjectIoResult<ProjectRelativePath>::success(path_hint);
}

ProjectIoResult<std::filesystem::path> ProjectAssetResolver::resolve_existing(
    const ProjectAssetReference &reference) const {
  if (file_system_ == nullptr || !file_system_->valid()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project asset resolver has no open project filesystem.");
  }
  if (!reference.stable_id.valid() ||
      reference.kind == ProjectAssetKind::Unknown) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project asset reference is incomplete.");
  }
  if (reference.kind == ProjectAssetKind::Texture && manifest_ != nullptr) {
    const auto path = resolve_project_texture_path(
        *manifest_, reference.stable_id, reference.path);
    if (!path.ok()) {
      return ProjectIoResult<std::filesystem::path>::failure(
          path.error().code, path.error().message, path.error().path);
    }
    return file_system_->resolve_existing_file(path.value());
  }
  return file_system_->resolve_existing_file(reference.path);
}

} // namespace quader
