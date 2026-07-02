////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "math/types.hpp"
#include "scene/components.hpp"

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace quader {

class AssetRegistry;
class MaterialBindingService;
class MeshAsset;
class SceneDocument;

/**
 * Describes one scene mesh extracted for render sync.
 */
struct SceneRenderMeshEntry {
  EntityId entity_id = kInvalidEntityId;
  AssetId mesh_id = kInvalidAssetId;
  const MeshAsset *mesh = nullptr;
  const MeshComponent *mesh_component = nullptr;
  const MaterialComponent *material_component = nullptr;
  AssetId material_id = kInvalidAssetId;
  std::uint64_t material_revision = 0;
  QMat4 world_transform;

  [[nodiscard]] bool renderable() const;
  [[nodiscard]] bool cast_shadows() const;
  [[nodiscard]] bool double_sided() const;
};

/**
 * Describes one scene light extracted for render sync.
 */
struct SceneRenderLightEntry {
  EntityId entity_id = kInvalidEntityId;
  const LightComponent *light = nullptr;
  QMat4 world_transform;
};

/**
 * Shared render extraction plan consumed by cache invalidation and rebuild.
 */
struct SceneRenderExtractionPlan {
    std::vector<SceneRenderMeshEntry> meshes;
    std::vector<SceneRenderLightEntry> lights;
    std::unordered_set<EntityId> live_mesh_entities;
    std::unordered_set<EntityId> live_light_entities;
};

[[nodiscard]] SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets,
    const MaterialBindingService &material_binding);

} // namespace quader
