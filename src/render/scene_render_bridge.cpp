////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/scene_render_bridge.hpp"

#include "diagnostics/profile.hpp"
#include "render/light_render_policy.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/scene_render_light_refresh.hpp"

#include <algorithm>
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
    filament::Texture &solid_color_texture,
    std::filesystem::path resource_root) {
  shutdown();
  engine_ = &engine;
  scene_ = &scene;
  texture_binding_.initialize(engine, resource_root, solid_color_texture);
  material_binding_.initialize(shaded_mesh_material, rendered_mesh_material,
                               texture_binding_);
  mesh_factory_.initialize(engine, scene, material_binding_);
  mesh_cache_.initialize(mesh_factory_);
  instance_cache_.initialize(mesh_factory_, engine.getMaxAutomaticInstances());
  tool_preview_cache_.initialize(mesh_factory_, material_binding_,
                                 engine.getMaxAutomaticInstances());
  tool_preview_light_cache_.initialize(engine, scene);
  overlay_proxy_factory_.initialize(engine, scene, overlay_material,
                                    overlay_line_material,
                                    edit_wire_line_material,
                                    overlay_point_material,
                                    overlay_icon_material, resource_root);
  overlay_cache_.initialize(overlay_proxy_factory_);
  // Each overlay layer reserves 64 global blend-order slots. Dynamic tool
  // records occupy the upper half so their ordering is deterministic above
  // retained selection/scene proxies created by the sibling cache.
  dynamic_overlay_cache_.initialize(overlay_proxy_factory_, 32u);
  instance_selection_overlay_cache_.initialize(
      engine, scene, mesh_factory_, instance_selection_fill_material,
      selected_object_line_material, engine.getMaxAutomaticInstances());
  collision_shape_overlay_cache_.initialize(
      engine, scene, mesh_factory_, instance_selection_fill_material,
      selected_object_line_material,
      engine.getMaxAutomaticInstances());
  scene_wireframe_overlay_cache_.initialize(engine, scene,
                                            selected_object_line_material,
                                            engine.getMaxAutomaticInstances());
  selected_object_overlay_cache_.initialize(engine, scene, overlay_material);
  sync_service_.initialize(engine, scene, mesh_cache_, instance_cache_,
                           material_binding_);
}

void SceneRenderBridge::shutdown() {
  ++tool_preview_submission_revision_;
  overlay_submission_coordinator_.invalidate_all();
  if (engine_) {
    tool_preview_light_cache_.shutdown();
    tool_preview_cache_.shutdown();
    sync_service_.shutdown();
    selected_object_overlay_cache_.shutdown();
    scene_wireframe_overlay_cache_.shutdown();
    instance_selection_overlay_cache_.shutdown();
    collision_shape_overlay_cache_.shutdown();
    mesh_cache_.shutdown();
    instance_cache_.shutdown();
    dynamic_overlay_cache_.shutdown();
    overlay_cache_.shutdown();
    overlay_proxy_factory_.shutdown();
    mesh_factory_.shutdown();
    material_binding_.shutdown();
    texture_binding_.shutdown();
  }

  previous_scene_snapshot_ = {};
  has_previous_scene_snapshot_ = false;
  curve_solid_geometry_cache_.clear();
  scene_binding_ = {};
  project_material_resolver_ = nullptr;
  has_scene_binding_ = false;
  tool_preview_suppression_signature_ = 0u;
  tool_preview_suppression_valid_ = false;
  tool_preview_suppressed_entities_.clear();
  tool_preview_occurrence_suppressions_.clear();
  tool_preview_suppression_ = {};
  tool_preview_overlay_frame_ = {};
  ++tool_preview_overlay_revision_;
  filtered_scene_overlay_snapshot_ = {};
  filtered_collision_snapshot_ = {};
  filtered_collision_valid_ = false;
  filtered_scene_overlay_signature_ = 0u;
  filtered_scene_overlay_valid_ = false;
  tool_preview_overlay_suppression_dirty_ = false;
  active_overlay_viewport_mask_ = static_cast<std::uint8_t>(
      (1u << OverlayRenderableCache::kMaxViewportOverlaySlots) - 1u);
  engine_ = nullptr;
  scene_ = nullptr;
}

