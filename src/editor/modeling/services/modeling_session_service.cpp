////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/i_undoable_command.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/commands/modeling/scene_aware_delete_selection_command.hpp"
#include "editor/modeling/services/modeling_hierarchy_duplicate_service.hpp"
#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_session_services.hpp"
#include "editor/modeling/services/selection_mode_adapter.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "editor/selection/selection_projection_service.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <editor/modeling/operations/operation_command.hpp>
#include <editor/modeling/runtime/repeat_last_action.hpp>
#include <editor/modeling/runtime/runtime_command.hpp>
#include <editor/modeling/runtime/session.hpp>
#include <editor/interactions/box_tool.hpp>
#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/mirror_tool.hpp>
#include <editor/interactions/move_tool.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <editor/interactions/rotate_tool.hpp>
#include <editor/interactions/scale_tool.hpp>
#include <editor/interactions/selection_edit.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/metadata/host_contract.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

quader::QVec3 toRuntime(QVec3 value) {
    return {value.x, value.y, value.z};
}

quader::QVec2 toRuntime(QVec2 value) {
    return {value.x, value.y};
}

quader_poly::Ray toRuntime(const ModelingRay& ray) {
    return {
            .origin = toRuntime(ray.origin),
            .direction = toRuntime(normalize(ray.direction)),
    };
}

quader_editor_core::ViewportCameraState toRuntime(const ModelingViewportCamera& value) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = value.orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = {
            .x_axis = toRuntime(value.xAxis),
            .y_axis = toRuntime(value.yAxis),
            .z_axis = toRuntime(value.zAxis),
            .origin = toRuntime(value.origin),
    };
    camera.viewport_size = {
            std::max(value.viewportSize.x, 1.0f),
            std::max(value.viewportSize.y, 1.0f),
    };
    camera.orthographic_size = std::max(value.orthographicSize, 0.0001);
    camera.fov_degrees = value.fovDegrees;
    return camera;
}

quader_editor_core::SelectionEditOperation toRuntime(ModelingSelectionEdit edit) {
    switch (edit) {
    case ModelingSelectionEdit::Add:
        return quader_editor_core::SelectionEditOperation::add;
    case ModelingSelectionEdit::Remove:
        return quader_editor_core::SelectionEditOperation::remove;
    case ModelingSelectionEdit::Toggle:
        return quader_editor_core::SelectionEditOperation::toggle;
    case ModelingSelectionEdit::Replace:
        return quader_editor_core::SelectionEditOperation::replace;
    }
    return quader_editor_core::SelectionEditOperation::replace;
}

quader_poly::Transform3 toRuntime(const ModelingTransform3& transform) {
    return {
            .x_axis = toRuntime(transform.xAxis),
            .y_axis = toRuntime(transform.yAxis),
            .z_axis = toRuntime(transform.zAxis),
            .origin = toRuntime(transform.origin),
    };
}

QMat4 matrixFromRuntimeTransform(const quader_poly::Transform3& transform) {
    glm::mat4 matrix{1.0f};
    matrix[0] = glm::vec4{toGlm(transform.x_axis), 0.0f};
    matrix[1] = glm::vec4{toGlm(transform.y_axis), 0.0f};
    matrix[2] = glm::vec4{toGlm(transform.z_axis), 0.0f};
    matrix[3] = glm::vec4{toGlm(transform.origin), 1.0f};
    return fromGlm(matrix);
}

