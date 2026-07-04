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
  AssetId material = kInvalidAssetId;
  MeshAsset mesh;
  QMat4 transform;
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
                                       const MaterialComponent *material);
[[nodiscard]] std::uint64_t render_material_binding_signature(
    const RenderMaterialBindingSnapshot &material);
[[nodiscard]] RenderSceneSnapshot
build_render_scene_snapshot(const SceneDocument &document,
                            const AssetRegistry &assets,
                            RenderSceneSnapshotBuildOptions options = {});
[[nodiscard]] SceneRenderExtractionPlan build_scene_render_extraction_plan(
    const SceneDocument &document, const AssetRegistry &assets);
[[nodiscard]] std::vector<RenderToolPreviewMeshRecord>
build_render_tool_preview_snapshot(const std::vector<ToolPreviewMesh> &meshes,
                                   const AssetRegistry &assets);

} // namespace quader
