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
#include "render/render_scene_snapshot.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Captures a transient tool preview mesh before render material dependencies
 * are expanded into a render-owned preview snapshot.
 */
struct ToolPreviewMesh {
  EntityId source_entity = kInvalidEntityId;
  // Local Visibility can be owned by a public instance occurrence while the
  // preview source remains a canonical definition member.
  EntityId visibility_entity = kInvalidEntityId;
  AssetId material = kInvalidAssetId;
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
};

using SceneRenderMeshEntry = RenderSceneMeshRecord;
using SceneRenderLightEntry = RenderSceneLightRecord;
using SceneRenderExtractionPlan = RenderSceneSnapshot;

[[nodiscard]] RenderMeshAssetSnapshot
render_mesh_asset_snapshot(AssetId mesh_id, const MeshAsset &asset);
[[nodiscard]] std::vector<MeshPrimitiveSpan>
normalized_render_mesh_primitive_spans(const RenderMeshAssetSnapshot &mesh);
[[nodiscard]] std::vector<MeshPrimitiveSpan>
coalesced_render_mesh_primitive_spans(const RenderMeshAssetSnapshot &mesh);
[[nodiscard]] RenderMaterialBindingSnapshot
build_render_material_binding_snapshot(const AssetRegistry &assets,
                                       const MaterialComponent *material,
                                       const RenderProjectMaterialResolver *
                                           project_material_resolver = nullptr);
[[nodiscard]] std::uint64_t render_material_binding_signature(
    const RenderMaterialBindingSnapshot &material);
[[nodiscard]] RenderSceneSnapshot
build_render_scene_snapshot(const SceneDocument &document,
                            const AssetRegistry &assets,
                            RenderSceneSnapshotBuildOptions options = {});
[[nodiscard]] bool try_refresh_render_scene_snapshot_transforms(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index = 0,
    RenderSceneBindingKey scene_binding = {},
    std::uint64_t project_material_revision = 0u,
    RenderInstanceEditScope instance_edit_scope = {},
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);
[[nodiscard]] bool render_scene_snapshot_matches_sources(
    const RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, RenderSceneBindingKey scene_binding = {},
    std::uint64_t project_material_revision = 0u,
    RenderInstanceEditScope instance_edit_scope = {},
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);
[[nodiscard]] SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets);
[[nodiscard]] std::vector<RenderToolPreviewMeshRecord>
build_render_tool_preview_snapshot(const std::vector<ToolPreviewMesh> &meshes,
                                   const AssetRegistry &assets,
                                   const ViewportLocalVisibilitySnapshot *
                                       local_visibility = nullptr);

} // namespace quader