void SceneRenderBridge::set_project_material_resolver(
    const RenderProjectMaterialResolver *resolver) {
  if (project_material_resolver_ == resolver) {
    return;
  }
  project_material_resolver_ = resolver;
  material_binding_.set_project_material_resolver(resolver);
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
  curve_solid_geometry_cache_.clear();
  ++tool_preview_submission_revision_;
  tool_preview_suppression_signature_ = 0u;
  tool_preview_suppression_valid_ = false;
  tool_preview_suppressed_entities_.clear();
  tool_preview_occurrence_suppressions_.clear();
  tool_preview_suppression_ = {};
  tool_preview_overlay_frame_ = {};
  ++tool_preview_overlay_revision_;
  filtered_scene_overlay_snapshot_ = {};
  filtered_collision_snapshot_ = {};
  filtered_collision_valid_ = false;
  filtered_scene_overlay_signature_ = 0u;
  filtered_scene_overlay_valid_ = false;
  tool_preview_overlay_suppression_dirty_ = true;
  overlay_submission_coordinator_.invalidate_all();
  // Retire outgoing preview and viewport-overlay material consumers before
  // switching the scene. Their texture pins drain alongside the scene proxies.
  tool_preview_cache_.clear();
  tool_preview_light_cache_.clear();
  mesh_cache_.set_preview_suppressed_entities({});
  instance_cache_.set_preview_suppressed_entities({}, {}, {});
  instance_selection_overlay_cache_.clear(true);
  collision_shape_overlay_cache_.clear();
  for (std::size_t index = 0u;
       index < OverlayRenderableCache::kMaxViewportOverlaySlots; ++index) {
    overlay_cache_.clear_slot(index);
    dynamic_overlay_cache_.clear_slot(index);
  }
  overlay_cache_.reset_transform_position_streams();
  if (sync_service_.initialized()) {
    sync_service_.set_preview_suppressed_lights({});
    sync_service_.bind_scene(scene_binding);
  }
}

void SceneRenderBridge::sync(const SceneDocument &document,
                             const AssetRegistry &assets,
                             std::uint64_t frame_index,
                             RenderSceneBindingKey scene_binding,
                             RenderInstanceEditScope instance_edit_scope,
                             const ViewportLocalVisibilitySnapshot *
                                 local_visibility) {
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
                                          instance_edit_scope,
                                          local_visibility)) {
    // Keep draining retired GPU proxies while making unchanged camera-only
    // frames O(1) at the scene-extraction boundary.
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_light_properties(
          previous_scene_snapshot_, document, assets, frame_index,
          scene_binding, project_material_revision, instance_edit_scope,
          local_visibility)) {
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_curve_publication(
          previous_scene_snapshot_, document, assets, frame_index,
          scene_binding, project_material_revision, instance_edit_scope,
          local_visibility)) {
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_selection(
          previous_scene_snapshot_, document, assets, frame_index,
          scene_binding, project_material_revision,
          instance_edit_scope, local_visibility)) {
    (void)sync_service_.sync(previous_scene_snapshot_);
    return;
  }
  if (has_previous_scene_snapshot_ &&
      try_refresh_render_scene_snapshot_transforms(
          previous_scene_snapshot_, document, assets, frame_index,
          scene_binding, project_material_revision,
          instance_edit_scope, local_visibility)) {
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
       .instance_edit_scope = instance_edit_scope,
       .local_visibility = local_visibility,
       .curve_solid_geometry_cache = &curve_solid_geometry_cache_});
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
  tool_preview_cache_.update_surface_grid(grid_world_spacing, settings);
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

void SceneRenderBridge::perform_idle_resource_maintenance() {
  QDR_PROFILE_FUNCTION();
  if (!tool_preview_cache_.initialized()) {
    return;
  }
  tool_preview_cache_.drain_retired_destroy_queue(1u);
}

void SceneRenderBridge::set_active_overlay_viewports(
    std::uint8_t viewport_mask) {
  const std::uint8_t supported_mask = static_cast<std::uint8_t>(
      (1u << OverlayRenderableCache::kMaxViewportOverlaySlots) - 1u);
  viewport_mask = static_cast<std::uint8_t>(viewport_mask & supported_mask);
  if (viewport_mask == active_overlay_viewport_mask_) {
    return;
  }
  for (std::size_t index = 0u;
       index < OverlayRenderableCache::kMaxViewportOverlaySlots; ++index) {
    const std::uint8_t slot_bit =
        static_cast<std::uint8_t>(1u << index);
    if ((viewport_mask & slot_bit) != 0u) {
      continue;
    }
    overlay_cache_.clear_slot(index);
    dynamic_overlay_cache_.clear_slot(index);
  }
  overlay_submission_coordinator_.set_active_viewports(viewport_mask);
  active_overlay_viewport_mask_ = viewport_mask;
}

