////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/mesh_asset.hpp"
#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <memory>
#include <vector>

namespace quader {

/**
 * Carries one renderer-neutral native modeling preview mesh.
 */
struct ModelingToolPreviewMesh {
  int source_object_id = -1;
  EntityId source_entity = kInvalidEntityId;
  // Preserve the source entity's complete binding. A primary-only reference
  // loses per-primitive slot overrides and cannot faithfully represent a
  // project-owned material while the source renderable is suppressed.
  MaterialComponent material;
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
  // Generated tool previews remain visible from either side by default.
  // Source-replacing previews must override this with the source mesh
  // component's culling policy so transient and committed rendering agree.
  bool double_sided = true;
  bool dynamic_positions = false;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
  // Participant-local token for the packed position stream. A collection
  // update must leave this value and the payload unchanged for participants
  // whose packed positions did not change.
  std::uint64_t position_revision = 0u;
  // Position-only transform previews retain immutable topology in `mesh` and
  // publish one separately pinned packed position stream per revision. This
  // keeps render snapshots valid without cloning indices or primitive spans.
  std::shared_ptr<const std::vector<QVec3>> dynamic_position_payload;
};

} // namespace quader
