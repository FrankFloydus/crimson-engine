////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/SelectionOverlayProjection.h"

#include "math/GlmBridge.h"
#include "render/ViewportOverlayPolicy.h"
#include "scene/SceneDocument.h"

#include <mesh/polygon/document.h>

namespace quader {
namespace {

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

ViewportOverlayLayer fromRuntime(quader_editor_core::SelectionOverlayLayer layer) {
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::selected_visible_face:
        return ViewportOverlayLayer::SelectedVisibleFace;
    case quader_editor_core::SelectionOverlayLayer::selected_face_edge:
        return ViewportOverlayLayer::SelectedFaceEdge;
    case quader_editor_core::SelectionOverlayLayer::unselected_vertex:
        return ViewportOverlayLayer::UnselectedVertex;
    case quader_editor_core::SelectionOverlayLayer::selected_vertex:
        return ViewportOverlayLayer::SelectedVertex;
    case quader_editor_core::SelectionOverlayLayer::hover_face:
        return ViewportOverlayLayer::HoverFace;
    case quader_editor_core::SelectionOverlayLayer::hover_face_edge:
        return ViewportOverlayLayer::HoverFaceEdge;
    case quader_editor_core::SelectionOverlayLayer::hover_edge:
        return ViewportOverlayLayer::HoverEdge;
    case quader_editor_core::SelectionOverlayLayer::hover_vertex:
        return ViewportOverlayLayer::HoverVertex;
    case quader_editor_core::SelectionOverlayLayer::selected_edge:
        return ViewportOverlayLayer::SelectedEdge;
    case quader_editor_core::SelectionOverlayLayer::selected_xray_face:
        return ViewportOverlayLayer::SelectedXrayFace;
    case quader_editor_core::SelectionOverlayLayer::selected_face_mask:
        return ViewportOverlayLayer::SelectedFaceMask;
    case quader_editor_core::SelectionOverlayLayer::source_wire:
        return ViewportOverlayLayer::SourceWire;
    case quader_editor_core::SelectionOverlayLayer::open_edge:
        return ViewportOverlayLayer::OpenEdge;
    case quader_editor_core::SelectionOverlayLayer::snap_target_vertex:
        return ViewportOverlayLayer::SnapTargetVertex;
    case quader_editor_core::SelectionOverlayLayer::diagnostic_edge:
        return ViewportOverlayLayer::DiagnosticEdge;
    case quader_editor_core::SelectionOverlayLayer::source_wire_depth_stamp:
        return ViewportOverlayLayer::SourceWireDepthStamp;
    }
    return ViewportOverlayLayer::SourceWire;
}

ViewportOverlayStyleRole triangleStyleRole(
        quader_editor_core::SelectionOverlayLayer layer,
        std::uint32_t colorAbgr) {
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::selected_visible_face:
    case quader_editor_core::SelectionOverlayLayer::selected_xray_face:
    case quader_editor_core::SelectionOverlayLayer::selected_face_mask:
        return ViewportOverlayStyleRole::SelectionFace;
    case quader_editor_core::SelectionOverlayLayer::hover_face:
        return viewportOverlayIsNativeRemoveColor(colorAbgr)
                ? ViewportOverlayStyleRole::RemoveSelectionFace
                : ViewportOverlayStyleRole::HoverFace;
    case quader_editor_core::SelectionOverlayLayer::source_wire_depth_stamp:
        return ViewportOverlayStyleRole::SourceWire;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole lineStyleRole(
        quader_editor_core::SelectionOverlayLayer layer,
        std::uint32_t colorAbgr) {
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::source_wire:
        return viewportOverlayIsNativeSelectionWireColor(colorAbgr)
                ? ViewportOverlayStyleRole::SelectedSourceWire
                : ViewportOverlayStyleRole::SourceWire;
    case quader_editor_core::SelectionOverlayLayer::selected_face_edge:
    case quader_editor_core::SelectionOverlayLayer::selected_edge:
        return ViewportOverlayStyleRole::SelectionWire;
    case quader_editor_core::SelectionOverlayLayer::hover_face_edge:
    case quader_editor_core::SelectionOverlayLayer::hover_edge:
        return viewportOverlayIsNativeRemoveColor(colorAbgr)
                ? ViewportOverlayStyleRole::RemoveSelectionWire
                : ViewportOverlayStyleRole::HoverWire;
    case quader_editor_core::SelectionOverlayLayer::open_edge:
        return ViewportOverlayStyleRole::OpenEdge;
    case quader_editor_core::SelectionOverlayLayer::diagnostic_edge:
        return ViewportOverlayStyleRole::DiagnosticEdge;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole pointStyleRole(
        quader_editor_core::SelectionOverlayLayer layer,
        std::uint32_t colorAbgr) {
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::unselected_vertex:
        return ViewportOverlayStyleRole::Vertex;
    case quader_editor_core::SelectionOverlayLayer::selected_vertex:
        return ViewportOverlayStyleRole::SelectedVertex;
    case quader_editor_core::SelectionOverlayLayer::hover_vertex:
        return viewportOverlayIsNativeRemoveColor(colorAbgr)
                ? ViewportOverlayStyleRole::RemoveSelectionVertex
                : ViewportOverlayStyleRole::HoverVertex;
    case quader_editor_core::SelectionOverlayLayer::snap_target_vertex:
        return ViewportOverlayStyleRole::SnapTargetVertex;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlaySource overlaySourceForRole(ViewportOverlayStyleRole role) {
    switch (role) {
    case ViewportOverlayStyleRole::HoverFace:
    case ViewportOverlayStyleRole::HoverWire:
    case ViewportOverlayStyleRole::HoverVertex:
    case ViewportOverlayStyleRole::RemoveSelectionFace:
    case ViewportOverlayStyleRole::RemoveSelectionWire:
    case ViewportOverlayStyleRole::RemoveSelectionVertex:
        return ViewportOverlaySource::Hover;
    case ViewportOverlayStyleRole::OpenEdge:
    case ViewportOverlayStyleRole::DiagnosticEdge:
        return ViewportOverlaySource::Diagnostic;
    case ViewportOverlayStyleRole::ToolPlaneFill:
    case ViewportOverlayStyleRole::ToolPlaneLine:
    case ViewportOverlayStyleRole::ToolHandle:
    case ViewportOverlayStyleRole::ToolPreviewFill:
    case ViewportOverlayStyleRole::ToolPreviewLine:
    case ViewportOverlayStyleRole::ToolPreviewSeam:
    case ViewportOverlayStyleRole::CutPreviewKeepFill:
    case ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill:
    case ViewportOverlayStyleRole::CutPreviewDiscardFill:
    case ViewportOverlayStyleRole::CutPreviewKeepBothOutline:
    case ViewportOverlayStyleRole::CutPreviewDiscardOutline:
    case ViewportOverlayStyleRole::CutPreviewSeam:
        return ViewportOverlaySource::Tool;
    case ViewportOverlayStyleRole::LayerDefault:
    case ViewportOverlayStyleRole::ExplicitColor:
    case ViewportOverlayStyleRole::SelectionFace:
    case ViewportOverlayStyleRole::SelectionWire:
    case ViewportOverlayStyleRole::SelectedSourceWire:
    case ViewportOverlayStyleRole::SourceWire:
    case ViewportOverlayStyleRole::Vertex:
    case ViewportOverlayStyleRole::SelectedVertex:
    case ViewportOverlayStyleRole::SnapTargetVertex:
    case ViewportOverlayStyleRole::GroupBounds:
    case ViewportOverlayStyleRole::WireframeBase:
        return ViewportOverlaySource::Selection;
    }
    return ViewportOverlaySource::Unknown;
}

ViewportOverlayElementRef runtimeElementRef(
        int objectId,
        quader_poly::ElementId vertexId,
        quader_poly::Edge edge,
        quader_poly::ElementId faceId) {
    return {
            .objectId = objectId,
            .vertexId = static_cast<int>(vertexId),
            .edgeA = static_cast<int>(edge.a),
            .edgeB = static_cast<int>(edge.b),
            .faceId = static_cast<int>(faceId),
    };
}

QVec3 transformObjectPoint(
        int objectId,
        QVec3 point,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene) {
    const auto entity = entityByObject.find(objectId);
    if (entity == entityByObject.end()) {
        return point;
    }
    const TransformComponent* transform = scene.transform(entity->second);
    if (transform == nullptr) {
        return point;
    }
    return transformPoint(scene.worldTransformMatrix(entity->second), point);
}

bool objectEffectivelyVisible(
        int objectId,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene) {
    if (objectId < 0) {
        return true;
    }
    const auto entity = entityByObject.find(objectId);
    return entity != entityByObject.end() && scene.isEffectivelyVisible(entity->second);
}

} // namespace

ViewportOverlayFrame SelectionOverlayProjection::projectSelectionOverlayFrame(
        const quader_editor_core::SelectionOverlayFrame& frame,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene) {
    ViewportOverlayFrame result;
    result.triangles.reserve(frame.triangles.size());
    result.lines.reserve(frame.lines.size());
    result.points.reserve(frame.points.size());
    result.hoverTargetSelected = frame.hover_target_selected;

    for (const quader_editor_core::SelectionOverlayTriangle& triangle : frame.triangles) {
        if (!objectEffectivelyVisible(triangle.object_id, entityByObject, scene)) {
            continue;
        }
        const ViewportOverlayLayer layer = fromRuntime(triangle.layer);
        const ViewportOverlayStyleRole role = triangleStyleRole(triangle.layer, triangle.color_abgr);
        result.triangles.push_back(ViewportOverlayTriangle{
                .layer = layer,
                .a = transformObjectPoint(triangle.object_id, fromRuntime(triangle.a), entityByObject, scene),
                .b = transformObjectPoint(triangle.object_id, fromRuntime(triangle.b), entityByObject, scene),
                .c = transformObjectPoint(triangle.object_id, fromRuntime(triangle.c), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, triangle.color_abgr),
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = runtimeElementRef(triangle.object_id, quader_poly::invalid_element_id, {}, triangle.face_id),
        });
    }
    for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
        if (!objectEffectivelyVisible(line.object_id, entityByObject, scene)) {
            continue;
        }
        const ViewportOverlayLayer layer = fromRuntime(line.layer);
        const ViewportOverlayStyleRole role = lineStyleRole(line.layer, line.color_abgr);
        result.lines.push_back(ViewportOverlayLine{
                .layer = layer,
                .start = transformObjectPoint(line.object_id, fromRuntime(line.start), entityByObject, scene),
                .end = transformObjectPoint(line.object_id, fromRuntime(line.end), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, line.color_abgr),
                .widthPixels = line.width_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = runtimeElementRef(
                        line.object_id,
                        quader_poly::invalid_element_id,
                        line.edge,
                        line.face_id),
        });
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : frame.points) {
        if (!objectEffectivelyVisible(point.object_id, entityByObject, scene)) {
            continue;
        }
        const ViewportOverlayLayer layer = fromRuntime(point.layer);
        const ViewportOverlayStyleRole role = pointStyleRole(point.layer, point.color_abgr);
        result.points.push_back(ViewportOverlayPoint{
                .layer = layer,
                .position = transformObjectPoint(point.object_id, fromRuntime(point.position), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, point.color_abgr),
                .sizePixels = point.size_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = runtimeElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
        });
    }
    return result;
}

void SelectionOverlayProjection::appendProjectedOverlayLines(
        ViewportOverlayFrame& frame,
        const std::vector<quader_editor_core::SelectionOverlayLine>& lines,
        const std::unordered_map<int, EntityId>& entityByObject,
        const SceneDocument& scene,
        ViewportOverlayLayer layer,
        ViewportOverlayStyleRole styleRole,
        ViewportOverlaySource source) {
    frame.lines.reserve(frame.lines.size() + lines.size());
    for (const quader_editor_core::SelectionOverlayLine& line : lines) {
        if (!objectEffectivelyVisible(line.object_id, entityByObject, scene)) {
            continue;
        }
        frame.lines.push_back(ViewportOverlayLine{
                .layer = layer,
                .start = transformObjectPoint(line.object_id, fromRuntime(line.start), entityByObject, scene),
                .end = transformObjectPoint(line.object_id, fromRuntime(line.end), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, line.color_abgr),
                .widthPixels = line.width_pixels,
                .styleRole = styleRole,
                .source = source,
                .element = runtimeElementRef(
                        line.object_id,
                        quader_poly::invalid_element_id,
                        line.edge,
                        line.face_id),
        });
    }
}

void SelectionOverlayProjection::appendWorldSpaceOverlayPoints(
        ViewportOverlayFrame& frame,
        const std::vector<quader_editor_core::SelectionOverlayPoint>& points) {
    frame.points.reserve(frame.points.size() + points.size());
    for (const quader_editor_core::SelectionOverlayPoint& point : points) {
        const ViewportOverlayLayer layer = fromRuntime(point.layer);
        const ViewportOverlayStyleRole role = pointStyleRole(point.layer, point.color_abgr);
        frame.points.push_back(ViewportOverlayPoint{
                .layer = layer,
                .position = fromRuntime(point.position),
                .color = viewportOverlayColorFor(layer, point.color_abgr),
                .sizePixels = point.size_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = runtimeElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
        });
    }
}

} // namespace quader