void SceneRenderBridge::sync_scene_asset_overlays(
    const RenderOverlaySelectedObjectSnapshot &selected_object_snapshot,
    const RenderOverlaySceneWireframeSnapshot &wireframe_snapshot,
    const RenderOverlayCollisionSnapshot &collision_snapshot) {
  QDR_PROFILE_FUNCTION();
  bind_scene(selected_object_snapshot.scene_binding);
  const RenderOverlaySelectedObjectSnapshot *selected =
      &selected_object_snapshot;
  const RenderOverlaySceneWireframeSnapshot *wireframe = &wireframe_snapshot;
  const RenderOverlayCollisionSnapshot *collision = &collision_snapshot;
  RenderOverlaySelectedObjectSnapshot filtered_selected;
  RenderOverlaySceneWireframeSnapshot filtered_wireframe;
  if (!tool_preview_suppressed_entities_.empty() ||
      !tool_preview_occurrence_suppressions_.empty()) {
    QDR_PROFILE_SCOPE("Tool preview filter persistent overlays");
    const auto suppressed =
        [this](EntityId entity, EntityId occurrence, EntityId definition,
               EntityId member) {
          if ((entity != kInvalidEntityId &&
               tool_preview_suppressed_entities_.contains(entity)) ||
              (occurrence != kInvalidEntityId &&
               tool_preview_suppressed_entities_.contains(occurrence)) ||
              (definition != kInvalidEntityId &&
               tool_preview_suppressed_entities_.contains(definition)) ||
              (member != kInvalidEntityId &&
               tool_preview_suppressed_entities_.contains(member))) {
            return true;
          }
          return std::ranges::any_of(
              tool_preview_occurrence_suppressions_,
              [&](const RenderInstancePreviewSuppression &suppression) {
                return suppression.occurrence == occurrence &&
                       (suppression.source_entity == member ||
                        suppression.source_entity == definition);
              });
        };
    filtered_selected = selected_object_snapshot;
    std::erase_if(filtered_selected.meshes, [&](const auto &mesh) {
      return suppressed(mesh.entity, mesh.occurrence, mesh.definition,
                        mesh.member);
    });
    std::erase_if(filtered_selected.instance_selection_fills,
                  [&](const auto &record) {
                    return suppressed(kInvalidEntityId, record.occurrence,
                                      record.definition, record.member);
                  });
    std::erase_if(filtered_selected.instance_selection_wires,
                  [&](const auto &record) {
                    return suppressed(kInvalidEntityId, record.occurrence,
                                      record.definition, record.member);
                  });
    filtered_selected.local_visibility_revision =
        RenderSignatureBuilder(
            filtered_selected.local_visibility_revision)
            .add_value(tool_preview_suppression_signature_)
            .value();
    selected = &filtered_selected;

    filtered_wireframe = wireframe_snapshot;
    std::erase_if(filtered_wireframe.meshes, [&](const auto &mesh) {
      return suppressed(mesh.entity, mesh.occurrence, mesh.definition,
                        mesh.member);
    });
    filtered_wireframe.local_visibility_revision =
        RenderSignatureBuilder(
            filtered_wireframe.local_visibility_revision)
            .add_value(tool_preview_suppression_signature_)
            .value();
    wireframe = &filtered_wireframe;
    // Selection updates can resync sibling caches during a preview. Keep the
    // collision filter behind its own source gate before copying placements.
    if (!filtered_collision_valid_ || tool_preview_overlay_suppression_dirty_ ||
        filtered_collision_snapshot_.visible != collision_snapshot.visible ||
        filtered_collision_source_stamp_.selection_revision !=
            collision_snapshot.selection_revision ||
        !render_source_dependencies_match(
            filtered_collision_source_stamp_, collision_snapshot,
            kRenderOverlayCollisionSourceDependencies)) {
      filtered_collision_snapshot_ = filter_render_overlay_collision_snapshot(
          collision_snapshot, tool_preview_suppression_);
      filtered_collision_source_stamp_ = collision_snapshot;
      filtered_collision_valid_ = true;
    }
    filtered_collision_snapshot_.color = collision_snapshot.color;
    collision = &filtered_collision_snapshot_;
  } else {
    filtered_collision_valid_ = false;
  }
  if (engine_ && scene_ && collision_shape_overlay_cache_.initialized()) {
    collision_shape_overlay_cache_.sync(*collision, selected_object_snapshot);
  }
  if (engine_ && scene_ && selected_object_overlay_cache_.initialized()) {
    selected_object_overlay_cache_.sync(*selected);
  }
  if (engine_ && scene_ && instance_selection_overlay_cache_.initialized()) {
    instance_selection_overlay_cache_.sync(*selected);
  }
  if (engine_ && scene_ && scene_wireframe_overlay_cache_.initialized()) {
    scene_wireframe_overlay_cache_.sync(*wireframe);
  }
  tool_preview_overlay_suppression_dirty_ = false;
}