quader_editor_core::ViewportInteractionTool toRuntime(ModelingViewportTool tool) {
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

ModelingViewportTool fromRuntimeViewportTool(quader_editor_core::ViewportInteractionTool tool) {
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

CommandResult modelingServiceCommandResultFrom(ModelingCommandResult result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = std::move(result.message),
    };
}

std::string transformDragHistoryLabel(quader_editor_core::ViewportInteractionTool tool) {
    switch (tool) {
    case quader_editor_core::ViewportInteractionTool::move:
        return "Move Selection";
    case quader_editor_core::ViewportInteractionTool::rotate:
        return "Rotate Selection";
    case quader_editor_core::ViewportInteractionTool::scale:
        return "Scale Selection";
    default:
        return "Transform Selection";
    }
}

/**
 * Records an already-committed modeling preview as an editor history command.
 */
class RecordedModelingPreviewMementoCommand final : public IUndoableCommand {
public:
    using Restore = std::function<ModelingCommandResult(const ModelingSessionMemento&)>;

    RecordedModelingPreviewMementoCommand(
            Restore restore,
            std::string label,
            ModelingSessionMemento before,
            ModelingSessionMemento after)
        : restore_(std::move(restore))
        , label_(std::move(label))
        , before_(std::move(before))
        , after_(std::move(after)) {}

    [[nodiscard]] const std::string& label() const override {
        return label_;
    }

    CommandResult undo() override {
        return modelingServiceCommandResultFrom(restore_(before_));
    }

    CommandResult redo() override {
        return modelingServiceCommandResultFrom(restore_(after_));
    }

private:
    Restore restore_;
    std::string label_;
    ModelingSessionMemento before_;
    ModelingSessionMemento after_;
};

quader_editor_core::CutMode toRuntime(ModelingCutMode mode) {
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

ModelingCutMode fromRuntime(quader_editor_core::CutMode mode) {
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

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

ModelingTransform3 fromRuntime(quader_poly::Transform3 value) {
    return {
            .xAxis = fromRuntime(value.x_axis),
            .yAxis = fromRuntime(value.y_axis),
            .zAxis = fromRuntime(value.z_axis),
            .origin = fromRuntime(value.origin),
    };
}

quader_editor_core::TransformAxis toRuntime(ModelingMirrorAxis axis) {
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

quader_editor_core::TransformAxis toRuntime(ModelingTransformAxis axis) {
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

ModelingTransformAxis fromRuntime(quader_editor_core::TransformAxis axis) {
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

quader_editor_core::ViewportInteractionTool toRuntime(ModelingTransformTool tool) {
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

ModelingTransformTool fromRuntime(quader_editor_core::ViewportInteractionTool tool) {
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

quader_editor_core::TransformDragOperation toRuntime(ModelingTransformOperation operation) {
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

ModelingTransformOperation fromRuntime(quader_editor_core::TransformDragOperation operation) {
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

quader_editor_core::ViewportFlipOrientation toRuntime(ModelingFlipOrientation orientation) {
    switch (orientation) {
    case ModelingFlipOrientation::Horizontal:
        return quader_editor_core::ViewportFlipOrientation::horizontal;
    case ModelingFlipOrientation::Vertical:
        return quader_editor_core::ViewportFlipOrientation::vertical;
    }
    return quader_editor_core::ViewportFlipOrientation::horizontal;
}

ModelingActiveSelectionKind fromRuntime(quader_editor_core::ActiveSelectionKind kind) {
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

/**
 * Stores a validated polygon component selection edit before it is committed into runtime selection state.
 */
struct ComponentSelectionEditResult {
    bool ok = true;
    quader_poly::Selection selection;
    std::string_view message;
};

bool polygonEdgeExists(const quader_poly::Document& document, quader_poly::Edge edge) {
    if (edge.a == quader_poly::invalid_element_id || edge.b == quader_poly::invalid_element_id || edge.a == edge.b) {
        return false;
    }
    for (const quader_poly::Face& face : document.faces) {
        if (face.vertices.size() < 2U) {
            continue;
        }
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const quader_poly::ElementId a = face.vertices[index];
            const quader_poly::ElementId b = face.vertices[(index + 1U) % face.vertices.size()];
            if ((a == edge.a && b == edge.b) || (a == edge.b && b == edge.a)) {
                return true;
            }
        }
    }
    return false;
}

template <typename Element>
void applyComponentSelectionValue(std::vector<Element>& values, Element value, ModelingSelectionEdit edit) {
    const auto found = std::find(values.begin(), values.end(), value);
    const bool selected = found != values.end();
    if (edit == ModelingSelectionEdit::Remove || (edit == ModelingSelectionEdit::Toggle && selected)) {
        if (selected) {
            values.erase(found);
        }
        return;
    }
    if (!selected) {
        values.push_back(value);
    }
}

ComponentSelectionEditResult validatedComponentSelectionEdit(
        const quader_editor_core::PolygonObject& object,
        const ModelingComponentSelectionRequest& request,
        quader_editor_core::SelectionMode mode,
        const quader_poly::Selection* currentSelection) {
    ComponentSelectionEditResult result;
    if (currentSelection != nullptr) {
        result.selection = *currentSelection;
    }
    result.selection.mode = quader_editor_core::component_selection_mode(mode);

    for (const int vertexId : request.vertices) {
        if (vertexId <= 0) {
            continue;
        }
        const auto id = static_cast<quader_poly::ElementId>(vertexId);
        if (quader_poly::find_vertex(object.document, id) == nullptr) {
            return {.ok = false, .message = "Selection vertex was not found."};
        }
        applyComponentSelectionValue(result.selection.vertices, id, request.edit);
    }

    for (const std::pair<int, int>& edgeIds : request.edges) {
        if (edgeIds.first <= 0 || edgeIds.second <= 0) {
            continue;
        }
        const quader_poly::Edge edge = quader_poly::make_edge(
                static_cast<quader_poly::ElementId>(edgeIds.first),
                static_cast<quader_poly::ElementId>(edgeIds.second));
        if (!polygonEdgeExists(object.document, edge)) {
            return {.ok = false, .message = "Selection edge was not found."};
        }
        applyComponentSelectionValue(result.selection.edges, edge, request.edit);
    }

    for (const int faceId : request.faces) {
        if (faceId <= 0) {
            continue;
        }
        const auto id = static_cast<quader_poly::ElementId>(faceId);
        if (quader_poly::find_face(object.document, id) == nullptr) {
            return {.ok = false, .message = "Selection face was not found."};
        }
        applyComponentSelectionValue(result.selection.faces, id, request.edit);
    }

    result.selection.has_active = false;
    if (request.activeKind == ModelingActiveSelectionKind::Vertex && request.activeVertex > 0) {
        const auto active = static_cast<quader_poly::ElementId>(request.activeVertex);
        if (quader_poly::find_vertex(object.document, active) == nullptr) {
            return {.ok = false, .message = "Active selection vertex was not found."};
        }
        if (std::find(result.selection.vertices.begin(), result.selection.vertices.end(), active) !=
                result.selection.vertices.end()) {
            result.selection.has_active = true;
            result.selection.active_kind = quader_poly::ElementKind::vertex;
            result.selection.active_vertex = active;
        }
    } else if (request.activeKind == ModelingActiveSelectionKind::Edge &&
            request.activeEdge.first > 0 &&
            request.activeEdge.second > 0) {
        const quader_poly::Edge active = quader_poly::make_edge(
                static_cast<quader_poly::ElementId>(request.activeEdge.first),
                static_cast<quader_poly::ElementId>(request.activeEdge.second));
        if (!polygonEdgeExists(object.document, active)) {
            return {.ok = false, .message = "Active selection edge was not found."};
        }
        if (std::find(result.selection.edges.begin(), result.selection.edges.end(), active) !=
                result.selection.edges.end()) {
            result.selection.has_active = true;
            result.selection.active_kind = quader_poly::ElementKind::edge;
            result.selection.active_edge = active;
        }
    } else if (request.activeKind == ModelingActiveSelectionKind::Face && request.activeFace > 0) {
        const auto active = static_cast<quader_poly::ElementId>(request.activeFace);
        if (quader_poly::find_face(object.document, active) == nullptr) {
            return {.ok = false, .message = "Active selection face was not found."};
        }
        if (std::find(result.selection.faces.begin(), result.selection.faces.end(), active) !=
                result.selection.faces.end()) {
            result.selection.has_active = true;
            result.selection.active_kind = quader_poly::ElementKind::face;
            result.selection.active_face = active;
        }
    }

    return result;
}

ModelingHoverTarget fromRuntime(const quader_editor_core::SelectionHoverState& hover) {
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

ModelingHoverSummary fromRuntimeHoverSummary(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::SelectionHoverState& hover,
        bool changed) {
    return {
            .changed = changed,
            .target = fromRuntime(hover),
            .targetSelected = quader_editor_core::selection_hover_target_matches_selection(
                    session.objects(),
                    session.selection_mode(),
                    hover),
    };
}

ModelingBoxDragState fromRuntime(quader_editor_core::BoxDragState state) {
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

const quader_editor_core::PolygonObject* findObject(
        const quader_editor_core::EditorSession& session,
        int modelingObjectId) {
    const auto& objects = session.objects();
    const auto it = std::find_if(objects.begin(), objects.end(), [modelingObjectId](const auto& object) {
        return object.id == modelingObjectId;
    });
    return it == objects.end() ? nullptr : &(*it);
}

quader_editor_core::PolygonObject* findObject(
        quader_editor_core::EditorSession& session,
        int modelingObjectId) {
    auto& objects = session.objects();
    const auto it = std::find_if(objects.begin(), objects.end(), [modelingObjectId](const auto& object) {
        return object.id == modelingObjectId;
    });
    return it == objects.end() ? nullptr : &(*it);
}

bool runtimeSelectionMatches(const quader_poly::Selection& left, const quader_poly::Selection& right) {
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

quader_poly::PickOptions pickOptionsFromRequest(const ModelingHoverRequest& request) {
    return quader_editor_core::selection_pick_options_from_radii(request.vertexRadius, request.edgeRadius);
}

ModelingBoxPreview fromRuntime(
        const quader_editor_core::BoxPreviewDescriptor& preview,
        const std::vector<quader::QVec3>& corners = {}) {
    ModelingBoxPreview result;
    result.ok = preview.ok;
    result.valid = preview.ok && preview.footprint.footprint.size() >= 4U && !preview.bounds.empty;
    result.dragState = fromRuntime(preview.drag_state);
    result.message = preview.message;
    result.normal = fromRuntime(preview.normal);
    result.distance = preview.distance;
    result.boundsEmpty = preview.bounds.empty;
    result.bounds = {
            .min = fromRuntime(preview.bounds.min),
            .max = fromRuntime(preview.bounds.max),
    };

    result.footprintCount = std::min<std::size_t>(result.footprint.size(), preview.footprint.footprint.size());
    for (std::size_t index = 0; index < result.footprintCount; ++index) {
        result.footprint[index] = fromRuntime(preview.footprint.footprint[index]);
    }

    result.cornerCount = std::min<std::size_t>(result.corners.size(), corners.size());
    for (std::size_t index = 0; index < result.cornerCount; ++index) {
        result.corners[index] = fromRuntime(corners[index]);
    }
    return result;
}

ModelingCommandDescriptor makeBoxToolDescriptor() {
    const ToolDescriptor* tool = findViewportToolDescriptor(ModelingViewportTool::Box);
    return ModelingCommandDescriptor{
            .command = ModelingCommandId::ActivateBoxTool,
            .id = tool != nullptr ? std::string{tool->id} : std::string{},
            .label = tool != nullptr ? std::string{tool->label} : std::string{},
            .shortcut = tool != nullptr ? std::string{tool->shortcut} : std::string{},
            .viewportScoped = tool != nullptr && tool->ownsViewport,
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

quader_editor_core::EditorRuntimeCommand noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId commandId) {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = commandId;
    return command;
}


} // namespace

/**
 * Stores the private runtime session state for a Modeling Session Memento.
 */
struct ModelingSessionMemento::Impl {
    Impl(
            quader_editor_core::SessionState snapshot,
            std::unordered_map<int, ModelingObjectSceneIdentity> sceneIdentitySnapshot)
        : state(std::move(snapshot))
        , sceneIdentities(std::move(sceneIdentitySnapshot)) {}

    quader_editor_core::SessionState state;
    std::unordered_map<int, ModelingObjectSceneIdentity> sceneIdentities;
};

ModelingSessionMemento::ModelingSessionMemento() = default;
ModelingSessionMemento::~ModelingSessionMemento() = default;

ModelingSessionMemento::ModelingSessionMemento(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSessionMemento::valid() const {
    return static_cast<bool>(impl_);
}

/**
 * Stores private runtime selection state for a modeling box-selection baseline.
 */
struct ModelingSelectionBoxBaseline::Impl {
    explicit Impl(quader_editor_core::RectangleSelectionState selection)
        : selection(std::move(selection)) {}

    quader_editor_core::RectangleSelectionState selection;
};

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline() = default;
ModelingSelectionBoxBaseline::~ModelingSelectionBoxBaseline() = default;

ModelingSelectionBoxBaseline::ModelingSelectionBoxBaseline(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool ModelingSelectionBoxBaseline::valid() const {
    return static_cast<bool>(impl_);
}

/**
 * Owns Impl behavior within the modeling application service layer.
 */
class ModelingSessionService::Impl {
public:
    Impl(SceneDocument& scene, AssetRegistry& assets)
        : scene_(scene), assets_(assets), sceneSyncService_(objectRegistry_) {}

    [[nodiscard]] ModelingCommandDescriptor boxToolCommand() const {
        return makeBoxToolDescriptor();
    }

    [[nodiscard]] ModelingCommandDescriptor createBoxFromBoundsCommand() const {
        return makeCreateBoxDescriptor();
    }

    [[nodiscard]] std::vector<ModelingObjectRecord> objects() const {
        return objectRegistry_.records(scene_);
    }

    [[nodiscard]] std::optional<EntityId> entityForModelingObject(int modelingObjectId) const {
        return objectRegistry_.entityForModelingObject(modelingObjectId);
    }

    [[nodiscard]] std::optional<int> modelingObjectForEntity(EntityId entity) const {
        return objectRegistry_.modelingObjectForEntity(entity);
    }

    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int modelingObjectId) const {
        const quader_editor_core::PolygonObject* object = findObject(session_, modelingObjectId);
        if (object == nullptr) {
            return std::nullopt;
        }
        return meshSyncService_.compileObjectMesh(*object);
    }

    [[nodiscard]] std::optional<EntityId> pickObjectEntity(const ModelingPickSelectRequest& request) const {
        const std::optional<int> objectId =
                selectionService_.pickObjectId(session_, scene_, objectRegistry_, request);
        if (!objectId.has_value()) {
            return std::nullopt;
        }
        return objectRegistry_.entityForModelingObject(*objectId);
    }

    [[nodiscard]] std::vector<EntityId> boxPickObjectEntities(const ModelingBoxSelectRequest& request) const {
        const std::vector<int> objectIds =
                selectionService_.boxPickObjectIds(session_, scene_, objectRegistry_, request);
        std::vector<EntityId> entities;
        entities.reserve(objectIds.size());
        for (const int objectId : objectIds) {
            const std::optional<EntityId> entity = objectRegistry_.entityForModelingObject(objectId);
            if (!entity.has_value() ||
                    std::find(entities.begin(), entities.end(), *entity) != entities.end()) {
                continue;
            }
            entities.push_back(*entity);
        }
        return entities;
    }

    [[nodiscard]] std::optional<QVec3> selectionCenter() const {
        return selectionProjectionService_.worldSelectionCenter(session_, scene_, assets_, objectRegistry_);
    }

    [[nodiscard]] ModelingSelectionMode selectionMode() const {
        if (appSelectionMode_ == ModelingSelectionMode::Group) {
            return ModelingSelectionMode::Group;
        }
        return runtimeSelectionModeToApp(session_.selection_mode());
    }

    [[nodiscard]] ModelingSelectionSummary selectionSummary() const {
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
                .mode = runtimeSelectionModeToApp(session_.selection_mode()),
                .hasSelection = session_.has_selection(),
                .activeKind = fromRuntime(active.kind),
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
    }

    [[nodiscard]] ModelingSelectionBoxBaseline captureBoxSelectionBaseline() const {
        return ModelingSelectionBoxBaseline(std::make_shared<ModelingSelectionBoxBaseline::Impl>(
                selectionService_.rectangleSelectionState(session_)));
    }

    [[nodiscard]] std::uint64_t selectionOverlaySignature() const {
        return overlayService_.selectionOverlaySignature(runtime_);
    }

    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const {
        return overlayService_.selectionOverlayFrame(
                session_,
                runtime_,
                scene_,
                objectRegistry_,
                cachedSceneTransformedObjectsForPicking());
    }

    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const {
        return overlayService_.toolPreviewMeshes(
                runtime_,
                scene_,
                objectRegistry_,
                cachedSceneTransformedObjectsForPicking());
    }

    [[nodiscard]] static std::unordered_set<int> liveModelingObjectIds(
            const std::vector<quader_editor_core::PolygonObject>& objects) {
        std::unordered_set<int> ids;
        ids.reserve(objects.size());
        for (const quader_editor_core::PolygonObject& object : objects) {
            ids.insert(object.id);
        }
        return ids;
    }

    [[nodiscard]] ModelingSessionMemento captureHistoryMementoForState(
            const quader_editor_core::SessionState& state) const {
        const std::unordered_set<int> liveIds = liveModelingObjectIds(state.objects);
        return ModelingSessionMemento(std::make_shared<ModelingSessionMemento::Impl>(
                state,
                objectRegistry_.captureSceneIdentitiesForModelingObjects(scene_, liveIds)));
    }

    void refreshLiveSceneIdentities() {
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            const std::optional<EntityId> entity = objectRegistry_.entityForModelingObject(object.id);
            if (entity.has_value() && scene_.exists(*entity)) {
                objectRegistry_.rememberSceneEntity(object.id, scene_, *entity);
            }
        }
    }

    [[nodiscard]] ModelingSessionMemento captureHistoryMemento() const {
        return captureHistoryMementoForState(session_.state());
    }

    [[nodiscard]] ModelingSessionMemento captureActivePreviewHistoryMemento() const {
        const std::optional<quader_editor_core::SessionState> state =
                session_.preview_transaction_history_state();
        return state.has_value() ? captureHistoryMementoForState(*state) : ModelingSessionMemento();
    }

    ModelingCommandResult restoreHistoryMemento(const ModelingSessionMemento& memento) {
        if (!memento.valid()) {
            return {
                    .ok = false,
                    .changed = false,
                    .message = "Modeling history restore point is invalid.",
            };
        }

        quader_editor_core::SessionState state = memento.impl_->state;
        const quader_editor_core::SessionState& current = session_.state();
        state.content_revision = std::max(state.content_revision, current.content_revision + 1U);
        state.selection_revision = std::max(state.selection_revision, current.selection_revision + 1U);
        objectRegistry_.restoreSceneIdentities(memento.impl_->sceneIdentities);
        session_.set_state(std::move(state));
        session_.clear_history();
        syncSceneObjectsFromModeling(true);
        objectRegistry_.restoreSceneIdentities(memento.impl_->sceneIdentities);
        refreshLiveSceneIdentities();
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return {
                .ok = true,
                .changed = true,
        };
    }

    void resetFromScene() {
        objectRegistry_.clear();
        quader_editor_core::SessionState state;
        state.selection_mode = quader_editor_core::SelectionMode::object;
        const EntityId activeEntity = scene_.primarySelection();

        for (const EntityId entity : scene_.entityTreeOrder()) {
            const ModelingObjectComponent* modelingObject = scene_.modelingObject(entity);
            const MeshComponent* meshComponent = scene_.mesh(entity);
            if (modelingObject == nullptr ||
                    modelingObject->objectId == InvalidModelingObjectId ||
                    meshComponent == nullptr ||
                    meshComponent->mesh == InvalidAssetId) {
                continue;
            }

            const MeshAsset* mesh = assets_.mesh(meshComponent->mesh);
            if (mesh == nullptr || !mesh->hasAuthoredPolygonDocument || !mesh->authoredPolygonDocument) {
                continue;
            }

            quader_editor_core::PolygonObject object;
            object.id = modelingObject->objectId;
            object.material_id = static_cast<int>(modelingObject->materialSlot);
            object.selected = scene_.selection(entity) != nullptr && scene_.selection(entity)->selected;
            object.document = *mesh->authoredPolygonDocument;
            state.objects.push_back(std::move(object));
            objectRegistry_.bind(modelingObject->objectId, entity);
            objectRegistry_.rememberSceneEntity(modelingObject->objectId, scene_, entity);
            if (entity == activeEntity) {
                state.active_object_id = modelingObject->objectId;
            }
        }

        state.next_object_id = quader_editor_core::next_polygon_object_id(state.objects, 1);
        state.content_revision = state.objects.empty() ? 0U : 1U;
        state.selection_revision = 0U;
        session_.set_state(std::move(state));
        session_.mark_clean();
        session_.clear_history();
        runtime_.reset_document_state(session_);
        sceneSyncService_.markSceneObjectsSynced(session_.state().content_revision);
        (void)selectionProjectionService_.projectObjectSelectionFromScene(
                session_,
                scene_,
                objectRegistry_,
                true);
        session_.mark_clean();
        invalidateSelectionOverlayCaches();
    }

    void syncObjectSelectionFromScene(bool forceObjectMode = false) {
        QDR_PROFILE_FUNCTION();
        if (!selectionProjectionService_.shouldProjectObjectSelectionFromScene(session_, forceObjectMode)) {
            return;
        }
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
        }

        selectionProjectionService_.projectObjectSelectionFromScene(
                session_,
                scene_,
                objectRegistry_,
                forceObjectMode);
    }

    void promoteDrawnPlaneToolObjectSelection() {
        QDR_PROFILE_FUNCTION();
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
        }

        if (selectionProjectionService_.promoteObjectSelectionForTool(session_, scene_, objectRegistry_)) {
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
        }
    }

    void syncSceneSelectionFromModeling() {
        QDR_PROFILE_FUNCTION();
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
        }
        selectionProjectionService_.projectSceneSelectionFromModeling(session_, scene_, objectRegistry_);
    }

    void restoreSceneSelection(const std::vector<EntityId>& selected, EntityId active) {
        selectionProjectionService_.restoreSceneSelection(scene_, selected, active);
    }

    [[nodiscard]] std::vector<ModelingObjectSceneIdentity> captureSelectedDuplicateSceneIdentities() const {
        QDR_PROFILE_FUNCTION();
        const std::vector<int> selectedObjectIds = session_.selected_object_ids();
        return objectRegistry_.captureSceneIdentitiesForModelingObjects(
                scene_,
                std::span<const int>(selectedObjectIds.data(), selectedObjectIds.size()));
    }

    void applySceneIdentitiesToSelectedDuplicates(const std::vector<ModelingObjectSceneIdentity>& sourceIdentities) {
        QDR_PROFILE_FUNCTION();
        if (sourceIdentities.empty()) {
            return;
        }

        const std::vector<int> selectedDuplicateObjectIds = session_.selected_object_ids();
        if (selectedDuplicateObjectIds.size() != sourceIdentities.size()) {
            return;
        }
        const std::unordered_set<int> duplicateObjectSet(
                selectedDuplicateObjectIds.begin(),
                selectedDuplicateObjectIds.end());
        std::vector<int> duplicateObjectIds;
        duplicateObjectIds.reserve(selectedDuplicateObjectIds.size());
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (duplicateObjectSet.contains(object.id)) {
                duplicateObjectIds.push_back(object.id);
            }
        }
        if (duplicateObjectIds.size() != sourceIdentities.size()) {
            return;
        }

        for (std::size_t index = 0; index < sourceIdentities.size(); ++index) {
            const std::optional<EntityId> duplicate = objectRegistry_.entityForModelingObject(duplicateObjectIds[index]);
            if (!duplicate.has_value() || !scene_.exists(*duplicate)) {
                continue;
            }

            meshSyncService_.applySceneIdentityToDuplicateEntity(scene_, *duplicate, sourceIdentities[index]);
            objectRegistry_.rememberSceneEntity(duplicateObjectIds[index], scene_, *duplicate);
        }
    }

    ModelingCommandResult duplicateSelectedHierarchy(
            DocumentCommandService& documentCommands,
            ModelingSelectionMode resultSelectionMode) {
        return hierarchyDuplicateService_.duplicateSelectedEntityHierarchy(
                session_,
                scene_,
                objectRegistry_,
                documentCommands,
                ModelingHierarchyDuplicateRuntime{
                        .ensureSceneObjectsSynced = [&]() {
                            if (!sceneObjectsSyncedForCurrentModelingContent()) {
                                syncSceneObjectsFromModeling();
                            }
                        },
                        .setSelectionMode = [&](ModelingSelectionMode mode) {
                            return setSelectionMode(mode);
                        },
                        .projectObjectSelectionFromScene = [&]() {
                            syncObjectSelectionFromScene();
                        },
                        .duplicateSelectedMeshes = [&]() {
                            return executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes, false);
                        },
                },
                resultSelectionMode);
    }

    ModelingCommandResult duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands) {
        return duplicateSelectedHierarchy(documentCommands, ModelingSelectionMode::Group);
    }

    ModelingCommandResult duplicateSelectedEntityHierarchy(DocumentCommandService& documentCommands) {
        return duplicateSelectedHierarchy(documentCommands, ModelingSelectionMode::Object);
    }

    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode) {
        if (mode == ModelingSelectionMode::Group) {
            const quader_editor_core::SelectionMode runtimeMode = appSelectionModeToRuntime(mode);
            const std::vector<int> selectedObjectIds = session_.selected_object_ids();
            const bool hadRuntimeObjectSelection = !selectedObjectIds.empty() || session_.active_object_id() > 0;
            const bool changed = appSelectionMode_ != ModelingSelectionMode::Group ||
                    session_.selection_mode() != runtimeMode ||
                    hadRuntimeObjectSelection;
            appSelectionMode_ = ModelingSelectionMode::Group;
            if (session_.selection_mode() != runtimeMode) {
                session_.set_selection_mode(runtimeMode);
                invalidateSelectionOverlayCaches();
            }
            if (hadRuntimeObjectSelection) {
                session_.set_selected_object_ids({}, -1);
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
        settings.selection_mode = static_cast<int>(appSelectionModeToRuntime(mode));

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_selection_mode;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        invalidateSelectionOverlayCaches();
        if (mode == ModelingSelectionMode::Object && !sceneOnlySelection.empty()) {
            syncObjectSelectionFromScene();
        }
        syncSceneSelectionFromModeling();
        selectionProjectionService_.addSceneSelection(scene_, sceneOnlySelection, activeSceneOnlySelection);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

    ModelingCommandResult selectAll() {
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::select_all, false);
    }

    ModelingCommandResult invertSelection() {
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::invert_selection, false);
    }

    ModelingCommandResult clearSelection() {
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::clear_selection, false);
    }

    ModelingCommandResult deleteComponentSelection() {
        const ModelingSelectionSummary summary = selectionSummary();
        if (sceneAwareDeleteUsesDocumentRoute(summary.mode)) {
            return {
                    .ok = false,
                    .changed = false,
                    .message = "Delete selection must use the scene-aware command route.",
            };
        }
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::delete_selection, true);
    }

    [[nodiscard]] bool hasSelectedSceneAwareDeleteRoot() const {
        return sceneAwareDeleteHasSelectedRoot(scene_);
    }

    std::unique_ptr<IUndoableCommand> createDeleteSelectionCommand() {
        return createSceneAwareDeleteSelectionCommand(
                scene_,
                SceneAwareDeleteRuntime{
                        .captureModelingMemento = [&]() {
                            return captureHistoryMemento();
                        },
                        .restoreModelingMemento = [&](const ModelingSessionMemento& memento) {
                            return restoreHistoryMemento(memento);
                        },
                        .appSelectionMode = [&]() {
                            return appSelectionMode_;
                        },
                        .setAppSelectionMode = [&](ModelingSelectionMode mode) {
                            appSelectionMode_ = mode;
                        },
                        .deleteModelingObjects = [&](const std::vector<int>& modelingObjectIds) {
                            if (modelingObjectIds.empty()) {
                                return ModelingCommandResult{.ok = true, .changed = false};
                            }
                            session_.set_selection_mode(quader_editor_core::SelectionMode::object);
                            session_.set_selected_object_ids(modelingObjectIds, modelingObjectIds.front());
                            return executeRuntimeCommand(
                                    noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::delete_selection),
                                    true);
                        },
                        .entityForModelingObject = [&](int modelingObjectId) {
                            return objectRegistry_.entityForModelingObject(modelingObjectId);
                        },
                        .syncObjectSelectionFromScene = [&](bool forceObjectMode) {
                            syncObjectSelectionFromScene(forceObjectMode);
                        },
                        .clearRuntimeObjectSelectionForGroupMode = [&]() {
                            session_.set_selection_mode(quader_editor_core::SelectionMode::object);
                            session_.set_selected_object_ids({}, -1);
                        },
                        .invalidateSelectionOverlayCaches = [&]() {
                            invalidateSelectionOverlayCaches();
                        },
                });
    }

    ModelingCommandResult copySelection() {
        return executeRuntimeCommand(clipboardService_.makeCopyCommand(), false);
    }

    ModelingCommandResult pasteSelection() {
        const quader_editor_core::EditorRuntimeCommand command = clipboardService_.makePasteCommand();
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromModeling();
        }
        syncSceneSelectionFromModeling();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

    ModelingCommandResult repeatLastAction() {
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = historyBridge_.executeRepeatLastAction(session_);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromModeling();
        }
        syncSceneSelectionFromModeling();
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
    }

    ModelingCommandResult mirrorSelection(ModelingMirrorAxis axis) {
        quader_editor_core::MirrorAxisRuntimeCommandSettings settings;
        settings.axis = toRuntime(axis);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::mirror_axis;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromModeling();
        }
        syncSceneSelectionFromModeling();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

    ModelingCommandResult flipSelection(ModelingFlipOrientation orientation) {
        quader_editor_core::ViewFlipRuntimeCommandSettings settings;
        settings.orientation = toRuntime(orientation);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::flip_view;
        command.settings = settings;

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromModeling();
        }
        syncSceneSelectionFromModeling();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

    ModelingCommandResult applyActiveMaterial(std::uint32_t materialSlot) {
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
            syncSceneObjectsFromModeling();
        }
        syncSceneSelectionFromModeling();
        historyBridge_.recordOperation(command, result.command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

    [[nodiscard]] ModelingToolStatus toolStatus() const {
        ModelingToolStatus status;
        const quader_editor_core::EditorRuntimeStatus runtimeStatus = runtime_.status();
        status.activeTool = fromRuntimeViewportTool(runtimeStatus.active_tool);
        status.modalToolOwnsViewport = modalToolOwnsViewport();
        status.insertEdgeLoopActive = runtimeStatus.insert_edge_loop_active;
        status.insertEdgeLoopDragging = runtimeStatus.insert_edge_loop_dragging;
        status.insertEdgeLoopHasPreview = runtimeStatus.insert_edge_loop_has_preview;
        status.knifeActive = runtimeStatus.knife_active;
        status.knifeHasCurrent = runtimeStatus.knife_has_current;
        status.knifeCanCommit = runtimeStatus.knife_can_commit;
        status.cutActive = runtimeStatus.cut_active;
        status.cutCanPreview = runtimeStatus.cut_can_preview;
        status.cutPreviewValid = runtimeStatus.cut_preview_valid;
        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        const std::vector<quader::QVec3> cutOverlayPoints =
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
                    fromRuntime(cutOverlayPoints[static_cast<std::size_t>(index)]);
        }
        status.cutMode = fromRuntime(cut.mode);
        status.mirrorActive = runtimeStatus.mirror_active;
        status.mirrorCanPreview = runtimeStatus.mirror_can_preview;
        status.mirrorPreviewValid = runtimeStatus.mirror_preview_valid;
        const quader_editor_core::MirrorToolState& mirror = runtime_.mirror_state();
        const std::vector<quader::QVec3> mirrorOverlayPoints =
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
                    fromRuntime(mirrorOverlayPoints[static_cast<std::size_t>(index)]);
        }
        status.polyActive = runtimeStatus.poly_active;
        status.polyClosed = runtimeStatus.poly_closed;
        status.polyHasCurrent = runtimeStatus.poly_has_current;
        status.polyCanCommit = runtimeStatus.poly_can_commit;
        status.polyPointCount = runtimeStatus.poly_point_count;
        return status;
    }

    [[nodiscard]] ModelingToolSettingsTarget toolSettingsTarget() const {
        const ModelingToolSettingsTarget previewTarget =
                toolSettingsService_.activePreviewTarget(session_.preview_transaction_status());
        if (previewTarget.active) {
            return previewTarget;
        }
        return toolSettingsService_.activeRuntimeToolTarget(runtime_.status());
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
            ModelingCommandResult result = updateActiveOperationPreviewSettings();
            result.changed = result.changed || changed;
            return result;
        }
        return {.ok = true, .changed = changed};
    }

    [[nodiscard]] bool modalToolOwnsViewport() const {
        return modalToolRuntime_.ownsViewport(fromRuntimeViewportTool(runtime_.active_tool()));
    }

    [[nodiscard]] bool canCancelActiveToolOrPreview() const {
        return toolSettingsTarget().operationPreviewActive ||
                runtime_.transform_drag_active() ||
                hasTransformPreview() ||
                sceneTransformDragService_.previewActive() ||
                modalToolOwnsViewport();
    }

    [[nodiscard]] bool canCancelActivePreviewForUndo() const {
        return toolSettingsTarget().operationPreviewActive ||
                runtime_.transform_drag_active() ||
                hasTransformPreview() ||
                sceneTransformDragService_.previewActive();
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

    [[nodiscard]] std::optional<ModelingActionItem> contextAction(
            std::string_view actionId,
            ModelingActionQuery query) const {
        return contextActionService_.action(selectionSummary(), actionId, query, modalToolOwnsViewport());
    }

    void resetSettingsForPreviewAction(ModelingActionPreviewKind previewKind) {
        interactionSettingsService_.resetForPreviewAction(previewKind);
    }

    void clearInactiveOperationPreviewMetadata() {
        if (!session_.preview_transaction_status().active) {
            toolSettingsService_.clearPreviewMetadata();
        }
    }

    ModelingCommandResult executeRuntimeOperationCommand(
            quader_editor_core::EditorOperationId runtimeOperation,
            quader_editor_core::EditorOperationCommandPhase phase,
            quader_editor_core::EditorOperationSettings settings) {
        RuntimeOperationExecutor::OperationCommand operationCommand;
        operationCommand.operation = runtimeOperation;
        operationCommand.phase = phase;
        operationCommand.settings = std::move(settings);
        const quader_editor_core::EditorOperationCommand command =
                RuntimeOperationExecutor::makeCommand(operationCommand);

        const quader_editor_core::PreviewTransactionStatus previewBefore = session_.preview_transaction_status();
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                runtimeOperationExecutor_.execute(session_, operationCommand);

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
            syncSceneObjectsFromModeling(syncPlan.forceMeshSync);
        }
        if (syncPlan.syncSelection) {
            syncSceneSelectionFromModeling();
        }
        clearInactiveOperationPreviewMetadata();
        return commandExecutor_.fromRuntime(result.command);
    }

    ModelingCommandResult beginOperationPreview(
            const ModelingActionItem& action,
            const quader_editor_core::EditorOperation& operation) {
        if (!operation.supports_preview()) {
            return {.ok = false, .message = "Action does not support a live settings preview."};
        }

        resetSettingsForPreviewAction(action.previewKind);
        ModelingCommandResult result = executeRuntimeOperationCommand(
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
        return executeRuntimeOperationCommand(
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
        return executeRuntimeOperationCommand(
                *operation,
                commit ? quader_editor_core::EditorOperationCommandPhase::commit_preview :
                         quader_editor_core::EditorOperationCommandPhase::cancel_preview,
                settingsForOperation(*operation, interactionSettingsService_.settings()));
    }

    [[nodiscard]] std::optional<quader_editor_core::PreviewTransactionKind> editableOperationPreviewKind() const {
        const quader_editor_core::PreviewTransactionStatus preview = session_.preview_transaction_status();
        if (!preview.active || !operationForPreviewKind(preview.kind).has_value()) {
            return std::nullopt;
        }
        if (preview.kind == quader_editor_core::PreviewTransactionKind::thicken &&
                session_.selection_mode() == quader_editor_core::SelectionMode::object) {
            return std::nullopt;
        }
        return preview.kind;
    }

    [[nodiscard]] bool restoreActiveOperationPreviewBaseForSelectionEdit() {
        const std::optional<quader_editor_core::PreviewTransactionKind> previewKind = editableOperationPreviewKind();
        if (!previewKind.has_value()) {
            return false;
        }
        const bool restored = session_.restore_preview_transaction_base_for_edit(*previewKind);
        if (restored) {
            selectionService_.invalidatePickingCache();
        }
        return restored;
    }

    void refreshActiveOperationPreviewAfterSelectionEdit(bool restoredPreviewBase, bool selectionChanged) {
        if (!restoredPreviewBase) {
            return;
        }
        const std::optional<quader_editor_core::PreviewTransactionKind> previewKind = editableOperationPreviewKind();
        if (!previewKind.has_value()) {
            syncSceneSelectionFromModeling();
            return;
        }
        if (selectionChanged) {
            (void)session_.capture_preview_transaction_base_after_edit(*previewKind);
        }
        (void)updateActiveOperationPreviewSettings();
    }

    ModelingCommandResult executeContextAction(std::string_view actionId) {
        if (actionId == std::string_view("delete_selection")) {
            return {.ok = false, .message = "Delete selection must use the command route."};
        }

        const ModelingContextActionResolution action =
                contextActionService_.resolveExecutableAction(selectionSummary(), actionId, modalToolOwnsViewport());
        if (!action.ok) {
            return {.ok = false, .message = action.message};
        }
        for (const ModelingInstantOperationCatalogEntry& entry : modelingInstantOperationCatalog()) {
            if (entry.id == action.action.actionId &&
                    (action.action.operationId.empty() || entry.operationId == action.action.operationId) &&
                    action.action.previewKind == ModelingActionPreviewKind::None) {
                return executeInstantOperation(entry.operation);
            }
        }
        if (action.action.kind != ModelingActionKind::Operation || action.action.operationId.empty()) {
            return {.ok = false, .message = "Action is not an executable modeling operation."};
        }

        const quader_editor_core::EditorOperation* operation =
                quader_editor_core::editor_operation_for_id(action.action.operationId);
        if (operation == nullptr) {
            return {.ok = false, .message = "Action references an unknown modeling operation."};
        }

        if (action.action.previewKind != ModelingActionPreviewKind::None) {
            return beginOperationPreview(action.action, *operation);
        }

        return executeRuntimeOperation(
                operation->operation_id(),
                settingsForOperation(operation->operation_id(), interactionSettingsService_.settings()));
    }

    ModelingCommandResult commitActiveOperationPreview() {
        return finishActiveOperationPreview(true);
    }

    ModelingCommandResult cancelActiveOperationPreview() {
        return finishActiveOperationPreview(false);
    }

    [[nodiscard]] std::vector<ModelingInstantOperationDescriptor> instantOperations() const {
        const ModelingSelectionSummary summary = selectionSummary();
        std::vector<ModelingInstantOperationDescriptor> operations;
        for (const ModelingInstantOperationCatalogEntry& entry : modelingInstantOperationCatalog()) {
            operations.push_back({
                    .operation = entry.operation,
                    .id = std::string(entry.id),
                    .label = std::string(entry.liveLabel),
                    .enabled = modelingInstantOperationEnabled(entry, summary),
                    .rebuildsMeshOnSuccess = entry.rebuildsMeshOnSuccess,
            });
        }
        return operations;
    }

    ModelingCommandResult executeRuntimeOperation(
            quader_editor_core::EditorOperationId runtimeOperation,
            quader_editor_core::EditorOperationSettings settings) {
        return executeRuntimeOperationCommand(
                runtimeOperation,
                quader_editor_core::EditorOperationCommandPhase::execute,
                std::move(settings));
    }

    ModelingCommandResult executeInstantOperation(
            ModelingInstantOperationId operation,
            bool preserveDuplicateSceneIdentity = true) {
        const std::vector<ModelingObjectSceneIdentity> duplicateSceneIdentities =
                operation == ModelingInstantOperationId::DuplicateMeshes && preserveDuplicateSceneIdentity
                ? captureSelectedDuplicateSceneIdentities()
                : std::vector<ModelingObjectSceneIdentity>{};

        const std::optional<RuntimeOperationExecutor::OperationCommand> command =
                runtimeOperationExecutor_.operationForInstantCommand(
                        operation,
                        interactionSettingsService_.settings());
        if (!command.has_value()) {
            return {.ok = false, .message = "Instant operation is not mapped to a runtime operation."};
        }

        ModelingCommandResult result = executeRuntimeOperation(command->operation, command->settings);
        if (result.ok && result.changed && operation == ModelingInstantOperationId::DuplicateMeshes) {
            applySceneIdentitiesToSelectedDuplicates(duplicateSceneIdentities);
        }
        return result;
    }

    ModelingCommandResult beginTransformPreview(bool duplicateSelected) {
        const std::vector<ModelingObjectSceneIdentity> duplicateSceneIdentities =
                duplicateSelected ? captureSelectedDuplicateSceneIdentities() : std::vector<ModelingObjectSceneIdentity>{};
        const quader_editor_core::CommandResult result = session_.begin_transform_preview(duplicateSelected);
        if (result.ok) {
            invalidateSelectionOverlayCaches();
        }
        if (result.changed) {
            syncSceneObjectsFromModeling(true);
            syncSceneSelectionFromModeling();
            if (duplicateSelected) {
                applySceneIdentitiesToSelectedDuplicates(duplicateSceneIdentities);
            }
        }
        return {
                .ok = result.ok,
                .changed = result.changed,
                .message = result.message,
        };
    }

    ModelingCommandResult updateTransformTranslatePreview(QVec3 delta) {
        return updateTransformPreviewResult(session_.update_transform_translate_preview(toRuntime(delta)));
    }

    ModelingCommandResult updateTransformMatrixPreview(const ModelingTransform3& transform, QVec3 pivot) {
        return updateTransformPreviewResult(session_.update_transform_matrix_preview(toRuntime(transform), toRuntime(pivot)));
    }

    ModelingCommandResult updateTransformExtrudePreview(QVec3 offset, float closedEdgeLedgeSize) {
        return updateTransformPreviewResult(
                session_.update_transform_extrude_preview(toRuntime(offset), closedEdgeLedgeSize));
    }

    ModelingCommandResult updateTransformInsetPreview(const ModelingTransform3& transform, QVec3 pivot) {
        return updateTransformPreviewResult(session_.update_transform_inset_preview(toRuntime(transform), toRuntime(pivot)));
    }

    ModelingCommandResult commitTransformPreview() {
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromModeling(finish.before.active);
        syncSceneSelectionFromModeling();
        return transformPreviewService_.finishResult(finish, false);
    }

    ModelingCommandResult cancelTransformPreview() {
        const quader_editor_core::PreviewTransactionFinishResult finish =
                session_.cancel_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromModeling(finish.before.active);
        syncSceneSelectionFromModeling();
        return transformPreviewService_.finishResult(finish, true);
    }

    ModelingCommandResult recordRepeatableTransform(const ModelingRepeatTransform& transform) {
        if (!transform.active) {
            return {.ok = false, .message = "No active transform to record."};
        }

        quader_editor_core::TransformDragState drag;
        drag.active = true;
        drag.tool = toRuntime(transform.tool);
        drag.operation = toRuntime(transform.operation);
        drag.axis = toRuntime(transform.axis);
        drag.move.active = transform.tool == ModelingTransformTool::Move;
        drag.move.axis = drag.axis;
        drag.move.preview_delta = toRuntime(transform.moveDelta);
        drag.rotate.active = transform.tool == ModelingTransformTool::Rotate;
        drag.rotate.axis = drag.axis;
        drag.rotate.preview_angle_degrees = transform.rotateAngleDegrees;
        drag.scale.active = transform.tool == ModelingTransformTool::Scale;
        drag.scale.axis = drag.axis;
        drag.scale.preview_scale = toRuntime(transform.scale);

        historyBridge_.recordTransform(
                session_,
                drag,
                transform.duplicateSelected);
        return {.ok = true};
    }

    ModelingTransformDragPreview beginTransformDrag(const ModelingTransformDragBeginRequest& request) {
        QDR_PROFILE_FUNCTION();
        if (runtime_.transform_drag_active() || hasTransformPreview()) {
            QDR_PROFILE_SCOPE("Modeling transform drag cancel active preview");
            (void)cancelTransformDrag();
        }
        if ((!session_.has_selection() && !hasSceneTransformSelection()) ||
                request.axis == ModelingTransformAxis::None) {
            return transformDragPreview(false);
        }

        const bool runtimeHasSelection = session_.has_selection();
        const bool sceneTransformSelection = !runtimeHasSelection && hasSceneTransformSelection();
        const quader_editor_core::SelectionMode dragSelectionMode = sceneTransformSelection
                ? quader_editor_core::SelectionMode::object
                : session_.selection_mode();
        const quader_editor_core::ViewportInteractionTool tool = toRuntime(request.tool);
        const quader_editor_core::TransformDragOperation operation =
                quader_editor_core::sanitize_transform_drag_operation(
                        toRuntime(request.operation),
                        tool,
                        dragSelectionMode);
        const quader_editor_core::TransformAxis axis = toRuntime(request.axis);
        const quader::QVec3 pivot = toRuntime(request.pivot);
        const TransformDragPreviewRouting previewRouting = TransformDragController::previewRouting(
                request.duplicateSelected,
                dragSelectionMode == quader_editor_core::SelectionMode::object,
                operation == quader_editor_core::TransformDragOperation::transform);

        {
            QDR_PROFILE_SCOPE("Modeling transform drag begin runtime state");
            runtime_.begin_transform_drag_state(tool, operation, axis, pivot);
        }
        {
            QDR_PROFILE_SCOPE("Modeling transform drag configure state");
            quader_editor_core::TransformDragState drag = runtime_.transform_drag_state();
            drag.scene_transform_preview_eligible = previewRouting.sceneTransformPreviewEligible;
            runtime_.set_transform_drag_state(drag);
        }
        {
            QDR_PROFILE_SCOPE("Modeling transform drag set preview routing");
            transformDragController_.setPreviewDriven(previewRouting.previewDriven, request.duplicateSelected);
        }

        if (previewRouting.needsRuntimePreviewTransaction) {
            QDR_PROFILE_SCOPE("Modeling transform drag runtime preview transaction");
            const std::vector<ModelingObjectSceneIdentity> duplicateSceneIdentities =
                    request.duplicateSelected
                    ? captureSelectedDuplicateSceneIdentities()
                    : std::vector<ModelingObjectSceneIdentity>{};
            const quader_editor_core::CommandResult beginPreview = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag begin transform preview");
                return session_.begin_transform_preview(request.duplicateSelected);
            }();
            if (!beginPreview.ok) {
                clearTransformDragRuntimeState();
                return {
                        .ok = false,
                        .message = beginPreview.message.empty()
                                ? "Could not start transform preview."
                                : beginPreview.message,
                };
            }
            {
                QDR_PROFILE_SCOPE("Modeling transform drag invalidate overlay caches");
                invalidateSelectionOverlayCaches();
            }
            if (beginPreview.changed) {
                {
                    QDR_PROFILE_SCOPE("Modeling transform drag sync scene objects after begin");
                    syncSceneObjectsFromModeling(true);
                }
                {
                    QDR_PROFILE_SCOPE("Modeling transform drag sync scene selection after begin");
                    syncSceneSelectionFromModeling();
                }
                if (request.duplicateSelected) {
                    applySceneIdentitiesToSelectedDuplicates(duplicateSceneIdentities);
                }
            }
        }

        if (previewRouting.sceneTransformPreviewEligible &&
                !([&]() {
                    QDR_PROFILE_SCOPE("Modeling transform drag begin scene transform preview");
                    return sceneTransformDragService_.begin(scene_, request.pivot, true, "Transform Selection");
                }())) {
            (void)cancelTransformDrag();
            return {
                    .ok = false,
                    .message = "Could not start scene transform preview.",
            };
        }

        const quader_editor_core::ViewportCameraState camera = toRuntime(request.camera);
        const quader::QVec2 screenPosition = toRuntime(request.screenPosition);
        const quader::QVec2 screenPivot = toRuntime(request.screenPivot);
        const quader::QVec2 screenTip = toRuntime(request.screenTip);

        if (tool == quader_editor_core::ViewportInteractionTool::move) {
            quader_editor_core::MoveBeginInput begin;
            begin.axis = axis;
            begin.has_world_direction = request.hasWorldDirection;
            begin.world_direction = toRuntime(request.worldDirection);
            begin.center_local = {};
            begin.has_world_center = true;
            begin.world_center = pivot;
            begin.has_screen_axis = true;
            begin.screen_pivot = screenPivot;
            begin.screen_tip = screenTip;
            begin.screen_position = screenPosition;
            begin.has_start_ray = true;
            begin.start_ray = toRuntime(request.ray);
            begin.has_camera = true;
            begin.camera_transform = camera.transform;
            const quader_editor_core::MoveBeginResult beginResult = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag begin move");
                return quader_editor_core::begin_move_drag(begin);
            }();
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
            begin.has_screen_tip = true;
            begin.screen_tip = screenTip;
            begin.has_world_direction = request.hasWorldDirection;
            begin.world_direction = toRuntime(request.worldDirection);
            begin.has_world_pivot = true;
            begin.world_pivot = pivot;
            begin.has_camera = true;
            begin.camera_transform = camera.transform;
            const quader_editor_core::RotateBeginResult beginResult = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag begin rotate");
                return quader_editor_core::begin_rotate_drag(begin);
            }();
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
            const quader_editor_core::Bounds3 bounds = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag scale bounds");
                return transformDragBounds();
            }();
            begin.bounds.has_bounds = !bounds.empty;
            begin.bounds.min = bounds.min;
            begin.bounds.max = bounds.max;
            const quader_editor_core::ScaleBeginResult beginResult = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag begin scale");
                return quader_editor_core::begin_scale_drag(begin);
            }();
            if (!beginResult.ok) {
                (void)cancelTransformDrag();
                return {.ok = false, .message = beginResult.message.empty() ? "Scale drag failed." : beginResult.message};
            }
            runtime_.set_transform_drag_scale_state(beginResult.state);
        } else {
            clearTransformDragRuntimeState();
            return {.ok = false, .message = "Transform drag only supports move, rotate, or scale."};
        }

        {
            QDR_PROFILE_SCOPE("Modeling transform drag first preview apply");
            return applyTransformDragPreview({
                    .screenPosition = request.screenPosition,
                    .ray = request.ray,
                    .camera = request.camera,
                    .gridSize = request.gridSize,
                    .vertexSnapRadius = request.vertexSnapRadius,
                    .gridSnapEnabled = request.gridSnapEnabled,
                    .precision = request.precision,
            });
        }
    }

    ModelingTransformDragPreview updateTransformDrag(const ModelingTransformDragUpdateRequest& request) {
        QDR_PROFILE_FUNCTION();
        if (!runtime_.transform_drag_active()) {
            if (hasTransformPreview()) {
                (void)cancelTransformDrag();
            }
            return transformDragPreview(false);
        }
        return applyTransformDragPreview(request);
    }

    ModelingTransformDragPreview commitTransformDrag(EditorHistoryService* history = nullptr) {
        QDR_PROFILE_FUNCTION();
        const bool previewDriven = transformDragController_.previewDriven();
        const bool finishPreviewTransaction = previewDriven || transformDragController_.duplicateSelected();
        const quader_editor_core::TransformDragState repeatDrag = runtime_.transform_drag_state();
        ModelingSessionMemento previewBefore = finishPreviewTransaction && previewDriven && history != nullptr
                ? captureActivePreviewHistoryMemento()
                : ModelingSessionMemento();
        bool changed = false;

        if (finishPreviewTransaction) {
            const quader_editor_core::PreviewTransactionFinishResult finish = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag commit preview transaction");
                return session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
            }();
            if (finish.before.active) {
                invalidateSelectionOverlayCaches();
            }
            {
                QDR_PROFILE_SCOPE("Modeling transform drag sync scene objects after commit");
                syncSceneObjectsFromModeling(finish.before.active);
            }
            {
                QDR_PROFILE_SCOPE("Modeling transform drag sync scene selection after commit");
                syncSceneSelectionFromModeling();
            }
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
            if (previewDriven && changed && repeatDrag.active) {
                historyBridge_.recordTransform(
                        session_,
                        repeatDrag,
                        transformDragController_.duplicateSelected());
            }
            if (previewDriven && changed && history != nullptr && previewBefore.valid()) {
                const ModelingSessionMemento previewAfter = captureHistoryMemento();
                const CommandResult recorded = history->recordExecuted(
                        std::make_unique<RecordedModelingPreviewMementoCommand>(
                                [this](const ModelingSessionMemento& memento) {
                                    return restoreHistoryMemento(memento);
                                },
                                transformDragHistoryLabel(repeatDrag.tool),
                                std::move(previewBefore),
                                previewAfter));
                if (!recorded.ok) {
                    clearTransformDragRuntimeState();
                    return {
                            .ok = false,
                            .previewDriven = previewDriven,
                            .message = recorded.message.empty()
                                    ? "Could not record transform drag history."
                                    : recorded.message,
                    };
                }
            }
        }

        if (sceneTransformDragService_.previewActive()) {
            const DocumentCommandResult sceneCommit = [&]() {
                QDR_PROFILE_SCOPE("Modeling transform drag commit scene transform preview");
                return history != nullptr
                        ? sceneTransformDragService_.commit(*history)
                        : sceneTransformDragService_.accept();
            }();
            if (!sceneCommit.ok) {
                clearTransformDragRuntimeState();
                return {
                        .ok = false,
                        .previewDriven = previewDriven,
                        .message = sceneCommit.message.empty()
                                ? "Could not commit scene transform preview."
                                : sceneCommit.message,
                };
            }
            changed = changed || sceneCommit.changed;
            if (sceneCommit.changed) {
                invalidateSelectionOverlayCaches();
            }
        }

        ModelingTransformDragPreview preview = transformDragPreview(changed);
        preview.previewDriven = previewDriven;
        clearTransformDragRuntimeState();
        return preview;
    }

    ModelingCommandResult cancelTransformDrag() {
        QDR_PROFILE_FUNCTION();
        const bool activeBefore = runtime_.transform_drag_active() || hasTransformPreview();
        const bool scenePreviewActive = sceneTransformDragService_.previewActive();
        if (scenePreviewActive) {
            const DocumentCommandResult sceneCancel = sceneTransformDragService_.cancel();
            if (!sceneCancel.ok) {
                clearTransformDragRuntimeState();
                return {
                        .ok = false,
                        .changed = false,
                        .message = sceneCancel.message,
                };
            }
            if (sceneCancel.changed) {
                invalidateSelectionOverlayCaches();
            }
        }
        const quader_editor_core::PreviewTransactionFinishResult finish =
                runtime_.cancel_transform_preview_state(session_);
        if (finish.before.active) {
            invalidateSelectionOverlayCaches();
        }
        syncSceneObjectsFromModeling(finish.before.active);
        syncSceneSelectionFromModeling();
        clearTransformDragRuntimeState();
        return {
                .ok = true,
                .changed = (activeBefore && finish.restored) || scenePreviewActive,
        };
    }

    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request) {
        const bool restoredPreviewBase = restoreActiveOperationPreviewBaseForSelectionEdit();
        ModelingPickSelectRequest effectiveRequest = request;
        if (restoredPreviewBase) {
            effectiveRequest.preserveSelectionOnMiss = true;
        }
        const std::uint64_t selectionBefore = session_.state().selection_revision;
        const bool selectionApplied =
                selectionService_.applyPickSelection(session_, scene_, objectRegistry_, effectiveRequest);
        const bool selectionChanged = session_.state().selection_revision != selectionBefore;
        if (restoredPreviewBase) {
            refreshActiveOperationPreviewAfterSelectionEdit(restoredPreviewBase, selectionChanged);
            return selectionSummary();
        }
        if (selectionApplied) {
            syncSceneSelectionFromModeling();
        }
        return selectionSummary();
    }

    ModelingSelectionSummary boxSelect(const ModelingBoxSelectRequest& request) {
        const bool restoredPreviewBase = restoreActiveOperationPreviewBaseForSelectionEdit();
        return boxSelectFromBaseline(
                request,
                selectionService_.rectangleSelectionState(session_),
                restoredPreviewBase);
    }

    ModelingSelectionSummary boxSelect(
            const ModelingBoxSelectRequest& request,
            const quader_editor_core::RectangleSelectionState& baselineSelection) {
        const bool restoredPreviewBase = restoreActiveOperationPreviewBaseForSelectionEdit();
        const quader_editor_core::RectangleSelectionState effectiveBaseline = restoredPreviewBase
                ? selectionService_.rectangleSelectionState(session_)
                : baselineSelection;
        return boxSelectFromBaseline(request, effectiveBaseline, restoredPreviewBase);
    }

    ModelingSelectionSummary boxSelectFromBaseline(
            const ModelingBoxSelectRequest& request,
            const quader_editor_core::RectangleSelectionState& baselineSelection,
            bool restoredPreviewBase) {
        std::vector<quader_editor_core::PolygonObject> pickingObjects = sceneTransformedObjectsForPicking();
        const quader_editor_core::RectangleProjectedHitsRequest hitsRequest =
                selectionService_.rectangleProjectedHitsRequest(
                        pickingObjects,
                        session_.selection_mode(),
                        request);
        quader_editor_core::RectangleSelectionRequest rectangleRequest;
        rectangleRequest.current_selection = baselineSelection;
        rectangleRequest.operation = toRuntime(request.edit);
        rectangleRequest.universe = selectionService_.rectangleSelectionUniverse(session_);
        rectangleRequest.hits = quader_editor_core::rectangle_hits_from_projected_candidates(hitsRequest);
        const quader_editor_core::RectangleSelectionResult result =
                quader_editor_core::apply_rectangle_selection(rectangleRequest);
        const std::uint64_t selectionBefore = session_.state().selection_revision;
        (void)selectionService_.applyRectangleSelection(session_, result.selection);
        const bool selectionChanged = session_.state().selection_revision != selectionBefore;
        if (restoredPreviewBase) {
            refreshActiveOperationPreviewAfterSelectionEdit(restoredPreviewBase, selectionChanged);
            return selectionSummary();
        }
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return selectionSummary();
    }

    ModelingCommandResult updateSelectionHover(const ModelingHoverRequest& request) {
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects = cachedSceneTransformedObjectsForPicking();
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_editor_core::SelectionHoverUpdateResult hover =
                quader_editor_core::update_selection_hover(
                        pickingObjects,
                        session_.selection_mode(),
                        runtime_.hover(),
                        toRuntime(request.ray),
                        request.selectedModifierPressed,
                        pickOptions,
                        cachedPickObjectBounds(),
                        cachedPickFaceGeometry(pickOptions));
        runtime_.apply_hover_update(hover);
        return {
                .ok = true,
                .changed = hover.changed,
        };
    }

    [[nodiscard]] ModelingHoverSummary selectionHoverSummary() const {
        return fromRuntimeHoverSummary(session_, runtime_.hover(), false);
    }

    [[nodiscard]] ModelingHoverSummary previewSelectionHover(const ModelingHoverRequest& request) const {
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects = cachedSceneTransformedObjectsForPicking();
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_editor_core::SelectionHoverUpdateResult hover =
                quader_editor_core::update_selection_hover(
                        pickingObjects,
                        session_.selection_mode(),
                        runtime_.hover(),
                        toRuntime(request.ray),
                        request.selectedModifierPressed,
                        pickOptions,
                        cachedPickObjectBounds(),
                        cachedPickFaceGeometry(pickOptions));
        return fromRuntimeHoverSummary(session_, hover.hover, hover.changed);
    }

    ModelingCommandResult clearSelectionHover() {
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::clear_hover;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }

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
            syncSceneObjectsFromModeling(syncPlan.forceMeshSync);
        }
        if (syncPlan.syncSelection) {
            syncSceneSelectionFromModeling();
        }
        if (syncPlan.invalidateOverlay) {
            invalidateSelectionOverlayCaches();
        }
        return commandExecutor_.fromRuntime(result.command);
    }

    ModelingCommandResult setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool tool) {
        quader_editor_core::SetActiveToolRuntimeCommandSettings settings;
        settings.tool = tool;

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_active_tool;
        command.settings = settings;

        const bool preserveGroupSelection = appSelectionMode_ == ModelingSelectionMode::Group;
        const bool preserveSceneTransformSelection =
                !preserveGroupSelection && !session_.has_selection() && hasSceneTransformSelection();
        const bool preserveSceneSelection = preserveGroupSelection || preserveSceneTransformSelection;
        const std::vector<EntityId> selectedBefore = preserveSceneSelection ? scene_.selectedEntities() : std::vector<EntityId>{};
        const EntityId activeBefore = preserveSceneSelection ? scene_.primarySelection() : InvalidEntityId;

        ModelingCommandResult result = executeRuntimeCommand(command, false);
        if (preserveSceneSelection) {
            restoreSceneSelection(selectedBefore, activeBefore);
            if (preserveGroupSelection) {
                appSelectionMode_ = ModelingSelectionMode::Group;
            }
            invalidateSelectionOverlayCaches();
        }
        return result;
    }

    ModelingCommandResult activateViewportTool(ModelingViewportTool tool) {
        const bool preserveGroupSelection = appSelectionMode_ == ModelingSelectionMode::Group;
        const bool preserveSceneTransformSelection =
                !preserveGroupSelection && !session_.has_selection() && hasSceneTransformSelection();
        const bool preserveSceneSelection = preserveGroupSelection || preserveSceneTransformSelection;
        const std::vector<EntityId> selectedBefore = preserveSceneSelection ? scene_.selectedEntities() : std::vector<EntityId>{};
        const EntityId activeBefore = preserveSceneSelection ? scene_.primarySelection() : InvalidEntityId;
        const auto finish = [&](ModelingCommandResult result) {
            if (preserveSceneSelection) {
                restoreSceneSelection(selectedBefore, activeBefore);
                if (preserveGroupSelection) {
                    appSelectionMode_ = ModelingSelectionMode::Group;
                }
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
        return finish(setActiveRuntimeTool(toRuntime(tool)));
    }

    ModelingCommandResult cancelActiveModalTool() {
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
    }

    ModelingCommandResult updateInsertEdgeLoopHover(const ModelingToolRayEvent& event) {
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_insert_edge_loop_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult beginInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_insert_edge_loop_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult updateInsertEdgeLoopDrag(const ModelingToolRayEvent& event) {
        quader_editor_core::InsertEdgeLoopPickRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.vertex_radius = event.vertexRadius;
        settings.edge_radius = event.edgeRadius;
        settings.snap_enabled = event.snapEnabled;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_insert_edge_loop_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult commitInsertEdgeLoopDrag() {
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::commit_insert_edge_loop_drag),
                true);
    }

    ModelingCommandResult updateKnifeHover(const ModelingToolRayEvent& event) {
        const bool changed = runtime_.update_knife_preview_from_pick(
                session_,
                toRuntime(event.ray),
                toRuntime(event.camera),
                event.gridSize,
                event.snapEnabled,
                event.constrainAxis);
        if (changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = true, .changed = changed};
    }

    ModelingCommandResult acceptKnifePoint(const ModelingToolRayEvent& event) {
        const bool changed = runtime_.accept_knife_current_point(session_, toRuntime(event.camera), event.gridSize);
        if (changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = true, .changed = changed};
    }

    ModelingCommandResult commitKnifeTool() {
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = runtime_.commit_knife_tool(session_);
        if (session_.state().content_revision != contentBefore || result.changed) {
            syncSceneObjectsFromModeling(result.changed);
        }
        syncSceneSelectionFromModeling();
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
    }

    ModelingCommandResult updateCutHover(const ModelingToolRayEvent& event) {
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_cut_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult clickCut(const ModelingToolRayEvent& event) {
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::click_cut;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult beginCutPointDrag(int pointIndex) {
        quader_editor_core::CutPointDragRuntimeCommandSettings settings;
        settings.point_index = pointIndex;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_cut_point_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult beginCutLineDrag(const ModelingToolRayEvent& event) {
        quader_editor_core::CutRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = true;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_cut_line_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult endCutDrag() {
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::end_cut_drag),
                false);
    }

    ModelingCommandResult setCutMode(ModelingCutMode mode) {
        interactionSettingsService_.setCutMode(mode);
        quader_editor_core::SetCutModeRuntimeCommandSettings settings;
        settings.mode = quader_editor_core::cut_mode_id(toRuntime(mode));
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_cut_mode;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult cycleCutMode() {
        return setCutMode(fromRuntime(quader_editor_core::next_cut_mode(toRuntime(interactionSettingsService_.settings().cutMode))));
    }

    ModelingCommandResult finalizeCutTool() {
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::finalize_cut),
                true);
    }

    ModelingCommandResult updateMirrorHover(const ModelingToolRayEvent& event) {
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::update_mirror_hover;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult clickMirror(const ModelingToolRayEvent& event) {
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = event.dragging;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::click_mirror;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult beginMirrorPointDrag(int pointIndex) {
        quader_editor_core::MirrorPointDragRuntimeCommandSettings settings;
        settings.point_index = pointIndex;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_mirror_point_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult beginMirrorLineDrag(const ModelingToolRayEvent& event) {
        quader_editor_core::MirrorRayRuntimeCommandSettings settings;
        settings.camera = toRuntime(event.camera);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.dragging = true;
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::begin_mirror_line_drag;
        command.settings = settings;
        return executeRuntimeCommand(command, false);
    }

    ModelingCommandResult endMirrorDrag() {
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::end_mirror_drag),
                false);
    }

    ModelingCommandResult finalizeMirrorTool() {
        return executeRuntimeCommand(
                noSettingsRuntimeCommand(quader_editor_core::EditorRuntimeCommandId::finalize_mirror),
                true);
    }

    ModelingCommandResult updatePolyHover(const ModelingToolRayEvent& event) {
        const quader_editor_core::ViewportCameraState camera = toRuntime(event.camera);
        const quader_editor_core::CutGridPointResult gridPoint =
                quader_editor_core::viewport_grid_point_from_ray(camera, toRuntime(event.ray), event.gridSize, event.snapEnabled);
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
    }

    ModelingCommandResult acceptPolyPoint() {
        const quader_editor_core::PolyToolResult result = runtime_.accept_poly_current_point();
        if (result.changed) {
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }

    ModelingCommandResult commitPolyTool() {
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result = runtime_.commit_poly_tool(session_);
        if (session_.state().content_revision != contentBefore || result.changed) {
            syncSceneObjectsFromModeling(result.changed);
        }
        syncSceneSelectionFromModeling();
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
    }

    ModelingCommandResult activateBoxTool() {
        if (quader_host_contract::find_tool_descriptor("box") == nullptr) {
            return {.ok = false, .message = "Box tool is not present in the modeling host contract."};
        }

        return setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::box);
    }

    ModelingCreateBoxResult createBoxFromBounds(const ModelingCreateBoxRequest& request) {
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::CreateBoxFromBoundsRuntimeCommandSettings settings;
        settings.min_bounds = toRuntime(request.bounds.min);
        settings.max_bounds = toRuntime(request.bounds.max);

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::create_box_from_bounds;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult runtimeResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!runtimeResult.command.ok || !runtimeResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = runtimeResult.command.ok,
                    .changed = runtimeResult.command.changed,
                    .message = runtimeResult.command.message.empty() ? "Modeling runtime did not create a box object." : runtimeResult.command.message,
            };
        }

        return createSceneObjectFromLatest(request.name.empty() ? "Box" : request.name, runtimeResult.command.message);
    }

    ModelingBoxPreview updateBoxHover(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.snap_enabled = event.snapEnabled;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_hover, settings);
    }

    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.snap_enabled = event.snapEnabled;
        ModelingBoxPreview preview =
                executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_footprint, settings);
        if (preview.ok) {
            scene_.clearSelection();
        }
        return preview;
    }

    ModelingBoxPreview updateBoxFootprint(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.snap_enabled = event.snapEnabled;
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_footprint, settings);
    }

    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toRuntime(event.screenPosition);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.snap_enabled = event.snapEnabled;
        settings.has_camera = event.hasCamera;
        if (event.hasCamera) {
            settings.camera = toRuntime(event.camera);
        }
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::begin_box_height, settings);
    }

    ModelingBoxPreview updateBoxHeight(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toRuntime(event.screenPosition);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
        settings.snap_enabled = event.snapEnabled;
        settings.has_camera = event.hasCamera;
        if (event.hasCamera) {
            settings.camera = toRuntime(event.camera);
        }
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_height, settings);
    }

    ModelingCreateBoxResult commitBox(std::string name) {
        const std::size_t objectCountBefore = session_.objects().size();
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::commit_box;

        const quader_editor_core::EditorRuntimeCommandResult runtimeResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!runtimeResult.command.ok || !runtimeResult.command.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = runtimeResult.command.ok,
                    .changed = runtimeResult.command.changed,
                    .message = runtimeResult.command.message.empty() ? "Modeling runtime did not commit a box object." : runtimeResult.command.message,
            };
        }

        ModelingCreateBoxResult created = createSceneObjectFromLatest(name.empty() ? "Box" : name, runtimeResult.command.message);
        const ModelingCommandResult selectTool = setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool::select);
        created.changed = created.changed || selectTool.changed;
        if (!selectTool.ok) {
            created.ok = false;
            created.message = selectTool.message.empty() ? "Box committed, but Modeling runtime tool reset failed." : selectTool.message;
        }
        return created;
    }

    ModelingCommandResult cancelBox() {
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
    }

    ModelingCreateBoxResult createMesh(const ModelingCreateMeshRequest& request) {
        const std::size_t objectCountBefore = session_.objects().size();
        const quader_editor_core::CommandResult runtimeResult =
                session_.create_object_from_document(request.document, request.name, request.materialSlot);
        if (!runtimeResult.ok || !runtimeResult.changed || session_.objects().size() <= objectCountBefore) {
            return {
                    .ok = runtimeResult.ok,
                    .changed = runtimeResult.changed,
                    .message = runtimeResult.message.empty() ? "Modeling runtime did not create a mesh object." : runtimeResult.message,
            };
        }

        return createSceneObjectFromLatest(request.name.empty() ? "Mesh" : request.name, runtimeResult.message);
    }

    ModelingCommandResult setObjectSelection(std::span<const EntityId> entities, EntityId active) {
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
        }

        const RuntimeObjectSelectionProjectionResult projection =
                selectionProjectionService_.projectObjectSelectionForSceneEntities(
                        session_,
                        entities,
                        active,
                        objectRegistry_);
        if (!projection.ok) {
            return {.ok = false, .message = "Selection does not contain modeling objects."};
        }

        if (!projection.changed) {
            syncSceneSelectionFromModeling();
            return {.ok = true, .changed = false};
        }

        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = true};
    }

    ModelingCommandResult setComponentSelection(const ModelingComponentSelectionRequest& request) {
        const bool restoredPreviewBase = restoreActiveOperationPreviewBaseForSelectionEdit();
        auto failAfterPreviewRestore = [&](std::string message) {
            if (restoredPreviewBase) {
                refreshActiveOperationPreviewAfterSelectionEdit(restoredPreviewBase, false);
            }
            return ModelingCommandResult{.ok = false, .message = std::move(message)};
        };

        quader_editor_core::PolygonObject* object = findObject(session_, request.modelingObjectId);
        if (object == nullptr) {
            return failAfterPreviewRestore("Selection target mesh was not found.");
        }

        const quader_editor_core::SelectionMode mode = appSelectionModeToRuntime(request.mode);
        const quader_poly::Selection* currentSelection = nullptr;
        if (request.edit != ModelingSelectionEdit::Replace &&
                session_.selection_mode() == mode &&
                object->selected) {
            currentSelection = &object->selection;
        }
        ComponentSelectionEditResult selectionEdit =
                validatedComponentSelectionEdit(*object, request, mode, currentSelection);
        if (!selectionEdit.ok) {
            return failAfterPreviewRestore(std::string(selectionEdit.message));
        }

        const std::uint64_t selectionBefore = session_.state().selection_revision;
        session_.set_component_selection(request.modelingObjectId, std::move(selectionEdit.selection), mode);
        const bool selectionChanged = session_.state().selection_revision != selectionBefore;
        if (restoredPreviewBase) {
            refreshActiveOperationPreviewAfterSelectionEdit(restoredPreviewBase, selectionChanged);
            return {.ok = true, .changed = selectionChanged};
        }
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = selectionChanged};
    }

    ModelingCommandResult moveSelection(QVec3 delta) {
        const quader_editor_core::CommandResult result = session_.translate_selected(toRuntime(delta));
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromModeling(true);
            }
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }

    ModelingCommandResult transformSelection(const ModelingTransform3& transform, QVec3 pivot) {
        const quader_editor_core::CommandResult result =
                session_.transform_selected(toRuntime(transform), toRuntime(pivot));
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromModeling(true);
            }
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }

    ModelingCommandResult assignMaterialSlotToSelection(std::uint32_t materialSlot) {
        const quader_editor_core::CommandResult result = session_.assign_material_to_selection(materialSlot);
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromModeling(true);
            }
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }

    ModelingCommandResult assignMaterialSlotToFace(int modelingObjectId, int faceId, std::uint32_t materialSlot) {
        const quader_editor_core::CommandResult result = session_.assign_material_to_face(
                modelingObjectId,
                static_cast<quader_poly::ElementId>(faceId),
                materialSlot);
        if (result.ok) {
            if (result.changed) {
                syncSceneObjectsFromModeling(true);
            }
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
        }
        return {.ok = result.ok, .changed = result.changed, .message = result.message};
    }

    ModelingCommandResult setSelectionHover(const ModelingDirectHoverRequest& request) {
        quader_editor_core::SelectionHoverState hover;
        hover.object_id = request.modelingObjectId;
        hover.selected_modifier_pressed = request.selectedModifierPressed;
        hover.candidate_object_id = request.kind == ModelingActiveSelectionKind::Object ? -1 : request.modelingObjectId;
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
    }

