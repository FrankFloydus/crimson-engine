#include "modeling/ModelingSessionService.h"

#include "assets/AssetRegistry.h"
#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionServices.h"
#include "render/ViewportOverlayPolicy.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if QUADER_HAS_MODELING_SDK
#include <quader_editor_core/operation_command.h>
#include <quader_editor_core/repeat_last_action.h>
#include <quader_editor_core/runtime_command.h>
#include <quader_editor_core/session.h>
#include <quader_editor_core/tool/box_tool.h>
#include <quader_editor_core/tool/cut_tool.h>
#include <quader_editor_core/tool/mirror_tool.h>
#include <quader_editor_core/tool/move_tool.h>
#include <quader_editor_core/tool/viewport_projection.h>
#include <quader_editor_core/tool/rotate_tool.h>
#include <quader_editor_core/tool/scale_tool.h>
#include <quader_editor_core/tool/selection_edit.h>
#include <quader_editor_core/tool/transform_drag.h>
#include <quader_host_contract/host_contract.h>
#include <quader_poly/document.h>
#endif

namespace quader {
namespace {

#if QUADER_HAS_MODELING_SDK
quader_poly::Vec3 toSdk(Vec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Vec2 toSdk(Vec2 value) {
    return {value.x, value.y};
}

quader_poly::Ray toSdk(const ModelingRay& ray) {
    return {
            .origin = toSdk(ray.origin),
            .direction = toSdk(normalize(ray.direction)),
    };
}

quader_editor_core::ViewportCameraState toSdk(const ModelingViewportCamera& value) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = value.orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = {
            .x_axis = toSdk(value.xAxis),
            .y_axis = toSdk(value.yAxis),
            .z_axis = toSdk(value.zAxis),
            .origin = toSdk(value.origin),
    };
    camera.viewport_size = {
            std::max(value.viewportSize.x, 1.0f),
            std::max(value.viewportSize.y, 1.0f),
    };
    camera.orthographic_size = std::max(value.orthographicSize, 0.0001);
    camera.fov_degrees = value.fovDegrees;
    return camera;
}

quader_poly::Transform3 toSdk(const ModelingTransform3& transform) {
    return {
            .x_axis = toSdk(transform.xAxis),
            .y_axis = toSdk(transform.yAxis),
            .z_axis = toSdk(transform.zAxis),
            .origin = toSdk(transform.origin),
    };
}

quader_editor_core::ViewportInteractionTool toSdk(ModelingViewportTool tool) {
    switch (tool) {
    case ModelingViewportTool::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ModelingViewportTool::Extend:
        return quader_editor_core::ViewportInteractionTool::extend;
    case ModelingViewportTool::Extrude:
        return quader_editor_core::ViewportInteractionTool::extrude;
    case ModelingViewportTool::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ModelingViewportTool::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    case ModelingViewportTool::Box:
        return quader_editor_core::ViewportInteractionTool::box;
    case ModelingViewportTool::InsertEdgeLoop:
        return quader_editor_core::ViewportInteractionTool::insert_edge_loop;
    case ModelingViewportTool::Knife:
        return quader_editor_core::ViewportInteractionTool::knife;
    case ModelingViewportTool::Cut:
        return quader_editor_core::ViewportInteractionTool::cut;
    case ModelingViewportTool::Poly:
        return quader_editor_core::ViewportInteractionTool::poly;
    case ModelingViewportTool::Mirror:
        return quader_editor_core::ViewportInteractionTool::mirror;
    case ModelingViewportTool::Select:
        return quader_editor_core::ViewportInteractionTool::select;
    }
    return quader_editor_core::ViewportInteractionTool::select;
}

ModelingViewportTool fromSdkViewportTool(quader_editor_core::ViewportInteractionTool tool) {
    switch (tool) {
    case quader_editor_core::ViewportInteractionTool::move:
        return ModelingViewportTool::Move;
    case quader_editor_core::ViewportInteractionTool::extend:
        return ModelingViewportTool::Extend;
    case quader_editor_core::ViewportInteractionTool::extrude:
        return ModelingViewportTool::Extrude;
    case quader_editor_core::ViewportInteractionTool::rotate:
        return ModelingViewportTool::Rotate;
    case quader_editor_core::ViewportInteractionTool::scale:
        return ModelingViewportTool::Scale;
    case quader_editor_core::ViewportInteractionTool::box:
        return ModelingViewportTool::Box;
    case quader_editor_core::ViewportInteractionTool::insert_edge_loop:
        return ModelingViewportTool::InsertEdgeLoop;
    case quader_editor_core::ViewportInteractionTool::knife:
        return ModelingViewportTool::Knife;
    case quader_editor_core::ViewportInteractionTool::cut:
        return ModelingViewportTool::Cut;
    case quader_editor_core::ViewportInteractionTool::poly:
        return ModelingViewportTool::Poly;
    case quader_editor_core::ViewportInteractionTool::mirror:
        return ModelingViewportTool::Mirror;
    case quader_editor_core::ViewportInteractionTool::select:
    case quader_editor_core::ViewportInteractionTool::unknown:
        break;
    }
    return ModelingViewportTool::Select;
}

quader_editor_core::CutMode toSdk(ModelingCutMode mode) {
    switch (mode) {
    case ModelingCutMode::DiscardLeft:
        return quader_editor_core::CutMode::keep_back;
    case ModelingCutMode::DiscardRight:
        return quader_editor_core::CutMode::keep_front;
    case ModelingCutMode::KeepBoth:
        return quader_editor_core::CutMode::keep_both;
    }
    return quader_editor_core::CutMode::keep_both;
}

ModelingCutMode fromSdk(quader_editor_core::CutMode mode) {
    switch (mode) {
    case quader_editor_core::CutMode::keep_back:
        return ModelingCutMode::DiscardLeft;
    case quader_editor_core::CutMode::keep_front:
        return ModelingCutMode::DiscardRight;
    case quader_editor_core::CutMode::keep_both:
        return ModelingCutMode::KeepBoth;
    }
    return ModelingCutMode::KeepBoth;
}

quader_editor_core::EditorOperationSettings settingsForOperation(
        quader_editor_core::EditorOperationId operation,
        const ModelingInteractionSettings& interactionSettings) {
    switch (operation) {
    case quader_editor_core::EditorOperationId::bridge:
        return quader_editor_core::BridgeOperationSettings{.steps = interactionSettings.bridgeSteps};
    case quader_editor_core::EditorOperationId::edge_bevel:
        return quader_editor_core::EdgeBevelOperationSettings{
                .width = interactionSettings.edgeBevelWidth,
                .profile = interactionSettings.edgeBevelProfile,
                .segments = interactionSettings.edgeBevelSegments,
        };
    case quader_editor_core::EditorOperationId::thicken:
        return quader_editor_core::ThickenOperationSettings{
                .thickness = interactionSettings.thickenThickness,
                .from_center = interactionSettings.thickenFromCenter,
        };
    case quader_editor_core::EditorOperationId::slice_quad:
        return quader_editor_core::SliceQuadOperationSettings{
                .x_slices = interactionSettings.sliceQuadXSlices,
                .y_slices = interactionSettings.sliceQuadYSlices,
        };
    case quader_editor_core::EditorOperationId::merge_by_distance:
        return quader_editor_core::MergeByDistanceOperationSettings{
                .tolerance = interactionSettings.mergeByDistanceTolerance,
        };
    case quader_editor_core::EditorOperationId::bevel_vertices:
        return quader_editor_core::BevelVerticesOperationSettings{};
    case quader_editor_core::EditorOperationId::duplicate_meshes:
        return quader_editor_core::DuplicateMeshesOperationSettings{};
    case quader_editor_core::EditorOperationId::recalculate_face_normals: {
        quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
        settings.outside = true;
        return settings;
    }
    case quader_editor_core::EditorOperationId::assign_material:
        return quader_editor_core::AssignMaterialOperationSettings{};
    case quader_editor_core::EditorOperationId::connect_vertices:
    case quader_editor_core::EditorOperationId::dissolve_vertices:
    case quader_editor_core::EditorOperationId::connect_edges:
    case quader_editor_core::EditorOperationId::split_edges:
    case quader_editor_core::EditorOperationId::dissolve_edges:
    case quader_editor_core::EditorOperationId::merge_edges:
    case quader_editor_core::EditorOperationId::collapse_edges:
    case quader_editor_core::EditorOperationId::fill_hole:
    case quader_editor_core::EditorOperationId::combine_faces:
    case quader_editor_core::EditorOperationId::collapse_faces:
    case quader_editor_core::EditorOperationId::radial_align:
    case quader_editor_core::EditorOperationId::flip_face_normals:
    case quader_editor_core::EditorOperationId::shade_faces_smooth:
    case quader_editor_core::EditorOperationId::shade_faces_flat:
    case quader_editor_core::EditorOperationId::harden_edge_normals:
    case quader_editor_core::EditorOperationId::soften_edge_normals:
    case quader_editor_core::EditorOperationId::combine_meshes:
    case quader_editor_core::EditorOperationId::extract_faces:
    case quader_editor_core::EditorOperationId::detach_faces:
    case quader_editor_core::EditorOperationId::snap_vertices_to_active:
    case quader_editor_core::EditorOperationId::snap_edges_to_active:
    case quader_editor_core::EditorOperationId::merge_vertices_to_active:
    case quader_editor_core::EditorOperationId::merge_vertices_to_center:
    case quader_editor_core::EditorOperationId::delete_selection:
        return quader_editor_core::SelectionOperationSettings{};
    }
    return quader_editor_core::SelectionOperationSettings{};
}

ModelingToolSettingsTargetKind toolSettingsTargetKindForPreview(quader_editor_core::PreviewTransactionKind kind) {
    switch (kind) {
    case quader_editor_core::PreviewTransactionKind::bridge:
        return ModelingToolSettingsTargetKind::Bridge;
    case quader_editor_core::PreviewTransactionKind::edge_bevel:
        return ModelingToolSettingsTargetKind::EdgeBevel;
    case quader_editor_core::PreviewTransactionKind::thicken:
        return ModelingToolSettingsTargetKind::Thicken;
    case quader_editor_core::PreviewTransactionKind::slice_quad:
        return ModelingToolSettingsTargetKind::SliceQuad;
    case quader_editor_core::PreviewTransactionKind::merge_by_distance:
        return ModelingToolSettingsTargetKind::MergeByDistance;
    case quader_editor_core::PreviewTransactionKind::none:
    case quader_editor_core::PreviewTransactionKind::transform:
        break;
    }
    return ModelingToolSettingsTargetKind::None;
}

std::optional<quader_editor_core::EditorOperationId> operationForPreviewKind(
        quader_editor_core::PreviewTransactionKind kind) {
    switch (kind) {
    case quader_editor_core::PreviewTransactionKind::bridge:
        return quader_editor_core::EditorOperationId::bridge;
    case quader_editor_core::PreviewTransactionKind::edge_bevel:
        return quader_editor_core::EditorOperationId::edge_bevel;
    case quader_editor_core::PreviewTransactionKind::thicken:
        return quader_editor_core::EditorOperationId::thicken;
    case quader_editor_core::PreviewTransactionKind::slice_quad:
        return quader_editor_core::EditorOperationId::slice_quad;
    case quader_editor_core::PreviewTransactionKind::merge_by_distance:
        return quader_editor_core::EditorOperationId::merge_by_distance;
    case quader_editor_core::PreviewTransactionKind::none:
    case quader_editor_core::PreviewTransactionKind::transform:
        break;
    }
    return std::nullopt;
}

const char* fallbackToolSettingsLabel(ModelingToolSettingsTargetKind kind) {
    switch (kind) {
    case ModelingToolSettingsTargetKind::Cut:
        return "Cut";
    case ModelingToolSettingsTargetKind::Mirror:
        return "Mirror";
    case ModelingToolSettingsTargetKind::Bridge:
        return "Interpolated Bridge";
    case ModelingToolSettingsTargetKind::EdgeBevel:
        return "Bevel Edges";
    case ModelingToolSettingsTargetKind::Thicken:
        return "Distance";
    case ModelingToolSettingsTargetKind::SliceQuad:
        return "Slice Quad";
    case ModelingToolSettingsTargetKind::MergeByDistance:
        return "Merge by Distance";
    case ModelingToolSettingsTargetKind::None:
        break;
    }
    return "Tool Settings";
}

Vec3 fromSdk(quader_poly::Vec3 value) {
    return {value.x, value.y, value.z};
}

ModelingTransform3 fromSdk(quader_poly::Transform3 value) {
    return {
            .xAxis = fromSdk(value.x_axis),
            .yAxis = fromSdk(value.y_axis),
            .zAxis = fromSdk(value.z_axis),
            .origin = fromSdk(value.origin),
    };
}

ModelingSelectionMode fromSdk(quader_editor_core::SelectionMode mode) {
    switch (mode) {
    case quader_editor_core::SelectionMode::vertex:
        return ModelingSelectionMode::Vertex;
    case quader_editor_core::SelectionMode::edge:
        return ModelingSelectionMode::Edge;
    case quader_editor_core::SelectionMode::face:
        return ModelingSelectionMode::Face;
    case quader_editor_core::SelectionMode::object:
        return ModelingSelectionMode::Object;
    }
    return ModelingSelectionMode::Object;
}

quader_editor_core::SelectionMode toSdk(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return quader_editor_core::SelectionMode::vertex;
    case ModelingSelectionMode::Edge:
        return quader_editor_core::SelectionMode::edge;
    case ModelingSelectionMode::Face:
        return quader_editor_core::SelectionMode::face;
    case ModelingSelectionMode::Object:
    case ModelingSelectionMode::Group:
        return quader_editor_core::SelectionMode::object;
    }
    return quader_editor_core::SelectionMode::object;
}

quader_editor_core::TransformAxis toSdk(ModelingMirrorAxis axis) {
    switch (axis) {
    case ModelingMirrorAxis::X:
        return quader_editor_core::TransformAxis::x;
    case ModelingMirrorAxis::Y:
        return quader_editor_core::TransformAxis::y;
    case ModelingMirrorAxis::Z:
        return quader_editor_core::TransformAxis::z;
    }
    return quader_editor_core::TransformAxis::none;
}

quader_editor_core::TransformAxis toSdk(ModelingTransformAxis axis) {
    switch (axis) {
    case ModelingTransformAxis::X:
        return quader_editor_core::TransformAxis::x;
    case ModelingTransformAxis::Y:
        return quader_editor_core::TransformAxis::y;
    case ModelingTransformAxis::Z:
        return quader_editor_core::TransformAxis::z;
    case ModelingTransformAxis::XY:
        return quader_editor_core::TransformAxis::xy;
    case ModelingTransformAxis::XZ:
        return quader_editor_core::TransformAxis::xz;
    case ModelingTransformAxis::YZ:
        return quader_editor_core::TransformAxis::yz;
    case ModelingTransformAxis::All:
        return quader_editor_core::TransformAxis::all;
    case ModelingTransformAxis::None:
        return quader_editor_core::TransformAxis::none;
    }
    return quader_editor_core::TransformAxis::none;
}

ModelingTransformAxis fromSdk(quader_editor_core::TransformAxis axis) {
    switch (axis) {
    case quader_editor_core::TransformAxis::x:
        return ModelingTransformAxis::X;
    case quader_editor_core::TransformAxis::y:
        return ModelingTransformAxis::Y;
    case quader_editor_core::TransformAxis::z:
        return ModelingTransformAxis::Z;
    case quader_editor_core::TransformAxis::xy:
        return ModelingTransformAxis::XY;
    case quader_editor_core::TransformAxis::xz:
        return ModelingTransformAxis::XZ;
    case quader_editor_core::TransformAxis::yz:
        return ModelingTransformAxis::YZ;
    case quader_editor_core::TransformAxis::all:
        return ModelingTransformAxis::All;
    case quader_editor_core::TransformAxis::none:
        return ModelingTransformAxis::None;
    }
    return ModelingTransformAxis::None;
}

quader_editor_core::ViewportInteractionTool toSdk(ModelingTransformTool tool) {
    switch (tool) {
    case ModelingTransformTool::Move:
        return quader_editor_core::ViewportInteractionTool::move;
    case ModelingTransformTool::Rotate:
        return quader_editor_core::ViewportInteractionTool::rotate;
    case ModelingTransformTool::Scale:
        return quader_editor_core::ViewportInteractionTool::scale;
    }
    return quader_editor_core::ViewportInteractionTool::unknown;
}

ModelingTransformTool fromSdk(quader_editor_core::ViewportInteractionTool tool) {
    switch (tool) {
    case quader_editor_core::ViewportInteractionTool::move:
        return ModelingTransformTool::Move;
    case quader_editor_core::ViewportInteractionTool::rotate:
        return ModelingTransformTool::Rotate;
    case quader_editor_core::ViewportInteractionTool::scale:
        return ModelingTransformTool::Scale;
    default:
        return ModelingTransformTool::Move;
    }
}

quader_editor_core::TransformDragOperation toSdk(ModelingTransformOperation operation) {
    switch (operation) {
    case ModelingTransformOperation::Transform:
        return quader_editor_core::TransformDragOperation::transform;
    case ModelingTransformOperation::Extrude:
        return quader_editor_core::TransformDragOperation::extrude;
    case ModelingTransformOperation::Inset:
        return quader_editor_core::TransformDragOperation::inset;
    }
    return quader_editor_core::TransformDragOperation::transform;
}

ModelingTransformOperation fromSdk(quader_editor_core::TransformDragOperation operation) {
    switch (operation) {
    case quader_editor_core::TransformDragOperation::transform:
        return ModelingTransformOperation::Transform;
    case quader_editor_core::TransformDragOperation::extrude:
        return ModelingTransformOperation::Extrude;
    case quader_editor_core::TransformDragOperation::inset:
        return ModelingTransformOperation::Inset;
    }
    return ModelingTransformOperation::Transform;
}

quader_editor_core::ViewportFlipOrientation toSdk(ModelingFlipOrientation orientation) {
    switch (orientation) {
    case ModelingFlipOrientation::Horizontal:
        return quader_editor_core::ViewportFlipOrientation::horizontal;
    case ModelingFlipOrientation::Vertical:
        return quader_editor_core::ViewportFlipOrientation::vertical;
    }
    return quader_editor_core::ViewportFlipOrientation::horizontal;
}

ModelingActiveSelectionKind fromSdk(quader_editor_core::ActiveSelectionKind kind) {
    switch (kind) {
    case quader_editor_core::ActiveSelectionKind::none:
        return ModelingActiveSelectionKind::None;
    case quader_editor_core::ActiveSelectionKind::object:
        return ModelingActiveSelectionKind::Object;
    case quader_editor_core::ActiveSelectionKind::vertex:
        return ModelingActiveSelectionKind::Vertex;
    case quader_editor_core::ActiveSelectionKind::edge:
        return ModelingActiveSelectionKind::Edge;
    case quader_editor_core::ActiveSelectionKind::face:
        return ModelingActiveSelectionKind::Face;
    }
    return ModelingActiveSelectionKind::None;
}

ModelingHoverTarget fromSdk(const quader_editor_core::SelectionHoverState& hover) {
    ModelingHoverTarget target;
    target.objectId = hover.object_id;
    switch (hover.kind) {
    case quader_editor_core::SelectionHoverKind::object:
        target.kind = ModelingActiveSelectionKind::Object;
        break;
    case quader_editor_core::SelectionHoverKind::vertex:
        target.kind = ModelingActiveSelectionKind::Vertex;
        target.vertexId = static_cast<int>(hover.vertex_id);
        break;
    case quader_editor_core::SelectionHoverKind::edge:
        target.kind = ModelingActiveSelectionKind::Edge;
        target.edgeA = static_cast<int>(hover.edge.a);
        target.edgeB = static_cast<int>(hover.edge.b);
        break;
    case quader_editor_core::SelectionHoverKind::face:
        target.kind = ModelingActiveSelectionKind::Face;
        target.faceId = static_cast<int>(hover.face_id);
        break;
    case quader_editor_core::SelectionHoverKind::none:
        target.kind = ModelingActiveSelectionKind::None;
        target.objectId = -1;
        break;
    }
    return target;
}

ModelingHoverSummary fromSdkHoverSummary(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::SelectionHoverState& hover,
        bool changed) {
    return {
            .changed = changed,
            .target = fromSdk(hover),
            .targetSelected = quader_editor_core::selection_hover_target_matches_selection(
                    session.objects(),
                    session.selection_mode(),
                    hover),
    };
}

ViewportOverlayLayer fromSdk(quader_editor_core::SelectionOverlayLayer layer) {
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
    }
    return ViewportOverlayLayer::SourceWire;
}

