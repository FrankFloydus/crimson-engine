////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_bridge.hpp"

#include "diagnostics/profile.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"

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
                                    overlay_point_material,
                                    overlay_icon_material, resource_root);
  overlay_cache_.initialize(overlay_proxy_factory_);
  scene_wireframe_overlay_cache_.initialize(engine, scene,
                                            selected_object_line_material);
  selected_object_overlay_cache_.initialize(engine, scene, overlay_material,
                                            selected_object_line_material);
  sync_service_.initialize(engine, scene, mesh_cache_, material_binding_);
}

void SceneRenderBridge::shutdown() {
    if (!engine_) {
        return;
    }

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

    engine_ = nullptr;
    scene_ = nullptr;
}

void SceneRenderBridge::sync(const RenderSceneSnapshot &snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !sync_service_.initialized()) {
    return;
  }
  (void)sync_service_.sync(snapshot);
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
      std::move(base_frame), snapshot, request.point_light_gizmos_visible);
  if (request.wireframe_pane) {
    frame = ViewportSceneOverlayBuilder::strip_wireframe_selection_fills(
        std::move(frame), request.selection_mode);
  }
  return frame;
}

void SceneRenderBridge::set_tool_preview_meshes(
    std::vector<RenderToolPreviewMeshRecord> meshes) {
  QDR_PROFILE_FUNCTION();
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
