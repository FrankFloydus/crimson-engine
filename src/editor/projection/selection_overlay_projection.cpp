////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/selection_overlay_projection.hpp"

#include "diagnostics/profile.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "math/glm_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <optional>
#include <unordered_map>

#include <mesh/polygon/document.hpp>

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
    if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
                quader_editor_core::selection_overlay_style_role_from_token(colorAbgr)) {
        switch (*role) {
        case quader_editor_core::SelectionOverlayStyleRole::selection_face:
            return ViewportOverlayStyleRole::SelectionFace;
        case quader_editor_core::SelectionOverlayStyleRole::hover_face:
            return ViewportOverlayStyleRole::HoverFace;
        case quader_editor_core::SelectionOverlayStyleRole::remove_selection_face:
            return ViewportOverlayStyleRole::RemoveSelectionFace;
        default:
            break;
        }
    }
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::selected_visible_face:
    case quader_editor_core::SelectionOverlayLayer::selected_xray_face:
    case quader_editor_core::SelectionOverlayLayer::selected_face_mask:
        return ViewportOverlayStyleRole::SelectionFace;
    case quader_editor_core::SelectionOverlayLayer::hover_face:
        return ViewportOverlayStyleRole::HoverFace;
    case quader_editor_core::SelectionOverlayLayer::source_wire_depth_stamp:
        return ViewportOverlayStyleRole::SourceWire;
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole lineStyleRole(
        quader_editor_core::SelectionOverlayLayer layer,
        std::uint32_t colorAbgr) {
    if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
                quader_editor_core::selection_overlay_style_role_from_token(colorAbgr)) {
        switch (*role) {
        case quader_editor_core::SelectionOverlayStyleRole::source_wire:
            return ViewportOverlayStyleRole::SourceWire;
        case quader_editor_core::SelectionOverlayStyleRole::selection_wire:
            return layer == quader_editor_core::SelectionOverlayLayer::source_wire
                    ? ViewportOverlayStyleRole::SelectedSourceWire
                    : ViewportOverlayStyleRole::SelectionWire;
        case quader_editor_core::SelectionOverlayStyleRole::hover_wire:
            return ViewportOverlayStyleRole::HoverWire;
        case quader_editor_core::SelectionOverlayStyleRole::remove_selection_wire:
            return ViewportOverlayStyleRole::RemoveSelectionWire;
        case quader_editor_core::SelectionOverlayStyleRole::open_edge:
            return ViewportOverlayStyleRole::OpenEdge;
        case quader_editor_core::SelectionOverlayStyleRole::diagnostic_edge:
            return ViewportOverlayStyleRole::DiagnosticEdge;
        default:
            break;
        }
    }
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::source_wire:
        return ViewportOverlayStyleRole::SourceWire;
    case quader_editor_core::SelectionOverlayLayer::selected_face_edge:
    case quader_editor_core::SelectionOverlayLayer::selected_edge:
        return ViewportOverlayStyleRole::SelectionWire;
    case quader_editor_core::SelectionOverlayLayer::hover_face_edge:
    case quader_editor_core::SelectionOverlayLayer::hover_edge:
        return ViewportOverlayStyleRole::HoverWire;
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
    if (const std::optional<quader_editor_core::SelectionOverlayStyleRole> role =
                quader_editor_core::selection_overlay_style_role_from_token(colorAbgr)) {
        switch (*role) {
        case quader_editor_core::SelectionOverlayStyleRole::vertex:
            return ViewportOverlayStyleRole::Vertex;
        case quader_editor_core::SelectionOverlayStyleRole::selected_vertex:
            return ViewportOverlayStyleRole::SelectedVertex;
        case quader_editor_core::SelectionOverlayStyleRole::hover_vertex:
            return ViewportOverlayStyleRole::HoverVertex;
        case quader_editor_core::SelectionOverlayStyleRole::remove_selection_wire:
            return ViewportOverlayStyleRole::RemoveSelectionVertex;
        case quader_editor_core::SelectionOverlayStyleRole::snap_target_vertex:
            return ViewportOverlayStyleRole::SnapTargetVertex;
        default:
            break;
        }
    }
    switch (layer) {
    case quader_editor_core::SelectionOverlayLayer::unselected_vertex:
        return ViewportOverlayStyleRole::Vertex;
    case quader_editor_core::SelectionOverlayLayer::selected_vertex:
        return ViewportOverlayStyleRole::SelectedVertex;
    case quader_editor_core::SelectionOverlayLayer::hover_vertex:
        return ViewportOverlayStyleRole::HoverVertex;
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

std::uint32_t projectedColorFor(
        ViewportOverlayLayer layer,
        ViewportOverlayStyleRole role,
        std::uint32_t authoredColor) {
    switch (role) {
    case ViewportOverlayStyleRole::SelectionFace:
        return kOverlaySelectionFaceColor;
    case ViewportOverlayStyleRole::SelectionWire:
    case ViewportOverlayStyleRole::SelectedSourceWire:
        return kOverlaySelectionWireColor;
    case ViewportOverlayStyleRole::SourceWire:
        return kOverlaySourceWireColor;
    case ViewportOverlayStyleRole::Vertex:
        return kOverlayVertexColor;
    case ViewportOverlayStyleRole::SelectedVertex:
        return kOverlaySelectedVertexColor;
    case ViewportOverlayStyleRole::HoverFace:
        return kOverlayHoverFaceColor;
    case ViewportOverlayStyleRole::HoverWire:
        return kOverlayHoverWireColor;
    case ViewportOverlayStyleRole::HoverVertex:
        return kOverlayHoverVertexColor;
    case ViewportOverlayStyleRole::RemoveSelectionFace:
        return kOverlayRemoveFaceColor;
    case ViewportOverlayStyleRole::RemoveSelectionWire:
    case ViewportOverlayStyleRole::RemoveSelectionVertex:
        return kOverlayRemoveWireColor;
    case ViewportOverlayStyleRole::OpenEdge:
        return kOverlayOpenEdgeColor;
    case ViewportOverlayStyleRole::DiagnosticEdge:
        return kOverlayDiagnosticEdgeColor;
    case ViewportOverlayStyleRole::SnapTargetVertex:
        return kOverlayDiagnosticEdgeColor;
    default:
        return viewportOverlayColorFor(layer, authoredColor);
    }
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

struct ProjectionObjectState {
    bool visible = true;
    bool hasWorldTransform = false;
    QMat4 worldTransform;
};

class ProjectionObjectStateCache {
public:
    ProjectionObjectStateCache() {
        states_.reserve(64);
    }

    [[nodiscard]] const ProjectionObjectState& stateFor(
            int objectId,
            const ModelingObjectRuntimeMap& objectRegistry,
            const SceneDocument& scene) {
        if (objectId < 0) {
            lastObjectId_ = objectId;
            lastState_ = &defaultState_;
            return defaultState_;
        }
        if (objectId == lastObjectId_ && lastState_ != nullptr) {
            return *lastState_;
        }
        const auto existing = states_.find(objectId);
        if (existing != states_.end()) {
            lastObjectId_ = objectId;
            lastState_ = &existing->second;
            return existing->second;
        }

        ProjectionObjectState state;
        const std::optional<EntityId> entity = objectRegistry.entityForModelingObject(objectId);
        if (!entity.has_value()) {
            state.visible = false;
        } else {
            state.visible = scene.isEffectivelyVisible(*entity);
            if (state.visible && scene.transform(*entity) != nullptr) {
                state.hasWorldTransform = true;
                state.worldTransform = scene.worldTransformMatrix(*entity);
            }
        }
        const auto [inserted, _] = states_.emplace(objectId, state);
        lastObjectId_ = objectId;
        lastState_ = &inserted->second;
        return *lastState_;
    }

private:
    ProjectionObjectState defaultState_;
    std::unordered_map<int, ProjectionObjectState> states_;
    int lastObjectId_ = -1;
    const ProjectionObjectState* lastState_ = nullptr;
};

QVec3 transformObjectPoint(const ProjectionObjectState& state, QVec3 point) {
    return state.hasWorldTransform ? transformPoint(state.worldTransform, point) : point;
}

} // namespace

ViewportOverlayFrame SelectionOverlayProjection::projectSelectionOverlayFrame(
        const quader_editor_core::SelectionOverlayFrame& frame,
        const ModelingObjectRuntimeMap& objectRegistry,
        const SceneDocument& scene) {
    QDR_PROFILE_FUNCTION();
    ViewportOverlayFrame result;
    result.triangles.reserve(frame.triangles.size());
    result.lines.reserve(frame.lines.size());
    result.points.reserve(frame.points.size());
    result.hoverTargetSelected = frame.hover_target_selected;
    ProjectionObjectStateCache objectStateCache;

    {
        QDR_PROFILE_SCOPE("Selection overlay project triangles");
        for (const quader_editor_core::SelectionOverlayTriangle& triangle : frame.triangles) {
            const ProjectionObjectState& objectState =
                    objectStateCache.stateFor(triangle.object_id, objectRegistry, scene);
            if (!objectState.visible) {
                continue;
            }
            const ViewportOverlayLayer layer = fromRuntime(triangle.layer);
            const ViewportOverlayStyleRole role = triangleStyleRole(triangle.layer, triangle.color_abgr);
            result.triangles.push_back(ViewportOverlayTriangle{
                    .layer = layer,
                    .a = transformObjectPoint(objectState, fromRuntime(triangle.a)),
                    .b = transformObjectPoint(objectState, fromRuntime(triangle.b)),
                    .c = transformObjectPoint(objectState, fromRuntime(triangle.c)),
                    .color = projectedColorFor(layer, role, triangle.color_abgr),
                    .styleRole = role,
                    .source = overlaySourceForRole(role),
                    .element = runtimeElementRef(triangle.object_id, quader_poly::invalid_element_id, {}, triangle.face_id),
            });
        }
    }
    {
        QDR_PROFILE_SCOPE("Selection overlay project lines");
        for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
            const ProjectionObjectState& objectState =
                    objectStateCache.stateFor(line.object_id, objectRegistry, scene);
            if (!objectState.visible) {
                continue;
            }
            const ViewportOverlayLayer layer = fromRuntime(line.layer);
            const ViewportOverlayStyleRole role = lineStyleRole(line.layer, line.color_abgr);
            result.lines.push_back(ViewportOverlayLine{
                    .layer = layer,
                    .start = transformObjectPoint(objectState, fromRuntime(line.start)),
                    .end = transformObjectPoint(objectState, fromRuntime(line.end)),
                    .color = projectedColorFor(layer, role, line.color_abgr),
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
    }
    {
        QDR_PROFILE_SCOPE("Selection overlay project points");
        for (const quader_editor_core::SelectionOverlayPoint& point : frame.points) {
            const ProjectionObjectState& objectState =
                    objectStateCache.stateFor(point.object_id, objectRegistry, scene);
            if (!objectState.visible) {
                continue;
            }
            const ViewportOverlayLayer layer = fromRuntime(point.layer);
            const ViewportOverlayStyleRole role = pointStyleRole(point.layer, point.color_abgr);
            result.points.push_back(ViewportOverlayPoint{
                    .layer = layer,
                    .position = transformObjectPoint(objectState, fromRuntime(point.position)),
                    .color = projectedColorFor(layer, role, point.color_abgr),
                    .sizePixels = point.size_pixels,
                    .styleRole = role,
                    .source = overlaySourceForRole(role),
                    .element = runtimeElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
            });
        }
    }
    return result;
}

void SelectionOverlayProjection::appendProjectedOverlayLines(
        ViewportOverlayFrame& frame,
        const std::vector<quader_editor_core::SelectionOverlayLine>& lines,
        const ModelingObjectRuntimeMap& objectRegistry,
        const SceneDocument& scene,
        ViewportOverlayLayer layer,
        ViewportOverlayStyleRole styleRole,
        ViewportOverlaySource source) {
    QDR_PROFILE_FUNCTION();
    frame.lines.reserve(frame.lines.size() + lines.size());
    ProjectionObjectStateCache objectStateCache;
    for (const quader_editor_core::SelectionOverlayLine& line : lines) {
        const ProjectionObjectState& objectState =
                objectStateCache.stateFor(line.object_id, objectRegistry, scene);
        if (!objectState.visible) {
            continue;
        }
        frame.lines.push_back(ViewportOverlayLine{
                .layer = layer,
                .start = transformObjectPoint(objectState, fromRuntime(line.start)),
                .end = transformObjectPoint(objectState, fromRuntime(line.end)),
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
                .color = projectedColorFor(layer, role, point.color_abgr),
                .sizePixels = point.size_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = runtimeElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
        });
    }
}

} // namespace quader
