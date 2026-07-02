////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_extraction.hpp"

#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "diagnostics/profile.hpp"
#include "render/material_binding_service.hpp"
#include "scene/scene_document.hpp"

namespace quader {

bool SceneRenderMeshEntry::renderable() const {
    return mesh != nullptr && !mesh->vertices.empty() && !mesh->indices.empty();
}

bool SceneRenderMeshEntry::cast_shadows() const {
  return mesh_component != nullptr && mesh_component->cast_shadows;
}

bool SceneRenderMeshEntry::double_sided() const {
  return mesh_component != nullptr && mesh_component->double_sided;
}

SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets,
    const MaterialBindingService &material_binding) {
  QDR_PROFILE_FUNCTION();
  SceneRenderExtractionPlan plan;

  for (const EntityId entity_id : document.entities()) {
    QDR_PROFILE_SCOPE("Scene render extract entity");
    if (!document.is_effectively_visible(entity_id)) {
      continue;
    }

    const QMat4 world_transform = document.world_transform_matrix(entity_id);

    if (const MeshComponent *mesh_component = document.mesh(entity_id)) {
      const MaterialComponent *material_component =
          document.material(entity_id);
      const AssetId material_id =
          material_component ? material_component->material : kInvalidAssetId;
      SceneRenderMeshEntry entry{
          .entity_id = entity_id,
          .mesh_id = mesh_component->mesh,
          .mesh = assets.mesh(mesh_component->mesh),
          .mesh_component = mesh_component,
          .material_component = material_component,
          .material_id = material_id,
          .material_revision =
              material_binding.material_revision(assets, material_component),
          .world_transform = world_transform,
      };
      if (entry.renderable()) {
        plan.live_mesh_entities.insert(entity_id);
      }
      plan.meshes.push_back(entry);
    }

    if (const LightComponent *light = document.light(entity_id)) {
      plan.live_light_entities.insert(entity_id);
      plan.lights.push_back(SceneRenderLightEntry{
          .entity_id = entity_id,
          .light = light,
          .world_transform = world_transform,
      });
    }
  }

  return plan;
}

} // namespace quader