ViewportOverlayStyleRole sdkTriangleStyleRole(
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
    default:
        return ViewportOverlayStyleRole::ExplicitColor;
    }
}

ViewportOverlayStyleRole sdkLineStyleRole(
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

ViewportOverlayStyleRole sdkPointStyleRole(
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

ViewportOverlayElementRef sdkElementRef(
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

ModelingBoxDragState fromSdk(quader_editor_core::BoxDragState state) {
    switch (state) {
    case quader_editor_core::BoxDragState::idle:
        return ModelingBoxDragState::Idle;
    case quader_editor_core::BoxDragState::footprint:
        return ModelingBoxDragState::Footprint;
    case quader_editor_core::BoxDragState::height:
        return ModelingBoxDragState::Height;
    }
    return ModelingBoxDragState::Idle;
}

Vec3 transformObjectPoint(
        int objectId,
        Vec3 point,
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

ViewportOverlayFrame fromSdk(
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
        const ViewportOverlayLayer layer = fromSdk(triangle.layer);
        const ViewportOverlayStyleRole role = sdkTriangleStyleRole(triangle.layer, triangle.color_abgr);
        result.triangles.push_back(ViewportOverlayTriangle{
                .layer = layer,
                .a = transformObjectPoint(triangle.object_id, fromSdk(triangle.a), entityByObject, scene),
                .b = transformObjectPoint(triangle.object_id, fromSdk(triangle.b), entityByObject, scene),
                .c = transformObjectPoint(triangle.object_id, fromSdk(triangle.c), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, triangle.color_abgr),
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = sdkElementRef(triangle.object_id, quader_poly::invalid_element_id, {}, triangle.face_id),
        });
    }
    for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
        if (!objectEffectivelyVisible(line.object_id, entityByObject, scene)) {
            continue;
        }
        const ViewportOverlayLayer layer = fromSdk(line.layer);
        const ViewportOverlayStyleRole role = sdkLineStyleRole(line.layer, line.color_abgr);
        result.lines.push_back(ViewportOverlayLine{
                .layer = layer,
                .start = transformObjectPoint(line.object_id, fromSdk(line.start), entityByObject, scene),
                .end = transformObjectPoint(line.object_id, fromSdk(line.end), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, line.color_abgr),
                .widthPixels = line.width_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = sdkElementRef(
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
        const ViewportOverlayLayer layer = fromSdk(point.layer);
        const ViewportOverlayStyleRole role = sdkPointStyleRole(point.layer, point.color_abgr);
        result.points.push_back(ViewportOverlayPoint{
                .layer = layer,
                .position = transformObjectPoint(point.object_id, fromSdk(point.position), entityByObject, scene),
                .color = viewportOverlayColorFor(layer, point.color_abgr),
                .sizePixels = point.size_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = sdkElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
        });
    }
    return result;
}

void appendWorldSpaceSdkOverlayPoints(
        ViewportOverlayFrame& frame,
        const std::vector<quader_editor_core::SelectionOverlayPoint>& points) {
    frame.points.reserve(frame.points.size() + points.size());
    for (const quader_editor_core::SelectionOverlayPoint& point : points) {
        const ViewportOverlayLayer layer = fromSdk(point.layer);
        const ViewportOverlayStyleRole role = sdkPointStyleRole(point.layer, point.color_abgr);
        frame.points.push_back(ViewportOverlayPoint{
                .layer = layer,
                .position = fromSdk(point.position),
                .color = viewportOverlayColorFor(layer, point.color_abgr),
                .sizePixels = point.size_pixels,
                .styleRole = role,
                .source = overlaySourceForRole(role),
                .element = sdkElementRef(point.object_id, point.vertex_id, {}, quader_poly::invalid_element_id),
        });
    }
}

struct DrawnPlanePreviewFrame {
    Vec3 lineStart;
    Vec3 lineEnd;
    Vec3 normal;
    Vec3 side;
    float lineLength = 0.0f;
};

bool buildDrawnPlanePreviewFrame(
        Vec3 point0,
        Vec3 point1,
        Vec3 support,
        DrawnPlanePreviewFrame& frame) {
    const Vec3 line = point1 - point0;
    const float lineLength = length(line);
    if (lineLength <= 0.0001f) {
        return false;
    }

    const Vec3 lineDirection = line / lineLength;
    const Vec3 supportVector = support - point0;
    Vec3 normal = cross(line, supportVector);
    if (length(normal) <= 0.000001f) {
        normal = cross(lineDirection, {0.0f, 1.0f, 0.0f});
        if (length(normal) <= 0.000001f) {
            normal = cross(lineDirection, {0.0f, 0.0f, 1.0f});
        }
    }
    normal = normalize(normal);
    if (length(normal) <= 0.000001f) {
        return false;
    }

    Vec3 side = cross(normal, lineDirection);
    if (length(side) <= 0.000001f) {
        side = {0.0f, 1.0f, 0.0f};
    }
    side = normalize(side);
    frame = {
            .lineStart = point0,
            .lineEnd = point1,
            .normal = normal,
            .side = side,
            .lineLength = lineLength,
    };
    return true;
}

void appendDrawnPlaneToolOverlay(
        ViewportOverlayFrame& frame,
        std::span<const quader_poly::Vec3> visiblePoints,
        quader_poly::Vec3 supportPoint,
        bool hasLine) {
    std::array<Vec3, 3> points{};
    const std::size_t count = std::min<std::size_t>(visiblePoints.size(), points.size());
    for (std::size_t index = 0; index < count; ++index) {
        points[index] = fromSdk(visiblePoints[index]);
    }
    if (count <= 0U) {
        return;
    }

    for (std::size_t index = 0; index < count; ++index) {
        frame.points.push_back({
                .layer = ViewportOverlayLayer::ToolHandle,
                .position = points[index],
                .color = kOverlayToolHandleColor,
                .sizePixels = 8.0f,
                .styleRole = ViewportOverlayStyleRole::ToolHandle,
                .source = ViewportOverlaySource::Tool,
        });
    }

    if (!hasLine || count < 2U) {
        return;
    }

    frame.lines.push_back({
            .layer = ViewportOverlayLayer::ToolHandle,
            .start = points[0],
            .end = points[1],
            .color = kOverlayToolHandleColor,
            .widthPixels = 2.0f,
            .styleRole = ViewportOverlayStyleRole::ToolHandle,
            .source = ViewportOverlaySource::Tool,
    });

    DrawnPlanePreviewFrame planeFrame;
    if (!buildDrawnPlanePreviewFrame(points[0], points[1], fromSdk(supportPoint), planeFrame)) {
        return;
    }

    const Vec3 side = planeFrame.side * planeFrame.lineLength;
    const std::array<Vec3, 4> corners{{
            planeFrame.lineStart,
            planeFrame.lineEnd,
            planeFrame.lineEnd + side,
            planeFrame.lineStart + side,
    }};

    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::ToolPlane,
            .a = corners[0],
            .b = corners[1],
            .c = corners[2],
            .color = kOverlayToolPlaneColor,
            .styleRole = ViewportOverlayStyleRole::ToolPlaneFill,
            .source = ViewportOverlaySource::Tool,
    });
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::ToolPlane,
            .a = corners[0],
            .b = corners[2],
            .c = corners[3],
            .color = kOverlayToolPlaneColor,
            .styleRole = ViewportOverlayStyleRole::ToolPlaneFill,
            .source = ViewportOverlaySource::Tool,
    });
    for (std::size_t index = 0; index < corners.size(); ++index) {
        frame.lines.push_back({
                .layer = ViewportOverlayLayer::ToolPlaneLine,
                .start = corners[index],
                .end = corners[(index + 1U) % corners.size()],
                .color = kOverlayToolPlaneLineColor,
                .widthPixels = 1.0f,
                .styleRole = ViewportOverlayStyleRole::ToolPlaneLine,
                .source = ViewportOverlaySource::Tool,
        });
    }
}

float sdkLengthSquared(quader_poly::Vec3 value) {
    return value.x * value.x + value.y * value.y + value.z * value.z;
}

void mixToolStateHash(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

void mixToolStateHash(std::uint64_t& hash, bool value) {
    mixToolStateHash(hash, value ? 1ULL : 0ULL);
}

void mixToolStateHash(std::uint64_t& hash, int value) {
    mixToolStateHash(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void mixToolStateHash(std::uint64_t& hash, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    mixToolStateHash(hash, bits);
}

void mixToolStateHash(std::uint64_t& hash, quader_poly::Vec3 value) {
    mixToolStateHash(hash, static_cast<double>(value.x));
    mixToolStateHash(hash, static_cast<double>(value.y));
    mixToolStateHash(hash, static_cast<double>(value.z));
}

quader_poly::Vec3 drawnPlaneSupportPoint(
        std::span<const quader_poly::Vec3> visiblePoints,
        quader_poly::Vec3 faceNormal) {
    if (visiblePoints.size() > 2U) {
        return visiblePoints[2];
    }
    if (visiblePoints.size() > 1U && sdkLengthSquared(faceNormal) > 0.000001f) {
        return visiblePoints[0] + faceNormal;
    }
    return {};
}

struct PreviewTriangleMesh {
    std::vector<Vec3> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] bool empty() const {
        return vertices.empty() || indices.empty();
    }
};

PreviewTriangleMesh previewMeshFromDocuments(const std::vector<quader_poly::Document>& documents) {
    PreviewTriangleMesh mesh;
    for (const quader_poly::Document& document : documents) {
        const quader_poly::CompiledMesh compiled = quader_poly::compile_document(document);
        const std::uint32_t vertexOffset = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + compiled.vertices.size());
        mesh.indices.reserve(mesh.indices.size() + compiled.indices.size());
        for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
            mesh.vertices.push_back(fromSdk(vertex.position));
        }
        for (const std::uint32_t index : compiled.indices) {
            if (index < compiled.vertices.size()) {
                mesh.indices.push_back(vertexOffset + index);
            }
        }
    }
    return mesh;
}

void appendPreviewMeshFill(
        ViewportOverlayFrame& frame,
        const PreviewTriangleMesh& mesh,
        std::uint32_t color,
        ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::ToolPreviewFill) {
    if (mesh.empty()) {
        return;
    }

    for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
        const std::uint32_t ia = mesh.indices[index];
        const std::uint32_t ib = mesh.indices[index + 1U];
        const std::uint32_t ic = mesh.indices[index + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            continue;
        }
        frame.triangles.push_back({
                .layer = ViewportOverlayLayer::ToolPreviewFill,
                .a = mesh.vertices[ia],
                .b = mesh.vertices[ib],
                .c = mesh.vertices[ic],
                .color = color,
                .styleRole = styleRole,
                .source = ViewportOverlaySource::Tool,
        });
    }
}

constexpr float kCutPreviewPlaneQuantization = 0.0001f;
constexpr float kCutPreviewFeatureEdgeCosine = 0.985f;

struct CutPreviewPointKey {
    long long x = 0;
    long long y = 0;
    long long z = 0;
};

long long quantizeCutPreviewCoordinate(float value) {
    return static_cast<long long>(std::llround(value / kCutPreviewPlaneQuantization));
}

CutPreviewPointKey cutPreviewPointKey(Vec3 value) {
    return {
            .x = quantizeCutPreviewCoordinate(value.x),
            .y = quantizeCutPreviewCoordinate(value.y),
            .z = quantizeCutPreviewCoordinate(value.z),
    };
}

bool cutPreviewPointKeyLess(const CutPreviewPointKey& left, const CutPreviewPointKey& right) {
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.z < right.z;
}

struct CutPreviewEdgeKey {
    CutPreviewPointKey a;
    CutPreviewPointKey b;

    CutPreviewEdgeKey(Vec3 first, Vec3 second) {
        CutPreviewPointKey firstKey = cutPreviewPointKey(first);
        CutPreviewPointKey secondKey = cutPreviewPointKey(second);
        if (cutPreviewPointKeyLess(secondKey, firstKey)) {
            a = secondKey;
            b = firstKey;
        } else {
            a = firstKey;
            b = secondKey;
        }
    }

    [[nodiscard]] bool operator<(const CutPreviewEdgeKey& other) const {
        if (cutPreviewPointKeyLess(a, other.a)) {
            return true;
        }
        if (cutPreviewPointKeyLess(other.a, a)) {
            return false;
        }
        return cutPreviewPointKeyLess(b, other.b);
    }
};

struct CutPreviewEdgeAccumulator {
    Vec3 a;
    Vec3 b;
    Vec3 normal;
    bool hasNormal = false;
    bool hasCapTriangle = false;
    bool hasSideTriangle = false;
    bool feature = false;
    int count = 0;

    void add(Vec3 first, Vec3 second, bool onPlane, bool capTriangle, Vec3 triangleNormal) {
        if (length(first - second) <= 0.000001f) {
            return;
        }
        if (count == 0) {
            a = first;
            b = second;
            normal = triangleNormal;
            hasNormal = true;
        } else if (hasNormal && std::fabs(dot(normal, triangleNormal)) < kCutPreviewFeatureEdgeCosine) {
            feature = true;
        }

        ++count;
        if (onPlane && capTriangle) {
            hasCapTriangle = true;
        }
        if (!capTriangle) {
            hasSideTriangle = true;
        }
    }

    [[nodiscard]] bool seam() const {
        return hasCapTriangle && hasSideTriangle;
    }

    [[nodiscard]] bool outline() const {
        return count == 1 || feature;
    }
};

float signedDistanceToPlane(Vec3 point, Vec3 planePoint, Vec3 planeNormal) {
    return dot(point - planePoint, planeNormal);
}

float previewMeshBoundsMagnitude(const PreviewTriangleMesh& mesh) {
    if (mesh.vertices.empty()) {
        return 0.0f;
    }

    Vec3 min = mesh.vertices.front();
    Vec3 max = mesh.vertices.front();
    for (const Vec3 vertex : mesh.vertices) {
        min.x = std::min(min.x, vertex.x);
        min.y = std::min(min.y, vertex.y);
        min.z = std::min(min.z, vertex.z);
        max.x = std::max(max.x, vertex.x);
        max.y = std::max(max.y, vertex.y);
        max.z = std::max(max.z, vertex.z);
    }
    return length(max - min);
}

void addCutPreviewEdge(
        std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator>& edges,
        Vec3 a,
        Vec3 b,
        bool onPlane,
        bool capTriangle,
        Vec3 triangleNormal) {
    if (length(a - b) <= 0.000001f) {
        return;
    }
    edges[CutPreviewEdgeKey(a, b)].add(a, b, onPlane, capTriangle, triangleNormal);
}

std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> collectPreviewEdges(
        const PreviewTriangleMesh& mesh,
        std::optional<Vec3> planePoint,
        Vec3 planeNormal) {
    std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges;
    if (mesh.empty()) {
        return edges;
    }

    const bool hasPlane = planePoint.has_value() && length(planeNormal) > 0.000001f;
    const Vec3 normalizedPlaneNormal = hasPlane ? normalize(planeNormal) : Vec3{};
    const float planeScale = std::max(1.0f, previewMeshBoundsMagnitude(mesh));
    const float planeEpsilon = std::max(kCutPreviewPlaneQuantization * 8.0f, planeScale * 0.00002f);

    for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
        const std::uint32_t ia = mesh.indices[index];
        const std::uint32_t ib = mesh.indices[index + 1U];
        const std::uint32_t ic = mesh.indices[index + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            continue;
        }

        const Vec3 a = mesh.vertices[ia];
        const Vec3 b = mesh.vertices[ib];
        const Vec3 c = mesh.vertices[ic];
        Vec3 triangleNormal = cross(b - a, c - a);
        if (length(triangleNormal) <= 0.000001f) {
            continue;
        }
        triangleNormal = normalize(triangleNormal);

        float da = 1.0f;
        float db = 1.0f;
        float dc = 1.0f;
        bool capTriangle = false;
        if (hasPlane) {
            da = signedDistanceToPlane(a, *planePoint, normalizedPlaneNormal);
            db = signedDistanceToPlane(b, *planePoint, normalizedPlaneNormal);
            dc = signedDistanceToPlane(c, *planePoint, normalizedPlaneNormal);
            capTriangle = std::fabs(da) <= planeEpsilon &&
                    std::fabs(db) <= planeEpsilon &&
                    std::fabs(dc) <= planeEpsilon;
        }

        addCutPreviewEdge(edges, a, b, hasPlane && std::fabs(da) <= planeEpsilon && std::fabs(db) <= planeEpsilon, capTriangle, triangleNormal);
        addCutPreviewEdge(edges, b, c, hasPlane && std::fabs(db) <= planeEpsilon && std::fabs(dc) <= planeEpsilon, capTriangle, triangleNormal);
        addCutPreviewEdge(edges, c, a, hasPlane && std::fabs(dc) <= planeEpsilon && std::fabs(da) <= planeEpsilon, capTriangle, triangleNormal);
    }
    return edges;
}

void appendPreviewLine(
        ViewportOverlayFrame& frame,
        ViewportOverlayLayer layer,
        Vec3 start,
        Vec3 end,
        std::uint32_t color,
        float widthPixels,
        ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::ToolPreviewLine) {
    if (length(start - end) <= 0.000001f) {
        return;
    }
    frame.lines.push_back({
            .layer = layer,
            .start = start,
            .end = end,
            .color = color,
            .widthPixels = widthPixels,
            .styleRole = styleRole,
            .source = ViewportOverlaySource::Tool,
    });
}

void appendCutPreviewResultLines(
        ViewportOverlayFrame& frame,
        const PreviewTriangleMesh& mesh,
        Vec3 planePoint,
        Vec3 planeNormal,
        std::uint32_t outlineColor) {
    const std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges =
            collectPreviewEdges(mesh, planePoint, planeNormal);
    for (const auto& [key, edge] : edges) {
        (void)key;
        if (edge.outline()) {
            appendPreviewLine(frame,
                    ViewportOverlayLayer::ToolPreviewLine,
                    edge.a,
                    edge.b,
                    outlineColor,
                    1.15f,
                    outlineColor == kOverlayCutPreviewKeepBothOutlineColor
                            ? ViewportOverlayStyleRole::CutPreviewKeepBothOutline
                            : ViewportOverlayStyleRole::CutPreviewDiscardOutline);
        }
        if (edge.seam()) {
            appendPreviewLine(frame,
                    ViewportOverlayLayer::ToolPreviewSeam,
                    edge.a,
                    edge.b,
                    kOverlayCutPreviewSeamColor,
                    2.25f,
                    ViewportOverlayStyleRole::CutPreviewSeam);
        }
    }
}

void appendPreviewFeatureLines(
        ViewportOverlayFrame& frame,
        const PreviewTriangleMesh& mesh,
        std::uint32_t color) {
    const std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges =
            collectPreviewEdges(mesh, std::nullopt, {});
    for (const auto& [key, edge] : edges) {
        (void)key;
        if (!edge.outline()) {
            continue;
        }
        appendPreviewLine(frame,
                ViewportOverlayLayer::ToolPreviewLine,
                edge.a,
                edge.b,
                color,
                1.0f);
    }
}

