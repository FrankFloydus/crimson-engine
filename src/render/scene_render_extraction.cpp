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

bool SceneRenderMeshEntry::castShadows() const {
    return meshComponent != nullptr && meshComponent->castShadows;
}

bool SceneRenderMeshEntry::doubleSided() const {
    return meshComponent != nullptr && meshComponent->doubleSided;
}

SceneRenderExtractionPlan buildSceneRenderExtractionPlan(
        const SceneDocument& document,
        const AssetRegistry& assets,
        const MaterialBindingService& materialBinding) {
    QDR_PROFILE_FUNCTION();
    SceneRenderExtractionPlan plan;

    for (const EntityId entityId : document.entities()) {
        QDR_PROFILE_SCOPE("Scene render extract entity");
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }

        const QMat4 worldTransform = document.worldTransformMatrix(entityId);

        if (const MeshComponent* meshComponent = document.mesh(entityId)) {
            const MaterialComponent* materialComponent = document.material(entityId);
            const AssetId materialId = materialComponent ? materialComponent->material : InvalidAssetId;
            SceneRenderMeshEntry entry{
                    .entityId = entityId,
                    .meshId = meshComponent->mesh,
                    .mesh = assets.mesh(meshComponent->mesh),
                    .meshComponent = meshComponent,
                    .materialComponent = materialComponent,
                    .materialId = materialId,
                    .materialRevision = materialBinding.materialRevision(assets, materialComponent),
                    .worldTransform = worldTransform,
            };
            if (entry.renderable()) {
                plan.liveMeshEntities.insert(entityId);
            }
            plan.meshes.push_back(entry);
        }

        if (const LightComponent* light = document.light(entityId)) {
            plan.liveLightEntities.insert(entityId);
            plan.lights.push_back(SceneRenderLightEntry{
                    .entityId = entityId,
                    .light = light,
                    .worldTransform = worldTransform,
            });
        }
    }

    return plan;
}

} // namespace quader
