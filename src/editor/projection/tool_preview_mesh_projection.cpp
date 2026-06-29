////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/tool_preview_mesh_projection.hpp"

#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "scene/scene_document.hpp"

#include <utility>

#include <editor/interactions/box_tool.hpp>

namespace quader {

std::optional<MeshAsset> ToolPreviewMeshProjection::boxPreviewMeshFromDescriptor(
        const quader_editor_core::BoxPreviewDescriptor& preview) {
    if (!preview.ok ||
            preview.drag_state != quader_editor_core::BoxDragState::height ||
            preview.bounds.empty) {
        return std::nullopt;
    }

    const std::vector<QVec3> corners = quader_editor_core::box_preview_corners(preview);
    if (corners.size() != 8U) {
        return std::nullopt;
    }

    std::vector<quader_editor_core::PolygonObject> objects;
    int activeObjectId = -1;
    quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
    const quader_editor_core::CommandResult result =
            quader_editor_core::create_box_from_corners(objects, 1, corners, activeObjectId, selectionMode);
    if (!result.ok || objects.empty()) {
        return std::nullopt;
    }

    MeshAsset mesh = MeshProjectionService::compilePolygonDocumentMesh(objects.front().document);
    mesh.name = "Box Preview";
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return std::nullopt;
    }
    return mesh;
}

std::vector<ModelingToolPreviewMesh> ToolPreviewMeshProjection::mirrorPreviewMeshes(
        const quader_editor_core::EditorRuntime& runtime,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        const std::vector<quader_editor_core::PolygonObject>& previewObjects) {
    std::vector<ModelingToolPreviewMesh> previewMeshes;
    const quader_editor_core::MirrorToolState& mirror = runtime.mirror_state();
    if (!quader_editor_core::mirror_is_active(mirror) ||
            !quader_editor_core::mirror_can_preview(mirror) ||
            !mirror.preview_valid) {
        return previewMeshes;
    }

    const std::vector<quader_poly::Document> documents =
            runtime.mirror_preview_documents(previewObjects);
    previewMeshes.reserve(documents.size());
    std::size_t documentIndex = 0;
    for (const int objectId : mirror.target_object_ids) {
        if (documentIndex >= documents.size()) {
            break;
        }
        MeshAsset mesh = MeshProjectionService::compilePolygonDocumentMesh(documents[documentIndex++]);
        if (mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }

        EntityId sourceEntity = InvalidEntityId;
        AssetId material = InvalidAssetId;
        if (const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(objectId)) {
            sourceEntity = *entity;
            if (const MaterialComponent* materialComponent = scene.material(sourceEntity)) {
                material = materialComponent->material;
            }
        }

        previewMeshes.push_back(ModelingToolPreviewMesh{
                .sourceEntity = sourceEntity,
                .material = material,
                .mesh = std::move(mesh),
        });
    }
    return previewMeshes;
}

} // namespace quader
