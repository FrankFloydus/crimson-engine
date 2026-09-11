////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_material_reference_resolver.hpp"

#include "assets/default_material_catalog.hpp"
#include "io/project/project_manifest.hpp"

namespace quader {

ProjectAssetId ProjectMaterialReferenceResolver::canonical_material_id(
    const ProjectManifest &manifest, ProjectAssetId id) {
  if (!manifest.is_material_deleted(id)) {
    return id;
  }
  static const ProjectAssetId kDefaultId =
      *ProjectAssetId::parse(kDefaultProjectMaterialId);
  return kDefaultId;
}

} // namespace quader