RenderToolPreviewSceneSnapshot
SceneRenderBridge::build_tool_preview_scene_snapshot(
    ToolPreviewScene preview, const AssetRegistry &assets,
    const ViewportLocalVisibilitySnapshot *local_visibility) {
  return build_render_tool_preview_scene_snapshot(
      std::move(preview), assets, local_visibility,
      project_material_resolver_, &curve_solid_geometry_cache_);
}

ToolPreviewSubmissionResult SceneRenderBridge::set_tool_preview_meshes(
    std::vector<RenderToolPreviewMeshRecord> meshes) {
  RenderToolPreviewSceneSnapshot preview;
  preview.meshes = std::move(meshes);
  for (const RenderToolPreviewMeshRecord &mesh : preview.meshes) {
    if (!mesh.suppress_source || mesh.source_entity == kInvalidEntityId) {
      continue;
    }
    const EntityId visibility =
        mesh.visibility_entity == kInvalidEntityId ? mesh.source_entity
                                                  : mesh.visibility_entity;
    if (visibility != mesh.source_entity) {
      preview.suppression.occurrences.push_back({
          .source_entity = mesh.source_entity,
          .occurrence = visibility,
      });
    } else {
      preview.suppression.entities.push_back(mesh.source_entity);
    }
  }
  std::ranges::sort(preview.suppression.entities);
  preview.suppression.entities.erase(
      std::ranges::unique(preview.suppression.entities).begin(),
      preview.suppression.entities.end());
  std::ranges::sort(
      preview.suppression.occurrences, {},
      [](const RenderInstancePreviewSuppression &suppression) {
        return std::pair{suppression.occurrence, suppression.source_entity};
      });
  preview.suppression.occurrences.erase(
      std::ranges::unique(preview.suppression.occurrences).begin(),
      preview.suppression.occurrences.end());
  RenderSignatureBuilder signature;
  signature.add_size(preview.suppression.entities.size());
  for (const EntityId entity : preview.suppression.entities) {
    signature.add_value(entity);
  }
  signature.add_size(preview.suppression.occurrences.size());
  for (const auto &occurrence : preview.suppression.occurrences) {
    signature.add_value(occurrence.source_entity)
        .add_value(occurrence.occurrence);
  }
  preview.suppression.signature = signature.value();
  return set_tool_preview_scene(std::move(preview));
}

