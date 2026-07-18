////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"

#include <vector>

namespace quader {

/**
 * Carries one renderer-neutral native modeling preview mesh.
 */
struct ModelingToolPreviewMesh {
  EntityId source_entity = kInvalidEntityId;
  MaterialReference material;
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
};

} // namespace quader
