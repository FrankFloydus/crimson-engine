////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_render_api.hpp"
#include "api/quader_api_session_internal_access.hpp"

#include "api/quader_api.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_api_type_conversions.hpp"
#include "api/quader_query_api.hpp"
#include "editor/array/linear_array_tool_service.hpp"
#include "editor/array/path_array_tool_service.hpp"
#include "editor/array/radial_array_tool_service.hpp"
#include "editor/instances/scene_instance_service.hpp"
#include "editor/modeling/modeling_tool_preview_scene_adapter.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/session/viewport_local_visibility_service.hpp"
#include "render/overlay/render_overlay_snapshot.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_bridge.hpp"
#include "editor/session/scene_session.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <utility>

namespace quader {
namespace {

const SceneSession &read_scene_session(QuaderApiSession session) {
  return QuaderApiSessionInternalAccess::read_scene_session(session);
}

RenderInstanceEditScope instance_edit_render_scope(const SceneSession &session) {
  const InstanceEditContext edit_scope =
      session.instances().instance_edit_scope();
  return edit_scope.active()
             ? RenderInstanceEditScope{.occurrence = edit_scope.occurrence(),
                                       .definition = edit_scope.definition()}
             : RenderInstanceEditScope{};
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
    const MaterialComponent *source_material =
        session.scene().material(source.material_entity);
    const MeshComponent *source_mesh =
        session.scene().mesh(source.source_entity);
    previews.push_back(ModelingToolPreviewMesh{
        .source_entity = source.source_entity,
        .material = source_material != nullptr
                        ? *source_material
                        : MaterialComponent{.material = source.material},
        .mesh = *mesh,
        .transform = {},
        .instance_transforms = source.generated_world_transforms,
        .cast_shadows = source_mesh != nullptr && source_mesh->cast_shadows,
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
    const MaterialComponent *source_material =
        session.scene().material(source.material_entity);
    const MeshComponent *source_mesh =
        session.scene().mesh(source.source_entity);
    previews.push_back(ModelingToolPreviewMesh{
        .source_entity = source.source_entity,
        .material = source_material != nullptr
                        ? *source_material
                        : MaterialComponent{.material = source.material},
        .mesh = *mesh,
        .transform = {},
        .instance_transforms = source.generated_world_transforms,
        .cast_shadows = source_mesh != nullptr && source_mesh->cast_shadows,
    });
  }
}

EntityId preview_visibility_entity(const SceneSession &session,
                                   EntityId source_entity) {
  if (source_entity == kInvalidEntityId) {
    return kInvalidEntityId;
  }
  const SceneInstanceEditState edit_state =
      session.instances().instance_edit_state();
  if (edit_state.active &&
      session.scene().containing_instance_definition(source_entity) ==
          edit_state.definition) {
    return edit_state.occurrence;
  }
  return source_entity;
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
  native_array_preview_local_visibility_revision_ = 0;
  native_array_preview_kind_ = 0;
  native_array_preview_attempt_failed_ = false;
  transform_preview_sync_bridge_ = nullptr;
  transform_preview_bridge_submission_revision_ = 0;
  transform_preview_revision_ = 0;
  transform_preview_asset_revision_ = 0;
  transform_preview_local_visibility_revision_ = 0;
  transform_preview_attempt_failed_ = false;
  generated_tool_preview_sync_bridge_ = nullptr;
  generated_tool_preview_bridge_submission_revision_ = 0;
  generated_tool_preview_source_ = {};
  generated_tool_preview_source_valid_ = false;
  generated_tool_preview_signature_ = 0;
  generated_tool_preview_asset_revision_ = 0;
  generated_tool_preview_local_visibility_revision_ = 0;
  generated_tool_preview_attempt_failed_ = false;
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
  const SceneSession &scene_session = read_scene_session(api.session_);
  const SceneSessionReadView session = scene_session.read_view();
  const ViewportLocalVisibilitySnapshot local_visibility =
      scene_session.viewport_local_visibility().snapshot();
  bridge.sync(session.scene, session.assets, 0, scene_binding(api),
              instance_edit_render_scope(scene_session),
              &local_visibility);
  return api.runtime_status("Render scene synchronized.");
}

QuaderApiVoidResult
QuaderRenderApiRuntimeAccess::sync_tool_previews(
    QuaderRenderApi &api, SceneRenderBridge &bridge,
    const ToolPreviewPublisher &publish,
    const ToolPreviewScenePublisher &publish_scene) {
  bridge.bind_scene(scene_binding(api));
  const SceneSession &project = read_scene_session(api.session_);
  const ViewportLocalVisibilitySnapshot local_visibility =
      project.viewport_local_visibility().snapshot();
  const std::uint64_t local_visibility_revision = local_visibility.revision();
  const LinearArrayPreviewState &linear_preview =
      project.linear_array().preview_state();
  const RadialArrayPreviewState &radial_preview =
      project.radial_array().preview_state();
  const PathArrayPreviewState &path_preview =
      project.path_array().preview_state();

  std::uint8_t native_array_kind = 0;
  std::uint64_t native_array_revision = 0;
  const RenderProjectMaterialResolver *project_material_resolver =
      bridge.project_material_resolver();
  const std::uint64_t project_material_revision =
      project_material_resolver != nullptr
          ? project_material_resolver->revision()
          : 0u;
  const std::uint64_t native_array_asset_revision =
      RenderSignatureBuilder{}
          .add_value(project.assets().revision())
          .add_value(project_material_revision)
          .add_value(project.scene().render_component_revision())
          .add_value(project.scene().texture_mapping_revision())
          .value();
  if (path_preview.active) {
    native_array_kind = 3u;
    native_array_revision = path_preview.revision;
  } else if (linear_preview.active != radial_preview.active) {
    native_array_kind = linear_preview.active ? 1u : 2u;
    native_array_revision = linear_preview.active ? linear_preview.revision
                                                  : radial_preview.revision;
  }
  const auto cached_publication_failure = [&]() {
    return quader_api_failure_status(
        project,
        "Render tool preview candidate could not be published; waiting for "
        "a preview source change.");
  };
  if (native_array_kind != 0u &&
      api.native_array_preview_sync_bridge_ == &bridge &&
      api.native_array_preview_bridge_submission_revision_ ==
          bridge.tool_preview_submission_revision() &&
      api.native_array_preview_kind_ == native_array_kind &&
      api.native_array_preview_revision_ == native_array_revision &&
      api.native_array_preview_asset_revision_ ==
          native_array_asset_revision &&
      api.native_array_preview_local_visibility_revision_ ==
          local_visibility_revision) {
    if (api.native_array_preview_attempt_failed_) {
      return cached_publication_failure();
    }
    return api.runtime_status("Render tool previews unchanged.");
  }

  const bool transform_preview_active =
      project.modeling().transient_transform_preview_active();
  const std::uint64_t transform_preview_revision =
      project.modeling().transient_transform_preview_revision();
  const bool generated_tool_preview_active =
      native_array_kind == 0u && !transform_preview_active;
  QuaderRenderApi::GeneratedToolPreviewSourceStamp
      generated_tool_preview_source;
  if (generated_tool_preview_active) {
    generated_tool_preview_source = {
        .runtime_revision =
            project.modeling().tool_preview_source_revision(),
        .content_revision = project.modeling().content_revision(),
        .object_map_revision =
            project.modeling().modeling_object_map_revision(),
        .scene_transform_revision = project.scene().transform_revision(),
        .hierarchy_presentation_revision =
            project.scene().hierarchy_presentation_revision(),
        .instance_edit_revision =
            project.instances().instance_edit_revision(),
    };
  }
  if (generated_tool_preview_active &&
      api.generated_tool_preview_sync_bridge_ == &bridge &&
      api.generated_tool_preview_bridge_submission_revision_ ==
          bridge.tool_preview_submission_revision() &&
      api.generated_tool_preview_source_valid_ &&
      api.generated_tool_preview_source_ == generated_tool_preview_source &&
      api.generated_tool_preview_asset_revision_ == native_array_asset_revision &&
      api.generated_tool_preview_local_visibility_revision_ ==
          local_visibility_revision) {
    if (api.generated_tool_preview_attempt_failed_) {
      return cached_publication_failure();
    }
    return api.runtime_status("Render generated tool previews unchanged.");
  }

  std::uint64_t generated_tool_preview_signature = 0;
  if (generated_tool_preview_active) {
    generated_tool_preview_signature =
        RenderSignatureBuilder{}
            .add_value(project.modeling().tool_preview_signature())
            .add_value(generated_tool_preview_source.content_revision)
            .add_value(generated_tool_preview_source.object_map_revision)
            .add_value(generated_tool_preview_source.scene_transform_revision)
            .add_value(
                generated_tool_preview_source.hierarchy_presentation_revision)
            .add_value(generated_tool_preview_source.instance_edit_revision)
            .value();
  }
  if (generated_tool_preview_active &&
      !api.generated_tool_preview_attempt_failed_ &&
      api.generated_tool_preview_sync_bridge_ == &bridge &&
      api.generated_tool_preview_bridge_submission_revision_ ==
          bridge.tool_preview_submission_revision() &&
      api.generated_tool_preview_signature_ == generated_tool_preview_signature &&
      api.generated_tool_preview_asset_revision_ == native_array_asset_revision &&
      api.generated_tool_preview_local_visibility_revision_ ==
          local_visibility_revision) {
    api.generated_tool_preview_source_ = generated_tool_preview_source;
    api.generated_tool_preview_source_valid_ = true;
    return api.runtime_status("Render generated tool previews unchanged.");
  }
  if (native_array_kind == 0u && transform_preview_active &&
      api.transform_preview_sync_bridge_ == &bridge &&
      api.transform_preview_bridge_submission_revision_ ==
          bridge.tool_preview_submission_revision() &&
      api.transform_preview_revision_ == transform_preview_revision &&
      api.transform_preview_asset_revision_ == native_array_asset_revision &&
      api.transform_preview_local_visibility_revision_ ==
          local_visibility_revision) {
    if (api.transform_preview_attempt_failed_) {
      return cached_publication_failure();
    }
    return api.runtime_status("Render transform preview unchanged.");
  }

  const SceneSessionReadView session = project.read_view();
  ToolPreviewScene neutral_preview;
  if (native_array_kind == 3u) {
    neutral_preview = project_modeling_tool_preview_scene(path_preview.scene,
                                                          session.scene);
  } else {
    std::vector<ModelingToolPreviewMesh> modeling_preview_meshes =
        project.modeling().tool_preview_meshes();
    append_linear_array_preview(project, linear_preview,
                                modeling_preview_meshes);
    append_radial_array_preview(project, radial_preview,
                                modeling_preview_meshes);
    neutral_preview.meshes.reserve(modeling_preview_meshes.size());
    for (ModelingToolPreviewMesh &preview_mesh : modeling_preview_meshes) {
      const EntityId visibility_entity =
          preview_mesh.visibility_entity != kInvalidEntityId
              ? preview_mesh.visibility_entity
              : preview_visibility_entity(project,
                                          preview_mesh.source_entity);
      const QMat4 mapping = effective_texture_sampling_from_local(
          session.scene, preview_mesh.source_entity,
          visibility_entity == preview_mesh.source_entity
              ? kInvalidEntityId
              : visibility_entity)
                                .value_or(QMat4{});
      neutral_preview.meshes.push_back(ToolPreviewMesh{
          .source_object_id = preview_mesh.source_object_id,
          .source_entity = preview_mesh.source_entity,
          .visibility_entity = visibility_entity,
          .material = std::move(preview_mesh.material),
          .mesh = std::move(preview_mesh.mesh),
          .transform = preview_mesh.transform,
          .instance_transforms =
              std::move(preview_mesh.instance_transforms),
          .sampling_from_local = mapping,
          .instance_sampling_from_local =
              std::move(preview_mesh.instance_sampling_from_local),
          .double_sided = preview_mesh.double_sided,
          .cast_shadows = preview_mesh.cast_shadows,
          .dynamic_positions = preview_mesh.dynamic_positions,
          .suppress_source = preview_mesh.suppress_source,
          .layout_key = preview_mesh.layout_key,
          .position_revision = preview_mesh.position_revision,
          .dynamic_position_payload =
              std::move(preview_mesh.dynamic_position_payload),
      });
    }
  }
  RenderToolPreviewSceneSnapshot preview_snapshot =
      bridge.build_tool_preview_scene_snapshot(
          std::move(neutral_preview), session.assets, &local_visibility);
  const bool compound_publication_required =
      native_array_kind == 3u || !preview_snapshot.curves.empty() ||
      !preview_snapshot.lights.empty() ||
      !preview_snapshot.group_markers.empty();
  ToolPreviewSubmissionResult preview_result;
  if (!preview_snapshot.complete) {
    preview_result = ToolPreviewSubmissionResult::Failed;
  } else if (publish_scene) {
    preview_result = publish_scene(std::move(preview_snapshot));
  } else if (publish && !compound_publication_required) {
    preview_result = publish(std::move(preview_snapshot.meshes));
  } else if (publish) {
    // A mesh-only observer cannot acknowledge Curve, Light, marker, or exact
    // subtree-suppression state without silently publishing a partial scene.
    preview_result = ToolPreviewSubmissionResult::Failed;
  } else {
    preview_result = bridge.set_tool_preview_scene(
        std::move(preview_snapshot));
  }
  const bool publication_failed =
      preview_result == ToolPreviewSubmissionResult::Failed;
  if (generated_tool_preview_active) {
    api.generated_tool_preview_sync_bridge_ = &bridge;
    api.generated_tool_preview_bridge_submission_revision_ =
        bridge.tool_preview_submission_revision();
    api.generated_tool_preview_source_ = generated_tool_preview_source;
    api.generated_tool_preview_source_valid_ = true;
    api.generated_tool_preview_signature_ = generated_tool_preview_signature;
    api.generated_tool_preview_asset_revision_ = native_array_asset_revision;
    api.generated_tool_preview_local_visibility_revision_ =
        local_visibility_revision;
    api.generated_tool_preview_attempt_failed_ = publication_failed;
  } else {
    api.generated_tool_preview_sync_bridge_ = nullptr;
    api.generated_tool_preview_bridge_submission_revision_ = 0;
    api.generated_tool_preview_source_ = {};
    api.generated_tool_preview_source_valid_ = false;
    api.generated_tool_preview_signature_ = 0;
    api.generated_tool_preview_asset_revision_ = 0;
    api.generated_tool_preview_local_visibility_revision_ = 0;
    api.generated_tool_preview_attempt_failed_ = false;
  }
  if (native_array_kind != 0u) {
    api.native_array_preview_sync_bridge_ = &bridge;
    api.native_array_preview_bridge_submission_revision_ =
        bridge.tool_preview_submission_revision();
    api.native_array_preview_revision_ = native_array_revision;
    api.native_array_preview_asset_revision_ = native_array_asset_revision;
    api.native_array_preview_local_visibility_revision_ =
        local_visibility_revision;
    api.native_array_preview_kind_ = native_array_kind;
    api.native_array_preview_attempt_failed_ = publication_failed;
  } else {
    api.native_array_preview_sync_bridge_ = nullptr;
    api.native_array_preview_bridge_submission_revision_ = 0;
    api.native_array_preview_revision_ = 0;
    api.native_array_preview_asset_revision_ = 0;
    api.native_array_preview_local_visibility_revision_ = 0;
    api.native_array_preview_kind_ = 0;
    api.native_array_preview_attempt_failed_ = false;
  }
  if (transform_preview_active) {
    api.transform_preview_sync_bridge_ = &bridge;
    api.transform_preview_bridge_submission_revision_ =
        bridge.tool_preview_submission_revision();
    api.transform_preview_revision_ = transform_preview_revision;
    api.transform_preview_asset_revision_ = native_array_asset_revision;
    api.transform_preview_local_visibility_revision_ =
        local_visibility_revision;
    api.transform_preview_attempt_failed_ = publication_failed;
  } else {
    api.transform_preview_sync_bridge_ = nullptr;
    api.transform_preview_bridge_submission_revision_ = 0;
    api.transform_preview_revision_ = 0;
    api.transform_preview_asset_revision_ = 0;
    api.transform_preview_local_visibility_revision_ = 0;
    api.transform_preview_attempt_failed_ = false;
  }
  if (publication_failed) {
    return cached_publication_failure();
  }
  return api.runtime_status("Render tool previews synchronized.");
}

QuaderApiVoidResult QuaderRenderApiRuntimeAccess::sync_scene_asset_overlays(
    QuaderRenderApi &api, SceneRenderBridge &bridge,
    RenderOverlaySnapshotCache &snapshot_cache,
    const QuaderRenderSceneOverlaySyncRequest &request) {
  const SceneSession &scene_session = read_scene_session(api.session_);
  const SceneSessionReadView session = scene_session.read_view();
  const ViewportLocalVisibilitySnapshot local_visibility =
      scene_session.viewport_local_visibility().snapshot();
  const RenderSceneBindingKey binding = scene_binding(api);
  bridge.bind_scene(binding);
  const RenderOverlayPersistentSnapshotUpdate update =
      snapshot_cache.update_persistent(
          session.scene, session.assets,
          request.selected_object_overlay_visible,
          request.scene_wireframe_layer_mask, request.style, binding,
          &local_visibility, request.selected_scene_wire_visible,
          instance_edit_render_scope(scene_session), request.raster_groups);
  if (update.changed() || bridge.scene_asset_overlay_sync_pending()) {
    bridge.sync_scene_asset_overlays(*update.selected_object,
                                     *update.scene_wireframe);
  }
  return api.runtime_status("Render scene overlays synchronized.");
}

const RenderOverlaySelectionSnapshot &
QuaderRenderApiRuntimeAccess::build_scene_overlay_snapshot(
    QuaderRenderApi &api, RenderOverlaySnapshotCache &snapshot_cache) {
  const SceneSession &scene_session = read_scene_session(api.session_);
  const SceneSessionReadView session = scene_session.read_view();
  const ViewportLocalVisibilitySnapshot local_visibility =
      scene_session.viewport_local_visibility().snapshot();
  return snapshot_cache.selection_snapshot(session.scene, session.assets,
                                           scene_binding(api),
                                           &local_visibility);
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
  const PathArrayPreviewState &path_preview =
      session.path_array().preview_state();
  if (path_preview.active) {
    previews.insert(previews.end(), path_preview.scene.meshes.begin(),
                    path_preview.scene.meshes.end());
  }
  std::vector<QuaderToolPreviewMesh> result;
  result.reserve(previews.size());
  for (const ModelingToolPreviewMesh &preview : previews) {
    const EntityId visibility_entity =
        preview.visibility_entity == kInvalidEntityId
            ? preview.source_entity
            : preview.visibility_entity;
    if (visibility_entity != kInvalidEntityId &&
        !session.viewport_local_visibility().allows_selection_entity(
            visibility_entity)) {
      continue;
    }
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