ToolPreviewSubmissionResult SceneRenderBridge::set_tool_preview_scene(
    RenderToolPreviewSceneSnapshot preview) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !scene_ || !tool_preview_cache_.initialized() ||
      !tool_preview_light_cache_.initialized() || !preview.complete) {
    return ToolPreviewSubmissionResult::Failed;
  }
  std::unordered_set<EntityId> candidate_suppressed_entities(
      preview.suppression.entities.begin(),
      preview.suppression.entities.end());
  std::size_t preview_punctual_light_count = 0u;
  for (const RenderToolPreviewLightRecord &light : preview.lights) {
    if (!filament_light_is_punctual(light.light.type)) {
      continue;
    }
    if (light.instance_transforms.size() >
        kMaximumFilamentPunctualLightCount -
            preview_punctual_light_count) {
      return ToolPreviewSubmissionResult::Failed;
    }
    preview_punctual_light_count += light.instance_transforms.size();
  }
  std::size_t retained_punctual_light_count = 0u;
  for (const RenderSceneLightRecord &light : previous_scene_snapshot_.lights) {
    if (!candidate_suppressed_entities.contains(light.entity_id) &&
        filament_light_is_punctual(light.light.type)) {
      ++retained_punctual_light_count;
    }
  }
  if (preview_punctual_light_count > 0u &&
      (retained_punctual_light_count >
           kMaximumFilamentPunctualLightCount ||
       preview_punctual_light_count >
           kMaximumFilamentPunctualLightCount -
               retained_punctual_light_count)) {
    return ToolPreviewSubmissionResult::Failed;
  }
  ViewportOverlayFrame overlay_candidate =
      build_render_tool_preview_overlay_frame(preview);
  const ToolPreviewSubmissionResult light_result =
      tool_preview_light_cache_.prepare(preview.lights);
  if (light_result == ToolPreviewSubmissionResult::Failed) {
    return light_result;
  }
  const ToolPreviewSubmissionResult mesh_result =
      tool_preview_cache_.set_meshes(preview.meshes);
  if (mesh_result == ToolPreviewSubmissionResult::Failed) {
    QDR_PROFILE_SCOPE("Tool preview preserve previous source coverage");
    tool_preview_light_cache_.discard_prepared();
    return mesh_result;
  }
  tool_preview_light_cache_.publish_prepared();
  ++tool_preview_submission_revision_;
  (void)overlay_cache_.set_transform_position_streams(preview.meshes);
  tool_preview_overlay_frame_ = std::move(overlay_candidate);
  ++tool_preview_overlay_revision_;

  if (!tool_preview_suppression_valid_ ||
      tool_preview_suppression_signature_ != preview.suppression.signature ||
      tool_preview_suppression_.entities != preview.suppression.entities ||
      tool_preview_suppression_.occurrences !=
          preview.suppression.occurrences) {
    QDR_PROFILE_SCOPE("Tool preview update source suppression");
    std::unordered_set<EntityId> suppressed_entities(
        preview.suppression.entities.begin(),
        preview.suppression.entities.end());
    std::vector<RenderInstancePreviewSuppression> occurrence_suppressions =
        preview.suppression.occurrences;
    mesh_cache_.set_preview_suppressed_entities(suppressed_entities);
    tool_preview_suppressed_entities_ = suppressed_entities;
    tool_preview_occurrence_suppressions_ = occurrence_suppressions;
    instance_cache_.set_preview_suppressed_entities(
        std::move(suppressed_entities), std::move(occurrence_suppressions),
        has_previous_scene_snapshot_
            ? std::span<const RenderSceneInstancePrimitiveRecord>(
                  previous_scene_snapshot_.instance_primitives)
            : std::span<const RenderSceneInstancePrimitiveRecord>{});
    sync_service_.set_preview_suppressed_lights(
        tool_preview_suppressed_entities_,
        has_previous_scene_snapshot_
            ? std::span<const RenderSceneLightRecord>(
                  previous_scene_snapshot_.lights)
            : std::span<const RenderSceneLightRecord>{});
    tool_preview_suppression_ = preview.suppression;
    tool_preview_suppression_signature_ = preview.suppression.signature;
    tool_preview_suppression_valid_ = true;
    tool_preview_overlay_suppression_dirty_ = true;
    filtered_scene_overlay_valid_ = false;
  }
  return mesh_result == ToolPreviewSubmissionResult::Published ||
                 light_result == ToolPreviewSubmissionResult::Published
             ? ToolPreviewSubmissionResult::Published
             : ToolPreviewSubmissionResult::Unchanged;
}

