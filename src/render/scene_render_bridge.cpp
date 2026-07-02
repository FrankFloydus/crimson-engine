////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_bridge.hpp"

#include "diagnostics/profile.hpp"

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
    std::filesystem::path resource_root) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  texture_binding_.initialize(engine, std::move(resource_root));
  material_binding_.initialize(shaded_mesh_material, rendered_mesh_material,
                               texture_binding_);
  mesh_factory_.initialize(engine, scene, material_binding_);
  mesh_cache_.initialize(mesh_factory_);
  tool_preview_cache_.initialize(mesh_factory_, material_binding_);
  overlay_proxy_factory_.initialize(engine, scene, overlay_material,
                                    overlay_line_material,
                                    overlay_point_material);
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

void SceneRenderBridge::sync(const SceneDocument& document, const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !scene_ || !sync_service_.initialized()) {
      return;
    }
    (void)sync_service_.sync(document, assets);
}

void SceneRenderBridge::sync_selected_object_overlay(
    const SceneDocument &document, const AssetRegistry &assets, bool visible) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !selected_object_overlay_cache_.initialized()) {
    return;
  }
  selected_object_overlay_cache_.sync(document, assets, visible);
}

void SceneRenderBridge::sync_scene_wireframe_overlay(
    const SceneDocument &document, const AssetRegistry &assets,
    std::uint8_t layer_mask) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !scene_wireframe_overlay_cache_.initialized()) {
    return;
  }
  scene_wireframe_overlay_cache_.sync(document, assets, layer_mask);
}

void SceneRenderBridge::set_tool_preview_meshes(std::vector<PreviewMesh> meshes,
                                                const AssetRegistry &assets) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !tool_preview_cache_.initialized()) {
    return;
  }
  (void)tool_preview_cache_.set_meshes(meshes, assets);
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
