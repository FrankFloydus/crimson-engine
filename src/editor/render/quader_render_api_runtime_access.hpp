////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_render_api.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/overlay/viewport_persistent_overlay_style.hpp"
#include "render/render_scene_snapshot.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace quader {

class SceneRenderBridge;
class ViewportRenderer;
class ViewportCameraController;
enum class ToolPreviewSubmissionResult : std::uint8_t;

/**
 * Stores viewport presentation state needed to synchronize persistent
 * scene-derived overlay renderables without exposing scene or asset owners to
 * UI panels.
 */
struct QuaderRenderSceneOverlaySyncRequest {
  bool selected_object_overlay_visible = false;
  std::uint8_t scene_wireframe_layer_mask = 0u;
  std::uint8_t wireframe_on_shaded_layer_mask = 0u;
  bool selected_scene_wire_visible = true;
  /// Object-mode presentation: preserve fill and leave the selected mesh outline
  /// to the per-viewport silhouette stream, including in wireframe panes.
  bool selected_mesh_outline_only = false;
  ViewportPersistentOverlayStyleSnapshot style;
  ViewportPersistentOverlayRasterGroupSet raster_groups;
  bool collision_visible = false;
  ViewportSettingColor collision_color{0.0f, 0.6f, 0.7f, 0.42f};
};

/**
 * Provides runtime-only access to render synchronization without exposing render resources on the public API.
 */
class QuaderRenderApiRuntimeAccess final {
public:
  /// Headless hosts may retain and acknowledge the same extracted preview payload.
  using ToolPreviewPublisher = std::function<ToolPreviewSubmissionResult(
      std::vector<RenderToolPreviewMeshRecord>)>;
  /// Compound-aware headless hosts receive the complete candidate atomically.
  using ToolPreviewScenePublisher = std::function<ToolPreviewSubmissionResult(
      RenderToolPreviewSceneSnapshot)>;

  static QuaderApiVoidResult sync_scene(QuaderRenderApi &api,
                                        SceneRenderBridge &bridge);
  /// Warms a derived GPU selection certificate only after this camera settles.
  static void prepare_selection_depth(QuaderRenderApi &api, ViewportRenderer &renderer,
                                      const ViewportCameraController &camera, bool eligible);
  static QuaderApiVoidResult sync_tool_previews(
      QuaderRenderApi &api, SceneRenderBridge &bridge,
      const ToolPreviewPublisher &publish = {},
      const ToolPreviewScenePublisher &publish_scene = {});
  static QuaderApiVoidResult sync_scene_asset_overlays(
      QuaderRenderApi &api, SceneRenderBridge &bridge,
      RenderOverlaySnapshotCache &snapshot_cache,
      const QuaderRenderSceneOverlaySyncRequest &request);
  [[nodiscard]] static const RenderOverlaySelectionSnapshot &
  build_scene_overlay_snapshot(QuaderRenderApi &api,
                               RenderOverlaySnapshotCache &snapshot_cache);

private:
  [[nodiscard]] static RenderSceneBindingKey
  scene_binding(const QuaderRenderApi &api);
};

} // namespace quader
