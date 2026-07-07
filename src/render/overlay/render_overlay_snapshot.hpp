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
#include "render/viewport_overlay_frame.hpp"
#include "scene/components.hpp"
#include "viewport/viewport_camera_clip_planes.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace quader_poly {
struct Face;
struct Vertex;
} // namespace quader_poly

namespace quader {

class AssetRegistry;
class SceneDocument;

/**
 * Captures a render-bound mesh overlay input without exposing scene/asset owner
 * objects to overlay builders.
 */
struct RenderOverlayMeshSnapshot {
  EntityId entity = kInvalidEntityId;
  AssetId mesh_id = kInvalidAssetId;
  std::uint64_t mesh_revision = 0;
  QMat4 world_transform;
  bool selected = false;
  std::span<const MeshVertex> vertices;
  std::span<const std::uint32_t> indices;
  std::span<const quader_poly::Vertex> authored_vertices;
  std::span<const quader_poly::Face> authored_faces;

  [[nodiscard]] bool has_triangle_geometry() const {
    return !vertices.empty() && indices.size() >= 3u;
  }

  [[nodiscard]] bool has_authored_polygon_geometry() const {
    return !authored_vertices.empty() && !authored_faces.empty();
  }
};

/**
 * Stores one selected group or hierarchy bounds cage in world space.
 */
struct RenderOverlayBoundsSnapshot {
  QVec3 min;
  QVec3 max;
};

/**
 * Stores one selected non-billboard light marker input for render overlay
 * projection.
 */
struct RenderOverlayLightMarkerSnapshot {
  EntityId entity = kInvalidEntityId;
  LightType type = LightType::Directional;
  QMat4 world_transform;
};

/**
 * Captures persistent selected-object overlay inputs and the dirty revisions
 * needed by the cache.
 */
struct RenderOverlaySelectedObjectSnapshot {
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::uint64_t selection_revision = 0;
  std::uint64_t visibility_revision = 0;
  std::uint64_t transform_revision = 0;
  std::uint64_t mesh_binding_revision = 0;
  ViewportCameraClipPlanes clip_planes;
  bool visible = true;
  std::vector<RenderOverlayMeshSnapshot> meshes;
};

/**
 * Captures persistent wireframe overlay inputs and the dirty revisions needed
 * by the cache.
 */
struct RenderOverlaySceneWireframeSnapshot {
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  ViewportCameraClipPlanes clip_planes;
  std::uint8_t layer_mask = 0u;
  std::vector<RenderOverlayMeshSnapshot> meshes;
};

/**
 * Captures transient scene/tool overlay records appended to the runtime overlay
 * frame before style resolution.
 */
struct RenderOverlaySelectionSnapshot {
  std::uint64_t scene_revision = 0;
  std::uint64_t asset_revision = 0;
  std::vector<RenderOverlayMeshSnapshot> hierarchy_selection_wires;
  std::vector<RenderOverlayBoundsSnapshot> group_bounds;
  std::vector<RenderOverlayLightMarkerSnapshot> selected_lights;
  std::vector<RenderOverlayLightMarkerSnapshot> point_light_gizmos;
  ViewportOverlayFrame tool_overlays;
};

[[nodiscard]] RenderOverlaySelectedObjectSnapshot
build_render_overlay_selected_object_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              bool visible);

[[nodiscard]] RenderOverlaySceneWireframeSnapshot
build_render_overlay_scene_wireframe_snapshot(const SceneDocument &document,
                                              const AssetRegistry &assets,
                                              std::uint8_t layer_mask);

[[nodiscard]] RenderOverlaySelectionSnapshot
build_render_overlay_selection_snapshot(
    const SceneDocument &document, const AssetRegistry &assets,
    ViewportOverlayFrame tool_overlays = {});

} // namespace quader
