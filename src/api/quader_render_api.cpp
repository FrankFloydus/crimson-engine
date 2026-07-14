////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_render_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_bridge.hpp"
#include "editor/session/scene_session.hpp"

#include <utility>

namespace quader {
namespace {

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
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
    const SceneSession &session,
    const LinearArrayPreviewState &preview,
    std::vector<ModelingToolPreviewMesh> &previews) {
  if (!preview.active) {
    return;
  }
  for (const LinearArrayPreviewSource &source : preview.sources) {
    const MeshAsset *mesh = session.assets().mesh(source.mesh);
    if (mesh == nullptr || source.generated_world_transforms.empty()) {
      continue;
    }
    previews.push_back(ModelingToolPreviewMesh{
        .source_entity = source.source_entity,
        .material = source.material,
        .mesh = *mesh,
        .transform = {},
        .instance_transforms = source.generated_world_transforms,
    });
  }
}

void append_radial_array_preview(
    const SceneSession &session,
    const RadialArrayPreviewState &preview,
    std::vector<ModelingToolPreviewMesh> &previews) {
  if (!preview.active) {
    return;
  }
  for (const RadialArrayPreviewSource &source : preview.sources) {
    const MeshAsset *mesh = session.assets().mesh(source.mesh);
    if (mesh == nullptr || source.generated_world_transforms.empty()) {
      continue;
    }
    previews.push_back(ModelingToolPreviewMesh{
        .source_entity = source.source_entity,
        .material = source.material,
        .mesh = *mesh,
        .transform = {},
        .instance_transforms = source.generated_world_transforms,
    });
  }
}

} // namespace

QuaderRenderApi::QuaderRenderApi(QuaderApiSession session, QuaderApi& api)
    : session_(session)
    , api_(&api) {}

void QuaderRenderApi::rebind_session(QuaderApiSession session,
                                     std::uint64_t scene_session_id,
                                     std::uint64_t activation_generation) {
  session_ = session;
  scene_session_id_ = scene_session_id;
  activation_generation_ = activation_generation;
  ++scene_binding_revision_;
  native_array_preview_sync_bridge_ = nullptr;
  native_array_preview_bridge_submission_revision_ = 0;
  native_array_preview_revision_ = 0;
  native_array_preview_asset_revision_ = 0;
  native_array_preview_kind_ = 0;
}

RenderSceneBindingKey QuaderRenderApiRuntimeAccess::scene_binding(
    const QuaderRenderApi &api) {
  return {
      .scene_session_id = api.scene_session_id_,
      .activation_generation = api.activation_generation_,
      .binding_revision = api.scene_binding_revision_,
  };
}

QuaderApiVoidResult
QuaderRenderApiRuntimeAccess::sync_scene(QuaderRenderApi &api,
                                         SceneRenderBridge &bridge) {
  const SceneSessionReadView session =
      read_scene_session(api.session_).read_view();
  bridge.sync(session.scene, session.assets, 0, scene_binding(api));
  return api.runtime_status("Render scene synchronized.");
}

QuaderApiVoidResult
QuaderRenderApiRuntimeAccess::sync_tool_previews(QuaderRenderApi &api,
                                                 SceneRenderBridge &bridge) {
  bridge.bind_scene(scene_binding(api));
  const SceneSession &project = read_scene_session(api.session_);
  const LinearArrayPreviewState &linear_preview =
      project.linear_array().preview_state();
  const RadialArrayPreviewState &radial_preview =
      project.radial_array().preview_state();

  std::uint8_t native_array_kind = 0;
  std::uint64_t native_array_revision = 0;
  const std::uint64_t native_array_asset_revision =
      project.assets().revision();
  if (linear_preview.active != radial_preview.active) {
    native_array_kind = linear_preview.active ? 1u : 2u;
    native_array_revision = linear_preview.active ? linear_preview.revision
                                                  : radial_preview.revision;
  }
  if (native_array_kind != 0u &&
      api.native_array_preview_sync_bridge_ == &bridge &&
      api.native_array_preview_bridge_submission_revision_ ==
          bridge.tool_preview_submission_revision() &&
      api.native_array_preview_kind_ == native_array_kind &&
      api.native_array_preview_revision_ == native_array_revision &&
      api.native_array_preview_asset_revision_ ==
          native_array_asset_revision) {
    return api.runtime_status("Render tool previews unchanged.");
  }

  std::vector<ModelingToolPreviewMesh> modeling_preview_meshes =
      project.modeling().tool_preview_meshes();
  append_linear_array_preview(project, linear_preview,
                              modeling_preview_meshes);
  append_radial_array_preview(project, radial_preview,
                              modeling_preview_meshes);
  const SceneSessionReadView session = project.read_view();
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
  if (native_array_kind != 0u) {
    api.native_array_preview_sync_bridge_ = &bridge;
    api.native_array_preview_bridge_submission_revision_ =
        bridge.tool_preview_submission_revision();
    api.native_array_preview_revision_ = native_array_revision;
    api.native_array_preview_asset_revision_ = native_array_asset_revision;
    api.native_array_preview_kind_ = native_array_kind;
  } else {
    api.native_array_preview_sync_bridge_ = nullptr;
    api.native_array_preview_bridge_submission_revision_ = 0;
    api.native_array_preview_revision_ = 0;
    api.native_array_preview_asset_revision_ = 0;
    api.native_array_preview_kind_ = 0;
  }
  return api.runtime_status("Render tool previews synchronized.");
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
    QuaderRenderApi &api, SceneRenderBridge &bridge,
    RenderOverlaySnapshotCache &snapshot_cache,
    const QuaderRenderSceneOverlaySyncRequest &request) {
  const SceneSessionReadView session =
      read_scene_session(api.session_).read_view();
  const RenderSceneBindingKey binding = scene_binding(api);
  bridge.bind_scene(binding);
  const RenderOverlayPersistentSnapshotUpdate update =
      snapshot_cache.update_persistent(
          session.scene, session.assets,
          request.selected_object_overlay_visible,
          request.scene_wireframe_layer_mask, request.style, binding);
  if (update.changed()) {
    bridge.sync_scene_asset_overlays(*update.selected_object,
                                     *update.scene_wireframe);
  }
  return api.runtime_status("Render scene overlays synchronized.");
}

const RenderOverlaySelectionSnapshot &
QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
    QuaderRenderApi &api, RenderOverlaySnapshotCache &snapshot_cache) {
  const SceneSessionReadView session =
      read_scene_session(api.session_).read_view();
  return snapshot_cache.selection_snapshot(session.scene, session.assets,
                                           scene_binding(api));
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
  bridge.bind_scene(scene_binding(api));
  ViewportOverlayFrame frame =
      read_scene_session(api.session_).modeling().selection_overlay_frame();
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
  return api_->query().selection_overlay();
}

std::vector<QuaderToolPreviewMesh>
QuaderRenderApi::tool_preview_meshes() const {
  const SceneSession &session = read_scene_session(session_);
  std::vector<ModelingToolPreviewMesh> previews =
      session.modeling().tool_preview_meshes();
  append_linear_array_preview(session, session.linear_array().preview_state(),
                              previews);
  append_radial_array_preview(session, session.radial_array().preview_state(),
                              previews);
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
      .revisions = api_->query().revisions(),
  };
}

} // namespace quader
