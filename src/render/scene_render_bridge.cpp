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

#include <unordered_set>
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
    filament::Material &instance_selection_fill_material,
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
  instance_cache_.initialize(mesh_factory_, engine.getMaxAutomaticInstances());
  tool_preview_cache_.initialize(mesh_factory_, material_binding_);
  overlay_proxy_factory_.initialize(engine, scene, overlay_material,
                                    overlay_line_material,
                                    edit_wire_line_material,
                                    overlay_point_material,
                                    overlay_icon_material, resource_root);
  overlay_cache_.initialize(overlay_proxy_factory_);
  instance_selection_overlay_cache_.initialize(
      engine, scene, mesh_factory_, instance_selection_fill_material,
      selected_object_line_material, engine.getMaxAutomaticInstances());
  scene_wireframe_overlay_cache_.initialize(engine, scene,
                                            selected_object_line_material,
                                            engine.getMaxAutomaticInstances());
  selected_object_overlay_cache_.initialize(engine, scene, overlay_material);
  sync_service_.initialize(engine, scene, mesh_cache_, instance_cache_,
                           material_binding_);
}

void SceneRenderBridge::shutdown() {
  ++tool_preview_submission_revision_;
  if (engine_) {
    tool_preview_cache_.shutdown();
    sync_service_.shutdown();
    selected_object_overlay_cache_.shutdown();
    scene_wireframe_overlay_cache_.shutdown();
    instance_selection_overlay_cache_.shutdown();
    mesh_cache_.shutdown();
    instance_cache_.shutdown();
    overlay_cache_.shutdown();
    overlay_proxy_factory_.shutdown();
    mesh_factory_.shutdown();
    material_binding_.shutdown();
    texture_binding_.shutdown();
  }

  previous_scene_snapshot_ = {};
  has_previous_scene_snapshot_ = false;
  scene_binding_ = {};
  project_material_resolver_ = nullptr;
  has_scene_binding_ = false;
  engine_ = nullptr;
  scene_ = nullptr;
}

void SceneRenderBridge::set_project_material_resolver(
    const RenderProjectMaterialResolver *resolver) {
  if (project_material_resolver_ == resolver) {
    return;
  }
  project_material_resolver_ = resolver;
  previous_scene_snapshot_ = {};
  has_previous_scene_snapshot_ = false;
}

void SceneRenderBridge::bind_scene(RenderSceneBindingKey scene_binding) {
  if (has_scene_binding_ && scene_binding_ == scene_binding) {
    return;
  }

  scene_binding_ = scene_binding;
  has_scene_binding_ = true;
  previous_scene_snapshot_ = {};
  has_previous_scene_snapshot_ = false;
  ++tool_preview_submission_revision_;
  // Preview and viewport-overlay proxies may own material instances that
  // still reference the outgoing scene's texture cache. Destroy them before
  // the scene sync service invalidates those textures.
  tool_preview_cache_.clear();
  instance_selection_overlay_cache_.clear(true);
  for (std::size_t index = 0u;
       index < OverlayRenderableCache::kMaxViewportOverlaySlots; ++index) {
    overlay_cache_.clear_slot(index);
  }
  if (sync_service_.initialized()) {
    sync_service_.bind_scene(scene_binding);
  }
}

void SceneRenderBridge::sync(const SceneDocument &document,
                             const AssetRegistry &assets,
                             std::uint64_t frame_index,
                             RenderSceneBindingKey scene_binding,
                             RenderInstanceEditScope instance_edit_scope) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !sync_service_.initialized()) {
    return;
  }
  bind_scene(scene_binding);
  const std::uint64_t project_material_revision =
      project_material_resolver_ != nullptr
          ? project_material_resolver_->revision()
          : 0u;
  if (has_previous_scene_snapshot_ && render_scene_snapshot_matches_sources(
                                          previous_scene_snapshot_, document,
                                          assets, scene_binding,
                                          project_material_revision,
                                          instance_edit_scope)) {
    // Keep draining retired GPU proxies while making unchanged camera-only
    // frames O(1) at the scene-extraction boundary.
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_transforms(
          previous_scene_snapshot_, document, assets, frame_index,
          scene_binding, project_material_revision,
          instance_edit_scope)) {
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  RenderSceneSnapshot snapshot = build_render_scene_snapshot(
      document, assets,
      {.previous = has_previous_scene_snapshot_ ? &previous_scene_snapshot_
                                                : nullptr,
       .frame_index = frame_index,
       .scene_binding = scene_binding,
       .project_material_resolver = project_material_resolver_,
       .project_material_revision = project_material_revision,
       .instance_edit_scope = instance_edit_scope});
  sync(std::move(snapshot));
}

