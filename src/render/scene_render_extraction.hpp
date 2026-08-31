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
#include <memory>
#include <vector>

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Captures a transient tool preview mesh before render material dependencies
 * are expanded into a render-owned preview snapshot.
 */
struct ToolPreviewMesh {
  int source_object_id = -1;
  EntityId source_entity = kInvalidEntityId;
  // Local Visibility can be owned by a public instance occurrence while the
  // preview source remains a canonical definition member.
  EntityId visibility_entity = kInvalidEntityId;
  MaterialComponent material;
  MeshAsset mesh;
  QMat4 transform;
  std::vector<QMat4> instance_transforms;
  QMat4 sampling_from_local;
  std::vector<QMat4> instance_sampling_from_local;
  // Source-replacing previews carry source culling; generated previews keep
  // the historical two-sided default.
  bool double_sided = true;
  bool cast_shadows = false;
  bool dynamic_positions = false;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
  // Participant-local token preserved from the modeling producer.
  std::uint64_t position_revision = 0u;
  std::shared_ptr<const std::vector<QVec3>> dynamic_position_payload;
};

/** Captures one Curve binding before its immutable sample payload is pinned. */
struct ToolPreviewCurve final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  AssetId curve = kInvalidAssetId;
  std::vector<QMat4> instance_transforms;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
};

/** Captures one transient Light participant and all of its placements. */
struct ToolPreviewLight final {
  EntityId source_entity = kInvalidEntityId;
  EntityId visibility_entity = kInvalidEntityId;
  LightComponent light;
  std::vector<QMat4> instance_transforms;
  bool suppress_source = false;
  std::uint64_t layout_key = 0u;
};

/** Captures one local bounds cage repeated with a source root. */
struct ToolPreviewGroupMarker final {
  EntityId source_entity = kInvalidEntityId;
  QVec3 local_min;
  QVec3 local_max;
  std::vector<QMat4> instance_transforms;
  std::uint64_t layout_key = 0u;
};

/** Renderer-neutral compound tool preview input. */
struct ToolPreviewScene final {
  std::uint64_t revision = 0u;
  std::vector<ToolPreviewMesh> meshes;
  std::vector<ToolPreviewCurve> curves;
  std::vector<ToolPreviewLight> lights;
  std::vector<ToolPreviewGroupMarker> group_markers;
  std::vector<EntityId> suppressed_entities;
  std::vector<RenderInstancePreviewSuppression> occurrence_suppressions;
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
/**
 * Advances an existing main-scene snapshot across a curve-only publication.
 * Authored curves are rendered by the overlay snapshot, so no mesh or light
 * record changes when the exact curve/render revision pair is the only
 * render-component change.
 */
[[nodiscard]] bool try_refresh_render_scene_snapshot_curve_publication(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index = 0,
    RenderSceneBindingKey scene_binding = {},
    std::uint64_t project_material_revision = 0u,
    RenderInstanceEditScope instance_edit_scope = {},
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);
[[nodiscard]] bool try_refresh_render_scene_snapshot_selection(
    RenderSceneSnapshot &snapshot, const SceneDocument &document,
    const AssetRegistry &assets, std::uint64_t frame_index = 0,
    RenderSceneBindingKey scene_binding = {},
    std::uint64_t project_material_revision = 0u,
    RenderInstanceEditScope instance_edit_scope = {},
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr);
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
build_render_tool_preview_snapshot(std::vector<ToolPreviewMesh> meshes,
                                   const AssetRegistry &assets,
                                   const ViewportLocalVisibilitySnapshot *
                                       local_visibility = nullptr,
                                   const RenderProjectMaterialResolver *
                                       project_material_resolver = nullptr);

[[nodiscard]] RenderToolPreviewSceneSnapshot
build_render_tool_preview_scene_snapshot(
    ToolPreviewScene preview, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility = nullptr,
    const RenderProjectMaterialResolver *project_material_resolver = nullptr);

/** Builds camera-independent Curve, Light-marker, and group-marker geometry. */
[[nodiscard]] ViewportOverlayFrame build_render_tool_preview_overlay_frame(
    const RenderToolPreviewSceneSnapshot &preview);

} // namespace quader
