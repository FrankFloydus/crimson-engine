////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiTypes.h"
#include "assets/MaterialDocument.h"
#include "assets/MeshAsset.h"
#include "assets/TextureAsset.h"
#include "editor/modeling/services/ModelingActionCatalog.h"
#include "editor/modeling/ModelingTypes.h"
#include "render/ViewportOverlayFrame.h"
#include "scene/Components.h"

namespace quader {

inline EntityKind toSceneEntityKind(QuaderEntityKind kind) {
    switch (kind) {
    case QuaderEntityKind::Empty:
        return EntityKind::Empty;
    case QuaderEntityKind::Mesh:
        return EntityKind::Mesh;
    case QuaderEntityKind::Light:
        return EntityKind::Light;
    case QuaderEntityKind::Generic:
        return EntityKind::Generic;
    }
    return EntityKind::Generic;
}

inline QuaderEntityKind toApiEntityKind(EntityKind kind) {
    switch (kind) {
    case EntityKind::Empty:
        return QuaderEntityKind::Empty;
    case EntityKind::Mesh:
        return QuaderEntityKind::Mesh;
    case EntityKind::Light:
        return QuaderEntityKind::Light;
    case EntityKind::Generic:
        return QuaderEntityKind::Generic;
    }
    return QuaderEntityKind::Generic;
}

inline TransformComponent toSceneTransform(const QuaderTransform& transform) {
    return {
            .position = transform.position,
            .rotationEuler = transform.rotationEuler,
            .scale = transform.scale,
    };
}

inline QuaderTransform toApiTransform(const TransformComponent& transform) {
    return {
            .position = transform.position,
            .rotationEuler = transform.rotationEuler,
            .scale = transform.scale,
    };
}

inline MeshComponent toSceneMeshComponent(const QuaderMeshComponent& component) {
    return {.mesh = component.mesh};
}

inline QuaderMeshComponent toApiMeshComponent(const MeshComponent& component) {
    return {.mesh = component.mesh};
}

inline ModelingBounds toModelingBounds(const QuaderBounds& bounds) {
    return {
            .min = bounds.min,
            .max = bounds.max,
    };
}

inline QuaderBounds toApiBounds(const ModelingBounds& bounds) {
    return {
            .min = bounds.min,
            .max = bounds.max,
    };
}

inline ModelingSelectionMode toModelingSelectionMode(QuaderSelectionMode mode) {
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

inline ModelingSelectionEdit toModelingSelectionEdit(QuaderSelectionEdit edit) {
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

inline ModelingBoxSelectionRule toModelingBoxSelectionRule(QuaderBoxSelectionRule rule) {
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

inline ModelingCutMode toModelingCutMode(QuaderCutMode mode) {
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

inline QuaderCutMode toApiCutMode(ModelingCutMode mode) {
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

inline ModelingInteractionSettings toModelingInteractionSettings(const QuaderInteractionSettings& settings) {
    return {
            .bridgeSteps = settings.bridgeSteps,
            .edgeBevelWidth = settings.edgeBevelWidth,
            .edgeBevelProfile = settings.edgeBevelProfile,
            .edgeBevelSegments = settings.edgeBevelSegments,
            .thickenThickness = settings.thickenThickness,
            .thickenFromCenter = settings.thickenFromCenter,
            .sliceQuadXSlices = settings.sliceQuadXSlices,
            .sliceQuadYSlices = settings.sliceQuadYSlices,
            .mergeByDistanceTolerance = settings.mergeByDistanceTolerance,
            .cutMode = toModelingCutMode(settings.cutMode),
    };
}

inline QuaderSelectionMode toApiSelectionMode(ModelingSelectionMode mode) {
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

inline QuaderActiveSelectionKind toApiActiveSelectionKind(ModelingActiveSelectionKind kind) {
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

inline QuaderSelectionSummary toApiSelectionSummary(const ModelingSelectionSummary& summary) {
    return {
            .mode = toApiSelectionMode(summary.mode),
            .hasSelection = summary.hasSelection,
            .activeKind = toApiActiveSelectionKind(summary.activeKind),
            .activeObjectId = summary.activeObjectId,
            .activeVertexId = summary.activeVertexId,
            .activeEdgeA = summary.activeEdgeA,
            .activeEdgeB = summary.activeEdgeB,
            .activeFaceId = summary.activeFaceId,
            .selectedObjectCount = summary.selectedObjectCount,
            .selectedVertexCount = summary.selectedVertexCount,
            .selectedEdgeCount = summary.selectedEdgeCount,
            .selectedFaceCount = summary.selectedFaceCount,
            .contentRevision = summary.contentRevision,
            .selectionRevision = summary.selectionRevision,
    };
}

inline ModelingRay toModelingRay(const QuaderRay& ray) {
    return {
            .origin = ray.origin,
            .direction = ray.direction,
    };
}

inline QuaderRay toApiRay(const ModelingRay& ray) {
    return {
            .origin = ray.origin,
            .direction = ray.direction,
    };
}

inline ModelingViewportCamera toModelingViewportCamera(const QuaderViewportCamera& camera) {
    return {
            .orthographic = camera.orthographic,
            .origin = camera.origin,
            .xAxis = camera.xAxis,
            .yAxis = camera.yAxis,
            .zAxis = camera.zAxis,
            .viewportSize = camera.viewportSize,
            .orthographicSize = camera.orthographicSize,
            .fovDegrees = camera.fovDegrees,
    };
}

inline QuaderViewportCamera toApiViewportCamera(const ModelingViewportCamera& camera) {
    return {
            .orthographic = camera.orthographic,
            .origin = camera.origin,
            .xAxis = camera.xAxis,
            .yAxis = camera.yAxis,
            .zAxis = camera.zAxis,
            .viewportSize = camera.viewportSize,
            .orthographicSize = camera.orthographicSize,
            .fovDegrees = camera.fovDegrees,
    };
}

inline ModelingTransform3 toModelingTransform3(const QuaderTransform3& transform) {
    return {
            .xAxis = transform.xAxis,
            .yAxis = transform.yAxis,
            .zAxis = transform.zAxis,
            .origin = transform.origin,
    };
}

inline QuaderTransform3 toApiTransform3(const ModelingTransform3& transform) {
    return {
            .xAxis = transform.xAxis,
            .yAxis = transform.yAxis,
            .zAxis = transform.zAxis,
            .origin = transform.origin,
    };
}

inline ModelingTransformTool toModelingTransformTool(QuaderTransformTool tool) {
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

inline QuaderTransformTool toApiTransformTool(ModelingTransformTool tool) {
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

inline ModelingTransformOperation toModelingTransformOperation(QuaderTransformOperation operation) {
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

inline QuaderTransformOperation toApiTransformOperation(ModelingTransformOperation operation) {
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

inline ModelingTransformAxis toModelingTransformAxis(QuaderTransformAxis axis) {
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

inline QuaderTransformAxis toApiTransformAxis(ModelingTransformAxis axis) {
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

inline ModelingPickSelectRequest toModelingPickSelectRequest(const QuaderPickSelectRequest& request) {
    return {
            .ray = toModelingRay(request.ray),
            .clickCount = request.clickCount,
            .controlPressed = request.controlPressed,
            .shiftPressed = request.shiftPressed,
            .commandPressed = request.commandPressed,
            .altPressed = request.altPressed,
            .preserveSelectionOnMiss = request.preserveSelectionOnMiss,
            .preserveSelectionOnSelectedHit = request.preserveSelectionOnSelectedHit,
            .vertexRadius = request.vertexRadius,
            .edgeRadius = request.edgeRadius,
    };
}

inline ModelingBoxSelectRequest toModelingBoxSelectRequest(const QuaderBoxSelectRequest& request) {
    return {
            .camera = toModelingViewportCamera(request.camera),
            .start = request.start,
            .end = request.end,
            .edit = toModelingSelectionEdit(request.edit),
            .rule = toModelingBoxSelectionRule(request.rule),
    };
}

inline ModelingHoverRequest toModelingHoverRequest(const QuaderHoverRequest& request) {
    return {
            .ray = toModelingRay(request.ray),
            .selectedModifierPressed = request.selectedModifierPressed,
            .vertexRadius = request.vertexRadius,
            .edgeRadius = request.edgeRadius,
    };
}

inline QuaderHoverTarget toApiHoverTarget(const ModelingHoverTarget& target) {
    return {
            .kind = toApiActiveSelectionKind(target.kind),
            .objectId = target.objectId,
            .vertexId = target.vertexId,
            .edgeA = target.edgeA,
            .edgeB = target.edgeB,
            .faceId = target.faceId,
    };
}

inline QuaderHoverSummary toApiHoverSummary(const ModelingHoverSummary& summary) {
    return {
            .changed = summary.changed,
            .target = toApiHoverTarget(summary.target),
            .targetSelected = summary.targetSelected,
    };
}

inline ModelingTransformDragBeginRequest toModelingTransformDragBeginRequest(
        const QuaderTransformDragBeginRequest& request) {
    return {
            .tool = toModelingTransformTool(request.tool),
            .operation = toModelingTransformOperation(request.operation),
            .axis = toModelingTransformAxis(request.axis),
            .pivot = request.pivot,
            .screenPosition = request.screenPosition,
            .screenPivot = request.screenPivot,
            .screenTip = request.screenTip,
            .hasWorldDirection = request.hasWorldDirection,
            .worldDirection = request.worldDirection,
            .ray = toModelingRay(request.ray),
            .camera = toModelingViewportCamera(request.camera),
            .gridSize = request.gridSize,
            .vertexSnapRadius = request.vertexSnapRadius,
            .duplicateSelected = request.duplicateSelected,
    };
}

inline ModelingTransformDragUpdateRequest toModelingTransformDragUpdateRequest(
        const QuaderTransformDragUpdateRequest& request) {
    return {
            .screenPosition = request.screenPosition,
            .ray = toModelingRay(request.ray),
            .camera = toModelingViewportCamera(request.camera),
            .gridSize = request.gridSize,
            .vertexSnapRadius = request.vertexSnapRadius,
            .precision = request.precision,
    };
}

inline QuaderTransformDragPreview toApiTransformDragPreview(const ModelingTransformDragPreview& preview) {
    return {
            .ok = preview.ok,
            .active = preview.active,
            .changed = preview.changed,
            .previewDriven = preview.previewDriven,
            .message = preview.message,
            .tool = toApiTransformTool(preview.tool),
            .operation = toApiTransformOperation(preview.operation),
            .axis = toApiTransformAxis(preview.axis),
            .pivot = preview.pivot,
            .moveDelta = preview.moveDelta,
            .rotateAngleDegrees = preview.rotateAngleDegrees,
            .scale = preview.scale,
            .contentRevision = preview.contentRevision,
            .selectionRevision = preview.selectionRevision,
            .sceneTransform = preview.sceneTransform,
            .hasScenePreviewTransform = preview.hasScenePreviewTransform,
            .scenePreviewTransform = toApiTransform3(preview.scenePreviewTransform),
    };
}

inline QuaderViewportTool toApiViewportTool(ModelingViewportTool tool) {
    switch (tool) {
    case ModelingViewportTool::Select:
        return QuaderViewportTool::Select;
    case ModelingViewportTool::Move:
        return QuaderViewportTool::Move;
    case ModelingViewportTool::Extend:
        return QuaderViewportTool::Extend;
    case ModelingViewportTool::Extrude:
        return QuaderViewportTool::Extrude;
    case ModelingViewportTool::Rotate:
        return QuaderViewportTool::Rotate;
    case ModelingViewportTool::Scale:
        return QuaderViewportTool::Scale;
    case ModelingViewportTool::Box:
        return QuaderViewportTool::Box;
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

inline QuaderToolStatus toApiToolStatus(const ModelingToolStatus& status) {
    return {
            .activeTool = toApiViewportTool(status.activeTool),
            .modalToolOwnsViewport = status.modalToolOwnsViewport,
            .insertEdgeLoopActive = status.insertEdgeLoopActive,
            .insertEdgeLoopDragging = status.insertEdgeLoopDragging,
            .insertEdgeLoopHasPreview = status.insertEdgeLoopHasPreview,
            .knifeActive = status.knifeActive,
            .knifeHasCurrent = status.knifeHasCurrent,
            .knifeCanCommit = status.knifeCanCommit,
            .cutActive = status.cutActive,
            .cutCanPreview = status.cutCanPreview,
            .cutPreviewValid = status.cutPreviewValid,
            .cutDragging = status.cutDragging,
            .cutDraggingNewPoint = status.cutDraggingNewPoint,
            .cutDraggingLine = status.cutDraggingLine,
            .cutDraggingPointIndex = status.cutDraggingPointIndex,
            .cutPointCount = status.cutPointCount,
            .cutCapturedPointCount = status.cutCapturedPointCount,
            .cutHasLine = status.cutHasLine,
            .cutPoints = status.cutPoints,
            .cutMode = toApiCutMode(status.cutMode),
            .mirrorActive = status.mirrorActive,
            .mirrorCanPreview = status.mirrorCanPreview,
            .mirrorPreviewValid = status.mirrorPreviewValid,
            .mirrorDragging = status.mirrorDragging,
            .mirrorDraggingNewPoint = status.mirrorDraggingNewPoint,
            .mirrorDraggingLine = status.mirrorDraggingLine,
            .mirrorDraggingPointIndex = status.mirrorDraggingPointIndex,
            .mirrorPointCount = status.mirrorPointCount,
            .mirrorCapturedPointCount = status.mirrorCapturedPointCount,
            .mirrorHasLine = status.mirrorHasLine,
            .mirrorPoints = status.mirrorPoints,
            .polyActive = status.polyActive,
            .polyClosed = status.polyClosed,
            .polyHasCurrent = status.polyHasCurrent,
            .polyCanCommit = status.polyCanCommit,
            .polyPointCount = status.polyPointCount,
    };
}

inline QuaderModelingObjectRecord toApiModelingObjectRecord(const ModelingObjectRecord& record) {
    return {
            .modelingObjectId = record.modelingObjectId,
            .entity = record.entity,
            .mesh = record.mesh,
            .name = record.name,
    };
}

inline ModelingActionSurface toModelingActionSurface(QuaderActionSurface surface) {
    switch (surface) {
    case QuaderActionSurface::ViewportContextMenu:
        return ModelingActionSurface::ViewportContextMenu;
    case QuaderActionSurface::OperationsPanel:
        return ModelingActionSurface::OperationsPanel;
    }
    return ModelingActionSurface::OperationsPanel;
}

inline QuaderActionSurface toApiActionSurface(ModelingActionSurface surface) {
    switch (surface) {
    case ModelingActionSurface::ViewportContextMenu:
        return QuaderActionSurface::ViewportContextMenu;
    case ModelingActionSurface::OperationsPanel:
        return QuaderActionSurface::OperationsPanel;
    }
    return QuaderActionSurface::OperationsPanel;
}

inline QuaderActionVisibility toApiActionVisibility(ModelingActionVisibility visibility) {
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

inline QuaderActionKind toApiActionKind(ModelingActionKind kind) {
    switch (kind) {
    case ModelingActionKind::Operation:
        return QuaderActionKind::Operation;
    case ModelingActionKind::Tool:
        return QuaderActionKind::Tool;
    }
    return QuaderActionKind::Operation;
}

inline QuaderActionPreviewKind toApiActionPreviewKind(ModelingActionPreviewKind kind) {
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
    }
    return QuaderActionPreviewKind::None;
}

inline ModelingActionQuery toModelingActionQuery(const QuaderActionQuery& query) {
    return {
            .surface = toModelingActionSurface(query.surface),
            .modalToolOwnsViewport = query.modalToolOwnsViewport,
            .suppressWhenModalToolOwnsViewport = query.suppressWhenModalToolOwnsViewport,
            .includeDisabled = query.includeDisabled,
            .includeHidden = query.includeHidden,
    };
}

inline QuaderActionItem toApiActionItem(const ModelingActionItem& item) {
    return {
            .actionId = item.actionId,
            .operationId = item.operationId,
            .toolId = item.toolId,
            .label = item.label,
            .kind = toApiActionKind(item.kind),
            .previewKind = toApiActionPreviewKind(item.previewKind),
            .visibility = toApiActionVisibility(item.visibility),
            .disabledReason = item.disabledReason,
            .disabledReasonKey = item.disabledReasonKey,
            .disabledReasonCount = item.disabledReasonCount,
            .rebuildsMeshOnSuccess = item.rebuildsMeshOnSuccess,
    };
}

inline QuaderActionPanel toApiActionPanel(const ModelingActionPanel& panel) {
    QuaderActionPanel result;
    result.panelId = panel.panelId;
    result.label = panel.label;
    result.selectionModeId = panel.selectionModeId;
    result.visibility = toApiActionVisibility(panel.visibility);
    result.actions.reserve(panel.actions.size());
    for (const ModelingActionItem& item : panel.actions) {
        result.actions.push_back(toApiActionItem(item));
    }
    return result;
}

inline QuaderActionSet toApiActionSet(const ModelingActionSet& set) {
    QuaderActionSet result;
    result.surface = toApiActionSurface(set.surface);
    result.surfaceId = set.surfaceId;
    result.panels.reserve(set.panels.size());
    for (const ModelingActionPanel& panel : set.panels) {
        result.panels.push_back(toApiActionPanel(panel));
    }
    return result;
}

inline QuaderInstantOperationDescriptor toApiInstantOperationDescriptor(
        const ModelingInstantOperationDescriptor& operation) {
    return {
            .id = operation.id,
            .label = operation.label,
            .enabled = operation.enabled,
            .rebuildsMeshOnSuccess = operation.rebuildsMeshOnSuccess,
    };
}

inline QuaderMeshVertex toApiMeshVertex(const MeshVertex& vertex) {
    return {
            .position = vertex.position,
            .normal = vertex.normal,
            .uv = vertex.uv,
            .rgba = vertex.rgba,
    };
}

inline MeshVertex toMeshVertex(const QuaderMeshVertex& vertex) {
    return {
            .position = vertex.position,
            .normal = vertex.normal,
            .uv = vertex.uv,
            .rgba = vertex.rgba,
    };
}

inline QuaderMeshPrimitiveSpan toApiMeshPrimitiveSpan(const MeshPrimitiveSpan& primitive) {
    return {
            .indexOffset = primitive.indexOffset,
            .indexCount = primitive.indexCount,
            .materialSlot = primitive.materialSlot,
    };
}

inline MeshPrimitiveSpan toMeshPrimitiveSpan(const QuaderMeshPrimitiveSpan& primitive) {
    return {
            .indexOffset = primitive.indexOffset,
            .indexCount = primitive.indexCount,
            .materialSlot = primitive.materialSlot,
    };
}

inline QuaderMeshAsset toApiMeshAsset(const MeshAsset& mesh) {
    QuaderMeshAsset result;
    result.id = mesh.id;
    result.revision = mesh.revision.value;
    result.name = mesh.name;
    result.vertices.reserve(mesh.vertices.size());
    for (const MeshVertex& vertex : mesh.vertices) {
        result.vertices.push_back(toApiMeshVertex(vertex));
    }
    result.indices = mesh.indices;
    result.primitives.reserve(mesh.primitives.size());
    for (const MeshPrimitiveSpan& primitive : mesh.primitives) {
        result.primitives.push_back(toApiMeshPrimitiveSpan(primitive));
    }
    result.hasStableCpuTopology = mesh.hasStableCpuTopology;
    result.hasAuthoredPolygonDocument = mesh.hasAuthoredPolygonDocument;
    return result;
}

inline MeshAsset toMeshAsset(const QuaderMeshAsset& mesh) {
    MeshAsset result;
    result.id = mesh.id;
    result.revision.value = mesh.revision;
    result.name = mesh.name;
    result.vertices.reserve(mesh.vertices.size());
    for (const QuaderMeshVertex& vertex : mesh.vertices) {
        result.vertices.push_back(toMeshVertex(vertex));
    }
    result.indices = mesh.indices;
    result.primitives.reserve(mesh.primitives.size());
    for (const QuaderMeshPrimitiveSpan& primitive : mesh.primitives) {
        result.primitives.push_back(toMeshPrimitiveSpan(primitive));
    }
    result.hasStableCpuTopology = mesh.hasStableCpuTopology;
    result.hasAuthoredPolygonDocument = mesh.hasAuthoredPolygonDocument;
    return result;
}

inline QuaderTextureUsage toApiTextureUsage(TextureUsage usage) {
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

inline TextureUsage toTextureUsage(QuaderTextureUsage usage) {
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

inline QuaderTextureColorSpace toApiTextureColorSpace(TextureColorSpace colorSpace) {
    return colorSpace == TextureColorSpace::Linear ? QuaderTextureColorSpace::Linear : QuaderTextureColorSpace::Srgb;
}

inline TextureColorSpace toTextureColorSpace(QuaderTextureColorSpace colorSpace) {
    return colorSpace == QuaderTextureColorSpace::Linear ? TextureColorSpace::Linear : TextureColorSpace::Srgb;
}

inline QuaderTextureAsset toApiTextureAsset(const TextureAsset& texture) {
    return {
            .id = texture.id,
            .revision = texture.revision.value,
            .name = texture.name,
            .usage = toApiTextureUsage(texture.usage),
            .colorSpace = toApiTextureColorSpace(texture.colorSpace),
            .width = texture.width,
            .height = texture.height,
            .sourcePath = texture.sourcePath.generic_string(),
            .generated = texture.generated,
    };
}

inline TextureAsset toTextureAsset(const QuaderTextureAsset& texture) {
    TextureAsset result;
    result.id = texture.id;
    result.revision.value = texture.revision;
    result.name = texture.name;
    result.usage = toTextureUsage(texture.usage);
    result.colorSpace = toTextureColorSpace(texture.colorSpace);
    result.width = texture.width;
    result.height = texture.height;
    result.sourcePath = texture.sourcePath;
    result.generated = texture.generated;
    return result;
}

inline QuaderMaterialAsset toApiMaterialAsset(const MaterialDocument& material) {
    return {
            .id = material.id,
            .revision = material.revision.value,
            .name = material.name,
            .baseColor = material.baseColor,
            .roughness = material.roughness,
            .metallic = material.metallic,
            .textureSlots = material.textureSlots,
            .nodeGraphReserved = material.nodeGraphReserved,
    };
}

inline MaterialDocument toMaterialDocument(const QuaderMaterialAsset& material) {
    MaterialDocument result;
    result.id = material.id;
    result.revision.value = material.revision;
    result.name = material.name;
    result.baseColor = material.baseColor;
    result.roughness = material.roughness;
    result.metallic = material.metallic;
    result.textureSlots = material.textureSlots;
    result.nodeGraphReserved = material.nodeGraphReserved;
    return result;
}

inline QuaderToolPreviewMesh toApiToolPreviewMesh(const ModelingToolPreviewMesh& preview) {
    return {
            .sourceEntity = preview.sourceEntity,
            .material = preview.material,
            .mesh = toApiMeshAsset(preview.mesh),
    };
}

inline QuaderOverlayLayer toApiOverlayLayer(ViewportOverlayLayer layer) {
    return static_cast<QuaderOverlayLayer>(static_cast<int>(layer));
}

inline QuaderOverlayStyleRole toApiOverlayStyleRole(ViewportOverlayStyleRole role) {
    return static_cast<QuaderOverlayStyleRole>(static_cast<int>(role));
}

inline QuaderOverlaySource toApiOverlaySource(ViewportOverlaySource source) {
    return static_cast<QuaderOverlaySource>(static_cast<int>(source));
}

inline QuaderOverlayElementRef toApiOverlayElementRef(const ViewportOverlayElementRef& element) {
    return {
            .objectId = element.objectId,
            .vertexId = element.vertexId,
            .edgeA = element.edgeA,
            .edgeB = element.edgeB,
            .faceId = element.faceId,
    };
}

inline QuaderOverlayFrame toApiOverlayFrame(const ViewportOverlayFrame& frame) {
    QuaderOverlayFrame result;
    result.lines.reserve(frame.lines.size());
    for (const ViewportOverlayLine& line : frame.lines) {
        result.lines.push_back({
                .layer = toApiOverlayLayer(line.layer),
                .start = line.start,
                .end = line.end,
                .color = line.color,
                .widthPixels = line.widthPixels,
                .styleRole = toApiOverlayStyleRole(line.styleRole),
                .source = toApiOverlaySource(line.source),
                .element = toApiOverlayElementRef(line.element),
        });
    }
    result.triangles.reserve(frame.triangles.size());
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        result.triangles.push_back({
                .layer = toApiOverlayLayer(triangle.layer),
                .a = triangle.a,
                .b = triangle.b,
                .c = triangle.c,
                .color = triangle.color,
                .styleRole = toApiOverlayStyleRole(triangle.styleRole),
                .source = toApiOverlaySource(triangle.source),
                .element = toApiOverlayElementRef(triangle.element),
        });
    }
    result.points.reserve(frame.points.size());
    for (const ViewportOverlayPoint& point : frame.points) {
        result.points.push_back({
                .layer = toApiOverlayLayer(point.layer),
                .position = point.position,
                .color = point.color,
                .sizePixels = point.sizePixels,
                .styleRole = toApiOverlayStyleRole(point.styleRole),
                .source = toApiOverlaySource(point.source),
                .element = toApiOverlayElementRef(point.element),
                .outlineGrowthPixels = point.outlineGrowthPixels,
        });
    }
    result.hoverTargetSelected = frame.hoverTargetSelected;
    return result;
}

} // namespace quader
