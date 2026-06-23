////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiTypes.h"
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
