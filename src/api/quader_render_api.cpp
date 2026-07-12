////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_render_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_bridge.hpp"
#include "editor/session/project_session.hpp"

#include <utility>

namespace quader {
namespace {

const ProjectSession &read_project_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_project_session(session);
}

bool viewport_overlay_frame_visible(const ViewportOverlayFrame &frame) {
  return !frame.lines.empty() || !frame.triangles.empty() ||
         !frame.points.empty() || !frame.icons.empty();
}

void append_overlay_frame(ViewportOverlayFrame &frame,
                          const ViewportOverlayFrame &appended_frame) {
  frame.lines.insert(frame.lines.end(), appended_frame.lines.begin(),
                     appended_frame.lines.end());
  frame.triangles.insert(frame.triangles.end(),
                         appended_frame.triangles.begin(),
                         appended_frame.triangles.end());
  frame.points.insert(frame.points.end(), appended_frame.points.begin(),
                      appended_frame.points.end());
  frame.icons.insert(frame.icons.end(), appended_frame.icons.begin(),
                     appended_frame.icons.end());
}

void append_linear_array_preview(
    const ProjectSession &session,
    std::vector<ModelingToolPreviewMesh> &previews) {
  const LinearArrayPreviewState preview = session.linear_array().preview_state();
  const MeshAsset *mesh =
      preview.active ? session.assets().mesh(preview.mesh) : nullptr;
  if (mesh == nullptr) {
    return;
  }
  previews.push_back(ModelingToolPreviewMesh{
      .source_entity = preview.source_entity,
      .material = preview.material,
      .mesh = *mesh,
      .transform = {},
      .instance_transforms = preview.copy_world_transforms,
  });
}

} // namespace

QuaderRenderApi::QuaderRenderApi(QuaderApiSession session, QuaderQueryApi& query)
    : session_(session)
    , query_(query) {}

QuaderApiVoidResult
QuaderRenderApiRuntimeAccess::sync_scene(QuaderRenderApi &api,
                                         SceneRenderBridge &bridge) {
  const ProjectSessionReadView session =
      read_project_session(api.session_).read_view();
  bridge.sync(session.scene, session.assets);
  return api.runtime_status("Render scene synchronized.");
}

QuaderApiVoidResult
QuaderRenderApiRuntimeAccess::sync_tool_previews(QuaderRenderApi &api,
                                                 SceneRenderBridge &bridge) {
  const ProjectSession &project = read_project_session(api.session_);
  std::vector<ModelingToolPreviewMesh> modeling_preview_meshes =
      project.modeling().tool_preview_meshes();
  append_linear_array_preview(project, modeling_preview_meshes);
  const ProjectSessionReadView session = project.read_view();
  std::vector<ToolPreviewMesh> render_preview_meshes;
  render_preview_meshes.reserve(modeling_preview_meshes.size());
  for (ModelingToolPreviewMesh &preview_mesh : modeling_preview_meshes) {
    render_preview_meshes.push_back(ToolPreviewMesh{
        .source_entity = preview_mesh.source_entity,
        .material = preview_mesh.material,
        .mesh = std::move(preview_mesh.mesh),
        .transform = preview_mesh.transform,
        .instance_transforms =
            std::move(preview_mesh.instance_transforms),
    });
  }

  bridge.set_tool_preview_meshes(
      build_render_tool_preview_snapshot(render_preview_meshes,
                                         session.assets));
  return api.runtime_status("Render tool previews synchronized.");
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
    QuaderRenderApi &api, SceneRenderBridge &bridge,
    RenderOverlaySnapshotCache &snapshot_cache,
    const QuaderRenderSceneOverlaySyncRequest &request) {
  const ProjectSessionReadView session =
      read_project_session(api.session_).read_view();
  const RenderOverlayPersistentSnapshotUpdate update =
      snapshot_cache.update_persistent(
          session.scene, session.assets,
          request.selected_object_overlay_visible,
          request.scene_wireframe_layer_mask, request.style);
  if (update.changed()) {
    bridge.sync_scene_asset_overlays(*update.selected_object,
                                     *update.scene_wireframe);
  }
  return api.runtime_status("Render scene overlays synchronized.");
}

const RenderOverlaySelectionSnapshot &
QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
    QuaderRenderApi &api, RenderOverlaySnapshotCache &snapshot_cache) {
  const ProjectSessionReadView session =
      read_project_session(api.session_).read_view();
  return snapshot_cache.selection_snapshot(session.scene, session.assets);
}

ViewportOverlayFrame QuaderRenderApiRuntimeAccess::build_scene_overlay_frame(
    SceneRenderBridge &bridge, ViewportOverlayFrame base_frame,
    const RenderOverlaySelectionSnapshot &snapshot,
    const ViewportSceneOverlayFrameRequest &request) {
  return bridge.build_scene_overlay_frame(
      std::move(base_frame), snapshot, request);
}

QuaderApiResult<QuaderRenderOverlaySubmission>
QuaderRenderApiRuntimeAccess::sync_selection_overlay(
    QuaderRenderApi &api, SceneRenderBridge &bridge, std::size_t viewport_index,
    std::uint8_t layer_mask, ViewportOverlayCamera camera,
    const ViewportOverlayFrame *tool_overlay_frame) {
  ViewportOverlayFrame frame =
      read_project_session(api.session_).modeling().selection_overlay_frame();
  if (tool_overlay_frame != nullptr) {
    append_overlay_frame(frame, *tool_overlay_frame);
  }
  const bool visible = viewport_overlay_frame_visible(frame);
  bridge.set_overlay_frame(viewport_index, layer_mask, std::move(frame),
                           camera);

  const QuaderApiVoidResult status =
      api.runtime_status("Render selection overlay synchronized.");
  return quader_api_result_from_status(
      status, QuaderRenderOverlaySubmission{.visible = visible});
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::set_selection_overlay(
    QuaderRenderApi &api, SceneRenderBridge &bridge, std::size_t viewport_index,
    std::uint8_t layer_mask, ViewportOverlayCamera camera) {
  const QuaderApiResult<QuaderRenderOverlaySubmission> result =
      sync_selection_overlay(api, bridge, viewport_index, layer_mask, camera);
  return quader_api_status_from_result(result);
}

QuaderOverlayFrame QuaderRenderApi::selection_overlay_frame() const {
  return query_.selection_overlay();
}

std::vector<QuaderToolPreviewMesh>
QuaderRenderApi::tool_preview_meshes() const {
  const ProjectSession &session = read_project_session(session_);
  std::vector<ModelingToolPreviewMesh> previews =
      session.modeling().tool_preview_meshes();
  append_linear_array_preview(session, previews);
  std::vector<QuaderToolPreviewMesh> result;
  result.reserve(previews.size());
  for (const ModelingToolPreviewMesh &preview : previews) {
    result.push_back(to_api_tool_preview_mesh(preview));
  }
  return result;
}

QuaderApiVoidResult QuaderRenderApi::runtime_status(std::string message) const {
  return {
      .ok = true,
      .changed = false,
      .message = std::move(message),
      .mutation_kind = QuaderMutationKind::Runtime,
      .revisions = query_.revisions(),
  };
}

} // namespace quader
