////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/scene_render_extraction.hpp"
#include "render/overlay/viewport_overlay_submission_coordinator.hpp"

namespace quader {

/** Failed candidates preserve the accepted display and its source suppression. */
enum class ToolPreviewSubmissionResult : std::uint8_t { Unchanged, Published, Failed };

/**
 * Quader's render publication boundary. Implementations consume pinned source
 * snapshots and acknowledge complete candidates before changing source coverage.
 */
class RenderSceneSink {
public:
  virtual ~RenderSceneSink() = default;
  virtual void shutdown() = 0;
  virtual void set_project_material_resolver(const RenderProjectMaterialResolver *resolver) = 0;
  [[nodiscard]] virtual const RenderProjectMaterialResolver *project_material_resolver() const = 0;
  virtual void bind_scene(RenderSceneBindingKey binding) = 0;
  virtual void set_backface_culling_enabled(bool enabled) = 0;
  virtual void sync(const SceneDocument &document, const AssetRegistry &assets,
      std::uint64_t frame_index = 0, RenderSceneBindingKey binding = {},
      RenderInstanceEditScope instance_edit_scope = {},
      const ViewportLocalVisibilitySnapshot *local_visibility = nullptr) = 0;
  virtual void sync(RenderSceneSnapshot snapshot) = 0;
  virtual void update_mesh_surface_grid(float spacing, const ViewportSettings &settings) = 0;
  virtual void sync_scene_asset_overlays(
      const RenderOverlaySelectedObjectSnapshot &selected,
      const RenderOverlaySceneWireframeSnapshot &wireframe,
      const RenderOverlayCollisionSnapshot &collision = {}) = 0;
  [[nodiscard]] virtual RenderToolPreviewSceneSnapshot build_tool_preview_scene_snapshot(
      ToolPreviewScene preview, const AssetRegistry &assets,
      const ViewportLocalVisibilitySnapshot *local_visibility = nullptr) = 0;
  [[nodiscard]] virtual ToolPreviewSubmissionResult set_tool_preview_scene(
      RenderToolPreviewSceneSnapshot preview) = 0;
  [[nodiscard]] virtual const RenderToolPreviewSuppressionSnapshot &tool_preview_suppression() const noexcept = 0;
  [[nodiscard]] virtual std::uint64_t tool_preview_submission_revision() const = 0;
  [[nodiscard]] virtual bool scene_asset_overlay_sync_pending() const = 0;
  virtual void perform_idle_resource_maintenance() = 0;
  virtual void set_active_overlay_viewports(std::uint8_t viewport_mask) = 0;
  [[nodiscard]] virtual ViewportOverlaySubmissionResult submit_viewport_overlay(
      ViewportOverlaySubmissionRequest request, const ViewportSettings &settings) = 0;
};

} // namespace quader
