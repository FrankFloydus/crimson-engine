////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_bridge.hpp"

#include "diagnostics/profile.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "render/scene_render_extraction.hpp"

#include <utility>

namespace quader {

SceneRenderBridge::~SceneRenderBridge() {
    shutdown();
}

void SceneRenderBridge::initialize(
    filament::Engine &engine, filament::Scene &scene,
    filament::Material &shaded_mesh_material,
    filament::Material &rendered_mesh_material,
    filament::Material &overlay_material,
    filament::Material &overlay_line_material,
    filament::Material &selected_object_line_material,
    filament::Material &edit_wire_line_material,
    filament::Material &overlay_point_material,
    filament::Material &overlay_icon_material,
    std::filesystem::path resource_root) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  texture_binding_.initialize(engine, resource_root);
  material_binding_.initialize(shaded_mesh_material, rendered_mesh_material,
                               texture_binding_);
  mesh_factory_.initialize(engine, scene, material_binding_);
  mesh_cache_.initialize(mesh_factory_);
  tool_preview_cache_.initialize(mesh_factory_, material_binding_);
  overlay_proxy_factory_.initialize(engine, scene, overlay_material,
                                    overlay_line_material,
                                    edit_wire_line_material,
                                    overlay_point_material,
                                    overlay_icon_material, resource_root);
  overlay_cache_.initialize(overlay_proxy_factory_);
  scene_wireframe_overlay_cache_.initialize(engine, scene,
                                            selected_object_line_material);
  selected_object_overlay_cache_.initialize(engine, scene, overlay_material);
  sync_service_.initialize(engine, scene, mesh_cache_, material_binding_);
}

void SceneRenderBridge::shutdown() {
  ++tool_preview_submission_revision_;
  if (engine_) {
    tool_preview_cache_.shutdown();
    sync_service_.shutdown();
    selected_object_overlay_cache_.shutdown();
    scene_wireframe_overlay_cache_.shutdown();
    mesh_cache_.shutdown();
    overlay_cache_.shutdown();
    overlay_proxy_factory_.shutdown();
    mesh_factory_.shutdown();
    material_binding_.shutdown();
    texture_binding_.shutdown();
  }

  previous_scene_snapshot_ = {};
  has_previous_scene_snapshot_ = false;
  engine_ = nullptr;
  scene_ = nullptr;
}

void SceneRenderBridge::sync(const SceneDocument &document,
                             const AssetRegistry &assets,
                             std::uint64_t frame_index) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !sync_service_.initialized()) {
    return;
  }
  if (has_previous_scene_snapshot_ && render_scene_snapshot_matches_sources(
                                          previous_scene_snapshot_, document,
                                          assets)) {
    // Keep draining retired GPU proxies while making unchanged camera-only
    // frames O(1) at the scene-extraction boundary.
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_transforms(
          previous_scene_snapshot_, document, assets, frame_index)) {
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets,
      {.previous = has_previous_scene_snapshot_ ? &previous_scene_snapshot_
                                                : nullptr,
       .frame_index = frame_index});
  sync(std::move(snapshot));
}

void SceneRenderBridge::sync(RenderSceneSnapshot snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !sync_service_.initialized()) {
    return;
  }
  (void)sync_service_.sync(snapshot);
  previous_scene_snapshot_ = std::move(snapshot);
  has_previous_scene_snapshot_ = true;
}

void SceneRenderBridge::update_mesh_surface_grid(
    float grid_world_spacing, const ViewportSettings &settings) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !mesh_cache_.initialized()) {
    return;
  }
  mesh_cache_.update_surface_grid(grid_world_spacing, settings);
}

void SceneRenderBridge::sync_scene_asset_overlays(
    const RenderOverlaySelectedObjectSnapshot &selected_object_snapshot,
    const RenderOverlaySceneWireframeSnapshot &wireframe_snapshot) {
  QDR_PROFILE_FUNCTION();
  if (engine_ && scene_ && selected_object_overlay_cache_.initialized()) {
    selected_object_overlay_cache_.sync(selected_object_snapshot);
  }
  if (engine_ && scene_ && scene_wireframe_overlay_cache_.initialized()) {
    scene_wireframe_overlay_cache_.sync(wireframe_snapshot);
  }
}

ViewportOverlayFrame SceneRenderBridge::build_scene_overlay_frame(
    ViewportOverlayFrame base_frame,
    const RenderOverlaySelectionSnapshot &snapshot,
    const ViewportSceneOverlayFrameRequest &request) const {
  QDR_PROFILE_FUNCTION();
  ViewportOverlayFrame frame = ViewportSceneOverlayBuilder::build_selection_frame(
      std::move(base_frame), snapshot, request.point_light_gizmos_visible,
      request.selection_mode);
  if (request.wireframe_pane) {
    frame = ViewportSceneOverlayBuilder::strip_wireframe_selection_fills(
        std::move(frame), request.selection_mode);
  }
  return frame;
}

void SceneRenderBridge::set_tool_preview_meshes(
    std::vector<RenderToolPreviewMeshRecord> meshes) {
  QDR_PROFILE_FUNCTION();
  ++tool_preview_submission_revision_;
  if (!engine_ || !scene_ || !tool_preview_cache_.initialized()) {
    return;
  }
  (void)tool_preview_cache_.set_meshes(meshes);
}

void SceneRenderBridge::set_overlay_frame(ViewportOverlayFrame frame,
                                          ViewportOverlayCamera camera) {
  QDR_PROFILE_FUNCTION();
  (void)overlay_cache_.set_overlay_frame(std::move(frame), camera);
}

void SceneRenderBridge::set_overlay_frame(std::size_t viewport_index,
                                          std::uint8_t layer_mask,
                                          ViewportOverlayFrame frame,
                                          ViewportOverlayCamera camera) {
  QDR_PROFILE_FUNCTION();
  (void)overlay_cache_.set_overlay_frame(viewport_index, layer_mask,
                                         std::move(frame), camera);
}

} // namespace quader