MeshAsset toMeshAsset(const quader_poly::CompiledMesh& compiled) {
    MeshAsset mesh;
    mesh.name = "Mesh";
    mesh.vertices.reserve(compiled.vertices.size());
    mesh.indices = compiled.indices;
    mesh.primitives.reserve(compiled.primitives.size());
    mesh.hasStableCpuTopology = true;

    for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
        mesh.vertices.push_back(MeshVertex{
                .position = fromSdk(vertex.position),
                .normal = fromSdk(vertex.normal),
                .uv = {vertex.uv0.x, vertex.uv0.y},
                .rgba = vertex.color,
        });
    }
    for (const quader_poly::CompiledPrimitive& primitive : compiled.primitives) {
        mesh.primitives.push_back(MeshPrimitiveSpan{
                .indexOffset = primitive.index_offset,
                .indexCount = primitive.index_count,
                .materialSlot = primitive.material_slot,
        });
    }

    return mesh;
}

MeshAsset toMeshAsset(const quader_poly::Document& document) {
    return toMeshAsset(quader_poly::compile_document(document));
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    return toMeshAsset(object.document);
}

std::optional<MeshAsset> boxPreviewMeshFromDescriptor(const quader_editor_core::BoxPreviewDescriptor& preview) {
    if (!preview.ok ||
            preview.drag_state != quader_editor_core::BoxDragState::height ||
            preview.bounds.empty) {
        return std::nullopt;
    }

    const std::vector<quader_poly::Vec3> corners = quader_editor_core::box_preview_corners(preview);
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

    MeshAsset mesh = toMeshAsset(objects.front());
    mesh.name = "Box Preview";
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return std::nullopt;
    }
    return mesh;
}

const quader_editor_core::PolygonObject* findObject(
        const quader_editor_core::EditorSession& session,
        int sdkObjectId) {
    const auto& objects = session.objects();
    const auto it = std::find_if(objects.begin(), objects.end(), [sdkObjectId](const auto& object) {
        return object.id == sdkObjectId;
    });
    return it == objects.end() ? nullptr : &(*it);
}

quader_editor_core::PolygonObject* findObject(
        quader_editor_core::EditorSession& session,
        int sdkObjectId) {
    auto& objects = session.objects();
    const auto it = std::find_if(objects.begin(), objects.end(), [sdkObjectId](const auto& object) {
        return object.id == sdkObjectId;
    });
    return it == objects.end() ? nullptr : &(*it);
}

bool sdkSelectionMatches(const quader_poly::Selection& left, const quader_poly::Selection& right) {
    return left.mode == right.mode &&
            left.vertices == right.vertices &&
            left.edges == right.edges &&
            left.faces == right.faces &&
            left.has_active == right.has_active &&
            left.active_kind == right.active_kind &&
            left.active_vertex == right.active_vertex &&
            left.active_edge == right.active_edge &&
            left.active_face == right.active_face;
}

constexpr std::uint64_t kPickGeometryHashSeed = 1469598103934665603ull;
constexpr std::uint64_t kPickGeometryHashPrime = 1099511628211ull;

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint64_t value) {
    return (hash ^ value) * kPickGeometryHashPrime;
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, int value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, std::uint32_t value) {
    return mixPickGeometryHash(hash, static_cast<std::uint64_t>(value));
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixPickGeometryHash(hash, bits);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, quader_poly::Vec3 value) {
    hash = mixPickGeometryHash(hash, value.x);
    hash = mixPickGeometryHash(hash, value.y);
    hash = mixPickGeometryHash(hash, value.z);
    return hash;
}

std::uint64_t objectPickFaceGeometrySignature(
        const quader_editor_core::PolygonObject& object,
        quader_poly::PickOptions pickOptions = {}) {
    (void)pickOptions;
    std::uint64_t hash = mixPickGeometryHash(kPickGeometryHashSeed, object.id);
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.vertices.size()));
    hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(object.document.faces.size()));
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        hash = mixPickGeometryHash(hash, vertex.id);
        hash = mixPickGeometryHash(hash, vertex.position);
    }
    for (const quader_poly::Face& face : object.document.faces) {
        hash = mixPickGeometryHash(hash, face.id);
        hash = mixPickGeometryHash(hash, face.material_slot);
        hash = mixPickGeometryHash(hash, static_cast<std::uint64_t>(face.vertices.size()));
        for (const quader_poly::ElementId vertexId : face.vertices) {
            hash = mixPickGeometryHash(hash, vertexId);
        }
    }
    return hash;
}

void includePickBoundsPoint(quader_editor_core::PickObjectBounds& bounds, quader_poly::Vec3 point) {
    if (bounds.empty) {
        bounds.min = point;
        bounds.max = point;
        bounds.empty = false;
        return;
    }
    bounds.min.x = std::min(bounds.min.x, point.x);
    bounds.min.y = std::min(bounds.min.y, point.y);
    bounds.min.z = std::min(bounds.min.z, point.z);
    bounds.max.x = std::max(bounds.max.x, point.x);
    bounds.max.y = std::max(bounds.max.y, point.y);
    bounds.max.z = std::max(bounds.max.z, point.z);
}

quader_editor_core::PickObjectBounds buildObjectPickBounds(const quader_editor_core::PolygonObject& object) {
    quader_editor_core::PickObjectBounds bounds;
    bounds.object_id = object.id;
    for (const quader_poly::Vertex& vertex : object.document.vertices) {
        includePickBoundsPoint(bounds, vertex.position);
    }
    return bounds;
}

void appendOverlayFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& source) {
    target.triangles.reserve(target.triangles.size() + source.triangles.size());
    target.lines.reserve(target.lines.size() + source.lines.size());
    target.points.reserve(target.points.size() + source.points.size());
    target.triangles.insert(target.triangles.end(), source.triangles.begin(), source.triangles.end());
    target.lines.insert(target.lines.end(), source.lines.begin(), source.lines.end());
    target.points.insert(target.points.end(), source.points.begin(), source.points.end());
    target.hover_target_selected = target.hover_target_selected || source.hover_target_selected;
}

bool edgesMatchUnordered(quader_poly::Edge left, quader_poly::Edge right) {
    return (left.a == right.a && left.b == right.b) || (left.a == right.b && left.b == right.a);
}

bool hoverFrameSuppressesTriangle(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayTriangle& triangle) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_visible_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_xray_face &&
            triangle.layer != quader_editor_core::SelectionOverlayLayer::selected_face_mask) {
        return false;
    }
    return std::any_of(
            hoverFrame.triangles.begin(),
            hoverFrame.triangles.end(),
            [&](const quader_editor_core::SelectionOverlayTriangle& hoverTriangle) {
                return hoverTriangle.layer == quader_editor_core::SelectionOverlayLayer::hover_face &&
                        hoverTriangle.object_id == triangle.object_id &&
                        hoverTriangle.face_id == triangle.face_id;
            });
}

bool hoverFrameSuppressesLine(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayLine& line) {
    if (!suppressBaseUnderHover) {
        return false;
    }
    if (line.layer != quader_editor_core::SelectionOverlayLayer::selected_face_edge &&
            line.layer != quader_editor_core::SelectionOverlayLayer::selected_edge) {
        return false;
    }
    return std::any_of(
            hoverFrame.lines.begin(),
            hoverFrame.lines.end(),
            [&](const quader_editor_core::SelectionOverlayLine& hoverLine) {
                if (hoverLine.object_id != line.object_id) {
                    return false;
                }
                if (hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_face_edge) {
                    return line.face_id == hoverLine.face_id && edgesMatchUnordered(line.edge, hoverLine.edge);
                }
                return hoverLine.layer == quader_editor_core::SelectionOverlayLayer::hover_edge &&
                        edgesMatchUnordered(line.edge, hoverLine.edge);
            });
}

bool hoverFrameSuppressesPoint(
        bool suppressBaseUnderHover,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayPoint& point) {
    if (!suppressBaseUnderHover || point.layer != quader_editor_core::SelectionOverlayLayer::selected_vertex) {
        return false;
    }
    return std::any_of(
            hoverFrame.points.begin(),
            hoverFrame.points.end(),
            [&](const quader_editor_core::SelectionOverlayPoint& hoverPoint) {
                return hoverPoint.layer == quader_editor_core::SelectionOverlayLayer::hover_vertex &&
                        hoverPoint.object_id == point.object_id &&
                        hoverPoint.vertex_id == point.vertex_id;
            });
}

void appendSelectedHoverFilteredBaseFrame(
        quader_editor_core::SelectionOverlayFrame& target,
        const quader_editor_core::SelectionOverlayFrame& base,
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        bool suppressBaseUnderHover) {
    target.triangles.reserve(target.triangles.size() + base.triangles.size());
    target.lines.reserve(target.lines.size() + base.lines.size());
    target.points.reserve(target.points.size() + base.points.size());
    for (const quader_editor_core::SelectionOverlayTriangle& triangle : base.triangles) {
        if (!hoverFrameSuppressesTriangle(suppressBaseUnderHover, hoverFrame, triangle)) {
            target.triangles.push_back(triangle);
        }
    }
    for (const quader_editor_core::SelectionOverlayLine& line : base.lines) {
        if (!hoverFrameSuppressesLine(suppressBaseUnderHover, hoverFrame, line)) {
            target.lines.push_back(line);
        }
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : base.points) {
        if (!hoverFrameSuppressesPoint(suppressBaseUnderHover, hoverFrame, point)) {
            target.points.push_back(point);
        }
    }
}

quader_poly::PickOptions pickOptionsFromRequest(const ModelingPickSelectRequest& request) {
    return quader_editor_core::selection_pick_options_from_radii(request.vertexRadius, request.edgeRadius);
}

quader_poly::PickOptions pickOptionsFromRequest(const ModelingHoverRequest& request) {
    return quader_editor_core::selection_pick_options_from_radii(request.vertexRadius, request.edgeRadius);
}

ModelingBoxPreview fromSdk(
        const quader_editor_core::BoxPreviewDescriptor& preview,
        const std::vector<quader_poly::Vec3>& corners = {}) {
    ModelingBoxPreview result;
    result.ok = preview.ok;
    result.valid = preview.ok && preview.footprint.footprint.size() >= 4U && !preview.bounds.empty;
    result.dragState = fromSdk(preview.drag_state);
    result.message = preview.message;
    result.normal = fromSdk(preview.normal);
    result.distance = preview.distance;
    result.boundsEmpty = preview.bounds.empty;
    result.bounds = {
            .min = fromSdk(preview.bounds.min),
            .max = fromSdk(preview.bounds.max),
    };

    result.footprintCount = std::min<std::size_t>(result.footprint.size(), preview.footprint.footprint.size());
    for (std::size_t index = 0; index < result.footprintCount; ++index) {
        result.footprint[index] = fromSdk(preview.footprint.footprint[index]);
    }

    result.cornerCount = std::min<std::size_t>(result.corners.size(), corners.size());
    for (std::size_t index = 0; index < result.cornerCount; ++index) {
        result.corners[index] = fromSdk(corners[index]);
    }
    return result;
}
#endif

ModelingCommandDescriptor makeBoxToolDescriptor() {
    return ModelingCommandDescriptor{
            .command = ModelingCommandId::ActivateBoxTool,
            .id = "box",
            .label = "Box",
            .shortcut = "B",
            .viewportScoped = true,
            .rebuildsMeshOnSuccess = true,
    };
}

ModelingCommandDescriptor makeCreateBoxDescriptor() {
    return ModelingCommandDescriptor{
            .command = ModelingCommandId::CreateBoxFromBounds,
            .id = "create_box_from_bounds",
            .label = "Create Box From Bounds",
            .shortcut = "B",
            .viewportScoped = true,
            .rebuildsMeshOnSuccess = true,
    };
}

#if QUADER_HAS_MODELING_SDK
quader_editor_core::EditorRuntimeCommand noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId commandId) {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = commandId;
    return command;
}

#endif

} // namespace

class ModelingSessionService::Impl {
public:
    Impl(SceneDocument& scene, AssetRegistry& assets)
        : scene_(scene), assets_(assets), sceneSyncService_(objectRegistry_) {}

    [[nodiscard]] bool sdkAvailable() const {
#if QUADER_HAS_MODELING_SDK
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] ModelingCommandDescriptor boxToolCommand() const {
        return makeBoxToolDescriptor();
    }

    [[nodiscard]] ModelingCommandDescriptor createBoxFromBoundsCommand() const {
        return makeCreateBoxDescriptor();
    }

    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const {
        return objectRegistry_.records(scene_);
    }

    [[nodiscard]] std::optional<EntityId> entityForSdkObject(int sdkObjectId) const {
        return objectRegistry_.entityForSdkObject(sdkObjectId);
    }

    [[nodiscard]] std::optional<int> sdkObjectForEntity(EntityId entity) const {
        return objectRegistry_.sdkObjectForEntity(entity);
    }

    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int sdkObjectId) const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolygonObject* object = findObject(session_, sdkObjectId);
        if (object == nullptr) {
            return std::nullopt;
        }
        return meshSyncService_.compileObjectMesh(*object);
#else
        (void)sdkObjectId;
        return std::nullopt;
#endif
    }

    [[nodiscard]] std::optional<Vec3> selectionCenter() const {
#if QUADER_HAS_MODELING_SDK
        if (session_.selection_mode() == quader_editor_core::SelectionMode::object) {
            if (const std::optional<SceneBounds> bounds = selectedWorldBounds(scene_, assets_)) {
                return boundsCenter(*bounds);
            }
        }

        std::optional<SceneBounds> bounds;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (!object.selected || object.selection.empty()) {
                continue;
            }
            const std::vector<quader_poly::ElementId> vertexIds =
                    quader_poly::selected_vertex_ids(object.document, object.selection);
            for (const quader_poly::ElementId vertexId : vertexIds) {
                const quader_poly::Vertex* vertex = quader_poly::find_vertex(object.document, vertexId);
                if (vertex == nullptr) {
                    continue;
                }
                const Vec3 world = transformObjectPoint(
                        object.id,
                        fromSdk(vertex->position),
                        objectRegistry_.entitiesByObject(),
                        scene_);
                if (!bounds) {
                    bounds = SceneBounds{.min = world, .max = world};
                } else {
                    includePoint(*bounds, world);
                }
            }
        }
        if (bounds) {
            return boundsCenter(*bounds);
        }
        const std::optional<quader_poly::Vec3> center = session_.selection_center();
        return center ? std::optional<Vec3>{fromSdk(*center)} : std::nullopt;
#else
        return std::nullopt;
#endif
    }

    [[nodiscard]] ModelingSelectionMode selectionMode() const {
#if QUADER_HAS_MODELING_SDK
        if (appSelectionMode_ == ModelingSelectionMode::Group) {
            return ModelingSelectionMode::Group;
        }
        return fromSdk(session_.selection_mode());
#else
        return ModelingSelectionMode::Object;
#endif
    }

    [[nodiscard]] ModelingSelectionSummary selectionSummary() const {
#if QUADER_HAS_MODELING_SDK
        if (appSelectionMode_ == ModelingSelectionMode::Group) {
            const std::vector<EntityId> selected = scene_.selectedEntities();
            const bool hasSelection = !selected.empty();
            return ModelingSelectionSummary{
                    .mode = ModelingSelectionMode::Group,
                    .hasSelection = hasSelection,
                    .activeKind = hasSelection ? ModelingActiveSelectionKind::Object : ModelingActiveSelectionKind::None,
                    .selectedObjectCount = static_cast<std::uint32_t>(selected.size()),
                    .contentRevision = session_.state().content_revision,
                    .selectionRevision = scene_.revision(),
            };
        }
        const quader_editor_core::ActiveSelection active = session_.active_selection();
        const std::vector<int> selectedObjects = session_.selected_object_ids();
        std::uint32_t selectedVertexCount = 0;
        std::uint32_t selectedEdgeCount = 0;
        std::uint32_t selectedFaceCount = 0;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (!object.selected) {
                continue;
            }
            selectedVertexCount += static_cast<std::uint32_t>(object.selection.vertices.size());
            selectedEdgeCount += static_cast<std::uint32_t>(object.selection.edges.size());
            selectedFaceCount += static_cast<std::uint32_t>(object.selection.faces.size());
        }

        return ModelingSelectionSummary{
                .mode = fromSdk(session_.selection_mode()),
                .hasSelection = session_.has_selection(),
                .activeKind = fromSdk(active.kind),
                .activeObjectId = active.object_id,
                .activeVertexId = static_cast<int>(active.vertex_id),
                .activeEdgeA = static_cast<int>(active.edge.a),
                .activeEdgeB = static_cast<int>(active.edge.b),
                .activeFaceId = static_cast<int>(active.face_id),
                .selectedObjectCount = static_cast<std::uint32_t>(selectedObjects.size()),
                .selectedVertexCount = selectedVertexCount,
                .selectedEdgeCount = selectedEdgeCount,
                .selectedFaceCount = selectedFaceCount,
                .contentRevision = session_.state().content_revision,
                .selectionRevision = session_.state().selection_revision,
        };
#else
        return {};
#endif
    }

    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::SelectionHoverState& hover = runtime_.hover();
        const std::uint64_t contentRevision = session_.state().content_revision;
        const std::uint64_t selectionRevision = session_.state().selection_revision;
        const std::uint64_t sceneRevision = scene_.revision();
        const quader_editor_core::SelectionMode selectionMode = session_.selection_mode();
        const std::uint64_t toolSignature = selectionOverlayToolSignature();
        if (overlayService_.selectionFrameMatches(
                    contentRevision,
                    selectionRevision,
                    sceneRevision,
                    selectionMode,
                    toolSignature,
                    hover)) {
            return overlayService_.selectionFrame();
        }

        ViewportOverlayFrame frame = fromSdk(overlayService_.selectionOverlayCurrentFrame(session_, runtime_, hover),
                objectRegistry_.entitiesByObject(),
                scene_);
        if (runtime_.transform_drag_active()) {
            appendWorldSpaceSdkOverlayPoints(frame,
                    quader_editor_core::transform_drag_snap_overlay_points(runtime_.transform_drag_state()));
        }
        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        if (quader_editor_core::cut_is_active(cut)) {
            const std::vector<quader_poly::Vec3> cutPoints =
                    quader_editor_core::cut_overlay_points(cut, runtime_.cut_hover().position, runtime_.cut_hover().valid);
            if (quader_editor_core::cut_can_preview(cut) && cut.preview_valid) {
                const std::vector<quader_editor_core::PolygonObject> previewObjects =
                        sceneTransformedObjectsForPicking();
                const PreviewTriangleMesh keepMesh = previewMeshFromDocuments(runtime_.cut_preview_documents(
                        previewObjects,
                        quader_editor_core::CutPreviewMeshSide::keep));
                const PreviewTriangleMesh discardMesh = previewMeshFromDocuments(runtime_.cut_preview_documents(
                        previewObjects,
                        quader_editor_core::CutPreviewMeshSide::discard));

                if (cut.mode == quader_editor_core::CutMode::keep_both) {
                    appendPreviewMeshFill(frame,
                            keepMesh,
                            kOverlayCutPreviewKeepFillColor,
                            ViewportOverlayStyleRole::CutPreviewKeepFill);
                    appendPreviewMeshFill(frame,
                            discardMesh,
                            kOverlayCutPreviewKeepBothDiscardFillColor,
                            ViewportOverlayStyleRole::CutPreviewKeepBothDiscardFill);
                } else {
                    appendPreviewMeshFill(frame,
                            discardMesh,
                            kOverlayCutPreviewDiscardFillColor,
                            ViewportOverlayStyleRole::CutPreviewDiscardFill);
                }

                const PreviewTriangleMesh& lineSource = !discardMesh.empty() ? discardMesh : keepMesh;
                if (!lineSource.empty() && cutPoints.size() >= 2U) {
                    const std::uint32_t outlineColor = cut.mode == quader_editor_core::CutMode::keep_both
                            ? kOverlayCutPreviewKeepBothOutlineColor
                            : kOverlayCutPreviewDiscardOutlineColor;
                    appendCutPreviewResultLines(frame,
                            lineSource,
                            fromSdk(cutPoints[0]),
                            fromSdk(cut.face_normal),
                            outlineColor);
                }
            }
            appendDrawnPlaneToolOverlay(
                    frame,
                    cutPoints,
                    drawnPlaneSupportPoint(cutPoints, cut.face_normal),
                    cutPoints.size() >= 2U);
        }
        const quader_editor_core::MirrorToolState& mirror = runtime_.mirror_state();
        if (quader_editor_core::mirror_is_active(mirror)) {
            const std::vector<quader_poly::Vec3> mirrorPoints =
                    quader_editor_core::mirror_overlay_points(
                            mirror,
                            runtime_.mirror_hover().position,
                            runtime_.mirror_hover().valid);
            appendDrawnPlaneToolOverlay(
                    frame,
                    mirrorPoints,
                    drawnPlaneSupportPoint(mirrorPoints, mirror.plane.face_normal),
                    mirrorPoints.size() >= 2U);
        }
        overlayService_.storeSelectionFrame(
                std::move(frame),
                contentRevision,
                selectionRevision,
                sceneRevision,
                selectionMode,
                toolSignature,
                hover);
        return overlayService_.selectionFrame();
