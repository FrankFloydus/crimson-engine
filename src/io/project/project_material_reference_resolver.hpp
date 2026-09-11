////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"

namespace quader {

class ProjectManifest;

/// Resolves permanently deleted material identities at consumer boundaries.
class ProjectMaterialReferenceResolver final {
public:
  /// Maps only deleted identities to Default; all other identities are unchanged.
  [[nodiscard]] static ProjectAssetId
  canonical_material_id(const ProjectManifest &manifest, ProjectAssetId id);
};

} // namespace quader
