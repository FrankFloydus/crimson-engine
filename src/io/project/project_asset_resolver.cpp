////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_asset_resolver.hpp"

namespace quader {

ProjectIoResult<std::filesystem::path> ProjectAssetResolver::resolve_existing(
    const ProjectAssetReference &reference) const {
  if (file_system_ == nullptr || !file_system_->valid()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project asset resolver has no open project filesystem.");
  }
  if (!reference.stable_id.valid() ||
      reference.kind == ProjectAssetKind::Unknown ||
      !reference.path.valid()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Project asset reference is incomplete.");
  }
  return file_system_->resolve_existing_file(reference.path);
}

} // namespace quader