private:
    void invalidateSelectionOverlayCaches() const {
        overlayService_.invalidate();
    }

    [[nodiscard]] bool sceneObjectsSyncedForCurrentModelingContent() const {
        return sceneSyncService_.sceneObjectsSyncedForCurrentModelingContent(session_, scene_);
    }

    ModelingCommandResult updateTransformPreviewResult(const quader_editor_core::CommandResult& result) {
        if (result.ok) {
            invalidateSelectionOverlayCaches();
            syncSceneObjectsFromModeling(true);
            syncSceneSelectionFromModeling();
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

    [[nodiscard]] bool hasSceneTransformSelection() const {
        for (const EntityId entity : scene_.selectedEntities()) {
            if (scene_.transform(entity) != nullptr && !scene_.isEffectivelyLocked(entity)) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] quader_editor_core::Bounds3 transformDragBounds() const {
        quader_editor_core::Bounds3 bounds = session_.selection_bounds();
        if (!bounds.empty) {
            return bounds;
        }
        if (const std::optional<SceneBounds> sceneBounds = selectedTransformWorldBounds(scene_, assets_)) {
            bounds.empty = false;
            bounds.min = sceneBounds->min;
            bounds.max = sceneBounds->max;
        }
        return bounds;
    }

    void clearTransformDragRuntimeState() {
        runtime_.clear_transform_drag_state();
        runtime_.clear_gizmo_hover();
        transformDragController_.clear();
        sceneTransformDragService_.reset();
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
        preview.tool = fromRuntime(drag.tool);
        preview.operation = fromRuntime(drag.operation);
        preview.axis = fromRuntime(drag.axis);
        preview.pivot = fromRuntime(drag.pivot);
        preview.moveDelta = fromRuntime(drag.move.preview_delta);
        preview.rotateAngleDegrees = drag.rotate.preview_angle_degrees;
        preview.scale = fromRuntime(drag.scale.preview_scale);
        preview.contentRevision = session_.state().content_revision;
        preview.selectionRevision = session_.state().selection_revision;
        preview.sceneTransform = drag.scene_transform_preview_eligible;
        quader_poly::Transform3 sceneTransform;
        if (scenePreviewTransformForDrag(drag, sceneTransform)) {
            preview.hasScenePreviewTransform = true;
            preview.scenePreviewTransform = fromRuntime(sceneTransform);
        }
        return preview;
    }

    ModelingTransformDragPreview applyTransformDragPreview(const ModelingTransformDragUpdateRequest& request) {
        QDR_PROFILE_FUNCTION();
        quader_editor_core::TransformDragPreviewInput input;
        input.camera = toRuntime(request.camera);
        input.screen_position = toRuntime(request.screenPosition);
        input.ray = toRuntime(request.ray);
        input.grid_size = request.gridSize;
        input.grid_enabled = request.gridSnapEnabled;
        input.precision = request.precision;
        if (request.vertexSnapRadius > 0.0) {
            QDR_PROFILE_SCOPE("Modeling transform drag build snap caches");
            input.vertex_snap_enabled = true;
            input.vertex_snap_radius = request.vertexSnapRadius;
            input.snap_object_bounds = cachedPickObjectBounds();
            input.snap_face_geometry = cachedPickFaceGeometry();
        }

        const quader_editor_core::CommandResult result = [&]() {
            QDR_PROFILE_SCOPE("Modeling transform drag apply runtime preview");
            return runtime_.apply_transform_drag_preview(session_, input);
        }();
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

        bool changed = result.changed;
        if (sceneTransformDragService_.previewActive()) {
            QDR_PROFILE_SCOPE("Modeling transform drag apply scene preview");
            quader_poly::Transform3 sceneTransform;
            if (scenePreviewTransformForDrag(runtime_.transform_drag_state(), sceneTransform)) {
                const DocumentCommandResult scenePreview =
                        sceneTransformDragService_.previewWorldTransformDelta(
                                scene_,
                                matrixFromRuntimeTransform(sceneTransform));
                if (!scenePreview.ok) {
                    const bool previewDriven = transformDragController_.previewDriven();
                    (void)cancelTransformDrag();
                    return {
                            .ok = false,
                            .previewDriven = previewDriven,
                            .message = scenePreview.message.empty()
                                    ? "Could not update scene transform preview."
                                    : scenePreview.message,
                    };
                }
                changed = changed || scenePreview.changed;
            }
        }

        {
            QDR_PROFILE_SCOPE("Modeling transform drag invalidate overlay caches after apply");
            invalidateSelectionOverlayCaches();
        }
        if (transformDragController_.previewDriven()) {
            {
                QDR_PROFILE_SCOPE("Modeling transform drag sync scene objects after apply");
                syncSceneObjectsFromModeling(true);
            }
            {
                QDR_PROFILE_SCOPE("Modeling transform drag sync scene selection after apply");
                syncSceneSelectionFromModeling();
            }
        }
        return transformDragPreview(changed);
    }

    void syncSceneObjectsFromModeling(bool force = false) {
        QDR_PROFILE_FUNCTION();
        meshSyncService_.syncSceneObjectsFromModeling(session_, scene_, assets_, sceneSyncService_, force);
    }

    [[nodiscard]] const std::vector<quader_editor_core::PolygonObject>& cachedSceneTransformedObjectsForPicking() const {
        QDR_PROFILE_FUNCTION();
        return selectionService_.cachedSceneTransformedObjectsForPicking(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking() const {
        QDR_PROFILE_FUNCTION();
        return selectionService_.sceneTransformedObjectsForPicking(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds>& cachedPickObjectBounds() const {
        QDR_PROFILE_FUNCTION();
        return selectionService_.cachedPickObjectBounds(session_, scene_, objectRegistry_);
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry>& cachedPickFaceGeometry(
            quader_poly::PickOptions pickOptions = {}) const {
        QDR_PROFILE_FUNCTION();
        return selectionService_.cachedPickFaceGeometry(session_, scene_, objectRegistry_, pickOptions);
    }

    ModelingCommandResult executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId commandId, bool mayChangeContent) {
        return executeRuntimeCommand(noSettingsRuntimeCommand(commandId), mayChangeContent);
    }

    ModelingCreateBoxResult createSceneObjectFromLatest(const std::string& name, const std::string& message) {
        return meshSyncService_.createSceneObjectFromLatest(
                session_,
                scene_,
                assets_,
                sceneSyncService_,
                name,
                message);
    }

    template <typename Settings>
    ModelingBoxPreview executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId commandId, Settings settings) {
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = commandId;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult runtimeResult =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        if (!runtimeResult.command.ok) {
            return {
                    .ok = false,
                    .message = runtimeResult.command.message,
            };
        }

        quader_editor_core::BoxPreviewDescriptor preview = runtime_.box_preview_descriptor(settings.snap_enabled);
        std::vector<quader::QVec3> corners;
        if (preview.drag_state == quader_editor_core::BoxDragState::height) {
            corners = quader_editor_core::box_preview_corners(preview);
        }

        ModelingBoxPreview result = fromRuntime(preview, corners);
        result.ok = runtimeResult.command.ok;
        if (!runtimeResult.command.message.empty()) {
            result.message = runtimeResult.command.message;
        }
        return result;
    }

    SceneDocument& scene_;
    AssetRegistry& assets_;
    ModelingObjectRuntimeMap objectRegistry_;
    SceneModelingSyncService sceneSyncService_;
    ModelingMeshSyncService meshSyncService_;
    ModelingHierarchyDuplicateService hierarchyDuplicateService_;
    ModelingSelectionService selectionService_;
    SelectionProjectionService selectionProjectionService_;
    mutable ModelingOverlayService overlayService_;
    ModelingCommandExecutor commandExecutor_;
    ModelingContextActionService contextActionService_;
    RuntimeOperationExecutor runtimeOperationExecutor_;
    TransformPreviewService transformPreviewService_;
    ModelingInteractionSettingsService interactionSettingsService_;
    ModelingToolSettingsService toolSettingsService_;
    TransformDragController transformDragController_;
    SceneTransformDragService sceneTransformDragService_;
    ModalToolRuntime modalToolRuntime_;
    ModelingClipboardService clipboardService_;
    ModelingHistoryBridge historyBridge_;
    ModelingSelectionMode appSelectionMode_ = ModelingSelectionMode::Object;

    quader_editor_core::EditorSession session_;
    quader_editor_core::EditorRuntime runtime_;
};

ModelingSessionService::ModelingSessionService(SceneDocument& scene, AssetRegistry& assets)
    : impl_(std::make_unique<Impl>(scene, assets)) {}

ModelingSessionService::~ModelingSessionService() = default;
ModelingSessionService::ModelingSessionService(ModelingSessionService&&) noexcept = default;
ModelingSessionService& ModelingSessionService::operator=(ModelingSessionService&&) noexcept = default;

ModelingCommandDescriptor ModelingSessionService::boxToolCommand() const {
    return impl_->boxToolCommand();
}

ModelingCommandDescriptor ModelingSessionService::createBoxFromBoundsCommand() const {
    return impl_->createBoxFromBoundsCommand();
}

std::vector<ModelingObjectRecord> ModelingSessionService::objects() const {
    return impl_->objects();
}

std::optional<EntityId> ModelingSessionService::entityForModelingObject(int modelingObjectId) const {
    return impl_->entityForModelingObject(modelingObjectId);
}

std::optional<int> ModelingSessionService::modelingObjectForEntity(EntityId entity) const {
    return impl_->modelingObjectForEntity(entity);
}

std::optional<MeshAsset> ModelingSessionService::compileObjectMesh(int modelingObjectId) const {
    return impl_->compileObjectMesh(modelingObjectId);
}

std::optional<EntityId> ModelingSessionService::pickObjectEntity(const ModelingPickSelectRequest& request) const {
    return impl_->pickObjectEntity(request);
}

std::vector<EntityId> ModelingSessionService::boxPickObjectEntities(const ModelingBoxSelectRequest& request) const {
    return impl_->boxPickObjectEntities(request);
}

std::optional<QVec3> ModelingSessionService::selectionCenter() const {
    return impl_->selectionCenter();
}

ModelingSelectionMode ModelingSessionService::selectionMode() const {
    return impl_->selectionMode();
}

ModelingSelectionSummary ModelingSessionService::selectionSummary() const {
    return impl_->selectionSummary();
}

ModelingSelectionBoxBaseline ModelingSessionService::captureBoxSelectionBaseline() const {
    return impl_->captureBoxSelectionBaseline();
}

std::uint64_t ModelingSessionService::selectionOverlaySignature() const {
    return impl_->selectionOverlaySignature();
}

ViewportOverlayFrame ModelingSessionService::selectionOverlayFrame() const {
    return impl_->selectionOverlayFrame();
}

std::vector<ModelingToolPreviewMesh> ModelingSessionService::toolPreviewMeshes() const {
    return impl_->toolPreviewMeshes();
}

ModelingSessionMemento ModelingSessionService::captureHistoryMemento() const {
    return impl_->captureHistoryMemento();
}

ModelingSessionMemento ModelingSessionService::captureActivePreviewHistoryMemento() const {
    return impl_->captureActivePreviewHistoryMemento();
}

ModelingCommandResult ModelingSessionService::restoreHistoryMemento(const ModelingSessionMemento& memento) {
    return impl_->restoreHistoryMemento(memento);
}

void ModelingSessionService::resetFromScene() {
    impl_->resetFromScene();
}

void ModelingSessionService::projectObjectSelectionFromScene(bool forceObjectMode) {
    impl_->syncObjectSelectionFromScene(forceObjectMode);
}

ModelingCommandResult ModelingSessionService::duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands) {
    return impl_->duplicateSelectedHierarchyForGroupDrag(documentCommands);
}

ModelingCommandResult ModelingSessionService::duplicateSelectedEntityHierarchy(DocumentCommandService& documentCommands) {
    return impl_->duplicateSelectedEntityHierarchy(documentCommands);
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

std::unique_ptr<IUndoableCommand> ModelingSessionService::createDeleteSelectionCommand() {
    return impl_->createDeleteSelectionCommand();
}

bool ModelingSessionService::hasSelectedSceneAwareDeleteRoot() const {
    return impl_->hasSelectedSceneAwareDeleteRoot();
}

ModelingCommandResult ModelingSessionService::deleteComponentSelection() {
    return impl_->deleteComponentSelection();
}

ModelingCommandResult ModelingSessionService::copySelection() {
    return impl_->copySelection();
}

ModelingCommandResult ModelingSessionService::pasteSelection() {
    return impl_->pasteSelection();
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

bool ModelingSessionService::canCancelActiveToolOrPreview() const {
    return impl_->canCancelActiveToolOrPreview();
}

bool ModelingSessionService::canCancelActivePreviewForUndo() const {
    return impl_->canCancelActivePreviewForUndo();
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

std::optional<ModelingActionItem> ModelingSessionService::contextAction(
        std::string_view actionId,
        const ModelingActionQuery& query) const {
    return impl_->contextAction(actionId, query);
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

ModelingCommandResult ModelingSessionService::updateTransformTranslatePreview(QVec3 delta) {
    return impl_->updateTransformTranslatePreview(delta);
}

ModelingCommandResult ModelingSessionService::updateTransformMatrixPreview(const ModelingTransform3& transform, QVec3 pivot) {
    return impl_->updateTransformMatrixPreview(transform, pivot);
}

ModelingCommandResult ModelingSessionService::updateTransformExtrudePreview(QVec3 offset, float closedEdgeLedgeSize) {
    return impl_->updateTransformExtrudePreview(offset, closedEdgeLedgeSize);
}

ModelingCommandResult ModelingSessionService::updateTransformInsetPreview(const ModelingTransform3& transform, QVec3 pivot) {
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
    QDR_PROFILE_FUNCTION();
    return impl_->beginTransformDrag(request);
}

ModelingTransformDragPreview ModelingSessionService::updateTransformDrag(const ModelingTransformDragUpdateRequest& request) {
    QDR_PROFILE_FUNCTION();
    return impl_->updateTransformDrag(request);
}

ModelingTransformDragPreview ModelingSessionService::commitTransformDrag() {
    QDR_PROFILE_FUNCTION();
    return impl_->commitTransformDrag();
}

ModelingTransformDragPreview ModelingSessionService::commitTransformDrag(EditorHistoryService& history) {
    QDR_PROFILE_FUNCTION();
    return impl_->commitTransformDrag(&history);
}

ModelingCommandResult ModelingSessionService::cancelTransformDrag() {
    QDR_PROFILE_FUNCTION();
    return impl_->cancelTransformDrag();
}

ModelingSelectionSummary ModelingSessionService::pickSelect(const ModelingPickSelectRequest& request) {
    return impl_->pickSelect(request);
}

ModelingSelectionSummary ModelingSessionService::boxSelect(const ModelingBoxSelectRequest& request) {
    return impl_->boxSelect(request);
}

ModelingSelectionSummary ModelingSessionService::boxSelect(
        const ModelingBoxSelectRequest& request,
        const ModelingSelectionBoxBaseline& baseline) {
    return baseline.impl_ ? impl_->boxSelect(request, baseline.impl_->selection) : impl_->boxSelect(request);
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

ModelingCreateBoxResult ModelingSessionService::createMesh(const ModelingCreateMeshRequest& request) {
    return impl_->createMesh(request);
}

ModelingCommandResult ModelingSessionService::setObjectSelection(std::span<const EntityId> entities, EntityId active) {
    return impl_->setObjectSelection(entities, active);
}

ModelingCommandResult ModelingSessionService::setComponentSelection(const ModelingComponentSelectionRequest& request) {
    return impl_->setComponentSelection(request);
}

ModelingCommandResult ModelingSessionService::moveSelection(QVec3 delta) {
    return impl_->moveSelection(delta);
}

ModelingCommandResult ModelingSessionService::transformSelection(const ModelingTransform3& transform, QVec3 pivot) {
    return impl_->transformSelection(transform, pivot);
}

ModelingCommandResult ModelingSessionService::assignMaterialSlotToSelection(std::uint32_t materialSlot) {
    return impl_->assignMaterialSlotToSelection(materialSlot);
}

ModelingCommandResult ModelingSessionService::assignMaterialSlotToFace(
        int modelingObjectId,
        int faceId,
        std::uint32_t materialSlot) {
    return impl_->assignMaterialSlotToFace(modelingObjectId, faceId, materialSlot);
}

ModelingCommandResult ModelingSessionService::setSelectionHover(const ModelingDirectHoverRequest& request) {
    return impl_->setSelectionHover(request);
}

} // namespace quader
