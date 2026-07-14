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
#include "render/viewport_overlay_frame.hpp"

#include <cstddef>
#include <cstdint>

namespace quader {

class SceneRenderBridge;
struct ViewportSceneOverlayFrameRequest;

/**
 * Stores viewport presentation state needed to synchronize persistent
 * scene-derived overlay renderables without exposing scene or asset owners to
 * UI panels.
 */
struct QuaderRenderSceneOverlaySyncRequest {
  bool selected_object_overlay_visible = false;
  std::uint8_t scene_wireframe_layer_mask = 0u;
  ViewportPersistentOverlayStyleSnapshot style;
};

/**
 * Provides runtime-only access to render synchronization without exposing render resources on the public API.
 */
class QuaderRenderApiRuntimeAccess final {
public:
  static QuaderApiVoidResult sync_scene(QuaderRenderApi &api,
                                        SceneRenderBridge &bridge);
  static QuaderApiVoidResult sync_tool_previews(QuaderRenderApi &api,
                                                SceneRenderBridge &bridge);
  static QuaderApiVoidResult sync_scene_asset_overlays(
      QuaderRenderApi &api, SceneRenderBridge &bridge,
      RenderOverlaySnapshotCache &snapshot_cache,
      const QuaderRenderSceneOverlaySyncRequest &request);
  [[nodiscard]] static const RenderOverlaySelectionSnapshot &
  build_scene_overlay_snapshot(QuaderRenderApi &api,
                               RenderOverlaySnapshotCache &snapshot_cache);
  [[nodiscard]] static ViewportOverlayFrame build_scene_overlay_frame(
      SceneRenderBridge &bridge, ViewportOverlayFrame base_frame,
      const RenderOverlaySelectionSnapshot &snapshot,
      const ViewportSceneOverlayFrameRequest &request);
  static QuaderApiResult<QuaderRenderOverlaySubmission> sync_selection_overlay(
      QuaderRenderApi &api, SceneRenderBridge &bridge,
      std::size_t viewport_index, std::uint8_t layer_mask,
      ViewportOverlayCamera camera,
      const ViewportOverlayFrame *tool_overlay_frame = nullptr);
  static QuaderApiVoidResult
  set_selection_overlay(QuaderRenderApi &api, SceneRenderBridge &bridge,
                        std::size_t viewport_index, std::uint8_t layer_mask,
                        ViewportOverlayCamera camera);

private:
  [[nodiscard]] static RenderSceneBindingKey
  scene_binding(const QuaderRenderApi &api);
};

} // namespace quader
