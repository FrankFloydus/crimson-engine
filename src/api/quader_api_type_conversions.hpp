////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "assets/material_document.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "editor/modeling/services/modeling_action_catalog.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "scene/components.hpp"

namespace quader {

inline EntityKind to_scene_entity_kind(QuaderEntityKind kind) {
  switch (kind) {
  case QuaderEntityKind::Empty:
    return EntityKind::Empty;
  case QuaderEntityKind::Mesh:
    return EntityKind::Mesh;
  case QuaderEntityKind::Light:
    return EntityKind::Light;
  case QuaderEntityKind::Instance:
    return EntityKind::Instance;
  case QuaderEntityKind::Curve:
    return EntityKind::Curve;
  case QuaderEntityKind::Generic:
    return EntityKind::Generic;
  }
  return EntityKind::Generic;
}

inline QuaderEntityKind to_api_entity_kind(EntityKind kind) {
  switch (kind) {
  case EntityKind::Empty:
    return QuaderEntityKind::Empty;
  case EntityKind::Mesh:
    return QuaderEntityKind::Mesh;
  case EntityKind::Light:
    return QuaderEntityKind::Light;
  case EntityKind::Instance:
    return QuaderEntityKind::Instance;
  case EntityKind::Curve:
    return QuaderEntityKind::Curve;
  case EntityKind::Generic:
    return QuaderEntityKind::Generic;
  }
  return QuaderEntityKind::Generic;
}

inline LightType to_scene_light_type(QuaderLightType type) {
  switch (type) {
  case QuaderLightType::Directional:
    return LightType::Directional;
  case QuaderLightType::Point:
    return LightType::Point;
  case QuaderLightType::Spot:
    return LightType::Spot;
  }
  return LightType::Point;
}

inline QuaderLightType to_api_light_type(LightType type) {
  switch (type) {
  case LightType::Directional:
    return QuaderLightType::Directional;
  case LightType::Point:
    return QuaderLightType::Point;
  case LightType::Spot:
    return QuaderLightType::Spot;
  }
  return QuaderLightType::Point;
}

inline TransformComponent to_scene_transform(const QuaderTransform &transform) {
  return {
      .position = transform.position,
      .rotation_euler = transform.rotation_euler,
      .scale = transform.scale,
  };
}

inline QuaderTransform to_api_transform(const TransformComponent &transform) {
  return {
      .position = transform.position,
      .rotation_euler = transform.rotation_euler,
      .scale = transform.scale,
  };
}

inline MeshCollisionType
to_scene_mesh_collision_type(QuaderMeshCollisionType type) {
  switch (type) {
  case QuaderMeshCollisionType::Box:
    return MeshCollisionType::Box;
  case QuaderMeshCollisionType::Sphere:
    return MeshCollisionType::Sphere;
  case QuaderMeshCollisionType::Cylinder:
    return MeshCollisionType::Cylinder;
  case QuaderMeshCollisionType::ConvexHull:
    return MeshCollisionType::ConvexHull;
  case QuaderMeshCollisionType::Mesh:
    return MeshCollisionType::Mesh;
  }
  return MeshCollisionType::Box;
}

inline QuaderMeshCollisionType
to_api_mesh_collision_type(MeshCollisionType type) {
  switch (type) {
  case MeshCollisionType::Box:
    return QuaderMeshCollisionType::Box;
  case MeshCollisionType::Sphere:
    return QuaderMeshCollisionType::Sphere;
  case MeshCollisionType::Cylinder:
    return QuaderMeshCollisionType::Cylinder;
  case MeshCollisionType::ConvexHull:
    return QuaderMeshCollisionType::ConvexHull;
  case MeshCollisionType::Mesh:
    return QuaderMeshCollisionType::Mesh;
  }
  return QuaderMeshCollisionType::Box;
}

inline MeshPhysicsBodyType
to_scene_mesh_physics_body_type(QuaderMeshPhysicsBodyType type) {
  switch (type) {
  case QuaderMeshPhysicsBodyType::StaticBody:
    return MeshPhysicsBodyType::StaticBody;
  case QuaderMeshPhysicsBodyType::RigidBody:
    return MeshPhysicsBodyType::RigidBody;
  }
  return MeshPhysicsBodyType::StaticBody;
}

inline QuaderMeshPhysicsBodyType
to_api_mesh_physics_body_type(MeshPhysicsBodyType type) {
  switch (type) {
  case MeshPhysicsBodyType::StaticBody:
    return QuaderMeshPhysicsBodyType::StaticBody;
  case MeshPhysicsBodyType::RigidBody:
    return QuaderMeshPhysicsBodyType::RigidBody;
  }
  return QuaderMeshPhysicsBodyType::StaticBody;
}

inline MeshComponent
to_scene_mesh_component(const QuaderMeshComponent &component) {
  return {
      .mesh = component.mesh,
      .editor_only = component.editor_only,
      .cast_shadows = component.cast_shadows,
      .double_sided = component.double_sided,
      .generate_collisions = component.generate_collisions,
      .collision_type = to_scene_mesh_collision_type(component.collision_type),
      .display_collision = component.display_collision,
      .body_type = to_scene_mesh_physics_body_type(component.body_type),
  };
}

inline QuaderMeshComponent
to_api_mesh_component(const MeshComponent &component) {
  return {
      .mesh = component.mesh,
      .editor_only = component.editor_only,
      .cast_shadows = component.cast_shadows,
      .double_sided = component.double_sided,
      .generate_collisions = component.generate_collisions,
      .collision_type = to_api_mesh_collision_type(component.collision_type),
      .display_collision = component.display_collision,
      .body_type = to_api_mesh_physics_body_type(component.body_type),
  };
}

inline LightComponent
to_scene_light_component(const QuaderLightComponent &component) {
  return {
      .type = to_scene_light_type(component.type),
      .color = component.color,
      .intensity = component.intensity,
      .range = component.range,
      .casts_shadows = component.casts_shadows,
      .distance_attenuation = component.distance_attenuation,
      .light_unit = static_cast<LightUnit>(component.light_unit),
      .color_mode = static_cast<LightColorMode>(component.color_mode),
      .color_temperature_kelvin = component.color_temperature_kelvin,
      .source_preset = static_cast<LightSourcePreset>(component.source_preset),
  };
}

inline QuaderLightComponent
to_api_light_component(const LightComponent &component) {
  return {
      .type = to_api_light_type(component.type),
      .color = component.color,
      .intensity = component.intensity,
      .range = component.range,
      .casts_shadows = component.casts_shadows,
      .distance_attenuation = component.distance_attenuation,
      .light_unit = static_cast<QuaderLightUnit>(component.light_unit),
      .color_mode = static_cast<QuaderLightColorMode>(component.color_mode),
      .color_temperature_kelvin = component.color_temperature_kelvin,
      .source_preset = static_cast<QuaderLightSourcePreset>(component.source_preset),
  };
}

inline SceneToneMapping to_scene_tone_mapping(QuaderToneMapping tone_mapping) {
  switch (tone_mapping) {
  case QuaderToneMapping::Linear:
    return SceneToneMapping::Linear;
  case QuaderToneMapping::AcesLegacy:
    return SceneToneMapping::AcesLegacy;
  case QuaderToneMapping::Aces:
    return SceneToneMapping::Aces;
  case QuaderToneMapping::Filmic:
    return SceneToneMapping::Filmic;
  case QuaderToneMapping::DisplayRange:
    return SceneToneMapping::DisplayRange;
  case QuaderToneMapping::Agx:
    return SceneToneMapping::Agx;
  }
  return SceneToneMapping::Aces;
}

inline QuaderToneMapping to_api_tone_mapping(SceneToneMapping tone_mapping) {
  switch (tone_mapping) {
  case SceneToneMapping::Linear:
    return QuaderToneMapping::Linear;
  case SceneToneMapping::AcesLegacy:
    return QuaderToneMapping::AcesLegacy;
  case SceneToneMapping::Aces:
    return QuaderToneMapping::Aces;
  case SceneToneMapping::Filmic:
    return QuaderToneMapping::Filmic;
  case SceneToneMapping::DisplayRange:
    return QuaderToneMapping::DisplayRange;
  case SceneToneMapping::Agx:
    return QuaderToneMapping::Agx;
  }
  return QuaderToneMapping::Aces;
}

inline SceneSettingsComponent
to_scene_settings_component(const QuaderSceneSettings &settings) {
  return {
      .environment =
          SceneEnvironmentSettings{
              .environment_id = settings.environment.environment_id,
              .enabled = settings.environment.enabled,
              .show_background = settings.environment.show_background,
              .clear_color = settings.environment.clear_color,
              .intensity = settings.environment.intensity,
              .background_intensity = settings.environment.background_intensity,
              .rotation_y = settings.environment.rotation_y,
          },
      .camera =
          SceneCameraSettings{
              .aperture = settings.camera.aperture,
              .shutter_speed = settings.camera.shutter_speed,
              .sensitivity = settings.camera.sensitivity,
              .near_plane = settings.camera.near_plane,
              .far_plane = settings.camera.far_plane,
              .use_exposure = settings.camera.use_exposure,
          },
      .post_process =
          ScenePostProcessSettings{
              .enabled = settings.post_process.enabled,
              .anti_aliasing = settings.post_process.anti_aliasing,
              .ambient_occlusion = settings.post_process.ambient_occlusion,
              .ambient_occlusion_intensity =
                  settings.post_process.ambient_occlusion_intensity,
              .ambient_occlusion_radius =
                  settings.post_process.ambient_occlusion_radius,
              .bloom = settings.post_process.bloom,
              .bloom_strength = settings.post_process.bloom_strength,
              .dithering = settings.post_process.dithering,
              .tone_mapping =
                  to_scene_tone_mapping(settings.post_process.tone_mapping),
          },
      .fog = SceneFogSettings{
          .enabled = settings.fog.enabled,
          .distance = settings.fog.distance,
          .cut_off_distance = settings.fog.cut_off_distance,
          .maximum_opacity = settings.fog.maximum_opacity,
          .height = settings.fog.height,
          .height_falloff = settings.fog.height_falloff,
          .color = settings.fog.color,
          .density = settings.fog.density,
          .in_scattering_start = settings.fog.in_scattering_start,
          .in_scattering_size = settings.fog.in_scattering_size,
          .color_from_environment = settings.fog.color_from_environment,
      },
  };
}

inline QuaderSceneSettings
to_api_scene_settings(const SceneSettingsComponent &settings) {
  return {
      .environment =
          QuaderSceneEnvironmentSettings{
              .environment_id = settings.environment.environment_id,
              .enabled = settings.environment.enabled,
              .show_background = settings.environment.show_background,
              .clear_color = settings.environment.clear_color,
              .intensity = settings.environment.intensity,
              .background_intensity = settings.environment.background_intensity,
              .rotation_y = settings.environment.rotation_y,
          },
      .camera =
          QuaderSceneCameraSettings{
              .aperture = settings.camera.aperture,
              .shutter_speed = settings.camera.shutter_speed,
              .sensitivity = settings.camera.sensitivity,
              .near_plane = settings.camera.near_plane,
              .far_plane = settings.camera.far_plane,
              .use_exposure = settings.camera.use_exposure,
          },
      .post_process =
          QuaderScenePostProcessSettings{
              .enabled = settings.post_process.enabled,
              .anti_aliasing = settings.post_process.anti_aliasing,
              .ambient_occlusion = settings.post_process.ambient_occlusion,
              .ambient_occlusion_intensity =
                  settings.post_process.ambient_occlusion_intensity,
              .ambient_occlusion_radius =
                  settings.post_process.ambient_occlusion_radius,
              .bloom = settings.post_process.bloom,
              .bloom_strength = settings.post_process.bloom_strength,
              .dithering = settings.post_process.dithering,
              .tone_mapping =
                  to_api_tone_mapping(settings.post_process.tone_mapping),
          },
      .fog = QuaderSceneFogSettings{
          .enabled = settings.fog.enabled,
          .distance = settings.fog.distance,
          .cut_off_distance = settings.fog.cut_off_distance,
          .maximum_opacity = settings.fog.maximum_opacity,
          .height = settings.fog.height,
          .height_falloff = settings.fog.height_falloff,
          .color = settings.fog.color,
          .density = settings.fog.density,
          .in_scattering_start = settings.fog.in_scattering_start,
          .in_scattering_size = settings.fog.in_scattering_size,
          .color_from_environment = settings.fog.color_from_environment,
      },
  };
}

inline ModelingBounds to_modeling_bounds(const QuaderBounds &bounds) {
  return {
      .min = bounds.min,
      .max = bounds.max,
  };
}

inline QuaderBounds to_api_bounds(const ModelingBounds &bounds) {
  return {
      .min = bounds.min,
      .max = bounds.max,
  };
}

inline ModelingSelectionMode
to_modeling_selection_mode(QuaderSelectionMode mode) {
  switch (mode) {
  case QuaderSelectionMode::Vertex:
    return ModelingSelectionMode::Vertex;
  case QuaderSelectionMode::Edge:
    return ModelingSelectionMode::Edge;
  case QuaderSelectionMode::Face:
    return ModelingSelectionMode::Face;
  case QuaderSelectionMode::Object:
    return ModelingSelectionMode::Object;
  case QuaderSelectionMode::Group:
    return ModelingSelectionMode::Group;
  }
  return ModelingSelectionMode::Object;
}

inline ModelingSelectionEdit
to_modeling_selection_edit(QuaderSelectionEdit edit) {
  switch (edit) {
  case QuaderSelectionEdit::Add:
    return ModelingSelectionEdit::Add;
  case QuaderSelectionEdit::Remove:
    return ModelingSelectionEdit::Remove;
  case QuaderSelectionEdit::Toggle:
    return ModelingSelectionEdit::Toggle;
  case QuaderSelectionEdit::Replace:
    return ModelingSelectionEdit::Replace;
  }
  return ModelingSelectionEdit::Replace;
}

inline ModelingBoxSelectionRule
to_modeling_box_selection_rule(QuaderBoxSelectionRule rule) {
  switch (rule) {
  case QuaderBoxSelectionRule::Contain:
    return ModelingBoxSelectionRule::Contain;
  case QuaderBoxSelectionRule::Crossing:
    return ModelingBoxSelectionRule::Crossing;
  case QuaderBoxSelectionRule::DragDirection:
    return ModelingBoxSelectionRule::DragDirection;
  }
  return ModelingBoxSelectionRule::DragDirection;
}

inline ModelingCutMode to_modeling_cut_mode(QuaderCutMode mode) {
  switch (mode) {
  case QuaderCutMode::DiscardLeft:
    return ModelingCutMode::DiscardLeft;
  case QuaderCutMode::DiscardRight:
    return ModelingCutMode::DiscardRight;
  case QuaderCutMode::KeepBoth:
    return ModelingCutMode::KeepBoth;
  }
  return ModelingCutMode::KeepBoth;
}

inline QuaderCutMode to_api_cut_mode(ModelingCutMode mode) {
  switch (mode) {
  case ModelingCutMode::DiscardLeft:
    return QuaderCutMode::DiscardLeft;
  case ModelingCutMode::DiscardRight:
    return QuaderCutMode::DiscardRight;
  case ModelingCutMode::KeepBoth:
    return QuaderCutMode::KeepBoth;
  }
  return QuaderCutMode::KeepBoth;
}

inline ModelingHalfCutAxis
to_modeling_half_cut_axis(QuaderHalfCutAxis axis) {
  switch (axis) {
  case QuaderHalfCutAxis::X:
    return ModelingHalfCutAxis::X;
  case QuaderHalfCutAxis::Y:
    return ModelingHalfCutAxis::Y;
  case QuaderHalfCutAxis::Z:
    return ModelingHalfCutAxis::Z;
  }
  return ModelingHalfCutAxis::X;
}

inline ModelingInteractionSettings
to_modeling_interaction_settings(const QuaderInteractionSettings &settings) {
  return {
      .bridge_steps = settings.bridge_steps,
      .edge_bevel_width = settings.edge_bevel_width,
      .edge_bevel_profile = settings.edge_bevel_profile,
      .edge_bevel_segments = settings.edge_bevel_segments,
      .thicken_thickness = settings.thicken_thickness,
      .thicken_from_center = settings.thicken_from_center,
      .slice_quad_x_slices = settings.slice_quad_x_slices,
      .slice_quad_y_slices = settings.slice_quad_y_slices,
      .merge_by_distance_tolerance = settings.merge_by_distance_tolerance,
      .half_cut_axis = to_modeling_half_cut_axis(settings.half_cut_axis),
      .half_cut_separate_parts = settings.half_cut_separate_parts,
      .half_cut_fill_parts = settings.half_cut_fill_parts,
      .cut_mode = to_modeling_cut_mode(settings.cut_mode),
  };
}

inline QuaderSelectionMode to_api_selection_mode(ModelingSelectionMode mode) {
  switch (mode) {
  case ModelingSelectionMode::Vertex:
    return QuaderSelectionMode::Vertex;
  case ModelingSelectionMode::Edge:
    return QuaderSelectionMode::Edge;
  case ModelingSelectionMode::Face:
    return QuaderSelectionMode::Face;
  case ModelingSelectionMode::Object:
    return QuaderSelectionMode::Object;
  case ModelingSelectionMode::Group:
    return QuaderSelectionMode::Group;
  }
  return QuaderSelectionMode::Object;
}

inline QuaderActiveSelectionKind
to_api_active_selection_kind(ModelingActiveSelectionKind kind) {
  switch (kind) {
  case ModelingActiveSelectionKind::None:
    return QuaderActiveSelectionKind::None;
  case ModelingActiveSelectionKind::Object:
    return QuaderActiveSelectionKind::Object;
  case ModelingActiveSelectionKind::Vertex:
    return QuaderActiveSelectionKind::Vertex;
  case ModelingActiveSelectionKind::Edge:
    return QuaderActiveSelectionKind::Edge;
  case ModelingActiveSelectionKind::Face:
    return QuaderActiveSelectionKind::Face;
  case ModelingActiveSelectionKind::Light:
    return QuaderActiveSelectionKind::Light;
  }
  return QuaderActiveSelectionKind::None;
}

inline ModelingActiveSelectionKind
to_modeling_active_selection_kind(QuaderActiveSelectionKind kind) {
  switch (kind) {
  case QuaderActiveSelectionKind::None:
    return ModelingActiveSelectionKind::None;
  case QuaderActiveSelectionKind::Object:
    return ModelingActiveSelectionKind::Object;
  case QuaderActiveSelectionKind::Vertex:
    return ModelingActiveSelectionKind::Vertex;
  case QuaderActiveSelectionKind::Edge:
    return ModelingActiveSelectionKind::Edge;
  case QuaderActiveSelectionKind::Face:
    return ModelingActiveSelectionKind::Face;
  case QuaderActiveSelectionKind::Light:
    return ModelingActiveSelectionKind::Light;
  }
  return ModelingActiveSelectionKind::None;
}

inline QuaderSubSelectionKind
to_api_sub_selection_kind(ModelingSubSelectionKind kind) {
  switch (kind) {
  case ModelingSubSelectionKind::None:
    return QuaderSubSelectionKind::None;
  case ModelingSubSelectionKind::CurveControl:
    return QuaderSubSelectionKind::CurveControl;
  }
  return QuaderSubSelectionKind::None;
}

inline ModelingSubSelectionKind
to_modeling_sub_selection_kind(QuaderSubSelectionKind kind) {
  switch (kind) {
  case QuaderSubSelectionKind::None:
    return ModelingSubSelectionKind::None;
  case QuaderSubSelectionKind::CurveControl:
    return ModelingSubSelectionKind::CurveControl;
  }
  return ModelingSubSelectionKind::None;
}

inline QuaderSelectionSummary
to_api_selection_summary(const ModelingSelectionSummary &summary) {
  return {
      .mode = to_api_selection_mode(summary.mode),
      .has_selection = summary.has_selection,
      .active_kind = to_api_active_selection_kind(summary.active_kind),
      .active_object_id = summary.active_object_id,
      .active_vertex_id = summary.active_vertex_id,
      .active_edge_a = summary.active_edge_a,
      .active_edge_b = summary.active_edge_b,
      .active_face_id = summary.active_face_id,
      .selected_object_count = summary.selected_object_count,
      .selected_vertex_count = summary.selected_vertex_count,
      .selected_edge_count = summary.selected_edge_count,
      .selected_face_count = summary.selected_face_count,
      .sub_selection_kind =
          to_api_sub_selection_kind(summary.sub_selection_kind),
      .selected_sub_element_count = summary.selected_sub_element_count,
      .sub_selection_revision = summary.sub_selection_revision,
      .content_revision = summary.content_revision,
      .selection_revision = summary.selection_revision,
  };
}

inline ModelingSelectionSummary
to_modeling_selection_summary(const QuaderSelectionSummary &summary) {
  return {
      .mode = to_modeling_selection_mode(summary.mode),
      .has_selection = summary.has_selection,
      .active_kind = to_modeling_active_selection_kind(summary.active_kind),
      .active_object_id = summary.active_object_id,
      .active_vertex_id = summary.active_vertex_id,
      .active_edge_a = summary.active_edge_a,
      .active_edge_b = summary.active_edge_b,
      .active_face_id = summary.active_face_id,
      .selected_object_count = summary.selected_object_count,
      .selected_vertex_count = summary.selected_vertex_count,
      .selected_edge_count = summary.selected_edge_count,
      .selected_face_count = summary.selected_face_count,
      .sub_selection_kind =
          to_modeling_sub_selection_kind(summary.sub_selection_kind),
      .selected_sub_element_count = summary.selected_sub_element_count,
      .sub_selection_revision = summary.sub_selection_revision,
      .content_revision = summary.content_revision,
      .selection_revision = summary.selection_revision,
  };
}

inline ModelingRay to_modeling_ray(const QuaderRay &ray) {
  return {
      .origin = ray.origin,
      .direction = ray.direction,
  };
}

inline QuaderRay to_api_ray(const ModelingRay &ray) {
  return {
      .origin = ray.origin,
      .direction = ray.direction,
  };
}

inline ModelingViewportCamera
to_modeling_viewport_camera(const QuaderViewportCamera &camera) {
  return {
      .orthographic = camera.orthographic,
      .origin = camera.origin,
      .x_axis = camera.x_axis,
      .y_axis = camera.y_axis,
      .z_axis = camera.z_axis,
      .viewport_size = camera.viewport_size,
      .orthographic_size = camera.orthographic_size,
      .fov_degrees = camera.fov_degrees,
  };
}

inline QuaderViewportCamera
to_api_viewport_camera(const ModelingViewportCamera &camera) {
  return {
      .orthographic = camera.orthographic,
      .origin = camera.origin,
      .x_axis = camera.x_axis,
      .y_axis = camera.y_axis,
      .z_axis = camera.z_axis,
      .viewport_size = camera.viewport_size,
      .orthographic_size = camera.orthographic_size,
      .fov_degrees = camera.fov_degrees,
  };
}

inline ModelingTransform3
to_modeling_transform3(const QuaderTransform3 &transform) {
  return {
      .x_axis = transform.x_axis,
      .y_axis = transform.y_axis,
      .z_axis = transform.z_axis,
      .origin = transform.origin,
  };
}

inline QuaderTransform3 to_api_transform3(const ModelingTransform3 &transform) {
  return {
      .x_axis = transform.x_axis,
      .y_axis = transform.y_axis,
      .z_axis = transform.z_axis,
      .origin = transform.origin,
  };
}

inline ModelingTransformTool
to_modeling_transform_tool(QuaderTransformTool tool) {
  switch (tool) {
  case QuaderTransformTool::Move:
    return ModelingTransformTool::Move;
  case QuaderTransformTool::Rotate:
    return ModelingTransformTool::Rotate;
  case QuaderTransformTool::Scale:
    return ModelingTransformTool::Scale;
  }
  return ModelingTransformTool::Move;
}

inline QuaderTransformTool to_api_transform_tool(ModelingTransformTool tool) {
  switch (tool) {
  case ModelingTransformTool::Move:
    return QuaderTransformTool::Move;
  case ModelingTransformTool::Rotate:
    return QuaderTransformTool::Rotate;
  case ModelingTransformTool::Scale:
    return QuaderTransformTool::Scale;
  }
  return QuaderTransformTool::Move;
}

inline ModelingTransformOperation
to_modeling_transform_operation(QuaderTransformOperation operation) {
  switch (operation) {
  case QuaderTransformOperation::Transform:
    return ModelingTransformOperation::Transform;
  case QuaderTransformOperation::Extrude:
    return ModelingTransformOperation::Extrude;
  case QuaderTransformOperation::Inset:
    return ModelingTransformOperation::Inset;
  }
  return ModelingTransformOperation::Transform;
}

inline QuaderTransformOperation
to_api_transform_operation(ModelingTransformOperation operation) {
  switch (operation) {
  case ModelingTransformOperation::Transform:
    return QuaderTransformOperation::Transform;
  case ModelingTransformOperation::Extrude:
    return QuaderTransformOperation::Extrude;
  case ModelingTransformOperation::Inset:
    return QuaderTransformOperation::Inset;
  }
  return QuaderTransformOperation::Transform;
}

inline ModelingTransformAxis
to_modeling_transform_axis(QuaderTransformAxis axis) {
  switch (axis) {
  case QuaderTransformAxis::None:
    return ModelingTransformAxis::None;
  case QuaderTransformAxis::X:
    return ModelingTransformAxis::X;
  case QuaderTransformAxis::Y:
    return ModelingTransformAxis::Y;
  case QuaderTransformAxis::Z:
    return ModelingTransformAxis::Z;
  case QuaderTransformAxis::XY:
    return ModelingTransformAxis::XY;
  case QuaderTransformAxis::XZ:
    return ModelingTransformAxis::XZ;
  case QuaderTransformAxis::YZ:
    return ModelingTransformAxis::YZ;
  case QuaderTransformAxis::All:
    return ModelingTransformAxis::All;
  }
  return ModelingTransformAxis::None;
}

inline QuaderTransformAxis to_api_transform_axis(ModelingTransformAxis axis) {
  switch (axis) {
  case ModelingTransformAxis::None:
    return QuaderTransformAxis::None;
  case ModelingTransformAxis::X:
    return QuaderTransformAxis::X;
  case ModelingTransformAxis::Y:
    return QuaderTransformAxis::Y;
  case ModelingTransformAxis::Z:
    return QuaderTransformAxis::Z;
  case ModelingTransformAxis::XY:
    return QuaderTransformAxis::XY;
  case ModelingTransformAxis::XZ:
    return QuaderTransformAxis::XZ;
  case ModelingTransformAxis::YZ:
    return QuaderTransformAxis::YZ;
  case ModelingTransformAxis::All:
    return QuaderTransformAxis::All;
  }
  return QuaderTransformAxis::None;
}

inline ModelingPickSelectRequest
to_modeling_pick_select_request(const QuaderPickSelectRequest &request) {
  return {
      .ray = to_modeling_ray(request.ray),
      .click_count = request.click_count,
      .control_pressed = request.control_pressed,
      .shift_pressed = request.shift_pressed,
      .command_pressed = request.command_pressed,
      .alt_pressed = request.alt_pressed,
      .preserve_selection_on_miss = request.preserve_selection_on_miss,
      .preserve_selection_on_selected_hit =
          request.preserve_selection_on_selected_hit,
      .preserve_existing_component_selection_on_hit =
          request.preserve_existing_component_selection_on_hit,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
      .select_backfaces = request.select_backfaces,
      .cycle_depth = request.cycle_depth,
      .cycle_origin_tolerance = request.cycle_origin_tolerance,
  };
}

inline ModelingBoxSelectRequest
to_modeling_box_select_request(const QuaderBoxSelectRequest &request) {
  return {
      .camera = to_modeling_viewport_camera(request.camera),
      .start = request.start,
      .end = request.end,
      .edit = to_modeling_selection_edit(request.edit),
      .rule = to_modeling_box_selection_rule(request.rule),
      .select_backfaces = request.select_backfaces,
  };
}

inline ModelingHoverRequest
to_modeling_hover_request(const QuaderHoverRequest &request) {
  return {
      .ray = to_modeling_ray(request.ray),
      .selected_modifier_pressed = request.selected_modifier_pressed,
      .face_plane_hover = request.face_plane_hover,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
      .select_backfaces = request.select_backfaces,
  };
}

inline QuaderHoverRequest
to_api_hover_request(const ModelingHoverRequest &request) {
  return {
      .ray = to_api_ray(request.ray),
      .selected_modifier_pressed = request.selected_modifier_pressed,
      .face_plane_hover = request.face_plane_hover,
      .vertex_radius = request.vertex_radius,
      .edge_radius = request.edge_radius,
      .select_backfaces = request.select_backfaces,
  };
}

inline QuaderHoverTarget
to_api_hover_target(const ModelingHoverTarget &target) {
  return {
      .kind = to_api_active_selection_kind(target.kind),
      .object_id = target.object_id,
      .vertex_id = target.vertex_id,
      .edge_a = target.edge_a,
      .edge_b = target.edge_b,
      .face_id = target.face_id,
  };
}

inline ModelingHoverTarget
to_modeling_hover_target(const QuaderHoverTarget &target) {
  return {
      .kind = to_modeling_active_selection_kind(target.kind),
      .object_id = target.object_id,
      .vertex_id = target.vertex_id,
      .edge_a = target.edge_a,
      .edge_b = target.edge_b,
      .face_id = target.face_id,
  };
}

inline QuaderHoverSummary
to_api_hover_summary(const ModelingHoverSummary &summary) {
  return {
      .changed = summary.changed,
      .target = to_api_hover_target(summary.target),
      .target_selected = summary.target_selected,
  };
}

inline ModelingHoverSummary
to_modeling_hover_summary(const QuaderHoverSummary &summary) {
  return {
      .changed = summary.changed,
      .target = to_modeling_hover_target(summary.target),
      .target_selected = summary.target_selected,
  };
}

inline ModelingTransformDragBeginRequest
to_modeling_transform_drag_begin_request(
    const QuaderTransformDragBeginRequest &request) {
  return {
      .tool = to_modeling_transform_tool(request.tool),
      .operation = to_modeling_transform_operation(request.operation),
      .axis = to_modeling_transform_axis(request.axis),
      .space = request.space == QuaderTransformSpace::Local
                   ? ModelingTransformSpace::Local
                   : ModelingTransformSpace::Global,
      .orientation = to_modeling_transform3(request.orientation),
      .pivot = request.pivot,
      .screen_position = request.screen_position,
      .screen_pivot = request.screen_pivot,
      .screen_tip = request.screen_tip,
      .has_world_direction = request.has_world_direction,
      .world_direction = request.world_direction,
      .ray = to_modeling_ray(request.ray),
      .camera = to_modeling_viewport_camera(request.camera),
      .grid_size = request.grid_size,
      .vertex_snap_radius = request.vertex_snap_radius,
      .grid_snap_enabled = request.grid_snap_enabled,
      .rotation_snap_degrees = request.rotation_snap_degrees,
      .precision = request.precision,
      .duplicate_selected = request.duplicate_selected,
  };
}

inline ModelingTransformDragUpdateRequest
to_modeling_transform_drag_update_request(
    const QuaderTransformDragUpdateRequest &request) {
  return {
      .screen_position = request.screen_position,
      .ray = to_modeling_ray(request.ray),
      .camera = to_modeling_viewport_camera(request.camera),
      .grid_size = request.grid_size,
      .vertex_snap_radius = request.vertex_snap_radius,
      .grid_snap_enabled = request.grid_snap_enabled,
      .rotation_snap_degrees = request.rotation_snap_degrees,
      .precision = request.precision,
  };
}

inline QuaderTransformDragPreview
to_api_transform_drag_preview(const ModelingTransformDragPreview &preview) {
  return {
      .ok = preview.ok,
      .active = preview.active,
      .changed = preview.changed,
      .preview_driven = preview.preview_driven,
      .message = preview.message,
      .tool = to_api_transform_tool(preview.tool),
      .operation = to_api_transform_operation(preview.operation),
      .axis = to_api_transform_axis(preview.axis),
      .pivot = preview.pivot,
      .move_delta = preview.move_delta,
      .rotate_angle_degrees = preview.rotate_angle_degrees,
      .scale = preview.scale,
      .content_revision = preview.content_revision,
      .selection_revision = preview.selection_revision,
      .scene_transform = preview.scene_transform,
      .has_scene_preview_transform = preview.has_scene_preview_transform,
      .scene_preview_transform =
          to_api_transform3(preview.scene_preview_transform),
  };
}

inline QuaderViewportTool to_api_viewport_tool(ModelingViewportTool tool) {
  switch (tool) {
  case ModelingViewportTool::Select:
    return QuaderViewportTool::Select;
  case ModelingViewportTool::LassoSelect:
    return QuaderViewportTool::LassoSelect;
  case ModelingViewportTool::Move:
    return QuaderViewportTool::Move;
  case ModelingViewportTool::Extrude:
    return QuaderViewportTool::Extrude;
  case ModelingViewportTool::Rotate:
    return QuaderViewportTool::Rotate;
  case ModelingViewportTool::Scale:
    return QuaderViewportTool::Scale;
  case ModelingViewportTool::Box:
    return QuaderViewportTool::CreatePrimitive;
  case ModelingViewportTool::Cylinder:
    return QuaderViewportTool::CreatePrimitive;
  case ModelingViewportTool::Sphere:
    return QuaderViewportTool::CreatePrimitive;
  case ModelingViewportTool::InsertEdgeLoop:
    return QuaderViewportTool::InsertEdgeLoop;
  case ModelingViewportTool::Knife:
    return QuaderViewportTool::Knife;
  case ModelingViewportTool::Cut:
    return QuaderViewportTool::Cut;
  case ModelingViewportTool::Poly:
    return QuaderViewportTool::Poly;
  case ModelingViewportTool::Mirror:
    return QuaderViewportTool::Mirror;
  }
  return QuaderViewportTool::Select;
}

inline QuaderToolStatus to_api_tool_status(const ModelingToolStatus &status) {
  return {
      .active_tool = to_api_viewport_tool(status.active_tool),
      .modal_tool_owns_viewport = status.modal_tool_owns_viewport,
      .insert_edge_loop_active = status.insert_edge_loop_active,
      .insert_edge_loop_dragging = status.insert_edge_loop_dragging,
      .insert_edge_loop_has_preview = status.insert_edge_loop_has_preview,
      .knife_active = status.knife_active,
      .knife_has_current = status.knife_has_current,
      .knife_can_commit = status.knife_can_commit,
      .cut_active = status.cut_active,
      .cut_can_preview = status.cut_can_preview,
      .cut_preview_valid = status.cut_preview_valid,
      .cut_dragging = status.cut_dragging,
      .cut_dragging_new_point = status.cut_dragging_new_point,
      .cut_dragging_line = status.cut_dragging_line,
      .cut_dragging_point_index = status.cut_dragging_point_index,
      .cut_point_count = status.cut_point_count,
      .cut_captured_point_count = status.cut_captured_point_count,
      .cut_has_line = status.cut_has_line,
      .cut_points = status.cut_points,
      .cut_mode = to_api_cut_mode(status.cut_mode),
      .mirror_active = status.mirror_active,
      .mirror_can_preview = status.mirror_can_preview,
      .mirror_preview_valid = status.mirror_preview_valid,
      .mirror_dragging = status.mirror_dragging,
      .mirror_dragging_new_point = status.mirror_dragging_new_point,
      .mirror_dragging_line = status.mirror_dragging_line,
      .mirror_dragging_point_index = status.mirror_dragging_point_index,
      .mirror_point_count = status.mirror_point_count,
      .mirror_captured_point_count = status.mirror_captured_point_count,
      .mirror_has_line = status.mirror_has_line,
      .mirror_points = status.mirror_points,
      .poly_active = status.poly_active,
      .poly_closed = status.poly_closed,
      .poly_has_current = status.poly_has_current,
      .poly_can_commit = status.poly_can_commit,
      .poly_point_count = status.poly_point_count,
  };
}

inline QuaderModelingObjectRecord
to_api_modeling_object_record(const ModelingObjectRecord &record) {
  return {
      .modeling_object_id = record.modeling_object_id,
      .entity = record.entity,
      .mesh = record.mesh,
      .name = record.name,
  };
}

inline ModelingActionSurface
to_modeling_action_surface(QuaderActionSurface surface) {
  switch (surface) {
  case QuaderActionSurface::ViewportContextMenu:
    return ModelingActionSurface::ViewportContextMenu;
  case QuaderActionSurface::OperationsPanel:
    return ModelingActionSurface::OperationsPanel;
  }
  return ModelingActionSurface::OperationsPanel;
}

inline QuaderActionSurface
to_api_action_surface(ModelingActionSurface surface) {
  switch (surface) {
  case ModelingActionSurface::ViewportContextMenu:
    return QuaderActionSurface::ViewportContextMenu;
  case ModelingActionSurface::OperationsPanel:
    return QuaderActionSurface::OperationsPanel;
  }
  return QuaderActionSurface::OperationsPanel;
}

inline QuaderActionVisibility
to_api_action_visibility(ModelingActionVisibility visibility) {
  switch (visibility) {
  case ModelingActionVisibility::Hidden:
    return QuaderActionVisibility::Hidden;
  case ModelingActionVisibility::Disabled:
    return QuaderActionVisibility::Disabled;
  case ModelingActionVisibility::Enabled:
    return QuaderActionVisibility::Enabled;
  }
  return QuaderActionVisibility::Hidden;
}

inline QuaderActionKind to_api_action_kind(ModelingActionKind kind) {
  switch (kind) {
  case ModelingActionKind::Operation:
    return QuaderActionKind::Operation;
  case ModelingActionKind::Tool:
    return QuaderActionKind::Tool;
  }
  return QuaderActionKind::Operation;
}

inline QuaderActionPreviewKind
to_api_action_preview_kind(ModelingActionPreviewKind kind) {
  switch (kind) {
  case ModelingActionPreviewKind::None:
    return QuaderActionPreviewKind::None;
  case ModelingActionPreviewKind::Bridge:
    return QuaderActionPreviewKind::Bridge;
  case ModelingActionPreviewKind::EdgeBevel:
    return QuaderActionPreviewKind::EdgeBevel;
  case ModelingActionPreviewKind::Thicken:
    return QuaderActionPreviewKind::Thicken;
  case ModelingActionPreviewKind::SliceQuad:
    return QuaderActionPreviewKind::SliceQuad;
  case ModelingActionPreviewKind::MergeByDistance:
    return QuaderActionPreviewKind::MergeByDistance;
  case ModelingActionPreviewKind::HalfCut:
    return QuaderActionPreviewKind::HalfCut;
  }
  return QuaderActionPreviewKind::None;
}

inline ModelingActionQuery
to_modeling_action_query(const QuaderActionQuery &query) {
  return {
      .surface = to_modeling_action_surface(query.surface),
      .modal_tool_owns_viewport = query.modal_tool_owns_viewport,
      .suppress_when_modal_tool_owns_viewport =
          query.suppress_when_modal_tool_owns_viewport,
      .include_disabled = query.include_disabled,
      .include_hidden = query.include_hidden,
  };
}

inline QuaderActionItem to_api_action_item(const ModelingActionItem &item) {
  return {
      .action_id = item.action_id,
      .operation_id = item.operation_id,
      .tool_id = item.tool_id,
      .label = item.label,
      .kind = to_api_action_kind(item.kind),
      .preview_kind = to_api_action_preview_kind(item.preview_kind),
      .visibility = to_api_action_visibility(item.visibility),
      .disabled_reason = item.disabled_reason,
      .disabled_reason_key = item.disabled_reason_key,
      .disabled_reason_count = item.disabled_reason_count,
      .rebuilds_mesh_on_success = item.rebuilds_mesh_on_success,
  };
}

inline QuaderActionPanel to_api_action_panel(const ModelingActionPanel &panel) {
  QuaderActionPanel result;
  result.panel_id = panel.panel_id;
  result.label = panel.label;
  result.selection_mode_id = panel.selection_mode_id;
  result.visibility = to_api_action_visibility(panel.visibility);
  result.actions.reserve(panel.actions.size());
  for (const ModelingActionItem &item : panel.actions) {
    result.actions.push_back(to_api_action_item(item));
  }
  return result;
}

inline QuaderActionSet to_api_action_set(const ModelingActionSet &set) {
  QuaderActionSet result;
  result.surface = to_api_action_surface(set.surface);
  result.surface_id = set.surface_id;
  result.panels.reserve(set.panels.size());
  for (const ModelingActionPanel &panel : set.panels) {
    result.panels.push_back(to_api_action_panel(panel));
  }
  return result;
}

inline QuaderInstantOperationDescriptor to_api_instant_operation_descriptor(
    const ModelingInstantOperationDescriptor &operation) {
  return {
      .id = operation.id,
      .label = operation.label,
      .enabled = operation.enabled,
      .rebuilds_mesh_on_success = operation.rebuilds_mesh_on_success,
  };
}

inline QuaderMeshVertex to_api_mesh_vertex(const MeshVertex &vertex) {
  return {
      .position = vertex.position,
      .normal = vertex.normal,
      .uv = vertex.uv,
      .rgba = vertex.rgba,
      .uv_carrier = vertex.uv_carrier,
      .uv_geometric_normal = vertex.uv_geometric_normal,
  };
}

inline MeshVertex to_mesh_vertex(const QuaderMeshVertex &vertex) {
  return {
      .position = vertex.position,
      .normal = vertex.normal,
      .uv = vertex.uv,
      .rgba = vertex.rgba,
      .uv_carrier = vertex.uv_carrier,
      .uv_geometric_normal = vertex.uv_geometric_normal,
  };
}

inline QuaderMeshPrimitiveSpan
to_api_mesh_primitive_span(const MeshPrimitiveSpan &primitive) {
  return {
      .index_offset = primitive.index_offset,
      .index_count = primitive.index_count,
      .material_slot = primitive.material_slot,
  };
}

inline MeshPrimitiveSpan
to_mesh_primitive_span(const QuaderMeshPrimitiveSpan &primitive) {
  return {
      .index_offset = primitive.index_offset,
      .index_count = primitive.index_count,
      .material_slot = primitive.material_slot,
  };
}

inline QuaderMeshAsset to_api_mesh_asset(const MeshAsset &mesh) {
  QuaderMeshAsset result;
  const MeshGeometryPayload &geometry = mesh.geometry();
  result.id = mesh.id;
  result.revision = mesh.revision.value;
  result.name = mesh.name;
  result.vertices.reserve(geometry.vertices.size());
  for (const MeshVertex &vertex : geometry.vertices) {
    result.vertices.push_back(to_api_mesh_vertex(vertex));
  }
  result.indices = geometry.indices;
  result.primitives.reserve(geometry.primitives.size());
  for (const MeshPrimitiveSpan &primitive : geometry.primitives) {
    result.primitives.push_back(to_api_mesh_primitive_span(primitive));
  }
  result.has_stable_cpu_topology = mesh.has_stable_cpu_topology;
  result.has_authored_polygon_document = mesh.has_authored_polygon_document;
  if (mesh.authored_polygon_document != nullptr) {
    result.face_textures.reserve(mesh.authored_polygon_document->faces.size());
    for (const quader_poly::Face &face : mesh.authored_polygon_document->faces) {
      result.face_textures.push_back({
          .face = static_cast<QuaderElementId>(face.id),
          .scale = face.texture_transform.scale,
          .shift_uv = face.texture_transform.shift_uv,
          .rotation_degrees = face.texture_transform.rotation_degrees,
      });
    }
  }
  return result;
}

inline MeshAsset to_mesh_asset(const QuaderMeshAsset &mesh) {
  MeshAsset result;
  MeshGeometryPayload &geometry = result.edit_geometry();
  result.id = mesh.id;
  result.revision.value = mesh.revision;
  result.name = mesh.name;
  geometry.vertices.reserve(mesh.vertices.size());
  for (const QuaderMeshVertex &vertex : mesh.vertices) {
    geometry.vertices.push_back(to_mesh_vertex(vertex));
  }
  geometry.indices = mesh.indices;
  geometry.primitives.reserve(mesh.primitives.size());
  for (const QuaderMeshPrimitiveSpan &primitive : mesh.primitives) {
    geometry.primitives.push_back(to_mesh_primitive_span(primitive));
  }
  result.has_stable_cpu_topology = mesh.has_stable_cpu_topology;
  result.has_authored_polygon_document = mesh.has_authored_polygon_document;
  return result;
}

inline QuaderTextureUsage to_api_texture_usage(TextureUsage usage) {
  switch (usage) {
  case TextureUsage::Unknown:
    return QuaderTextureUsage::Unknown;
  case TextureUsage::BaseColor:
    return QuaderTextureUsage::BaseColor;
  case TextureUsage::Normal:
    return QuaderTextureUsage::Normal;
  case TextureUsage::Roughness:
    return QuaderTextureUsage::Roughness;
  case TextureUsage::Metallic:
    return QuaderTextureUsage::Metallic;
  case TextureUsage::Curvature:
    return QuaderTextureUsage::Curvature;
  case TextureUsage::Thickness:
    return QuaderTextureUsage::Thickness;
  case TextureUsage::AmbientOcclusion:
    return QuaderTextureUsage::AmbientOcclusion;
  case TextureUsage::Position:
    return QuaderTextureUsage::Position;
  case TextureUsage::Id:
    return QuaderTextureUsage::Id;
  case TextureUsage::Mask:
    return QuaderTextureUsage::Mask;
  }
  return QuaderTextureUsage::Unknown;
}

inline TextureUsage to_texture_usage(QuaderTextureUsage usage) {
  switch (usage) {
  case QuaderTextureUsage::Unknown:
    return TextureUsage::Unknown;
  case QuaderTextureUsage::BaseColor:
    return TextureUsage::BaseColor;
  case QuaderTextureUsage::Normal:
    return TextureUsage::Normal;
  case QuaderTextureUsage::Roughness:
    return TextureUsage::Roughness;
  case QuaderTextureUsage::Metallic:
    return TextureUsage::Metallic;
  case QuaderTextureUsage::Curvature:
    return TextureUsage::Curvature;
  case QuaderTextureUsage::Thickness:
    return TextureUsage::Thickness;
  case QuaderTextureUsage::AmbientOcclusion:
    return TextureUsage::AmbientOcclusion;
  case QuaderTextureUsage::Position:
    return TextureUsage::Position;
  case QuaderTextureUsage::Id:
    return TextureUsage::Id;
  case QuaderTextureUsage::Mask:
    return TextureUsage::Mask;
  }
  return TextureUsage::Unknown;
}

inline QuaderTextureColorSpace
to_api_texture_color_space(TextureColorSpace color_space) {
  return color_space == TextureColorSpace::Linear
             ? QuaderTextureColorSpace::Linear
             : QuaderTextureColorSpace::Srgb;
}

inline TextureColorSpace
to_texture_color_space(QuaderTextureColorSpace color_space) {
  return color_space == QuaderTextureColorSpace::Linear
             ? TextureColorSpace::Linear
             : TextureColorSpace::Srgb;
}

inline QuaderTextureAsset to_api_texture_asset(const TextureAsset &texture) {
  return {
      .id = texture.id,
      .revision = texture.revision.value,
      .name = texture.name,
      .usage = to_api_texture_usage(texture.usage),
      .color_space = to_api_texture_color_space(texture.color_space),
      .width = texture.width,
      .height = texture.height,
      .source_path = texture.source_path.generic_string(),
      .generated = texture.generated,
  };
}

inline TextureAsset to_texture_asset(const QuaderTextureAsset &texture) {
  TextureAsset result;
  result.id = texture.id;
  result.revision.value = texture.revision;
  result.name = texture.name;
  result.usage = to_texture_usage(texture.usage);
  result.color_space = to_texture_color_space(texture.color_space);
  result.width = texture.width;
  result.height = texture.height;
  result.source_path = texture.source_path;
  result.generated = texture.generated;
  return result;
}

inline QuaderMaterialAsset
to_api_material_asset(const MaterialDocument &material) {
  return {
      .id = material.id,
      .revision = material.revision.value,
      .name = material.name,
      .base_color = material.base_color,
      .roughness = material.roughness,
      .metallic = material.metallic,
      .diffuse_model = static_cast<QuaderMaterialDiffuseModel>(material.diffuse_model),
      .texture_slots = material.texture_slots,
      .node_graph_reserved = material.node_graph_reserved,
  };
}

inline MaterialDocument
to_material_document(const QuaderMaterialAsset &material) {
  MaterialDocument result;
  result.id = material.id;
  result.revision.value = material.revision;
  result.name = material.name;
  result.base_color = material.base_color;
  result.roughness = material.roughness;
  result.metallic = material.metallic;
  result.diffuse_model = static_cast<MaterialDiffuseModel>(material.diffuse_model);
  result.texture_slots = material.texture_slots;
  result.node_graph_reserved = material.node_graph_reserved;
  return result;
}

inline QuaderToolPreviewMesh
to_api_tool_preview_mesh(const ModelingToolPreviewMesh &preview) {
  return {
      .source_entity = preview.source_entity,
      .material = preview.material.material,
      .mesh = to_api_mesh_asset(preview.mesh),
      .transform = preview.transform,
      .instance_transforms = preview.instance_transforms,
  };
}

inline QuaderOverlayLayer to_api_overlay_layer(ViewportOverlayLayer layer) {
  return static_cast<QuaderOverlayLayer>(static_cast<int>(layer));
}

inline QuaderOverlayStyleRole
to_api_overlay_style_role(ViewportOverlayStyleRole role) {
  return static_cast<QuaderOverlayStyleRole>(static_cast<int>(role));
}

inline QuaderOverlaySource to_api_overlay_source(ViewportOverlaySource source) {
  return static_cast<QuaderOverlaySource>(static_cast<int>(source));
}

inline QuaderOverlayElementRef
to_api_overlay_element_ref(const ViewportOverlayElementRef &element) {
  return {
      .object_id = element.object_id,
      .vertex_id = element.vertex_id,
      .edge_a = element.edge_a,
      .edge_b = element.edge_b,
      .face_id = element.face_id,
  };
}

inline QuaderOverlayFrame
to_api_overlay_frame(const ViewportOverlayFrame &frame) {
  QuaderOverlayFrame result;
  result.lines.reserve(frame.lines.size());
  for (const ViewportOverlayLine &line : frame.lines) {
    result.lines.push_back({
        .layer = to_api_overlay_layer(line.layer),
        .start = line.start,
        .end = line.end,
        .color = line.color,
        .width_pixels = line.width_pixels,
        .style_role = to_api_overlay_style_role(line.style_role),
        .source = to_api_overlay_source(line.source),
        .element = to_api_overlay_element_ref(line.element),
    });
  }
  result.triangles.reserve(frame.triangles.size());
  for (const ViewportOverlayTriangle &triangle : frame.triangles) {
    result.triangles.push_back({
        .layer = to_api_overlay_layer(triangle.layer),
        .a = triangle.a,
        .b = triangle.b,
        .c = triangle.c,
        .color = triangle.color,
        .style_role = to_api_overlay_style_role(triangle.style_role),
        .source = to_api_overlay_source(triangle.source),
        .element = to_api_overlay_element_ref(triangle.element),
    });
  }
  result.points.reserve(frame.points.size());
  for (const ViewportOverlayPoint &point : frame.points) {
    result.points.push_back({
        .layer = to_api_overlay_layer(point.layer),
        .position = point.position,
        .color = point.color,
        .size_pixels = point.size_pixels,
        .style_role = to_api_overlay_style_role(point.style_role),
        .source = to_api_overlay_source(point.source),
        .element = to_api_overlay_element_ref(point.element),
        .outline_growth_pixels = point.outline_growth_pixels,
    });
  }
  result.hover_target_selected = frame.hover_target_selected;
  return result;
}

} // namespace quader
