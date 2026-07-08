////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/tool_preview_mesh_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"

#include <utility>

#include <editor/interactions/box_tool.hpp>

namespace quader {

std::optional<MeshAsset>
ToolPreviewMeshProjection::box_preview_mesh_from_descriptor(
    const quader_editor_core::BoxPreviewDescriptor &preview) {
  QDR_PROFILE_FUNCTION();
  if (!preview.ok ||
      preview.drag_state != quader_editor_core::BoxDragState::Height ||
      preview.bounds.empty) {
    return std::nullopt;
  }

  const std::vector<QVec3> corners =
      quader_editor_core::box_preview_corners(preview);
  if (corners.size() != 8U) {
    return std::nullopt;
  }

  std::vector<quader_editor_core::PolygonObject> objects;
  int active_object_id = -1;
  quader_editor_core::SelectionMode selection_mode =
      quader_editor_core::SelectionMode::Object;
  const quader_editor_core::CommandResult result =
      quader_editor_core::create_box_from_corners(
          objects, 1, corners, active_object_id, selection_mode);
  if (!result.ok || objects.empty()) {
    return std::nullopt;
  }

  MeshAsset mesh = MeshProjectionService::compile_polygon_document_mesh(
      objects.front().document);
  mesh.name = "Box Preview";
  if (mesh.vertices.empty() || mesh.indices.empty()) {
    return std::nullopt;
  }
  return mesh;
}

std::vector<ModelingToolPreviewMesh>
ToolPreviewMeshProjection::mirror_preview_meshes(
    const quader_editor_core::EditorRuntime &runtime,
    const SceneDocument &scene, const ModelingObjectRuntimeMap &object_registry,
    const std::vector<quader_editor_core::PolygonObject> &preview_objects) {
  QDR_PROFILE_FUNCTION();
  std::vector<ModelingToolPreviewMesh> preview_meshes;
  const quader_editor_core::MirrorToolState &mirror = runtime.mirror_state();
  if (!quader_editor_core::mirror_is_active(mirror) ||
      !quader_editor_core::mirror_can_preview(mirror) ||
      !mirror.preview_valid) {
    return preview_meshes;
  }

  const std::vector<quader_poly::Document> documents =
      runtime.mirror_preview_documents(preview_objects);
  preview_meshes.reserve(documents.size());
  std::size_t document_index = 0;
  for (const int object_id : mirror.target_object_ids) {
    if (document_index >= documents.size()) {
      break;
    }
    MeshAsset mesh = MeshProjectionService::compile_polygon_document_mesh(
        documents[document_index++]);
    if (mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }

    EntityId source_entity = kInvalidEntityId;
    AssetId material = kInvalidAssetId;
    if (const std::optional<EntityId> entity =
            object_registry.entity_for_modeling_object(object_id)) {
      source_entity = *entity;
      if (const MaterialComponent *material_component =
              scene.material(source_entity)) {
        material = material_component->material;
      }
    }

    preview_meshes.push_back(ModelingToolPreviewMesh{
        .source_entity = source_entity,
        .material = material,
        .mesh = std::move(mesh),
    });
  }
  return preview_meshes;
}

} // namespace quader