#else
        return {};
#endif
    }

    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const {
#if QUADER_HAS_MODELING_SDK
        std::vector<ModelingToolPreviewMesh> previewMeshes;
        if (const std::optional<MeshAsset> boxPreview =
                        boxPreviewMeshFromDescriptor(runtime_.box_preview_descriptor(true))) {
            previewMeshes.push_back(ModelingToolPreviewMesh{
                    .sourceEntity = InvalidEntityId,
                    .material = InvalidAssetId,
                    .mesh = *boxPreview,
            });
        }

        const quader_editor_core::MirrorToolState& mirror = runtime_.mirror_state();
        if (!quader_editor_core::mirror_is_active(mirror) ||
                !quader_editor_core::mirror_can_preview(mirror) ||
                !mirror.preview_valid) {
            return previewMeshes;
        }

        const std::vector<quader_editor_core::PolygonObject> previewObjects =
                sceneTransformedObjectsForPicking();
        const std::vector<quader_poly::Document> documents =
                runtime_.mirror_preview_documents(previewObjects);
        previewMeshes.reserve(previewMeshes.size() + documents.size());
        std::size_t documentIndex = 0;
        for (const int objectId : mirror.target_object_ids) {
            if (documentIndex >= documents.size()) {
                break;
            }
            MeshAsset mesh = toMeshAsset(documents[documentIndex++]);
            if (mesh.vertices.empty() || mesh.indices.empty()) {
                continue;
            }

            EntityId sourceEntity = InvalidEntityId;
            AssetId material = InvalidAssetId;
            if (const auto entity = objectRegistry_.entitiesByObject().find(objectId);
                    entity != objectRegistry_.entitiesByObject().end()) {
                sourceEntity = entity->second;
                if (const MaterialComponent* materialComponent = scene_.material(sourceEntity)) {
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
#else
        return {};
#endif
    }

    [[nodiscard]] bool canUndo() const {
#if QUADER_HAS_MODELING_SDK
        return session_.history_status().can_undo;
#else
        return false;
#endif
    }

    [[nodiscard]] bool canRedo() const {
#if QUADER_HAS_MODELING_SDK
        return session_.history_status().can_redo;
#else
        return false;
#endif
    }

    void syncObjectSelectionFromScene(bool forceObjectMode = false) {
#if QUADER_HAS_MODELING_SDK
        if (!forceObjectMode && session_.selection_mode() != quader_editor_core::SelectionMode::object) {
            return;
        }
        if (!sceneObjectsSyncedForCurrentSdkContent()) {
            syncSceneObjectsFromSdk();
        }

        std::vector<int> objectIds;
        objectIds.reserve(scene_.selectedEntities().size());
        for (const EntityId entity : scene_.selectedEntities()) {
            const auto it = objectRegistry_.objectsByEntity().find(entity);
            if (it != objectRegistry_.objectsByEntity().end()) {
                objectIds.push_back(it->second);
            }
        }

        int activeObjectId = -1;
        const auto primary = objectRegistry_.objectsByEntity().find(scene_.primarySelection());
        if (primary != objectRegistry_.objectsByEntity().end()) {
            activeObjectId = primary->second;
        }

        std::vector<int> currentObjectIds = session_.selected_object_ids();
        std::sort(currentObjectIds.begin(), currentObjectIds.end());
        std::vector<int> desiredObjectIds = objectIds;
        std::sort(desiredObjectIds.begin(), desiredObjectIds.end());
        const bool alreadyObjectMode = session_.selection_mode() == quader_editor_core::SelectionMode::object;
        if ((!forceObjectMode || alreadyObjectMode) &&
                currentObjectIds == desiredObjectIds &&
                session_.active_object_id() == activeObjectId) {
            return;
        }

        session_.set_selected_object_ids(objectIds, activeObjectId);
#endif
    }

    void promoteDrawnPlaneToolObjectSelection() {
#if QUADER_HAS_MODELING_SDK
        if (!sceneObjectsSyncedForCurrentSdkContent()) {
            syncSceneObjectsFromSdk();
        }

        std::vector<int> sceneObjectIds;
        sceneObjectIds.reserve(scene_.selectedEntities().size());
        int activeObjectId = -1;
        for (const EntityId entity : scene_.selectedEntities()) {
            const auto it = objectRegistry_.objectsByEntity().find(entity);
            if (it == objectRegistry_.objectsByEntity().end()) {
                continue;
            }
            sceneObjectIds.push_back(it->second);
            if (entity == scene_.primarySelection()) {
                activeObjectId = it->second;
            }
        }

        if (!sceneObjectIds.empty()) {
            if (activeObjectId <= 0) {
                activeObjectId = sceneObjectIds.back();
            }
            session_.set_selected_object_ids(sceneObjectIds, activeObjectId);
            syncSceneSelectionFromSdk();
            invalidateSelectionOverlayCaches();
            return;
        }

        std::vector<int> sdkObjectIds = session_.selected_object_ids();
        if (sdkObjectIds.empty()) {
            return;
        }
        activeObjectId = session_.active_object_id() > 0 ? session_.active_object_id() : sdkObjectIds.back();
        session_.set_selected_object_ids(sdkObjectIds, activeObjectId);
        syncSceneSelectionFromSdk();
        invalidateSelectionOverlayCaches();
#endif
    }

    void syncSceneSelectionFromSdk() {
#if QUADER_HAS_MODELING_SDK
        if (!sceneObjectsSyncedForCurrentSdkContent()) {
            syncSceneObjectsFromSdk();
        }
        std::vector<int> selectedObjectIds = session_.selected_object_ids();
        const int activeObjectId = session_.active_object_id();
        std::unordered_set<EntityId> desiredSelection;
        desiredSelection.reserve(selectedObjectIds.size());
        for (const int objectId : selectedObjectIds) {
            const auto it = objectRegistry_.entitiesByObject().find(objectId);
            if (it != objectRegistry_.entitiesByObject().end()) {
                desiredSelection.insert(it->second);
            }
        }

        for (const EntityId entity : scene_.selectedEntities()) {
            if (!desiredSelection.contains(entity)) {
                scene_.setSelected(entity, false);
            }
        }
        for (const EntityId entity : desiredSelection) {
            const SelectionComponent* selection = scene_.selection(entity);
            if (selection == nullptr || !selection->selected) {
                scene_.setSelected(entity, true);
            }
        }
        const auto active = objectRegistry_.entitiesByObject().find(activeObjectId);
        if (active != objectRegistry_.entitiesByObject().end()) {
            const SelectionComponent* selection = scene_.selection(active->second);
            if (selection && selection->selected) {
                scene_.setActiveSelection(active->second);
            }
        }
#endif
    }

    void restoreSceneSelection(const std::vector<EntityId>& selected, EntityId active) {
        std::unordered_set<EntityId> desired;
        desired.reserve(selected.size());
        for (const EntityId entity : selected) {
            if (scene_.exists(entity)) {
                desired.insert(entity);
            }
        }

        for (const EntityId entity : scene_.selectedEntities()) {
            if (!desired.contains(entity)) {
                scene_.setSelected(entity, false);
            }
        }
        for (const EntityId entity : selected) {
            if (scene_.exists(entity)) {
                scene_.setSelected(entity, true);
            }
        }

        if (active != InvalidEntityId && desired.contains(active)) {
            scene_.setActiveSelection(active);
            return;
        }
        for (const EntityId entity : selected) {
            if (scene_.exists(entity)) {
                scene_.setActiveSelection(entity);
                return;
            }
        }
    }

    ModelingCommandResult duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands) {
        const std::vector<EntityId> selected = scene_.selectedEntities();
        std::unordered_set<EntityId> selectedSet(selected.begin(), selected.end());
        std::vector<EntityId> roots;
        for (const EntityId entity : scene_.entityTreeOrder()) {
            if (!selectedSet.contains(entity)) {
                continue;
            }
            bool ancestorSelected = false;
            EntityId parent = scene_.parent(entity);
            while (parent != InvalidEntityId) {
                if (selectedSet.contains(parent)) {
                    ancestorSelected = true;
                    break;
                }
                parent = scene_.parent(parent);
            }
            if (!ancestorSelected) {
                roots.push_back(entity);
            }
        }
        if (roots.empty()) {
            return {.ok = false, .changed = false, .message = "Select a group to duplicate."};
        }

        std::vector<EntityId> sourceMeshes;
        for (const EntityId root : roots) {
            for (const EntityId entity : scene_.descendants(root, true)) {
                if (scene_.modelingObject(entity) != nullptr) {
                    sourceMeshes.push_back(entity);
                }
            }
        }

        std::vector<EntityId> duplicateMeshes;
        auto restoreGroupSelection = [&]() {
            (void)documentCommands.selectEntities(roots, roots.front());
            (void)setSelectionMode(ModelingSelectionMode::Group);
        };
        if (!sourceMeshes.empty()) {
            const std::vector<EntityId> beforeEntities = scene_.entities();
            std::unordered_set<EntityId> beforeSet(beforeEntities.begin(), beforeEntities.end());
            (void)documentCommands.selectEntities(sourceMeshes);
            const ModelingCommandResult objectMode = setSelectionMode(ModelingSelectionMode::Object);
            if (!objectMode.ok) {
                restoreGroupSelection();
                return objectMode;
            }
            syncObjectSelectionFromScene();
            const ModelingCommandResult duplicate = executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
            if (!duplicate.ok) {
                restoreGroupSelection();
                return duplicate;
            }

            for (const EntityId entity : scene_.entities()) {
                if (!beforeSet.contains(entity) && scene_.modelingObject(entity) != nullptr) {
                    duplicateMeshes.push_back(entity);
                }
            }
            std::sort(duplicateMeshes.begin(), duplicateMeshes.end());
            if (duplicateMeshes.size() != sourceMeshes.size()) {
                restoreGroupSelection();
                return {
                        .ok = false,
                        .changed = duplicate.changed,
                        .message = "Group duplicate did not create matching mesh clones.",
                };
            }
        }

        std::unordered_map<EntityId, EntityId> cloneBySource;
        for (std::size_t index = 0; index < sourceMeshes.size() && index < duplicateMeshes.size(); ++index) {
            cloneBySource[sourceMeshes[index]] = duplicateMeshes[index];
        }

        auto copyCommonEntityState = [&](EntityId source, EntityId clone, bool renameClone = true) {
            if (renameClone) {
                if (const NameComponent* name = scene_.name(source)) {
                    (void)documentCommands.renameEntity(clone, SceneEntityNaming::nextName(scene_, name->name));
                }
            }
            if (const TransformComponent* sourceTransform = scene_.transform(source)) {
                (void)documentCommands.applyEntityTransform(clone, *sourceTransform);
            }
            if (const VisibilityComponent* visibility = scene_.visibility(source)) {
                (void)documentCommands.setVisible(clone, visibility->visible);
            }
            if (const LockComponent* lock = scene_.lock(source)) {
                (void)documentCommands.setLocked(clone, lock->locked);
            }
        };

        std::function<EntityId(EntityId)> cloneEntity = [&](EntityId source) -> EntityId {
            EntityId clone = InvalidEntityId;
            if (scene_.modelingObject(source) != nullptr) {
                const auto mapped = cloneBySource.find(source);
                if (mapped == cloneBySource.end()) {
                    return InvalidEntityId;
                }
                clone = mapped->second;
                copyCommonEntityState(source, clone);
            } else {
                const NameComponent* name = scene_.name(source);
                const std::string baseName = name ? name->name : "Entity";
                TransformComponent transform;
                if (const TransformComponent* sourceTransform = scene_.transform(source)) {
                    transform = *sourceTransform;
                }
                const DocumentCommandResult created = documentCommands.createEntity(
                        SceneEntityNaming::nextName(scene_, baseName),
                        scene_.kind(source),
                        InvalidEntityId,
                        transform,
                        &clone);
                if (!created.ok || !created.changed) {
                    return InvalidEntityId;
                }
                copyCommonEntityState(source, clone, false);
            }

            for (const EntityId child : scene_.children(source)) {
                const EntityId childClone = cloneEntity(child);
                if (childClone != InvalidEntityId) {
                    (void)documentCommands.reparentEntity(childClone, clone, false);
                }
            }
            return clone;
        };

        std::vector<EntityId> rootClones;
        for (const EntityId root : roots) {
            const EntityId clone = cloneEntity(root);
            if (clone != InvalidEntityId) {
                (void)documentCommands.reparentEntity(clone, scene_.parent(root), false);
                rootClones.push_back(clone);
            }
        }

        if (rootClones.empty()) {
            restoreGroupSelection();
            return {.ok = false, .changed = false, .message = "Group duplicate failed."};
        }

        (void)documentCommands.selectEntities(rootClones, rootClones.front());
        (void)setSelectionMode(ModelingSelectionMode::Group);
        return {.ok = true, .changed = true, .message = "Group duplicated."};
    }

    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode) {
#if QUADER_HAS_MODELING_SDK
        if (mode == ModelingSelectionMode::Group) {
            const bool changed = appSelectionMode_ != ModelingSelectionMode::Group ||
                    session_.selection_mode() != quader_editor_core::SelectionMode::object;
            appSelectionMode_ = ModelingSelectionMode::Group;
            if (session_.selection_mode() != quader_editor_core::SelectionMode::object) {
                session_.set_selection_mode(quader_editor_core::SelectionMode::object);
                invalidateSelectionOverlayCaches();
            }
            return {
                    .ok = true,
                    .changed = changed,
                    .message = changed ? "Selection mode set to Group." : "Selection mode already Group.",
            };
        }

        appSelectionMode_ = mode;
        std::vector<EntityId> sceneOnlySelection;
        sceneOnlySelection.reserve(scene_.selectedEntities().size());
        for (const EntityId entity : scene_.selectedEntities()) {
            if (scene_.modelingObject(entity) == nullptr) {
                sceneOnlySelection.push_back(entity);
            }
        }
        const EntityId activeSceneOnlySelection =
                scene_.modelingObject(scene_.primarySelection()) == nullptr ? scene_.primarySelection() : InvalidEntityId;
        if (!sceneOnlySelection.empty()) {
            syncObjectSelectionFromScene();
        }

        quader_editor_core::SetSelectionModeRuntimeCommandSettings settings;
        settings.selection_mode = static_cast<int>(toSdk(mode));

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_selection_mode;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        invalidateSelectionOverlayCaches();
        if (mode == ModelingSelectionMode::Object && !sceneOnlySelection.empty()) {
            syncObjectSelectionFromScene();
        }
        syncSceneSelectionFromSdk();
        for (const EntityId entity : sceneOnlySelection) {
            if (scene_.exists(entity)) {
                scene_.setSelected(entity, true);
            }
        }
        if (activeSceneOnlySelection != InvalidEntityId && scene_.exists(activeSceneOnlySelection)) {
            scene_.setActiveSelection(activeSceneOnlySelection);
        }
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)mode;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult selectAll() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::select_all, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult invertSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::invert_selection, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult clearSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::clear_selection, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult deleteSelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::delete_selection, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult copySelection() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(clipboardService_.copyCommand(), false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult pasteSelection() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = clipboardService_.pasteCommand();
        command.settings = quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings {};
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult undo() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(historyBridge_.undoCommand(), true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult redo() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(historyBridge_.redoCommand(), true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult repeatLastAction() {
#if QUADER_HAS_MODELING_SDK
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = historyBridge_.executeRepeatLastAction(session_);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult mirrorSelection(ModelingMirrorAxis axis) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorAxisRuntimeCommandSettings settings;
        settings.axis = toSdk(axis);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::mirror_axis;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)axis;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult flipSelection(ModelingFlipOrientation orientation) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::ViewFlipRuntimeCommandSettings settings;
        settings.orientation = toSdk(orientation);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::flip_view;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)orientation;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult applyActiveMaterial(std::uint32_t materialSlot) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::AssignMaterialOperationSettings settings;
        settings.material_slot = materialSlot;

        quader_editor_core::EditorOperationCommand command;
        command.operation_id = quader_editor_core::EditorOperationId::assign_material;
        command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                quader_editor_core::execute_command(session_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        historyBridge_.recordOperation(command, result.command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        (void)materialSlot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    [[nodiscard]] ModelingToolStatus toolStatus() const {
        ModelingToolStatus status;
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::EditorRuntimeStatus sdkStatus = runtime_.status();
        status.activeTool = fromSdkViewportTool(sdkStatus.active_tool);
        status.modalToolOwnsViewport = modalToolOwnsViewport();
        status.insertEdgeLoopActive = sdkStatus.insert_edge_loop_active;
        status.insertEdgeLoopDragging = sdkStatus.insert_edge_loop_dragging;
        status.insertEdgeLoopHasPreview = sdkStatus.insert_edge_loop_has_preview;
        status.knifeActive = sdkStatus.knife_active;
        status.knifeHasCurrent = sdkStatus.knife_has_current;
        status.knifeCanCommit = sdkStatus.knife_can_commit;
        status.cutActive = sdkStatus.cut_active;
        status.cutCanPreview = sdkStatus.cut_can_preview;
        status.cutPreviewValid = sdkStatus.cut_preview_valid;
        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        const std::vector<quader_poly::Vec3> cutOverlayPoints =
                quader_editor_core::cut_overlay_points(cut, runtime_.cut_hover().position, runtime_.cut_hover().valid);
        status.cutDragging = quader_editor_core::cut_is_dragging(cut);
        status.cutDraggingNewPoint = cut.dragging_new_point;
        status.cutDraggingLine = cut.dragging_line;
        status.cutDraggingPointIndex = cut.dragging_point_index;
        status.cutPointCount = static_cast<int>(std::min<std::size_t>(
                cutOverlayPoints.size(),
                status.cutPoints.size()));
        status.cutCapturedPointCount = static_cast<int>(std::min<std::size_t>(
                cut.points.size(),
                status.cutPoints.size()));
        status.cutHasLine = cutOverlayPoints.size() >= 2U;
        for (int index = 0; index < status.cutPointCount; ++index) {
            status.cutPoints[static_cast<std::size_t>(index)] =
                    fromSdk(cutOverlayPoints[static_cast<std::size_t>(index)]);
        }
        status.cutMode = fromSdk(cut.mode);
        status.mirrorActive = sdkStatus.mirror_active;
        status.mirrorCanPreview = sdkStatus.mirror_can_preview;
        status.mirrorPreviewValid = sdkStatus.mirror_preview_valid;
        const quader_editor_core::MirrorToolState& mirror = runtime_.mirror_state();
        const std::vector<quader_poly::Vec3> mirrorOverlayPoints =
                quader_editor_core::mirror_overlay_points(
                        mirror,
                        runtime_.mirror_hover().position,
                        runtime_.mirror_hover().valid);
        status.mirrorDragging = quader_editor_core::mirror_is_dragging(mirror);
        status.mirrorDraggingNewPoint = mirror.plane.dragging_new_point;
        status.mirrorDraggingLine = mirror.plane.dragging_line;
        status.mirrorDraggingPointIndex = mirror.plane.dragging_point_index;
        status.mirrorPointCount = static_cast<int>(std::min<std::size_t>(
                mirrorOverlayPoints.size(),
                status.mirrorPoints.size()));
        status.mirrorCapturedPointCount = static_cast<int>(std::min<std::size_t>(
                mirror.plane.points.size(),
                status.mirrorPoints.size()));
        status.mirrorHasLine = mirrorOverlayPoints.size() >= 2U;
        for (int index = 0; index < status.mirrorPointCount; ++index) {
            status.mirrorPoints[static_cast<std::size_t>(index)] =
                    fromSdk(mirrorOverlayPoints[static_cast<std::size_t>(index)]);
        }
        status.polyActive = sdkStatus.poly_active;
        status.polyClosed = sdkStatus.poly_closed;
        status.polyHasCurrent = sdkStatus.poly_has_current;
        status.polyCanCommit = sdkStatus.poly_can_commit;
        status.polyPointCount = sdkStatus.poly_point_count;
#endif
        return status;
    }

    [[nodiscard]] ModelingToolSettingsTarget toolSettingsTarget() const {
        ModelingToolSettingsTarget target;
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PreviewTransactionStatus preview = session_.preview_transaction_status();
        const ModelingToolSettingsTargetKind previewKind = preview.active ?
                toolSettingsTargetKindForPreview(preview.kind) :
                ModelingToolSettingsTargetKind::None;
        if (previewKind != ModelingToolSettingsTargetKind::None) {
            target.kind = previewKind;
            target.active = true;
            target.operationPreviewActive = true;
            std::string fallbackOperationId;
            if (const std::optional<quader_editor_core::EditorOperationId> operation =
                        operationForPreviewKind(preview.kind)) {
                if (const quader_editor_core::EditorOperation* schema =
                            quader_editor_core::editor_operation_for_id(*operation)) {
                    fallbackOperationId = std::string(schema->id());
                }
            }
            return toolSettingsService_.operationPreviewTarget(
                    previewKind,
                    fallbackToolSettingsLabel(previewKind),
                    fallbackOperationId);
        }

        const quader_editor_core::EditorRuntimeStatus sdkStatus = runtime_.status();
        if (sdkStatus.cut_active) {
            target.kind = ModelingToolSettingsTargetKind::Cut;
            target.active = true;
            target.label = fallbackToolSettingsLabel(ModelingToolSettingsTargetKind::Cut);
            return target;
        }
#endif
        return target;
    }

    [[nodiscard]] ModelingInteractionSettings interactionSettings() const {
        return interactionSettingsService_.settings();
    }

    ModelingCommandResult setInteractionSettings(const ModelingInteractionSettings& settings) {
        const ModelingToolSettingsTarget activeTarget = toolSettingsTarget();
        const ModelingInteractionSettings previous = interactionSettingsService_.settings();
        const ModelingInteractionSettings normalized = interactionSettingsService_.normalize(settings);
        const bool cutModeChanged = normalized.cutMode != previous.cutMode;
        const bool changed = interactionSettingsService_.setNormalized(normalized);
        if (cutModeChanged) {
            return setCutMode(normalized.cutMode);
        }
        if (changed && activeTarget.operationPreviewActive) {
#if QUADER_HAS_MODELING_SDK
            ModelingCommandResult result = updateActiveOperationPreviewSettings();
            result.changed = result.changed || changed;
            return result;
#else
            return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
        }
        return {.ok = true, .changed = changed};
    }

    [[nodiscard]] bool modalToolOwnsViewport() const {
#if QUADER_HAS_MODELING_SDK
        return modalToolRuntime_.ownsViewport(fromSdkViewportTool(runtime_.active_tool()));
#endif
        return false;
    }

    [[nodiscard]] ModelingActionQuery effectiveActionQuery(ModelingActionQuery query) const {
        return contextActionService_.effectiveActionQuery(query, modalToolOwnsViewport());
    }

    [[nodiscard]] ModelingActionSet contextActionSet(ModelingActionQuery query) const {
        return contextActionService_.actionSet(selectionSummary(), query, modalToolOwnsViewport());
    }

    [[nodiscard]] std::vector<ModelingActionItem> contextActions(ModelingActionQuery query) const {
        return contextActionService_.actions(selectionSummary(), query, modalToolOwnsViewport());
    }

    void resetSettingsForPreviewAction(ModelingActionPreviewKind previewKind) {
        interactionSettingsService_.resetForPreviewAction(previewKind);
    }

#if QUADER_HAS_MODELING_SDK
    void clearInactiveOperationPreviewMetadata() {
        if (!session_.preview_transaction_status().active) {
            toolSettingsService_.clearPreviewMetadata();
        }
    }

    ModelingCommandResult executeSdkOperationCommand(
            quader_editor_core::EditorOperationId sdkOperation,
            quader_editor_core::EditorOperationCommandPhase phase,
            quader_editor_core::EditorOperationSettings settings) {
        SdkOperationExecutor::OperationCommand operationCommand;
        operationCommand.operation = sdkOperation;
        operationCommand.phase = phase;
        operationCommand.settings = std::move(settings);
        const quader_editor_core::EditorOperationCommand command =
                SdkOperationExecutor::makeCommand(operationCommand);

        const quader_editor_core::PreviewTransactionStatus previewBefore = session_.preview_transaction_status();
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                sdkOperationExecutor_.execute(session_, operationCommand);

        historyBridge_.recordOperation(command, result.command);

        const ModelingCommandExecutor::SyncPlan syncPlan = commandExecutor_.operationSyncPlan(
                phase,
                previewBefore,
                result,
                contentBefore,
                session_.state().content_revision);
        if (syncPlan.invalidateOverlay) {
            invalidateSelectionOverlayCaches();
        }
        if (syncPlan.syncMeshes) {
            syncSceneObjectsFromSdk(syncPlan.forceMeshSync);
        }
        if (syncPlan.syncSelection) {
            syncSceneSelectionFromSdk();
        }
        clearInactiveOperationPreviewMetadata();
        return commandExecutor_.fromSdk(result.command);
    }

    ModelingCommandResult beginOperationPreview(
            const ModelingActionItem& action,
            const quader_editor_core::EditorOperation& operation) {
        if (!operation.supports_preview()) {
            return {.ok = false, .message = "Action does not support a live settings preview."};
        }

        resetSettingsForPreviewAction(action.previewKind);
        ModelingCommandResult result = executeSdkOperationCommand(
                operation.operation_id(),
                quader_editor_core::EditorOperationCommandPhase::begin_preview,
                settingsForOperation(operation.operation_id(), interactionSettingsService_.settings()));
        if (result.ok) {
            toolSettingsService_.setPreviewMetadata(
                    action.actionId,
                    action.operationId,
                    action.label.empty()
                            ? fallbackToolSettingsLabel(toolSettingsTargetKindForPreview(session_.preview_transaction_status().kind))
                            : action.label);
        }
        return result;
    }

    ModelingCommandResult updateActiveOperationPreviewSettings() {
        const quader_editor_core::PreviewTransactionStatus preview = session_.preview_transaction_status();
        if (!preview.active) {
            return {.ok = false, .message = "No active operation preview."};
        }
        const std::optional<quader_editor_core::EditorOperationId> operation = operationForPreviewKind(preview.kind);
        if (!operation.has_value()) {
            return {.ok = false, .message = "Active preview does not have editable operation settings."};
        }
        return executeSdkOperationCommand(
                *operation,
                quader_editor_core::EditorOperationCommandPhase::update_preview,
                settingsForOperation(*operation, interactionSettingsService_.settings()));
    }

    ModelingCommandResult finishActiveOperationPreview(bool commit) {
        const quader_editor_core::PreviewTransactionStatus preview = session_.preview_transaction_status();
        if (!preview.active) {
            return {.ok = false, .message = "No active operation preview."};
        }
        const std::optional<quader_editor_core::EditorOperationId> operation = operationForPreviewKind(preview.kind);
        if (!operation.has_value()) {
            return {.ok = false, .message = "Active preview does not belong to a settings operation."};
        }
        return executeSdkOperationCommand(
                *operation,
                commit ? quader_editor_core::EditorOperationCommandPhase::commit_preview :
                         quader_editor_core::EditorOperationCommandPhase::cancel_preview,
                settingsForOperation(*operation, interactionSettingsService_.settings()));
    }
#endif

    ModelingCommandResult executeContextAction(std::string_view actionId) {
#if QUADER_HAS_MODELING_SDK
        if (modalToolOwnsViewport()) {
            return {.ok = false, .message = "Viewport is owned by an active modal tool."};
        }

        const ModelingSelectionSummary summary = selectionSummary();
        ModelingActionQuery query;
        query.surface = ModelingActionSurface::ViewportContextMenu;
        query.modalToolOwnsViewport = false;
        query.includeDisabled = true;
        query.includeHidden = false;

        const std::vector<ModelingActionItem> actions = ModelingActionCatalog::actionsFor(summary, query);
        const auto found = std::find_if(actions.begin(), actions.end(), [&](const ModelingActionItem& action) {
            return action.actionId == actionId;
        });
        if (found == actions.end()) {
            return {.ok = false, .message = "Action is not available in the current mode."};
        }
        if (found->visibility != ModelingActionVisibility::Enabled) {
            return {
                    .ok = false,
                    .message = found->disabledReason.empty() ? "Action is disabled for the current selection." : found->disabledReason,
            };
        }
        if (found->actionId == "delete_selection") {
            return deleteSelection();
        }
        if (found->kind != ModelingActionKind::Operation || found->operationId.empty()) {
            return {.ok = false, .message = "Action is not an executable modeling operation."};
        }

        const quader_editor_core::EditorOperation* operation =
                quader_editor_core::editor_operation_for_id(found->operationId);
        if (operation == nullptr) {
            return {.ok = false, .message = "Action references an unknown modeling operation."};
        }

        if (found->previewKind != ModelingActionPreviewKind::None) {
            return beginOperationPreview(*found, *operation);
        }

        return executeSdkOperation(
                operation->operation_id(),
                settingsForOperation(operation->operation_id(), interactionSettingsService_.settings()));
#else
        (void)actionId;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitActiveOperationPreview() {
#if QUADER_HAS_MODELING_SDK
        return finishActiveOperationPreview(true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelActiveOperationPreview() {
#if QUADER_HAS_MODELING_SDK
        return finishActiveOperationPreview(false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const {
        const ModelingSelectionSummary summary = selectionSummary();
        const bool hasSelection = summary.hasSelection;
        const bool objectMode = summary.mode == ModelingSelectionMode::Object;
        const bool vertexMode = summary.mode == ModelingSelectionMode::Vertex;
        const bool edgeMode = summary.mode == ModelingSelectionMode::Edge;
        const bool faceMode = summary.mode == ModelingSelectionMode::Face;
        const bool hasVertices = summary.selectedVertexCount > 0;
        const bool hasEdges = summary.selectedEdgeCount > 0;
        const bool hasFaces = summary.selectedFaceCount > 0;

        return {
                {ModelingInstantOperationId::DeleteSelection, "delete_selection", "Delete", hasSelection},
                {ModelingInstantOperationId::DuplicateMeshes, "duplicate_meshes", "Duplicate", objectMode && summary.selectedObjectCount > 0},
                {ModelingInstantOperationId::CombineMeshes, "combine_meshes", "Combine Meshes", objectMode && summary.selectedObjectCount > 1},
                {ModelingInstantOperationId::ExtractFaces, "extract_faces", "Extract Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::DetachFaces, "detach_faces", "Detach Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::Bridge, "bridge", "Bridge", (edgeMode && hasEdges) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::SnapVerticesToActive, "snap_vertices_to_active", "Snap to Vertex", vertexMode && summary.selectedVertexCount > 1 && summary.activeKind == ModelingActiveSelectionKind::Vertex},
                {ModelingInstantOperationId::ConnectVertices, "connect_vertices", "Connect Vertices", vertexMode && hasVertices},
                {ModelingInstantOperationId::DissolveVertices, "dissolve_vertices", "Dissolve Vertices", vertexMode && hasVertices},
                {ModelingInstantOperationId::BevelVertices, "bevel_vertices", "Bevel Vertex", vertexMode && hasVertices},
                {ModelingInstantOperationId::MergeVerticesToActive, "merge_vertices_to_active", "Merge Vertices To Active", vertexMode && summary.selectedVertexCount > 1},
                {ModelingInstantOperationId::MergeVerticesToCenter, "merge_vertices_to_center", "Merge Vertices To Center", vertexMode && summary.selectedVertexCount > 1},
                {ModelingInstantOperationId::SnapEdgesToActive, "snap_edges_to_active", "Snap to Edge", edgeMode && summary.selectedEdgeCount > 1 && summary.activeKind == ModelingActiveSelectionKind::Edge},
                {ModelingInstantOperationId::ConnectEdges, "connect_edges", "Connect Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::SplitEdges, "split_edges", "Split Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::DissolveEdges, "dissolve_edges", "Dissolve Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::MergeEdges, "merge_edges", "Merge Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::CollapseEdges, "collapse_edges", "Collapse Edges", edgeMode && hasEdges},
                {ModelingInstantOperationId::FillHole, "fill_hole", "Fill Hole", edgeMode && hasEdges},
                {ModelingInstantOperationId::CombineFaces, "combine_faces", "Combine Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::CollapseFaces, "collapse_faces", "Collapse Faces", faceMode && hasFaces},
                {ModelingInstantOperationId::RadialAlign, "radial_align", "Radial Align", hasSelection && !objectMode},
                {ModelingInstantOperationId::FlipFaceNormals, "flip_face_normals", "Flip Face Normals", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::RecalculateFaceNormals, "recalculate_face_normals", "Recalculate Face Normals", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::ShadeFacesSmooth, "shade_faces_smooth", "Shade Smooth", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::ShadeFacesFlat, "shade_faces_flat", "Shade Flat", (objectMode && hasSelection) || (faceMode && hasFaces)},
                {ModelingInstantOperationId::HardenEdgeNormals, "harden_edge_normals", "Harden Edge Normals", edgeMode && hasEdges},
                {ModelingInstantOperationId::SoftenEdgeNormals, "soften_edge_normals", "Soften Edge Normals", edgeMode && hasEdges},
        };
    }

#if QUADER_HAS_MODELING_SDK
    ModelingCommandResult executeSdkOperation(
            quader_editor_core::EditorOperationId sdkOperation,
            quader_editor_core::EditorOperationSettings settings) {
        return executeSdkOperationCommand(
                sdkOperation,
                quader_editor_core::EditorOperationCommandPhase::execute,
                std::move(settings));
    }
#endif

    ModelingCommandResult executeInstantOperation(ModelingInstantOperationId operation) {
#if QUADER_HAS_MODELING_SDK
        if (operation == ModelingInstantOperationId::DeleteSelection) {
            return deleteSelection();
        }

        const std::optional<SdkOperationExecutor::OperationCommand> command =
                sdkOperationExecutor_.operationForInstantCommand(
                        operation,
                        interactionSettingsService_.settings());
        if (!command.has_value()) {
            return {.ok = false, .message = "Instant operation is not mapped to an SDK operation."};
        }

        return executeSdkOperation(command->operation, command->settings);
#else
        (void)operation;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginTransformPreview(bool duplicateSelected) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result = session_.begin_transform_preview(duplicateSelected);
        if (result.ok) {
            invalidateSelectionOverlayCaches();
        }
        if (result.changed) {
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
#else
        (void)duplicateSelected;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformTranslatePreview(Vec3 delta) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_translate_preview(toSdk(delta)));
#else
        (void)delta;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformMatrixPreview(const ModelingTransform3& transform, Vec3 pivot) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_matrix_preview(toSdk(transform), toSdk(pivot)));
#else
        (void)transform;
        (void)pivot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformExtrudePreview(Vec3 offset, float closedEdgeLedgeSize) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(
                session_.update_transform_extrude_preview(toSdk(offset), closedEdgeLedgeSize));
#else
        (void)offset;
        (void)closedEdgeLedgeSize;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateTransformInsetPreview(const ModelingTransform3& transform, Vec3 pivot) {
#if QUADER_HAS_MODELING_SDK
        return updateTransformPreviewResult(session_.update_transform_inset_preview(toSdk(transform), toSdk(pivot)));
#else
        (void)transform;
        (void)pivot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitTransformPreview() {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromSdk(finish.before.active);
        syncSceneSelectionFromSdk();
        return transformPreviewService_.finishResult(finish, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelTransformPreview() {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromSdk(finish.before.active);
        syncSceneSelectionFromSdk();
        return transformPreviewService_.finishResult(finish, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult recordRepeatableTransform(const ModelingRepeatTransform& transform) {
#if QUADER_HAS_MODELING_SDK
        if (!transform.active) {
            return {.ok = false, .message = "No active transform to record."};
        }

        quader_editor_core::TransformDragState drag;
        drag.active = true;
        drag.tool = toSdk(transform.tool);
        drag.operation = toSdk(transform.operation);
        drag.axis = toSdk(transform.axis);
        drag.move.active = transform.tool == ModelingTransformTool::Move;
        drag.move.axis = drag.axis;
        drag.move.preview_delta = toSdk(transform.moveDelta);
        drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
        drag.rotate.axis = drag.axis;
        drag.rotate.preview_angle_degrees = transform.rotateAngleDegrees;
        drag.scale.active = transform.tool == ModelingTransformTool::Scale;
        drag.scale.axis = drag.axis;
        drag.scale.preview_scale = toSdk(transform.scale);

        historyBridge_.recordTransform(
                session_,
                drag,
                transform.duplicateSelected);
        return {.ok = true};
#else
        (void)transform;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingTransformDragPreview beginTransformDrag(const ModelingTransformDragBeginRequest& request) {
#if QUADER_HAS_MODELING_SDK
        if (runtime_.transform_drag_active() || hasTransformPreview()) {
            (void)cancelTransformDrag();
        }
        if (!session_.has_selection() || request.axis == ModelingTransformAxis::None) {
            return transformDragPreview(false);
        }

        const quader_editor_core::ViewportInteractionTool tool = toSdk(request.tool);
        const quader_editor_core::TransformDragOperation operation =
                quader_editor_core::sanitize_transform_drag_operation(
                        toSdk(request.operation),
                        tool,
                        session_.selection_mode());
        const quader_editor_core::TransformAxis axis = toSdk(request.axis);
        const quader_poly::Vec3 pivot = toSdk(request.pivot);
        const bool previewDriven =
                request.duplicateSelected ||
                session_.selection_mode() != quader_editor_core::SelectionMode::object;

        runtime_.begin_transform_drag_state(tool, operation, axis, pivot);
        {
            quader_editor_core::TransformDragState drag = runtime_.transform_drag_state();
            drag.scene_transform_preview_eligible =
                    !previewDriven &&
                    operation == quader_editor_core::TransformDragOperation::transform;
            runtime_.set_transform_drag_state(drag);
        }
        transformDragController_.setPreviewDriven(previewDriven, request.duplicateSelected);

        if (previewDriven) {
            const quader_editor_core::CommandResult beginPreview =
                    session_.begin_transform_preview(request.duplicateSelected);
            if (!beginPreview.ok) {
                clearTransformDragRuntimeState();
                return {
                        .ok = false,
                        .message = beginPreview.message.empty()
                                ? "Could not start transform preview."
                                : beginPreview.message,
                };
            }
            invalidateSelectionOverlayCaches();
            if (beginPreview.changed) {
                syncSceneObjectsFromSdk(true);
                syncSceneSelectionFromSdk();
            }
        }

        const quader_editor_core::ViewportCameraState camera = toSdk(request.camera);
        const quader_poly::Vec2 screenPosition = toSdk(request.screenPosition);
        const quader_poly::Vec2 screenPivot = toSdk(request.screenPivot);
        const quader_poly::Vec2 screenTip = toSdk(request.screenTip);

        if (tool == quader_editor_core::ViewportInteractionTool::move) {
            quader_editor_core::MoveBeginInput begin;
            begin.axis = axis;
            begin.has_world_direction = request.hasWorldDirection;
            begin.world_direction = toSdk(request.worldDirection);
            begin.center_local = {};
            begin.has_world_center = true;
            begin.world_center = pivot;
            begin.has_screen_axis = true;
            begin.screen_pivot = screenPivot;
            begin.screen_tip = screenTip;
            begin.screen_position = screenPosition;
            begin.has_start_ray = true;
            begin.start_ray = toSdk(request.ray);
            begin.has_camera = true;
            begin.camera_transform = camera.transform;
            const quader_editor_core::MoveBeginResult beginResult = quader_editor_core::begin_move_drag(begin);
            if (!beginResult.ok) {
                (void)cancelTransformDrag();
                return {.ok = false, .message = beginResult.message.empty() ? "Move drag failed." : beginResult.message};
            }
            runtime_.set_transform_drag_move_state(beginResult.state);
        } else if (tool == quader_editor_core::ViewportInteractionTool::rotate) {
            quader_editor_core::RotateBeginInput begin;
            begin.axis = axis;
            begin.object_ids = session_.selected_object_ids();
            begin.pivot = pivot;
            begin.screen_position = screenPosition;
            begin.has_screen_pivot = true;
            begin.screen_pivot = screenPivot;
            begin.has_world_direction = request.hasWorldDirection;
            begin.world_direction = toSdk(request.worldDirection);
            begin.has_world_pivot = true;
            begin.world_pivot = pivot;
            begin.has_camera = true;
            begin.camera_transform = camera.transform;
            const quader_editor_core::RotateBeginResult beginResult = quader_editor_core::begin_rotate_drag(begin);
            if (!beginResult.ok) {
                (void)cancelTransformDrag();
                return {.ok = false, .message = beginResult.message.empty() ? "Rotate drag failed." : beginResult.message};
            }
            runtime_.set_transform_drag_rotate_state(beginResult.state);
        } else if (tool == quader_editor_core::ViewportInteractionTool::scale) {
            quader_editor_core::ScaleBeginInput begin;
            begin.axis = axis;
            begin.object_ids = session_.selected_object_ids();
            begin.pivot = pivot;
            begin.screen_position = screenPosition;
            begin.has_screen_pivot = true;
            begin.screen_pivot = screenPivot;
            begin.has_screen_tip = true;
            begin.screen_tip = screenTip;
            const quader_editor_core::Bounds3 bounds = session_.selection_bounds();
            begin.bounds.has_bounds = !bounds.empty;
            begin.bounds.min = bounds.min;
            begin.bounds.max = bounds.max;
            const quader_editor_core::ScaleBeginResult beginResult = quader_editor_core::begin_scale_drag(begin);
            if (!beginResult.ok) {
                (void)cancelTransformDrag();
                return {.ok = false, .message = beginResult.message.empty() ? "Scale drag failed." : beginResult.message};
            }
            runtime_.set_transform_drag_scale_state(beginResult.state);
        } else {
            clearTransformDragRuntimeState();
            return {.ok = false, .message = "Transform drag only supports move, rotate, or scale."};
        }

        return applyTransformDragPreview({
                .screenPosition = request.screenPosition,
                .ray = request.ray,
                .camera = request.camera,
                .gridSize = request.gridSize,
                .vertexSnapRadius = request.vertexSnapRadius,
                .precision = false,
        });
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingTransformDragPreview updateTransformDrag(const ModelingTransformDragUpdateRequest& request) {
#if QUADER_HAS_MODELING_SDK
        if (!runtime_.transform_drag_active()) {
            if (hasTransformPreview()) {
                (void)cancelTransformDrag();
            }
            return transformDragPreview(false);
        }
        return applyTransformDragPreview(request);
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingTransformDragPreview commitTransformDrag() {
#if QUADER_HAS_MODELING_SDK
        const bool previewDriven = transformDragController_.previewDriven();
        const quader_editor_core::TransformDragState repeatDrag = runtime_.transform_drag_state();
        bool changed = false;

        if (previewDriven) {
            const quader_editor_core::PreviewTransactionFinishResult finish =
                    session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
            if (finish.before.active) {
                invalidateSelectionOverlayCaches();
            }
            syncSceneObjectsFromSdk(finish.before.active);
            syncSceneSelectionFromSdk();
            const bool ok =
                    !finish.before.active ||
                    finish.before.kind == quader_editor_core::PreviewTransactionKind::transform;
            if (!ok) {
                clearTransformDragRuntimeState();
                return {
                        .ok = false,
                        .previewDriven = previewDriven,
                        .message = "Active preview transaction was not a transform preview.",
                };
            }
            changed = finish.before.changed;
            if (changed && repeatDrag.active) {
                historyBridge_.recordTransform(
                        session_,
                        repeatDrag,
                        transformDragController_.duplicateSelected());
            }
        }

        ModelingTransformDragPreview preview = transformDragPreview(changed);
        preview.previewDriven = previewDriven;
        clearTransformDragRuntimeState();
        return preview;
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelTransformDrag() {
#if QUADER_HAS_MODELING_SDK
        const bool activeBefore = runtime_.transform_drag_active() || hasTransformPreview();
        const quader_editor_core::PreviewTransactionFinishResult finish =
                runtime_.cancel_transform_preview_state(session_);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromSdk(finish.before.active);
        syncSceneSelectionFromSdk();
        clearTransformDragRuntimeState();
        return {
                .ok = true,
                .changed = activeBefore && finish.restored,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request) {
#if QUADER_HAS_MODELING_SDK
        std::vector<quader_editor_core::PolygonObject> pickingObjects = sceneTransformedObjectsForPicking();
        quader_editor_core::SelectionMode mode = session_.selection_mode();
        int activeObjectId = session_.active_object_id();
        quader_editor_core::SelectionEditOperation operation =
                quader_editor_core::selection_edit_operation_from_modifiers(
                        request.controlPressed,
                        request.shiftPressed);
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_poly::Ray ray = toSdk(request.ray);
        const bool paintStroke = request.clickCount == 0;

        if (paintStroke) {
            if (mode != quader_editor_core::SelectionMode::object) {
                if (operation == quader_editor_core::SelectionEditOperation::replace) {
                    operation = quader_editor_core::SelectionEditOperation::add;
                }
                (void)quader_editor_core::pick_select(
                        pickingObjects,
                        mode,
                        operation,
                        ray,
                        activeObjectId,
                        pickOptions,
                        false);
                applyPickedSelection(pickingObjects, mode, activeObjectId);
            }
            syncSceneSelectionFromSdk();
            return selectionSummary();
        }

        const bool multiClick = request.clickCount > 1;
        if (request.preserveSelectionOnSelectedHit &&
                operation == quader_editor_core::SelectionEditOperation::replace &&
                !multiClick) {
            const quader_editor_core::SelectionHoverUpdateResult hovered =
                    quader_editor_core::update_selection_hover(
                            pickingObjects,
                            mode,
                            {},
                            ray,
                            false,
                            pickOptions,
                            cachedPickObjectBounds(),
                            cachedPickFaceGeometry(pickOptions));
            bool hitAlreadySelected = false;
            if (mode == quader_editor_core::SelectionMode::object && hovered.hover.object_id > 0) {
                hitAlreadySelected = std::any_of(
                        pickingObjects.begin(),
                        pickingObjects.end(),
                        [&](const quader_editor_core::PolygonObject& object) {
                            return object.id == hovered.hover.object_id && object.selected;
                        });
            } else {
                hitAlreadySelected =
                        quader_editor_core::selection_hover_target_matches_selection(pickingObjects, mode, hovered.hover);
            }
            if (hitAlreadySelected) {
                return selectionSummary();
            }
        }

        bool hit = false;
        if (multiClick && mode == quader_editor_core::SelectionMode::face) {
            hit = quader_editor_core::pick_select_all_faces(
                    pickingObjects,
                    mode,
                    operation,
                    ray,
                    activeObjectId,
                    pickOptions);
        } else if (multiClick && mode == quader_editor_core::SelectionMode::edge) {
            hit = quader_editor_core::pick_select_edge_loop(
                    pickingObjects,
                    mode,
                    operation,
                    ray,
                    activeObjectId,
                    pickOptions);
        } else if (multiClick && mode == quader_editor_core::SelectionMode::vertex) {
            hit = quader_editor_core::pick_select_all_vertices(
                    pickingObjects,
                    mode,
                    operation,
                    ray,
                    activeObjectId,
                    pickOptions);
        } else {
            hit = quader_editor_core::pick_select(
                    pickingObjects,
                    mode,
                    operation,
                    ray,
                    activeObjectId,
                    pickOptions);
        }
        if (hit || !request.preserveSelectionOnMiss) {
            applyPickedSelection(pickingObjects, mode, activeObjectId);
        }
        syncSceneSelectionFromSdk();
        return selectionSummary();
#else
        (void)request;
        return {};
#endif
    }

    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request) {
#if QUADER_HAS_MODELING_SDK
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects = cachedSceneTransformedObjectsForPicking();
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_editor_core::SelectionHoverUpdateResult hover =
                quader_editor_core::update_selection_hover(
                        pickingObjects,
                        session_.selection_mode(),
                        runtime_.hover(),
                        toSdk(request.ray),
                        request.selectedModifierPressed,
                        pickOptions,
                        cachedPickObjectBounds(),
                        cachedPickFaceGeometry(pickOptions));
        runtime_.apply_hover_update(hover);
        return {
                .ok = true,
                .changed = hover.changed,
        };
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    [[nodiscard]] ModelingHoverSummary selectionHoverSummary() const {
#if QUADER_HAS_MODELING_SDK
        return fromSdkHoverSummary(session_, runtime_.hover(), false);
#else
        return {};
#endif
    }

    [[nodiscard]] ModelingHoverSummary previewSelectionHover(const ModelingHoverRequest& request) const {
#if QUADER_HAS_MODELING_SDK
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects = cachedSceneTransformedObjectsForPicking();
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_editor_core::SelectionHoverUpdateResult hover =
                quader_editor_core::update_selection_hover(
                        pickingObjects,
                        session_.selection_mode(),
                        runtime_.hover(),
                        toSdk(request.ray),
                        request.selectedModifierPressed,
                        pickOptions,
                        cachedPickObjectBounds(),
                        cachedPickFaceGeometry(pickOptions));
        return fromSdkHoverSummary(session_, hover.hover, hover.changed);
#else
        (void)request;
        return {};
#endif
    }

    ModelingCommandResult clearSelectionHover() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::clear_hover;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

#if QUADER_HAS_MODELING_SDK
    ModelingCommandResult executeRuntimeCommand(
            const quader_editor_core::EditorRuntimeCommand& command,
            bool mayChangeContent) {
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        const ModelingCommandExecutor::SyncPlan syncPlan = commandExecutor_.runtimeSyncPlan(
                mayChangeContent,
                contentBefore,
                session_.state().content_revision,
                result.command);
        if (syncPlan.syncMeshes) {
            syncSceneObjectsFromSdk(syncPlan.forceMeshSync);
        }
        if (syncPlan.syncSelection) {
            syncSceneSelectionFromSdk();
        }
        if (syncPlan.invalidateOverlay) {
            invalidateSelectionOverlayCaches();
        }
        return commandExecutor_.fromSdk(result.command);
    }

    ModelingCommandResult setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool tool) {
        quader_editor_core::SetActiveToolRuntimeCommandSettings settings;
        settings.tool = tool;

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_active_tool;
        command.settings = settings;

        const bool preserveGroupSelection = appSelectionMode_ == ModelingSelectionMode::Group;
        const std::vector<EntityId> selectedBefore = preserveGroupSelection ? scene_.selectedEntities() : std::vector<EntityId>{};
        const EntityId activeBefore = preserveGroupSelection ? scene_.primarySelection() : InvalidEntityId;

        ModelingCommandResult result = executeRuntimeCommand(command, false);
        if (preserveGroupSelection) {
            restoreSceneSelection(selectedBefore, activeBefore);
            appSelectionMode_ = ModelingSelectionMode::Group;
            invalidateSelectionOverlayCaches();
        }
        return result;
    }
#endif

    ModelingCommandResult activateViewportTool(ModelingViewportTool tool) {
#if QUADER_HAS_MODELING_SDK
        const bool preserveGroupSelection = appSelectionMode_ == ModelingSelectionMode::Group;
        const std::vector<EntityId> selectedBefore = preserveGroupSelection ? scene_.selectedEntities() : std::vector<EntityId>{};
        const EntityId activeBefore = preserveGroupSelection ? scene_.primarySelection() : InvalidEntityId;
        const auto finish = [&](ModelingCommandResult result) {
            if (preserveGroupSelection) {
                restoreSceneSelection(selectedBefore, activeBefore);
                appSelectionMode_ = ModelingSelectionMode::Group;
                invalidateSelectionOverlayCaches();
            }
            return result;
        };
        if (tool == ModelingViewportTool::Cut || tool == ModelingViewportTool::Mirror) {
            promoteDrawnPlaneToolObjectSelection();
        }
        if (tool == ModelingViewportTool::Cut) {
            ModelingCommandResult activated = executeRuntimeCommand(
                    noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::activate_cut),
                    false);
            if (activated.ok) {
                const ModelingCommandResult modeResult = setCutMode(interactionSettingsService_.settings().cutMode);
                activated.ok = modeResult.ok;
                activated.changed = activated.changed || modeResult.changed;
                if (!modeResult.message.empty()) {
                    activated.message = modeResult.message;
                }
            }
            return finish(activated);
        }
        if (tool == ModelingViewportTool::Mirror) {
            return finish(executeRuntimeCommand(
                    noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::activate_mirror),
                    false));
        }
        return finish(setActiveRuntimeTool(toSdk(tool)));
#else
        (void)tool;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelActiveModalTool() {
#if QUADER_HAS_MODELING_SDK
        switch (runtime_.active_tool()) {
        case quader_editor_core::ViewportInteractionTool::box:
            return cancelBox();
        case quader_editor_core::ViewportInteractionTool::insert_edge_loop:
            (void)executeRuntimeCommand(noSettingsRuntimeCommand(
                    quader_editor_core::EditorRuntimeCommandId::cancel_insert_edge_loop), false);
            return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        case quader_editor_core::ViewportInteractionTool::knife:
            runtime_.reset_knife_state();
            invalidateSelectionOverlayCaches();
            return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        case quader_editor_core::ViewportInteractionTool::cut:
            (void)executeRuntimeCommand(noSettingsRuntimeCommand(
                    quader_editor_core::EditorRuntimeCommandId::cancel_cut), false);
            return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        case quader_editor_core::ViewportInteractionTool::mirror:
            (void)executeRuntimeCommand(noSettingsRuntimeCommand(
                    quader_editor_core::EditorRuntimeCommandId::cancel_mirror), false);
            return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        case quader_editor_core::ViewportInteractionTool::poly:
            runtime_.reset_poly_state();
            invalidateSelectionOverlayCaches();
            return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        case quader_editor_core::ViewportInteractionTool::select:
        case quader_editor_core::ViewportInteractionTool::move:
        case quader_editor_core::ViewportInteractionTool::extend:
        case quader_editor_core::ViewportInteractionTool::extrude:
        case quader_editor_core::ViewportInteractionTool::rotate:
        case quader_editor_core::ViewportInteractionTool::scale:
        case quader_editor_core::ViewportInteractionTool::unknown:
            break;
        }
        return {.ok = true};
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateInsertEdgeLoopHover(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_insert_edge_loop_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_insert_edge_loop_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_insert_edge_loop_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitInsertEdgeLoopDrag() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::commit_insert_edge_loop_drag),
                true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateKnifeHover(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        const bool changed = runtime_.update_knife_preview_from_pick(
                session_,
                toSdk(event.ray),
                toSdk(event.camera),
                event.gridSize,
                event.snapEnabled,
                event.constrainAxis);
        if (changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = true, .changed = changed};
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult acceptKnifePoint(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        const bool changed = runtime_.accept_knife_current_point(session_, toSdk(event.camera), event.gridSize);
        if (changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = true, .changed = changed};
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitKnifeTool() {
#if QUADER_HAS_MODELING_SDK
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = runtime_.commit_knife_tool(session_);
        if (session_.state().content_revision != contentBefore || result.changed) {
            syncSceneObjectsFromSdk(result.changed);
        }
        syncSceneSelectionFromSdk();
        invalidateSelectionOverlayCaches();
        if (result.ok) {
            const ModelingCommandResult selectTool = setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
            return {
                    .ok = selectTool.ok,
                    .changed = result.changed || selectTool.changed,
                    .message = !result.message.empty() ? result.message : selectTool.message,
            };
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateCutHover(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_cut_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult clickCut(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::click_cut;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginCutPointDrag(int pointIndex) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::CutPointDragRuntimeCommandSettings settings;
        settings.point_index = pointIndex;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_cut_point_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)pointIndex;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginCutLineDrag(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = true;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_cut_line_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult endCutDrag() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::end_cut_drag),
                false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult setCutMode(ModelingCutMode mode) {
        interactionSettingsService_.setCutMode(mode);
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::SetCutModeRuntimeCommandSettings settings;
        settings.mode = quader_editor_core::cut_mode_id(toSdk(mode));
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_cut_mode;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        return {.ok = true, .changed = true};
#endif
    }

    ModelingCommandResult cycleCutMode() {
#if QUADER_HAS_MODELING_SDK
        return setCutMode(fromSdk(quader_editor_core::next_cut_mode(toSdk(interactionSettingsService_.settings().cutMode))));
#else
        interactionSettingsService_.cycleCutModeWithoutSdk();
        return {.ok = true, .changed = true};
#endif
    }

    ModelingCommandResult finalizeCutTool() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::finalize_cut),
                true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updateMirrorHover(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_mirror_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult clickMirror(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::click_mirror;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginMirrorPointDrag(int pointIndex) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorPointDragRuntimeCommandSettings settings;
        settings.point_index = pointIndex;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_mirror_point_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)pointIndex;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult beginMirrorLineDrag(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toSdk(event.camera);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = true;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_mirror_line_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult endMirrorDrag() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::end_mirror_drag),
                false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult finalizeMirrorTool() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::finalize_mirror),
                true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult updatePolyHover(const ModelingToolRayEvent& event) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::ViewportCameraState camera = toSdk(event.camera);
        const quader_editor_core::CutGridPointResult gridPoint =
                quader_editor_core::viewport_grid_point_from_ray(camera, toSdk(event.ray), event.gridSize, event.snapEnabled);
        if (!gridPoint.ok) {
            return {.ok = true, .message = gridPoint.message};
        }
        const quader_editor_core::PolyToolResult result = runtime_.update_poly_hover(
                gridPoint.position,
                gridPoint.plane_normal,
                event.constrainAxis,
                event.gridSize,
                event.snapEnabled);
        if (result.changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult acceptPolyPoint() {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolyToolResult result = runtime_.accept_poly_current_point();
        if (result.changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult commitPolyTool() {
#if QUADER_HAS_MODELING_SDK
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = runtime_.commit_poly_tool(session_);
        if (session_.state().content_revision != contentBefore || result.changed) {
            syncSceneObjectsFromSdk(result.changed);
        }
        syncSceneSelectionFromSdk();
        invalidateSelectionOverlayCaches();
        if (result.ok) {
            const ModelingCommandResult selectTool = setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
            return {
                    .ok = selectTool.ok,
                    .changed = result.changed || selectTool.changed,
                    .message = !result.message.empty() ? result.message : selectTool.message,
            };
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult activateBoxTool() {
#if QUADER_HAS_MODELING_SDK
        if (quader_host_contract::find_tool_descriptor("box") == nullptr) {
            return {.ok = false, .message = "Box tool is not present in the modeling host contract."};
        }

        return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::box);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCreateBoxResult createBoxFromBounds(const ModelingCreateBoxRequest& request) {
#if QUADER_HAS_MODELING_SDK
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::CreateBoxFromBoundsRuntimeCommandSettings settings;
        settings.min_bounds = toSdk(request.bounds.min);
        settings.max_bounds = toSdk(request.bounds.max);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::create_box_from_bounds;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok || !sdkResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = sdkResult.command.ok,
                    .changed = sdkResult.command.changed,
                    .message = sdkResult.command.message.empty() ? "SDK did not create a box object." : sdkResult.command.message,
            };
        }

        return createSceneObjectFromLatest(request.name.empty() ? "Box" : request.name, sdkResult.command.message);
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxHover(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_hover, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        ModelingBoxPreview preview =
                executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_footprint, settings);
        if (preview.ok) {
            scene_.clearSelection();
        }
        return preview;
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxFootprint(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_footprint, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toSdk(event.screenPosition);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.has_camera = event.hasCamera;
        if (event.hasCamera) {
            settings.camera = toSdk(event.camera);
        }
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_height, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingBoxPreview updateBoxHeight(const ModelingBoxToolEvent& event) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toSdk(event.screenPosition);
        settings.ray = toSdk(event.ray);
        settings.grid_size = event.gridSize;
        settings.has_camera = event.hasCamera;
        if (event.hasCamera) {
            settings.camera = toSdk(event.camera);
        }
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_height, settings);
#else
        (void)event;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCreateBoxResult commitBox(std::string name) {
#if QUADER_HAS_MODELING_SDK
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::commit_box;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok || !sdkResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = sdkResult.command.ok,
                    .changed = sdkResult.command.changed,
                    .message = sdkResult.command.message.empty() ? "SDK did not commit a box object." : sdkResult.command.message,
            };
        }

        ModelingCreateBoxResult created = createSceneObjectFromLatest(name.empty() ? "Box" : name, sdkResult.command.message);
        const ModelingCommandResult selectTool = setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        created.changed = created.changed || selectTool.changed;
        if (!selectTool.ok) {
            created.ok = false;
            created.message = selectTool.message.empty() ? "Box committed, but SDK tool reset failed." : selectTool.message;
        }
        return created;
#else
        (void)name;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult cancelBox() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::cancel_box;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        const ModelingCommandResult selectTool = setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        return {
                .ok = result.command.ok && selectTool.ok,
                .changed = result.command.changed || selectTool.changed,
                .message = !result.command.message.empty() ? result.command.message : selectTool.message,
        };
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

#if QUADER_HAS_MODELING_SDK
    ModelingCreateBoxResult createMesh(const ModelingCreateMeshRequest& request) {
        const std::size_t objectCountBefore = session_.objects().size();
        const quader_editor_core::CommandResult sdkResult =
                session_.create_object_from_document(request.document, request.name, request.materialSlot);
        if (!sdkResult.ok || !sdkResult.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = sdkResult.ok,
                    .changed = sdkResult.changed,
                    .message = sdkResult.message.empty() ? "SDK did not create a mesh object." : sdkResult.message,
            };
        }

        return createSceneObjectFromLatest(request.name.empty() ? "Mesh" : request.name, sdkResult.message);
    }
#endif

    ModelingCommandResult setObjectSelection(std::span<const EntityId> entities, EntityId active) {
#if QUADER_HAS_MODELING_SDK
        if (!sceneObjectsSyncedForCurrentSdkContent()) {
            syncSceneObjectsFromSdk();
        }

        std::vector<int> objectIds;
        objectIds.reserve(entities.size());
        for (const EntityId entity : entities) {
            const auto object = objectRegistry_.sdkObjectForEntity(entity);
            if (object.has_value()) {
                objectIds.push_back(*object);
            }
        }
        if (!entities.empty() && objectIds.empty()) {
            return {.ok = false, .message = "Selection does not contain modeling objects."};
        }

        int activeObjectId = -1;
        if (active != InvalidEntityId) {
            const auto object = objectRegistry_.sdkObjectForEntity(active);
            if (object.has_value()) {
                activeObjectId = *object;
            }
        }
        if (activeObjectId <= 0 && !objectIds.empty()) {
            activeObjectId = objectIds.back();
        }

        std::vector<int> currentObjectIds = session_.selected_object_ids();
        std::sort(currentObjectIds.begin(), currentObjectIds.end());
        std::vector<int> desiredObjectIds = objectIds;
        std::sort(desiredObjectIds.begin(), desiredObjectIds.end());
        if (session_.selection_mode() == quader_editor_core::SelectionMode::object &&
                currentObjectIds == desiredObjectIds &&
                session_.active_object_id() == activeObjectId) {
            syncSceneSelectionFromSdk();
            return {.ok = true, .changed = false};
        }

        session_.set_selected_object_ids(objectIds, activeObjectId);
        syncSceneSelectionFromSdk();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = true};
#else
        (void)entities;
        (void)active;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult setComponentSelection(const ModelingComponentSelectionRequest& request) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::PolygonObject* object = findObject(session_, request.sdkObjectId);
        if (object == nullptr) {
            return {.ok = false, .message = "Selection target mesh was not found."};
        }

        const quader_editor_core::SelectionMode mode = toSdk(request.mode);
        quader_poly::Selection selection;
        if (request.edit != ModelingSelectionEdit::Replace &&
                session_.selection_mode() == mode &&
                object->selected) {
            selection = object->selection;
        }
        selection.mode = quader_editor_core::component_selection_mode(mode);

        auto applyVertex = [&](int vertexId) {
            if (vertexId <= 0) {
                return;
            }
            const auto id = static_cast<quader_poly::ElementId>(vertexId);
            const auto found = std::find(selection.vertices.begin(), selection.vertices.end(), id);
            const bool selected = found != selection.vertices.end();
            if (request.edit == ModelingSelectionEdit::Remove || (request.edit == ModelingSelectionEdit::Toggle && selected)) {
                if (selected) {
                    selection.vertices.erase(found);
                }
                return;
            }
            if (!selected) {
                selection.vertices.push_back(id);
            }
        };

        auto applyEdge = [&](std::pair<int, int> edgeIds) {
            if (edgeIds.first <= 0 || edgeIds.second <= 0) {
                return;
            }
            const quader_poly::Edge edge = quader_poly::make_edge(
                    static_cast<quader_poly::ElementId>(edgeIds.first),
                    static_cast<quader_poly::ElementId>(edgeIds.second));
            const auto found = std::find(selection.edges.begin(), selection.edges.end(), edge);
            const bool selected = found != selection.edges.end();
            if (request.edit == ModelingSelectionEdit::Remove || (request.edit == ModelingSelectionEdit::Toggle && selected)) {
                if (selected) {
                    selection.edges.erase(found);
                }
                return;
            }
            if (!selected) {
                selection.edges.push_back(edge);
            }
        };

        auto applyFace = [&](int faceId) {
            if (faceId <= 0) {
                return;
            }
            const auto id = static_cast<quader_poly::ElementId>(faceId);
            const auto found = std::find(selection.faces.begin(), selection.faces.end(), id);
            const bool selected = found != selection.faces.end();
            if (request.edit == ModelingSelectionEdit::Remove || (request.edit == ModelingSelectionEdit::Toggle && selected)) {
                if (selected) {
                    selection.faces.erase(found);
                }
                return;
            }
            if (!selected) {
                selection.faces.push_back(id);
            }
        };

        for (const int vertex : request.vertices) {
            applyVertex(vertex);
        }
        for (const std::pair<int, int>& edge : request.edges) {
            applyEdge(edge);
        }
        for (const int face : request.faces) {
            applyFace(face);
        }

        selection.has_active = false;
        if (request.activeKind == ModelingActiveSelectionKind::Vertex && request.activeVertex > 0) {
            const auto active = static_cast<quader_poly::ElementId>(request.activeVertex);
            if (std::find(selection.vertices.begin(), selection.vertices.end(), active) != selection.vertices.end()) {
                selection.has_active = true;
                selection.active_kind = quader_poly::ElementKind::vertex;
                selection.active_vertex = active;
            }
        } else if (request.activeKind == ModelingActiveSelectionKind::Edge &&
                request.activeEdge.first > 0 &&
                request.activeEdge.second > 0) {
            const quader_poly::Edge active = quader_poly::make_edge(
                    static_cast<quader_poly::ElementId>(request.activeEdge.first),
                    static_cast<quader_poly::ElementId>(request.activeEdge.second));
            if (std::find(selection.edges.begin(), selection.edges.end(), active) != selection.edges.end()) {
                selection.has_active = true;
                selection.active_kind = quader_poly::ElementKind::edge;
                selection.active_edge = active;
            }
        } else if (request.activeKind == ModelingActiveSelectionKind::Face && request.activeFace > 0) {
            const auto active = static_cast<quader_poly::ElementId>(request.activeFace);
            if (std::find(selection.faces.begin(), selection.faces.end(), active) != selection.faces.end()) {
                selection.has_active = true;
                selection.active_kind = quader_poly::ElementKind::face;
                selection.active_face = active;
            }
        }

        const std::uint64_t selectionBefore = session_.state().selection_revision;
        session_.set_component_selection(request.sdkObjectId, std::move(selection), mode);
        syncSceneSelectionFromSdk();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = session_.state().selection_revision != selectionBefore};
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult moveSelection(Vec3 delta) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result = session_.translate_selected(toSdk(delta));
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromSdk(true);
            }
            syncSceneSelectionFromSdk();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        (void)delta;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult transformSelection(const ModelingTransform3& transform, Vec3 pivot) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result =
                session_.transform_selected(toSdk(transform), toSdk(pivot));
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromSdk(true);
            }
            syncSceneSelectionFromSdk();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        (void)transform;
        (void)pivot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult assignMaterialSlotToSelection(std::uint32_t materialSlot) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result = session_.assign_material_to_selection(materialSlot);
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromSdk(true);
            }
            syncSceneSelectionFromSdk();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        (void)materialSlot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult assignMaterialSlotToFace(int sdkObjectId, int faceId, std::uint32_t materialSlot) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::CommandResult result = session_.assign_material_to_face(
                sdkObjectId,
                static_cast<quader_poly::ElementId>(faceId),
                materialSlot);
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromSdk(true);
            }
            syncSceneSelectionFromSdk();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
#else
        (void)sdkObjectId;
        (void)faceId;
        (void)materialSlot;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult setSelectionHover(const ModelingDirectHoverRequest& request) {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::SelectionHoverState hover;
        hover.object_id = request.sdkObjectId;
        hover.selected_modifier_pressed = request.selectedModifierPressed;
        hover.candidate_object_id = request.kind == ModelingActiveSelectionKind::Object ? -1 : request.sdkObjectId;
        switch (request.kind) {
        case ModelingActiveSelectionKind::Object:
            hover.kind = quader_editor_core::SelectionHoverKind::object;
            break;
        case ModelingActiveSelectionKind::Vertex:
            hover.kind = quader_editor_core::SelectionHoverKind::vertex;
            hover.vertex_id = static_cast<quader_poly::ElementId>(request.vertexId);
            break;
        case ModelingActiveSelectionKind::Edge:
            hover.kind = quader_editor_core::SelectionHoverKind::edge;
            hover.edge = quader_poly::make_edge(
                    static_cast<quader_poly::ElementId>(request.edge.first),
                    static_cast<quader_poly::ElementId>(request.edge.second));
            break;
        case ModelingActiveSelectionKind::Face:
            hover.kind = quader_editor_core::SelectionHoverKind::face;
            hover.face_id = static_cast<quader_poly::ElementId>(request.faceId);
            break;
        case ModelingActiveSelectionKind::None:
        case ModelingActiveSelectionKind::Light:
            hover = {};
            break;
        }

        const bool changed = !SelectionOverlayCache::hoverMatches(runtime_.hover(), hover);
        runtime_.apply_hover_update(quader_editor_core::SelectionHoverUpdateResult{
                .changed = changed,
                .hover = hover,
        });
        if (changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = true, .changed = changed};
#else
        (void)request;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

private:
#if QUADER_HAS_MODELING_SDK
    void invalidateSelectionOverlayCaches() const {
        overlayService_.invalidate();
    }

    [[nodiscard]] std::uint64_t selectionOverlayToolSignature() const {
        std::uint64_t hash = 1469598103934665603ULL;
        mixToolStateHash(hash, static_cast<int>(runtime_.active_tool()));

        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        const quader_editor_core::CutHoverState& cutHover = runtime_.cut_hover();
        mixToolStateHash(hash, quader_editor_core::cut_is_active(cut));
        mixToolStateHash(hash, static_cast<int>(cut.points.size()));
        for (const quader_poly::Vec3& point : cut.points) {
            mixToolStateHash(hash, point);
        }
        mixToolStateHash(hash, static_cast<int>(cut.target_object_ids.size()));
        for (const int objectId : cut.target_object_ids) {
            mixToolStateHash(hash, objectId);
        }
        mixToolStateHash(hash, cut.source_object_id);
        mixToolStateHash(hash, cut.face_normal);
        mixToolStateHash(hash, static_cast<int>(cut.mode));
        mixToolStateHash(hash, cut.dragging_point_index);
        mixToolStateHash(hash, cut.dragging_new_point);
        mixToolStateHash(hash, cut.dragging_line);
        mixToolStateHash(hash, cut.drag_line_anchor);
        mixToolStateHash(hash, cut.preview_valid);
        mixToolStateHash(hash, cut.preview_counts.target_count);
        mixToolStateHash(hash, cut.preview_counts.split_count);
        mixToolStateHash(hash, cut.preview_counts.removed_count);
        mixToolStateHash(hash, cutHover.valid);
        mixToolStateHash(hash, cutHover.object_id);
        mixToolStateHash(hash, cutHover.position);

        const quader_editor_core::MirrorToolState& mirror = runtime_.mirror_state();
        const quader_editor_core::MirrorHoverState& mirrorHover = runtime_.mirror_hover();
        mixToolStateHash(hash, quader_editor_core::mirror_is_active(mirror));
        mixToolStateHash(hash, static_cast<int>(mirror.plane.points.size()));
        for (const quader_poly::Vec3& point : mirror.plane.points) {
            mixToolStateHash(hash, point);
        }
        mixToolStateHash(hash, mirror.plane.face_normal);
        mixToolStateHash(hash, mirror.plane.dragging_point_index);
        mixToolStateHash(hash, mirror.plane.dragging_new_point);
        mixToolStateHash(hash, mirror.plane.dragging_line);
        mixToolStateHash(hash, mirror.plane.drag_line_anchor);
        mixToolStateHash(hash, static_cast<int>(mirror.target_object_ids.size()));
        for (const int objectId : mirror.target_object_ids) {
            mixToolStateHash(hash, objectId);
        }
        mixToolStateHash(hash, mirror.preview_valid);
        mixToolStateHash(hash, mirror.preview_counts.target_count);
        mixToolStateHash(hash, mirror.preview_counts.mirrored_count);
        mixToolStateHash(hash, mirrorHover.valid);
        mixToolStateHash(hash, mirrorHover.object_id);
        mixToolStateHash(hash, mirrorHover.position);
        return hash;
    }

    [[nodiscard]] bool sceneObjectsSyncedForCurrentSdkContent() const {
        return sceneSyncService_.sceneObjectsSyncedForCurrentSdkContent(session_, scene_);
    }

    ModelingCommandResult updateTransformPreviewResult(const quader_editor_core::CommandResult& result) {
        if (result.ok) {
            invalidateSelectionOverlayCaches();
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
    }

    [[nodiscard]] bool hasTransformPreview() const {
        return transformPreviewService_.hasTransformPreview(session_);
    }

    void clearTransformDragRuntimeState() {
        runtime_.clear_transform_drag_state();
        runtime_.clear_gizmo_hover();
        transformDragController_.clear();
    }

    [[nodiscard]] bool scenePreviewTransformForDrag(
            const quader_editor_core::TransformDragState& drag,
            quader_poly::Transform3& outTransform) const {
        outTransform = {};
        if (!drag.active ||
                !drag.scene_transform_preview_eligible ||
                drag.operation != quader_editor_core::TransformDragOperation::transform) {
            return false;
        }
        if (drag.tool == quader_editor_core::ViewportInteractionTool::move) {
            outTransform.origin = drag.move.preview_delta;
            return true;
        }
        if (drag.tool == quader_editor_core::ViewportInteractionTool::rotate && drag.rotate.active) {
            quader_editor_core::RotateToolState rotate = drag.rotate;
            const quader_editor_core::RotatePreviewResult preview =
                    quader_editor_core::preview_rotate_from_angle_degrees(
                            rotate,
                            drag.rotate.preview_angle_degrees,
                            0.0);
            if (!preview.ok) {
                return false;
            }
            outTransform = preview.pivoted_transform;
            return true;
        }
        if (drag.tool == quader_editor_core::ViewportInteractionTool::scale && drag.scale.active) {
            quader_editor_core::ScaleToolState scale = drag.scale;
            const quader_editor_core::ScalePreviewResult preview =
                    quader_editor_core::preview_scale_from_scale(scale, drag.scale.preview_scale);
            if (!preview.ok) {
                return false;
            }
            outTransform = preview.pivoted_transform;
            return true;
        }
        return false;
    }

    [[nodiscard]] ModelingTransformDragPreview transformDragPreview(bool changed) const {
        ModelingTransformDragPreview preview;
        preview.ok = true;
        const quader_editor_core::TransformDragState drag = runtime_.transform_drag_state();
        preview.active = drag.active;
        preview.changed = changed;
        preview.previewDriven = transformDragController_.previewDriven();
        preview.tool = fromSdk(drag.tool);
        preview.operation = fromSdk(drag.operation);
        preview.axis = fromSdk(drag.axis);
        preview.pivot = fromSdk(drag.pivot);
        preview.moveDelta = fromSdk(drag.move.preview_delta);
        preview.rotateAngleDegrees = drag.rotate.preview_angle_degrees;
        preview.scale = fromSdk(drag.scale.preview_scale);
        preview.contentRevision = session_.state().content_revision;
        preview.selectionRevision = session_.state().selection_revision;
        preview.sceneTransform = drag.scene_transform_preview_eligible;
        quader_poly::Transform3 sceneTransform;
        if (scenePreviewTransformForDrag(drag, sceneTransform)) {
            preview.hasScenePreviewTransform = true;
            preview.scenePreviewTransform = fromSdk(sceneTransform);
        }
        return preview;
    }

    ModelingTransformDragPreview applyTransformDragPreview(const ModelingTransformDragUpdateRequest& request) {
        quader_editor_core::TransformDragPreviewInput input;
        input.camera = toSdk(request.camera);
        input.screen_position = toSdk(request.screenPosition);
        input.ray = toSdk(request.ray);
        input.grid_size = request.gridSize;
        input.precision = request.precision;
        if (request.vertexSnapRadius > 0.0) {
            input.vertex_snap_enabled = true;
            input.vertex_snap_radius = request.vertexSnapRadius;
            input.snap_object_bounds = cachedPickObjectBounds();
            input.snap_face_geometry = cachedPickFaceGeometry();
        }

        const quader_editor_core::CommandResult result =
                runtime_.apply_transform_drag_preview(session_, input);
        if (!result.ok) {
            const bool previewDriven = transformDragController_.previewDriven();
            (void)cancelTransformDrag();
            return {
                    .ok = false,
                    .previewDriven = previewDriven,
                    .message = result.message.empty()
                            ? "Could not update transform drag preview."
                            : result.message,
            };
        }

        invalidateSelectionOverlayCaches();
        if (transformDragController_.previewDriven()) {
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return transformDragPreview(result.changed);
    }
#endif

    void syncSceneObjectsFromSdk(bool force = false) {
#if QUADER_HAS_MODELING_SDK
        meshSyncService_.syncSceneObjectsFromSdk(session_, scene_, assets_, sceneSyncService_, force);
#endif
    }

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] const std::vector<quader_editor_core::PolygonObject>& cachedSceneTransformedObjectsForPicking() const {
        return selectionService_.cachedSceneTransformedObjectsForPicking(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking() const {
        return selectionService_.sceneTransformedObjectsForPicking(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds>& cachedPickObjectBounds() const {
        return selectionService_.cachedPickObjectBounds(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry>& cachedPickFaceGeometry(
            quader_poly::PickOptions pickOptions = {}) const {
        return selectionService_.cachedPickFaceGeometry(session_, scene_, objectRegistry_, pickOptions);
    }

    void applyPickedSelection(
            const std::vector<quader_editor_core::PolygonObject>& pickingObjects,
            quader_editor_core::SelectionMode mode,
            int activeObjectId) {
        quader_editor_core::SessionState& state = session_.state();
        bool changed = state.selection_mode != mode || state.active_object_id != activeObjectId;
        for (quader_editor_core::PolygonObject& object : state.objects) {
            const auto picked = std::find_if(
                    pickingObjects.begin(),
                    pickingObjects.end(),
                    [&](const quader_editor_core::PolygonObject& candidate) {
                        return candidate.id == object.id;
                    });
            const bool nextSelected = picked != pickingObjects.end() && picked->selected;
            const quader_poly::Selection nextSelection =
                    picked != pickingObjects.end() ? picked->selection : quader_poly::Selection{};
            changed = changed ||
                    object.selected != nextSelected ||
                    !ModelingSelectionService::selectionMatches(object.selection, nextSelection);
            object.selected = nextSelected;
            object.selection = nextSelection;
        }
        if (!changed) {
            return;
        }
        state.selection_mode = mode;
        state.active_object_id = activeObjectId;
        quader_editor_core::normalize_active_object_id(state.objects, state.active_object_id);
        state.dirty = true;
        ++state.selection_revision;
    }

    ModelingCommandResult executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId commandId, bool mayChangeContent) {
        return executeRuntimeCommand(noSettingsRuntimeCommand(commandId), mayChangeContent);
    }
#endif

    ModelingCreateBoxResult createSceneObjectFromLatest(const std::string& name, const std::string& message) {
#if QUADER_HAS_MODELING_SDK
        return meshSyncService_.createSceneObjectFromLatest(
                session_,
                scene_,
                assets_,
                sceneSyncService_,
                name,
                message);
#else
        (void)name;
        (void)message;
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

#if QUADER_HAS_MODELING_SDK
    template <typename Settings>
    ModelingBoxPreview executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId commandId, Settings settings) {
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = commandId;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult sdkResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!sdkResult.command.ok) {
            return {
                    .ok = false,
                    .message = sdkResult.command.message,
            };
        }

        quader_editor_core::BoxPreviewDescriptor preview = runtime_.box_preview_descriptor(true);
        std::vector<quader_poly::Vec3> corners;
        if (preview.drag_state == quader_editor_core::BoxDragState::height) {
            corners = quader_editor_core::box_preview_corners(preview);
        }

        ModelingBoxPreview result = fromSdk(preview, corners);
        result.ok = sdkResult.command.ok;
        if (!sdkResult.command.message.empty()) {
            result.message = sdkResult.command.message;
        }
        return result;
    }
#endif

    SceneDocument& scene_;
    AssetRegistry& assets_;
    ModelingObjectRegistry objectRegistry_;
    SceneModelingSyncService sceneSyncService_;
    ModelingMeshSyncService meshSyncService_;
    ModelingSelectionService selectionService_;
    mutable ModelingOverlayService overlayService_;
    ModelingCommandExecutor commandExecutor_;
    ModelingContextActionService contextActionService_;
    SdkOperationExecutor sdkOperationExecutor_;
    TransformPreviewService transformPreviewService_;
    ModelingInteractionSettingsService interactionSettingsService_;
    ModelingToolSettingsService toolSettingsService_;
    TransformDragController transformDragController_;
    ModalToolRuntime modalToolRuntime_;
    ModelingClipboardService clipboardService_;
    ModelingHistoryBridge historyBridge_;
    ModelingSelectionMode appSelectionMode_ = ModelingSelectionMode::Object;

#if QUADER_HAS_MODELING_SDK
    quader_editor_core::EditorSession session_;
    quader_editor_core::EditorRuntime runtime_;
#endif
};

ModelingSessionService::ModelingSessionService(SceneDocument& scene, AssetRegistry& assets)
    : impl_(std::make_unique<Impl>(scene, assets)) {}

ModelingSessionService::~ModelingSessionService() = default;
ModelingSessionService::ModelingSessionService(ModelingSessionService&&) noexcept = default;
ModelingSessionService& ModelingSessionService::operator=(ModelingSessionService&&) noexcept = default;

bool ModelingSessionService::sdkAvailable() const {
    return impl_->sdkAvailable();
}

ModelingCommandDescriptor ModelingSessionService::boxToolCommand() const {
    return impl_->boxToolCommand();
}

ModelingCommandDescriptor ModelingSessionService::createBoxFromBoundsCommand() const {
    return impl_->createBoxFromBoundsCommand();
}

std::vector<ModelingObjectRecord> ModelingSessionService::objects() const {
    return impl_->objects();
}

std::optional<EntityId> ModelingSessionService::entityForSdkObject(int sdkObjectId) const {
    return impl_->entityForSdkObject(sdkObjectId);
}

std::optional<int> ModelingSessionService::sdkObjectForEntity(EntityId entity) const {
    return impl_->sdkObjectForEntity(entity);
}

std::optional<MeshAsset> ModelingSessionService::compileObjectMesh(int sdkObjectId) const {
    return impl_->compileObjectMesh(sdkObjectId);
}

std::optional<Vec3> ModelingSessionService::selectionCenter() const {
    return impl_->selectionCenter();
}

ModelingSelectionMode ModelingSessionService::selectionMode() const {
    return impl_->selectionMode();
}

ModelingSelectionSummary ModelingSessionService::selectionSummary() const {
    return impl_->selectionSummary();
}

ViewportOverlayFrame ModelingSessionService::selectionOverlayFrame() const {
    return impl_->selectionOverlayFrame();
}

std::vector<ModelingToolPreviewMesh> ModelingSessionService::toolPreviewMeshes() const {
    return impl_->toolPreviewMeshes();
}

bool ModelingSessionService::canUndo() const {
    return impl_->canUndo();
}

bool ModelingSessionService::canRedo() const {
    return impl_->canRedo();
}

void ModelingSessionService::syncObjectSelectionFromScene() {
    impl_->syncObjectSelectionFromScene();
}

void ModelingSessionService::syncSceneSelectionFromSdk() {
    impl_->syncSceneSelectionFromSdk();
}

ModelingCommandResult ModelingSessionService::duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands) {
    return impl_->duplicateSelectedHierarchyForGroupDrag(documentCommands);
}

ModelingCommandResult ModelingSessionService::setSelectionMode(ModelingSelectionMode mode) {
    return impl_->setSelectionMode(mode);
}

ModelingCommandResult ModelingSessionService::selectAll() {
    return impl_->selectAll();
}

ModelingCommandResult ModelingSessionService::invertSelection() {
    return impl_->invertSelection();
}

ModelingCommandResult ModelingSessionService::clearSelection() {
    return impl_->clearSelection();
}

ModelingCommandResult ModelingSessionService::deleteSelection() {
    return impl_->deleteSelection();
}

ModelingCommandResult ModelingSessionService::copySelection() {
    return impl_->copySelection();
}

ModelingCommandResult ModelingSessionService::pasteSelection() {
    return impl_->pasteSelection();
}

ModelingCommandResult ModelingSessionService::undo() {
    return impl_->undo();
}

ModelingCommandResult ModelingSessionService::redo() {
    return impl_->redo();
}

ModelingCommandResult ModelingSessionService::repeatLastAction() {
    return impl_->repeatLastAction();
}

ModelingCommandResult ModelingSessionService::mirrorSelection(ModelingMirrorAxis axis) {
    return impl_->mirrorSelection(axis);
}

ModelingCommandResult ModelingSessionService::flipSelection(ModelingFlipOrientation orientation) {
    return impl_->flipSelection(orientation);
}

ModelingCommandResult ModelingSessionService::applyActiveMaterial(std::uint32_t materialSlot) {
    return impl_->applyActiveMaterial(materialSlot);
}

bool ModelingSessionService::modalToolOwnsViewport() const {
    return impl_->modalToolOwnsViewport();
}

ModelingToolStatus ModelingSessionService::toolStatus() const {
    return impl_->toolStatus();
}

ModelingToolSettingsTarget ModelingSessionService::toolSettingsTarget() const {
    return impl_->toolSettingsTarget();
}

ModelingInteractionSettings ModelingSessionService::interactionSettings() const {
    return impl_->interactionSettings();
}

ModelingCommandResult ModelingSessionService::setInteractionSettings(const ModelingInteractionSettings& settings) {
    return impl_->setInteractionSettings(settings);
}

ModelingActionSet ModelingSessionService::contextActionSet(const ModelingActionQuery& query) const {
    return impl_->contextActionSet(query);
}

std::vector<ModelingActionItem> ModelingSessionService::contextActions(const ModelingActionQuery& query) const {
    return impl_->contextActions(query);
}

ModelingCommandResult ModelingSessionService::executeContextAction(std::string_view actionId) {
    return impl_->executeContextAction(actionId);
}

ModelingCommandResult ModelingSessionService::commitActiveOperationPreview() {
    return impl_->commitActiveOperationPreview();
}

ModelingCommandResult ModelingSessionService::cancelActiveOperationPreview() {
    return impl_->cancelActiveOperationPreview();
}

std::vector<ModelingInstantOperationDescriptor> ModelingSessionService::instantOperations() const {
    return impl_->instantOperations();
}

ModelingCommandResult ModelingSessionService::executeInstantOperation(ModelingInstantOperationId operation) {
    return impl_->executeInstantOperation(operation);
}

ModelingCommandResult ModelingSessionService::beginTransformPreview(bool duplicateSelected) {
    return impl_->beginTransformPreview(duplicateSelected);
}

ModelingCommandResult ModelingSessionService::updateTransformTranslatePreview(Vec3 delta) {
    return impl_->updateTransformTranslatePreview(delta);
}

ModelingCommandResult ModelingSessionService::updateTransformMatrixPreview(const ModelingTransform3& transform, Vec3 pivot) {
    return impl_->updateTransformMatrixPreview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::updateTransformExtrudePreview(Vec3 offset, float closedEdgeLedgeSize) {
    return impl_->updateTransformExtrudePreview(offset, closedEdgeLedgeSize);
}

ModelingCommandResult ModelingSessionService::updateTransformInsetPreview(const ModelingTransform3& transform, Vec3 pivot) {
    return impl_->updateTransformInsetPreview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::commitTransformPreview() {
    return impl_->commitTransformPreview();
}

ModelingCommandResult ModelingSessionService::cancelTransformPreview() {
    return impl_->cancelTransformPreview();
}

ModelingCommandResult ModelingSessionService::recordRepeatableTransform(const ModelingRepeatTransform& transform) {
    return impl_->recordRepeatableTransform(transform);
}

ModelingTransformDragPreview ModelingSessionService::beginTransformDrag(const ModelingTransformDragBeginRequest& request) {
    return impl_->beginTransformDrag(request);
}

ModelingTransformDragPreview ModelingSessionService::updateTransformDrag(const ModelingTransformDragUpdateRequest& request) {
    return impl_->updateTransformDrag(request);
}

ModelingTransformDragPreview ModelingSessionService::commitTransformDrag() {
    return impl_->commitTransformDrag();
}

ModelingCommandResult ModelingSessionService::cancelTransformDrag() {
    return impl_->cancelTransformDrag();
}

ModelingSelectionSummary ModelingSessionService::pickSelect(const ModelingPickSelectRequest& request) {
    return impl_->pickSelect(request);
}

ModelingCommandResult ModelingSessionService::updateSelectionHover(const ModelingHoverRequest& request) {
    return impl_->updateSelectionHover(request);
}

ModelingHoverSummary ModelingSessionService::selectionHoverSummary() const {
    return impl_->selectionHoverSummary();
}

ModelingHoverSummary ModelingSessionService::previewSelectionHover(const ModelingHoverRequest& request) const {
    return impl_->previewSelectionHover(request);
}

ModelingCommandResult ModelingSessionService::clearSelectionHover() {
    return impl_->clearSelectionHover();
}

ModelingCommandResult ModelingSessionService::activateViewportTool(ModelingViewportTool tool) {
    return impl_->activateViewportTool(tool);
}

ModelingCommandResult ModelingSessionService::cancelActiveModalTool() {
    return impl_->cancelActiveModalTool();
}

ModelingCommandResult ModelingSessionService::updateInsertEdgeLoopHover(const ModelingToolRayEvent& event) {
    return impl_->updateInsertEdgeLoopHover(event);
}

ModelingCommandResult ModelingSessionService::beginInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
    return impl_->beginInsertEdgeLoopDrag(event);
}

ModelingCommandResult ModelingSessionService::updateInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
    return impl_->updateInsertEdgeLoopDrag(event);
}

ModelingCommandResult ModelingSessionService::commitInsertEdgeLoopDrag() {
    return impl_->commitInsertEdgeLoopDrag();
}

ModelingCommandResult ModelingSessionService::updateKnifeHover(const ModelingToolRayEvent& event) {
    return impl_->updateKnifeHover(event);
}

ModelingCommandResult ModelingSessionService::acceptKnifePoint(const ModelingToolRayEvent& event) {
    return impl_->acceptKnifePoint(event);
}

ModelingCommandResult ModelingSessionService::commitKnifeTool() {
    return impl_->commitKnifeTool();
}

ModelingCommandResult ModelingSessionService::updateCutHover(const ModelingToolRayEvent& event) {
    return impl_->updateCutHover(event);
}

ModelingCommandResult ModelingSessionService::clickCut(const ModelingToolRayEvent& event) {
    return impl_->clickCut(event);
}

ModelingCommandResult ModelingSessionService::beginCutPointDrag(int pointIndex) {
    return impl_->beginCutPointDrag(pointIndex);
}

ModelingCommandResult ModelingSessionService::beginCutLineDrag(const ModelingToolRayEvent& event) {
    return impl_->beginCutLineDrag(event);
}

ModelingCommandResult ModelingSessionService::endCutDrag() {
    return impl_->endCutDrag();
}

ModelingCommandResult ModelingSessionService::setCutMode(ModelingCutMode mode) {
    return impl_->setCutMode(mode);
}

ModelingCommandResult ModelingSessionService::cycleCutMode() {
    return impl_->cycleCutMode();
}

ModelingCommandResult ModelingSessionService::finalizeCutTool() {
    return impl_->finalizeCutTool();
}

ModelingCommandResult ModelingSessionService::updateMirrorHover(const ModelingToolRayEvent& event) {
    return impl_->updateMirrorHover(event);
}

ModelingCommandResult ModelingSessionService::clickMirror(const ModelingToolRayEvent& event) {
    return impl_->clickMirror(event);
}

ModelingCommandResult ModelingSessionService::beginMirrorPointDrag(int pointIndex) {
    return impl_->beginMirrorPointDrag(pointIndex);
}

ModelingCommandResult ModelingSessionService::beginMirrorLineDrag(const ModelingToolRayEvent& event) {
    return impl_->beginMirrorLineDrag(event);
}

ModelingCommandResult ModelingSessionService::endMirrorDrag() {
    return impl_->endMirrorDrag();
}

ModelingCommandResult ModelingSessionService::finalizeMirrorTool() {
    return impl_->finalizeMirrorTool();
}

ModelingCommandResult ModelingSessionService::updatePolyHover(const ModelingToolRayEvent& event) {
    return impl_->updatePolyHover(event);
}

ModelingCommandResult ModelingSessionService::acceptPolyPoint() {
    return impl_->acceptPolyPoint();
}

ModelingCommandResult ModelingSessionService::commitPolyTool() {
    return impl_->commitPolyTool();
}

ModelingCommandResult ModelingSessionService::activateBoxTool() {
    return impl_->activateBoxTool();
}

ModelingCreateBoxResult ModelingSessionService::createBoxFromBounds(const ModelingCreateBoxRequest& request) {
    return impl_->createBoxFromBounds(request);
}

ModelingBoxPreview ModelingSessionService::updateBoxHover(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxHover(event);
}

ModelingBoxPreview ModelingSessionService::beginBoxFootprint(const ModelingBoxToolEvent& event) {
    return impl_->beginBoxFootprint(event);
}

ModelingBoxPreview ModelingSessionService::updateBoxFootprint(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxFootprint(event);
}

ModelingBoxPreview ModelingSessionService::beginBoxHeight(const ModelingBoxToolEvent& event) {
    return impl_->beginBoxHeight(event);
}

ModelingBoxPreview ModelingSessionService::updateBoxHeight(const ModelingBoxToolEvent& event) {
    return impl_->updateBoxHeight(event);
}

ModelingCreateBoxResult ModelingSessionService::commitBox(std::string name) {
    return impl_->commitBox(std::move(name));
}

ModelingCommandResult ModelingSessionService::cancelBox() {
    return impl_->cancelBox();
}

#if QUADER_HAS_MODELING_SDK
ModelingCreateBoxResult ModelingSessionService::createMesh(const ModelingCreateMeshRequest& request) {
    return impl_->createMesh(request);
}
#endif

ModelingCommandResult ModelingSessionService::setObjectSelection(std::span<const EntityId> entities, EntityId active) {
    return impl_->setObjectSelection(entities, active);
}

ModelingCommandResult ModelingSessionService::setComponentSelection(const ModelingComponentSelectionRequest& request) {
    return impl_->setComponentSelection(request);
}

ModelingCommandResult ModelingSessionService::moveSelection(Vec3 delta) {
    return impl_->moveSelection(delta);
}

ModelingCommandResult ModelingSessionService::transformSelection(const ModelingTransform3& transform, Vec3 pivot) {
    return impl_->transformSelection(transform, pivot);
}

ModelingCommandResult ModelingSessionService::assignMaterialSlotToSelection(std::uint32_t materialSlot) {
    return impl_->assignMaterialSlotToSelection(materialSlot);
}

ModelingCommandResult ModelingSessionService::assignMaterialSlotToFace(
        int sdkObjectId,
        int faceId,
        std::uint32_t materialSlot) {
    return impl_->assignMaterialSlotToFace(sdkObjectId, faceId, materialSlot);
}

ModelingCommandResult ModelingSessionService::setSelectionHover(const ModelingDirectHoverRequest& request) {
    return impl_->setSelectionHover(request);
}

} // namespace quader
