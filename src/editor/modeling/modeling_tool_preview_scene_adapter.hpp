////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/modeling_preview_types.hpp"
#include "render/scene_render_extraction.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <utility>

namespace quader {

/** Projects an editor-owned compound preview into the render input contract. */
[[nodiscard]] inline ToolPreviewScene project_modeling_tool_preview_scene(
    ModelingToolPreviewScene preview, const SceneDocument &scene) {
  ToolPreviewScene projected;
  projected.revision = preview.revision;
  projected.meshes.reserve(preview.meshes.size());
  for (ModelingToolPreviewMesh &mesh : preview.meshes) {
    const EntityId visibility =
        mesh.visibility_entity == kInvalidEntityId ? mesh.source_entity
                                                   : mesh.visibility_entity;
    projected.meshes.push_back({
        .source_object_id = mesh.source_object_id,
        .source_entity = mesh.source_entity,
        .visibility_entity = visibility,
        .material = std::move(mesh.material),
        .mesh = std::move(mesh.mesh),
        .transform = mesh.transform,
        .instance_transforms = std::move(mesh.instance_transforms),
        .sampling_from_local = effective_texture_sampling_from_local(
                                   scene, mesh.source_entity,
                                   visibility == mesh.source_entity
                                       ? kInvalidEntityId
                                       : visibility)
                                   .value_or(QMat4{}),
        .instance_sampling_from_local =
            std::move(mesh.instance_sampling_from_local),
        .double_sided = mesh.double_sided,
        .cast_shadows = mesh.cast_shadows,
        .dynamic_positions = mesh.dynamic_positions,
        .suppress_source = mesh.suppress_source,
        .layout_key = mesh.layout_key,
        .position_revision = mesh.position_revision,
        .dynamic_position_payload =
            std::move(mesh.dynamic_position_payload),
    });
  }
  projected.curves.reserve(preview.curves.size());
  for (ModelingToolPreviewCurve &curve : preview.curves) {
    const EntityId visibility =
        curve.visibility_entity == kInvalidEntityId ? curve.source_entity
                                                    : curve.visibility_entity;
    projected.curves.push_back({
        .source_entity = curve.source_entity,
        .visibility_entity = visibility,
        .curve = curve.curve,
        .material = std::move(curve.material),
        .instance_transforms = std::move(curve.instance_transforms),
        .solidify = curve.solidify,
        .solidify_resolution = curve.solidify_resolution,
        .solidify_radius = curve.solidify_radius,
        .suppress_source = curve.suppress_source,
        .layout_key = curve.layout_key,
    });
  }
  projected.lights.reserve(preview.lights.size());
  for (ModelingToolPreviewLight &light : preview.lights) {
    const EntityId visibility =
        light.visibility_entity == kInvalidEntityId ? light.source_entity
                                                    : light.visibility_entity;
    projected.lights.push_back({
        .source_entity = light.source_entity,
        .visibility_entity = visibility,
        .light = std::move(light.light),
        .instance_transforms = std::move(light.instance_transforms),
        .suppress_source = light.suppress_source,
        .layout_key = light.layout_key,
    });
  }
  projected.group_markers.reserve(preview.group_markers.size());
  for (ModelingToolPreviewGroupMarker &marker : preview.group_markers) {
    projected.group_markers.push_back({
        .source_entity = marker.source_entity,
        .local_min = marker.local_min,
        .local_max = marker.local_max,
        .instance_transforms = std::move(marker.instance_transforms),
        .layout_key = marker.layout_key,
    });
  }
  projected.suppressed_entities = std::move(preview.suppression.entities);
  projected.occurrence_suppressions.reserve(
      preview.suppression.occurrences.size());
  for (const ModelingToolPreviewOccurrenceSuppression &suppression :
       preview.suppression.occurrences) {
    projected.occurrence_suppressions.push_back({
        .source_entity = suppression.source_entity,
        .occurrence = suppression.occurrence,
    });
  }
  return projected;
}

} // namespace quader