ViewportOverlaySubmissionResult SceneRenderBridge::submit_viewport_overlay(
    ViewportOverlaySubmissionRequest request,
    const ViewportSettings &settings) {
  if (!tool_preview_suppression_.empty() &&
      request.scene_overlay_snapshot != nullptr) {
    const RenderOverlaySelectionSnapshot &source =
        *request.scene_overlay_snapshot;
    const std::uint64_t filter_signature =
        RenderSignatureBuilder{}
            .add_value(tool_preview_suppression_.signature)
            .add_value(source.scene_revision)
            .add_value(source.selection_revision)
            .add_value(source.hierarchy_presentation_revision)
            .add_value(source.visibility_revision)
            .add_value(source.local_visibility_revision)
            .add_value(source.transform_revision)
            .add_value(source.mesh_binding_revision)
            .add_value(source.curve_binding_revision)
            .add_value(source.render_component_revision)
            .add_value(source.layer_revision)
            .add_value(source.asset_revision)
            .value();
    if (!filtered_scene_overlay_valid_ ||
        filtered_scene_overlay_signature_ != filter_signature) {
      filtered_scene_overlay_snapshot_ = source;
      const auto suppressed = [&](EntityId entity, EntityId occurrence,
                                  EntityId definition, EntityId member) {
        if ((entity != kInvalidEntityId &&
             tool_preview_suppressed_entities_.contains(entity)) ||
            (occurrence != kInvalidEntityId &&
             tool_preview_suppressed_entities_.contains(occurrence)) ||
            (definition != kInvalidEntityId &&
             tool_preview_suppressed_entities_.contains(definition)) ||
            (member != kInvalidEntityId &&
             tool_preview_suppressed_entities_.contains(member))) {
          return true;
        }
        return std::ranges::any_of(
            tool_preview_occurrence_suppressions_,
            [&](const RenderInstancePreviewSuppression &suppression) {
              return suppression.occurrence == occurrence &&
                     (suppression.source_entity == member ||
                      suppression.source_entity == definition);
            });
      };
      std::erase_if(filtered_scene_overlay_snapshot_.hierarchy_selection_wires,
                    [&](const RenderOverlayMeshSnapshot &mesh) {
                      return suppressed(mesh.entity, mesh.occurrence,
                                        mesh.definition, mesh.member);
                    });
      std::erase_if(filtered_scene_overlay_snapshot_.selected_mesh_bounds,
                    [&](const RenderOverlayBoundsSnapshot &bounds) {
                      return suppressed(bounds.entity, kInvalidEntityId,
                                        kInvalidEntityId, kInvalidEntityId);
                    });
      std::erase_if(filtered_scene_overlay_snapshot_.group_bounds,
                    [&](const RenderOverlayBoundsSnapshot &bounds) {
                      return suppressed(bounds.entity, kInvalidEntityId,
                                        kInvalidEntityId, kInvalidEntityId);
                    });
      std::erase_if(filtered_scene_overlay_snapshot_.selected_lights,
                    [&](const RenderOverlayLightMarkerSnapshot &light) {
                      return suppressed(light.entity, kInvalidEntityId,
                                        kInvalidEntityId, kInvalidEntityId);
                    });
      std::erase_if(filtered_scene_overlay_snapshot_.point_light_gizmos,
                    [&](const RenderOverlayLightMarkerSnapshot &light) {
                      return suppressed(light.entity, kInvalidEntityId,
                                        kInvalidEntityId, kInvalidEntityId);
                    });
      std::erase_if(filtered_scene_overlay_snapshot_.curves,
                    [&](const RenderOverlayCurveSnapshot &curve) {
                      return suppressed(curve.entity, kInvalidEntityId,
                                        kInvalidEntityId, kInvalidEntityId);
                    });
      filtered_scene_overlay_signature_ = filter_signature;
      filtered_scene_overlay_valid_ = true;
    }
    request.scene_overlay_snapshot = &filtered_scene_overlay_snapshot_;
  }
  request.tool_preview_overlay_frame = &tool_preview_overlay_frame_;
  request.tool_preview_overlay_identity =
      reinterpret_cast<std::uintptr_t>(this);
  request.tool_preview_overlay_revision = tool_preview_overlay_revision_;
  return overlay_submission_coordinator_.submit(std::move(request), settings,
                                                *this);
}

ViewportOverlayCameraUpdate SceneRenderBridge::update_overlay_cameras(
    std::size_t viewport_index, std::uint8_t layer_mask,
    ViewportOverlayCamera camera) {
  return {
      .persistent_available = overlay_cache_.update_overlay_camera(
          viewport_index, layer_mask, camera),
      .dynamic_available = dynamic_overlay_cache_.update_overlay_camera(
          viewport_index, layer_mask, camera),
  };
}

void SceneRenderBridge::submit_persistent_overlay(
    std::size_t viewport_index, std::uint8_t layer_mask,
    ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
  (void)overlay_cache_.set_overlay_frame(viewport_index, layer_mask,
                                         std::move(frame), camera);
}

void SceneRenderBridge::submit_dynamic_overlay(
    std::size_t viewport_index, std::uint8_t layer_mask,
    ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
  (void)dynamic_overlay_cache_.set_overlay_frame(
      viewport_index, layer_mask, std::move(frame), camera);
}

} // namespace quader