void SceneRenderBridge::sync(RenderSceneSnapshot snapshot) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !sync_service_.initialized()) {
    return;
  }
  bind_scene(snapshot.scene_binding);
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
  instance_cache_.update_surface_grid(grid_world_spacing, settings);
  tool_preview_cache_.update_shaded_lighting(settings);
}

SceneRenderResourceDiagnostics SceneRenderBridge::diagnostics() const {
  SceneRenderResourceDiagnostics result;
  result.device_instance_limit = instance_cache_.max_instances_per_chunk();

  std::unordered_set<const MeshGeometryPayload *> payloads;
  const auto add_payload = [&](const RenderMeshAssetSnapshot &mesh) {
    if (mesh.geometry_payload != nullptr &&
        payloads.insert(mesh.geometry_payload.get()).second) {
      result.unique_compiled_payload_bytes +=
          mesh.geometry_payload->byte_size();
    }
  };
  if (has_previous_scene_snapshot_) {
    for (const RenderSceneMeshRecord &mesh : previous_scene_snapshot_.meshes) {
      add_payload(mesh.mesh);
      if (mesh.renderable()) {
        result.standalone_primitive_submissions +=
            coalesced_render_mesh_primitive_spans(mesh.mesh).size() * 3u;
      }
    }
    for (const RenderSceneInstancePrimitiveRecord &primitive :
         previous_scene_snapshot_.instance_primitives) {
      add_payload(primitive.mesh);
    }
  }
  result.unique_compiled_payload_count = payloads.size();

  const MeshRenderableFactory::GeometryCacheStats geometry =
      mesh_factory_.geometry_cache_stats();
  const InstanceRenderableCacheStats instances = instance_cache_.stats();
  const SceneWireframeOverlayCacheStats wire =
      scene_wireframe_overlay_cache_.stats();
  const InstanceSelectionOverlayCacheStats selection =
      instance_selection_overlay_cache_.stats();
  result.gpu_geometry_resource_count =
      geometry.resource_count + wire.unique_geometry_resource_count +
      selection.unique_wire_geometry_resource_count;
  result.gpu_geometry_upload_count =
      geometry.upload_count + wire.geometry_upload_count +
      selection.wire_geometry_upload_count;
  result.gpu_geometry_reuse_count =
      geometry.cache_hit_count + wire.geometry_reuse_count +
      selection.wire_geometry_reuse_count;
  result.gpu_uploaded_vertex_bytes =
      geometry.uploaded_vertex_bytes + wire.derived_vertex_bytes +
      selection.wire_derived_vertex_bytes;
  result.gpu_uploaded_index_bytes =
      geometry.uploaded_index_bytes + wire.derived_index_bytes +
      selection.wire_derived_index_bytes;
  result.base_instance_participant_count = instances.participant_count;
  result.base_instance_batch_chunk_count = instances.chunk_count;
  result.wire_participant_count = wire.participant_count;
  result.wire_batch_chunk_count = wire.chunk_count;
  result.selection_fill_batch_chunk_count = selection.fill_chunk_count;
  result.selection_wire_batch_chunk_count = selection.wire_chunk_count;
  result.planned_primitive_submissions =
      result.standalone_primitive_submissions +
      result.base_instance_batch_chunk_count + result.wire_batch_chunk_count +
      result.selection_fill_batch_chunk_count +
      result.selection_wire_batch_chunk_count;
  return result;
}

void SceneRenderBridge::sync_scene_asset_overlays(
    const RenderOverlaySelectedObjectSnapshot &selected_object_snapshot,
    const RenderOverlaySceneWireframeSnapshot &wireframe_snapshot) {
  QDR_PROFILE_FUNCTION();
  bind_scene(selected_object_snapshot.scene_binding);
  if (engine_ && scene_ && selected_object_overlay_cache_.initialized()) {
    selected_object_overlay_cache_.sync(selected_object_snapshot);
  }
  if (engine_ && scene_ && instance_selection_overlay_cache_.initialized()) {
    instance_selection_overlay_cache_.sync(selected_object_snapshot);
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
