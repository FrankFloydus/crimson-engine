////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/ModelingSessionService.h"

#include "assets/AssetRegistry.h"
#include "core/IUndoableCommand.h"
#include "editor/commands/DocumentCommandService.h"
#include "editor/commands/modeling/SceneAwareDeleteSelectionCommand.h"
#include "editor/projection/MeshProjectionService.h"
#include "editor/projection/SelectionOverlayProjection.h"
#include "editor/modeling/services/ModelingHierarchyDuplicateService.h"
#include "editor/modeling/services/ModelingInstantOperationCatalog.h"
#include "editor/modeling/services/ModelingSessionServices.h"
#include "editor/modeling/services/SelectionModeAdapter.h"
#include "editor/modeling/services/ModelingOperationSettings.h"
#include "editor/selection/SelectionProjectionService.h"
#include "math/GlmBridge.h"
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
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <editor/modeling/operations/operation_command.h>
#include <editor/modeling/runtime/repeat_last_action.h>
#include <editor/modeling/runtime/runtime_command.h>
#include <editor/modeling/runtime/session.h>
#include <editor/interactions/box_tool.h>
#include <editor/interactions/cut_tool.h>
#include <editor/interactions/mirror_tool.h>
#include <editor/interactions/move_tool.h>
#include <editor/interactions/viewport_projection.h>
#include <editor/interactions/rotate_tool.h>
#include <editor/interactions/scale_tool.h>
#include <editor/interactions/selection_edit.h>
#include <editor/interactions/transform_drag.h>
#include <editor/metadata/host_contract.h>
#include <mesh/polygon/document.h>

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

quader_poly::Transform3 toRuntime(const ModelingTransform3& transform) {
    return {
            .x_axis = toRuntime(transform.xAxis),
            .y_axis = toRuntime(transform.yAxis),
            .z_axis = toRuntime(transform.zAxis),
            .origin = toRuntime(transform.origin),
    };
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

/**
 * Stores the Drawn Plane Preview Frame data contract used by the modeling application service layer.
 */
struct DrawnPlanePreviewFrame {
    QVec3 lineStart;
    QVec3 lineEnd;
    QVec3 normal;
    QVec3 side;
    float lineLength = 0.0f;
};

bool buildDrawnPlanePreviewFrame(
        QVec3 point0,
        QVec3 point1,
        QVec3 support,
        DrawnPlanePreviewFrame& frame) {
    const QVec3 line = point1 - point0;
    const float lineLength = length(line);
    if (lineLength <= 0.0001f) {
        return false;
    }

    const QVec3 lineDirection = line / lineLength;
    const QVec3 supportVector = support - point0;
    QVec3 normal = cross(line, supportVector);
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

    QVec3 side = cross(normal, lineDirection);
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
        std::span<const quader::QVec3> visiblePoints,
        quader::QVec3 supportPoint,
        bool hasLine) {
    std::array<QVec3, 3> points{};
    const std::size_t count = std::min<std::size_t>(visiblePoints.size(), points.size());
    for (std::size_t index = 0; index < count; ++index) {
        points[index] = fromRuntime(visiblePoints[index]);
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
    if (!buildDrawnPlanePreviewFrame(points[0], points[1], fromRuntime(supportPoint), planeFrame)) {
        return;
    }

    const QVec3 side = planeFrame.side * planeFrame.lineLength;
    const std::array<QVec3, 4> corners{{
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

float runtimeLengthSquared(quader::QVec3 value) {
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

void mixToolStateHash(std::uint64_t& hash, quader::QVec3 value) {
    mixToolStateHash(hash, static_cast<double>(value.x));
    mixToolStateHash(hash, static_cast<double>(value.y));
    mixToolStateHash(hash, static_cast<double>(value.z));
}

quader::QVec3 drawnPlaneSupportPoint(
        std::span<const quader::QVec3> visiblePoints,
        quader::QVec3 faceNormal) {
    if (visiblePoints.size() > 2U) {
        return visiblePoints[2];
    }
    if (visiblePoints.size() > 1U && runtimeLengthSquared(faceNormal) > 0.000001f) {
        return visiblePoints[0] + faceNormal;
    }
    return {};
}

/**
 * Represents a Preview Triangle Mesh value used by the modeling application service layer.
 */
struct PreviewTriangleMesh {
    std::vector<QVec3> vertices;
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
            mesh.vertices.push_back(fromRuntime(vertex.position));
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

/**
 * Stores the Cut Preview Point Key data contract used by the modeling application service layer.
 */
struct CutPreviewPointKey {
    long long x = 0;
    long long y = 0;
    long long z = 0;
};

long long quantizeCutPreviewCoordinate(float value) {
    return static_cast<long long>(std::llround(value / kCutPreviewPlaneQuantization));
}

CutPreviewPointKey cutPreviewPointKey(QVec3 value) {
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

/**
 * Stores the Cut Preview Edge Key data contract used by the modeling application service layer.
 */
struct CutPreviewEdgeKey {
    CutPreviewPointKey a;
    CutPreviewPointKey b;

    CutPreviewEdgeKey(QVec3 first, QVec3 second) {
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

/**
 * Represents a Cut Preview Edge Accumulator value used by the modeling application service layer.
 */
struct CutPreviewEdgeAccumulator {
    QVec3 a;
    QVec3 b;
    QVec3 normal;
    bool hasNormal = false;
    bool hasCapTriangle = false;
    bool hasSideTriangle = false;
    bool feature = false;
    int count = 0;

    void add(QVec3 first, QVec3 second, bool onPlane, bool capTriangle, QVec3 triangleNormal) {
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

float signedDistanceToPlane(QVec3 point, QVec3 planePoint, QVec3 planeNormal) {
    return dot(point - planePoint, planeNormal);
}

float previewMeshBoundsMagnitude(const PreviewTriangleMesh& mesh) {
    if (mesh.vertices.empty()) {
        return 0.0f;
    }

    QVec3 min = mesh.vertices.front();
    QVec3 max = mesh.vertices.front();
    for (const QVec3 vertex : mesh.vertices) {
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
        QVec3 a,
        QVec3 b,
        bool onPlane,
        bool capTriangle,
        QVec3 triangleNormal) {
    if (length(a - b) <= 0.000001f) {
        return;
    }
    edges[CutPreviewEdgeKey(a, b)].add(a, b, onPlane, capTriangle, triangleNormal);
}

std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> collectPreviewEdges(
        const PreviewTriangleMesh& mesh,
        std::optional<QVec3> planePoint,
        QVec3 planeNormal) {
    std::map<CutPreviewEdgeKey, CutPreviewEdgeAccumulator> edges;
    if (mesh.empty()) {
        return edges;
    }

    const bool hasPlane = planePoint.has_value() && length(planeNormal) > 0.000001f;
    const QVec3 normalizedPlaneNormal = hasPlane ? normalize(planeNormal) : QVec3{};
    const float planeScale = std::max(1.0f, previewMeshBoundsMagnitude(mesh));
    const float planeEpsilon = std::max(kCutPreviewPlaneQuantization * 8.0f, planeScale * 0.00002f);

    for (std::size_t index = 0; index + 2U < mesh.indices.size(); index += 3U) {
        const std::uint32_t ia = mesh.indices[index];
        const std::uint32_t ib = mesh.indices[index + 1U];
        const std::uint32_t ic = mesh.indices[index + 2U];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            continue;
        }

        const QVec3 a = mesh.vertices[ia];
        const QVec3 b = mesh.vertices[ib];
        const QVec3 c = mesh.vertices[ic];
        QVec3 triangleNormal = cross(b - a, c - a);
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
        QVec3 start,
        QVec3 end,
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
        QVec3 planePoint,
        QVec3 planeNormal,
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

std::optional<MeshAsset> boxPreviewMeshFromDescriptor(const quader_editor_core::BoxPreviewDescriptor& preview) {
    if (!preview.ok ||
            preview.drag_state != quader_editor_core::BoxDragState::height ||
            preview.bounds.empty) {
        return std::nullopt;
    }

    const std::vector<quader::QVec3> corners = quader_editor_core::box_preview_corners(preview);
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

std::uint64_t mixPickGeometryHash(std::uint64_t hash, bool value) {
    return mixPickGeometryHash(hash, value ? 1ull : 0ull);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return mixPickGeometryHash(hash, bits);
}

std::uint64_t mixPickGeometryHash(std::uint64_t hash, quader::QVec3 value) {
    hash = mixPickGeometryHash(hash, value.x);
    hash = mixPickGeometryHash(hash, value.y);
    hash = mixPickGeometryHash(hash, value.z);
    return hash;
}

std::uint64_t objectPickFaceGeometrySignature(
        const quader_editor_core::PolygonObject& object,
        quader_poly::PickOptions pickOptions = {}) {
    std::uint64_t hash = mixPickGeometryHash(kPickGeometryHashSeed, object.id);
    hash = mixPickGeometryHash(hash, static_cast<int>(pickOptions.mode));
    hash = mixPickGeometryHash(hash, pickOptions.vertex_radius);
    hash = mixPickGeometryHash(hash, pickOptions.edge_radius);
    hash = mixPickGeometryHash(hash, pickOptions.has_hidden_material_slot);
    hash = mixPickGeometryHash(hash, pickOptions.hidden_material_slot);
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

void includePickBoundsPoint(quader_editor_core::PickObjectBounds& bounds, quader::QVec3 point) {
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
 * Owns Impl behavior within the modeling application service layer.
 */
class ModelingSessionService::Impl {
public:
    Impl(SceneDocument& scene, AssetRegistry& assets)
        : scene_(scene), assets_(assets), sceneSyncService_(objectRegistry_) {}

    [[nodiscard]] bool runtimeAvailable() const {
        return true;
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

    [[nodiscard]] std::optional<QVec3> selectionCenter() const {
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
                const QVec3 world = transformObjectPoint(
                        object.id,
                        fromRuntime(vertex->position),
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
        const std::optional<quader::QVec3> center = session_.selection_center();
        return center ? std::optional<QVec3>{fromRuntime(*center)} : std::nullopt;
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

    [[nodiscard]] std::uint64_t selectionOverlaySignature() const {
        std::uint64_t hash = selectionOverlayToolSignature();

        const quader_editor_core::SelectionHoverState& hover = runtime_.hover();
        mixToolStateHash(hash, hover.object_id);
        mixToolStateHash(hash, static_cast<int>(hover.kind));
        mixToolStateHash(hash, static_cast<int>(hover.vertex_id));
        mixToolStateHash(hash, static_cast<int>(hover.edge.a));
        mixToolStateHash(hash, static_cast<int>(hover.edge.b));
        mixToolStateHash(hash, static_cast<int>(hover.face_id));
        mixToolStateHash(hash, hover.selected_modifier_pressed);
        mixToolStateHash(hash, hover.candidate_object_id);

        const quader_editor_core::TransformDragState& drag = runtime_.transform_drag_state();
        mixToolStateHash(hash, drag.active);
        mixToolStateHash(hash, static_cast<int>(drag.tool));
        mixToolStateHash(hash, static_cast<int>(drag.operation));
        mixToolStateHash(hash, static_cast<int>(drag.axis));
        mixToolStateHash(hash, drag.pivot);
        mixToolStateHash(hash, drag.scene_transform_preview_eligible);
        mixToolStateHash(hash, static_cast<int>(drag.snap_target.kind));
        mixToolStateHash(hash, drag.snap_target.object_id);
        mixToolStateHash(hash, static_cast<int>(drag.snap_target.vertex_id));
        mixToolStateHash(hash, drag.snap_target.position);
        if (drag.active) {
            const std::vector<quader_editor_core::SelectionOverlayPoint> snapPoints =
                    quader_editor_core::transform_drag_snap_overlay_points(drag);
            mixToolStateHash(hash, static_cast<int>(snapPoints.size()));
            for (const quader_editor_core::SelectionOverlayPoint& point : snapPoints) {
                mixToolStateHash(hash, static_cast<int>(point.layer));
                mixToolStateHash(hash, point.position);
                mixToolStateHash(hash, static_cast<std::uint64_t>(point.color_abgr));
                mixToolStateHash(hash, static_cast<double>(point.size_pixels));
                mixToolStateHash(hash, point.object_id);
                mixToolStateHash(hash, static_cast<int>(point.vertex_id));
            }
        }

        return hash;
    }

    [[nodiscard]] ViewportOverlayFrame selectionOverlayFrame() const {
        const quader_editor_core::SelectionHoverState& hover = runtime_.hover();
        const std::uint64_t contentRevision = session_.state().content_revision;
        const std::uint64_t selectionRevision = session_.state().selection_revision;
        const std::uint64_t sceneRevision = scene_.revision();
        const quader_editor_core::SelectionMode selectionMode = session_.selection_mode();
        const std::uint64_t overlaySignature = selectionOverlaySignature();
        if (overlayService_.selectionFrameMatches(
                    contentRevision,
                    selectionRevision,
                    sceneRevision,
                    selectionMode,
                    overlaySignature,
                    hover)) {
            return overlayService_.selectionFrame();
        }

        ViewportOverlayFrame frame = SelectionOverlayProjection::projectSelectionOverlayFrame(
                overlayService_.selectionOverlayCurrentFrame(session_, runtime_, hover),
                objectRegistry_.entitiesByObject(),
                scene_);
        if (runtime_.transform_drag_active()) {
            SelectionOverlayProjection::appendWorldSpaceOverlayPoints(frame,
                    quader_editor_core::transform_drag_snap_overlay_points(runtime_.transform_drag_state()));
        }
        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        if (quader_editor_core::cut_is_active(cut)) {
            const std::vector<quader::QVec3> cutPoints =
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
                            fromRuntime(cutPoints[0]),
                            fromRuntime(cut.face_normal),
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
            const std::vector<quader::QVec3> mirrorPoints =
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
                overlaySignature,
                hover);
        return overlayService_.selectionFrame();
    }

    [[nodiscard]] std::vector<ModelingToolPreviewMesh> toolPreviewMeshes() const {
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
            MeshAsset mesh = MeshProjectionService::compilePolygonDocumentMesh(documents[documentIndex++]);
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
    }

    [[nodiscard]] std::unordered_set<int> liveModelingObjectIds() const {
        std::unordered_set<int> ids;
        ids.reserve(session_.objects().size());
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            ids.insert(object.id);
        }
        return ids;
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
        const std::unordered_set<int> liveIds = liveModelingObjectIds();
        return ModelingSessionMemento(std::make_shared<ModelingSessionMemento::Impl>(
                session_.state(),
                objectRegistry_.captureSceneIdentitiesForModelingObjects(scene_, liveIds)));
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

    void syncObjectSelectionFromScene(bool forceObjectMode = false) {
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
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
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
            syncSceneSelectionFromModeling();
            invalidateSelectionOverlayCaches();
            return;
        }

        std::vector<int> modelingObjectIds = session_.selected_object_ids();
        if (modelingObjectIds.empty()) {
            return;
        }
        activeObjectId = session_.active_object_id() > 0 ? session_.active_object_id() : modelingObjectIds.back();
        session_.set_selected_object_ids(modelingObjectIds, activeObjectId);
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
    }

    void syncSceneSelectionFromModeling() {
        if (!sceneObjectsSyncedForCurrentModelingContent()) {
            syncSceneObjectsFromModeling();
        }
        selectionProjectionService_.projectSceneSelectionFromModeling(session_, scene_, objectRegistry_);
    }

    void restoreSceneSelection(const std::vector<EntityId>& selected, EntityId active) {
        selectionProjectionService_.restoreSceneSelection(scene_, selected, active);
    }

    /**
     * Stores the Duplicate Scene State data contract used by the modeling application service layer.
     */
    struct DuplicateSceneState {
        EntityId source = InvalidEntityId;
        EntityId parent = InvalidEntityId;
        TransformComponent transform;
        VisibilityComponent visibility;
        LockComponent lock;
        std::optional<MeshComponent> mesh;
        std::optional<MaterialComponent> material;
    };

    [[nodiscard]] std::vector<DuplicateSceneState> captureSelectedDuplicateSceneState() const {
        std::vector<int> selectedObjectIds = session_.selected_object_ids();
        std::unordered_set<int> selectedObjectSet(selectedObjectIds.begin(), selectedObjectIds.end());
        std::vector<DuplicateSceneState> states;
        states.reserve(selectedObjectIds.size());

        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            if (!selectedObjectSet.contains(object.id)) {
                continue;
            }
            const std::optional<EntityId> entity = objectRegistry_.entityForModelingObject(object.id);
            if (!entity.has_value() || !scene_.exists(*entity)) {
                continue;
            }
            const TransformComponent* transform = scene_.transform(*entity);
            if (transform == nullptr) {
                continue;
            }
            DuplicateSceneState state;
            state.source = *entity;
            state.parent = scene_.parent(*entity);
            state.transform = *transform;
            if (const VisibilityComponent* visibility = scene_.visibility(*entity)) {
                state.visibility = *visibility;
            }
            if (const LockComponent* lock = scene_.lock(*entity)) {
                state.lock = *lock;
            }
            if (const MeshComponent* mesh = scene_.mesh(*entity)) {
                state.mesh = *mesh;
            }
            if (const MaterialComponent* material = scene_.material(*entity)) {
                state.material = *material;
            }
            states.push_back(state);
        }
        return states;
    }

    void applySceneStateToSelectedDuplicates(const std::vector<DuplicateSceneState>& sourceStates) {
        if (sourceStates.empty()) {
            return;
        }

        const std::vector<int> selectedDuplicateObjectIds = session_.selected_object_ids();
        if (selectedDuplicateObjectIds.size() != sourceStates.size()) {
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
        if (duplicateObjectIds.size() != sourceStates.size()) {
            return;
        }

        for (std::size_t index = 0; index < sourceStates.size(); ++index) {
            const std::optional<EntityId> duplicate = objectRegistry_.entityForModelingObject(duplicateObjectIds[index]);
            if (!duplicate.has_value() || !scene_.exists(*duplicate)) {
                continue;
            }

            const DuplicateSceneState& source = sourceStates[index];
            scene_.setTransform(*duplicate, source.transform);
            if (source.parent != InvalidEntityId && scene_.exists(source.parent)) {
                (void)scene_.setParent(*duplicate, source.parent, false);
            }
            scene_.setVisible(*duplicate, source.visibility.visible);
            scene_.setLocked(*duplicate, source.lock.locked);
            if (source.mesh.has_value()) {
                if (const MeshComponent* duplicateMesh = scene_.mesh(*duplicate)) {
                    MeshComponent nextMesh = *source.mesh;
                    nextMesh.mesh = duplicateMesh->mesh;
                    scene_.setMesh(*duplicate, nextMesh);
                }
            }
            if (source.material.has_value()) {
                scene_.setMaterial(*duplicate, *source.material);
            } else {
                scene_.clearMaterial(*duplicate);
            }
            objectRegistry_.rememberSceneEntity(duplicateObjectIds[index], scene_, *duplicate);
        }
    }

    ModelingCommandResult duplicateSelectedHierarchyForGroupDrag(DocumentCommandService& documentCommands) {
        return hierarchyDuplicateService_.duplicateSelectedHierarchyForGroupDrag(
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
                });
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

    ModelingCommandResult deleteSelection() {
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::delete_selection, true);
    }

    [[nodiscard]] bool hasSelectedSceneGroupRoot() const {
        return sceneAwareDeleteHasSelectedSceneGroupRoot(scene_);
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

    ModelingCommandResult executeContextAction(std::string_view actionId) {
        const ModelingContextActionResolution action =
                contextActionService_.resolveExecutableAction(selectionSummary(), actionId, modalToolOwnsViewport());
        if (!action.ok) {
            return {.ok = false, .message = action.message};
        }
        if (action.action.actionId == "delete_selection") {
            return deleteSelection();
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
            bool preserveDuplicateSceneState = true) {
        if (operation == ModelingInstantOperationId::DeleteSelection) {
            return deleteSelection();
        }

        const std::vector<DuplicateSceneState> duplicateSceneState =
                operation == ModelingInstantOperationId::DuplicateMeshes && preserveDuplicateSceneState
                ? captureSelectedDuplicateSceneState()
                : std::vector<DuplicateSceneState>{};

        const std::optional<RuntimeOperationExecutor::OperationCommand> command =
                runtimeOperationExecutor_.operationForInstantCommand(
                        operation,
                        interactionSettingsService_.settings());
        if (!command.has_value()) {
            return {.ok = false, .message = "Instant operation is not mapped to a runtime operation."};
        }

        ModelingCommandResult result = executeRuntimeOperation(command->operation, command->settings);
        if (result.ok && result.changed && operation == ModelingInstantOperationId::DuplicateMeshes) {
            applySceneStateToSelectedDuplicates(duplicateSceneState);
        }
        return result;
    }

    ModelingCommandResult beginTransformPreview(bool duplicateSelected) {
        const std::vector<DuplicateSceneState> duplicateSceneState =
                duplicateSelected ? captureSelectedDuplicateSceneState() : std::vector<DuplicateSceneState>{};
        const quader_editor_core::CommandResult result = session_.begin_transform_preview(duplicateSelected);
        if (result.ok) {
            invalidateSelectionOverlayCaches();
        }
        if (result.changed) {
            syncSceneObjectsFromModeling(true);
            syncSceneSelectionFromModeling();
            if (duplicateSelected) {
                applySceneStateToSelectedDuplicates(duplicateSceneState);
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
        if (runtime_.transform_drag_active() || hasTransformPreview()) {
            (void)cancelTransformDrag();
        }
        if (!session_.has_selection() || request.axis == ModelingTransformAxis::None) {
            return transformDragPreview(false);
        }

        const quader_editor_core::ViewportInteractionTool tool = toRuntime(request.tool);
        const quader_editor_core::TransformDragOperation operation =
                quader_editor_core::sanitize_transform_drag_operation(
                        toRuntime(request.operation),
                        tool,
                        session_.selection_mode());
        const quader_editor_core::TransformAxis axis = toRuntime(request.axis);
        const quader::QVec3 pivot = toRuntime(request.pivot);
        const bool duplicateSceneTransformPreview =
                request.duplicateSelected &&
                session_.selection_mode() == quader_editor_core::SelectionMode::object &&
                operation == quader_editor_core::TransformDragOperation::transform;
        const bool previewDriven =
                !duplicateSceneTransformPreview &&
                (request.duplicateSelected ||
                session_.selection_mode() != quader_editor_core::SelectionMode::object);
        const bool needsRuntimePreviewTransaction = previewDriven || request.duplicateSelected;

        runtime_.begin_transform_drag_state(tool, operation, axis, pivot);
        {
            quader_editor_core::TransformDragState drag = runtime_.transform_drag_state();
            drag.scene_transform_preview_eligible =
                    (duplicateSceneTransformPreview || !previewDriven) &&
                    operation == quader_editor_core::TransformDragOperation::transform;
            runtime_.set_transform_drag_state(drag);
        }
        transformDragController_.setPreviewDriven(previewDriven, request.duplicateSelected);

        if (needsRuntimePreviewTransaction) {
            const std::vector<DuplicateSceneState> duplicateSceneState =
                    request.duplicateSelected ? captureSelectedDuplicateSceneState() : std::vector<DuplicateSceneState>{};
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
                syncSceneObjectsFromModeling(true);
                syncSceneSelectionFromModeling();
                if (request.duplicateSelected) {
                    applySceneStateToSelectedDuplicates(duplicateSceneState);
                }
            }
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
            begin.has_screen_tip = true;
            begin.screen_tip = screenTip;
            begin.has_world_direction = request.hasWorldDirection;
            begin.world_direction = toRuntime(request.worldDirection);
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
    }

    ModelingTransformDragPreview updateTransformDrag(const ModelingTransformDragUpdateRequest& request) {
        if (!runtime_.transform_drag_active()) {
            if (hasTransformPreview()) {
                (void)cancelTransformDrag();
            }
            return transformDragPreview(false);
        }
        return applyTransformDragPreview(request);
    }

    ModelingTransformDragPreview commitTransformDrag() {
        const bool previewDriven = transformDragController_.previewDriven();
        const bool finishPreviewTransaction = previewDriven || transformDragController_.duplicateSelected();
        const quader_editor_core::TransformDragState repeatDrag = runtime_.transform_drag_state();
        bool changed = false;

        if (finishPreviewTransaction) {
            const quader_editor_core::PreviewTransactionFinishResult finish =
                    session_.commit_preview_transaction(quader_editor_core::PreviewTransactionKind::transform);
            if (finish.before.active) {
                invalidateSelectionOverlayCaches();
            }
            syncSceneObjectsFromModeling(finish.before.active);
            syncSceneSelectionFromModeling();
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
        }

        ModelingTransformDragPreview preview = transformDragPreview(changed);
        preview.previewDriven = previewDriven;
        clearTransformDragRuntimeState();
        return preview;
    }

    ModelingCommandResult cancelTransformDrag() {
        const bool activeBefore = runtime_.transform_drag_active() || hasTransformPreview();
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
                .changed = activeBefore && finish.restored,
        };
    }

    ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request) {
        std::vector<quader_editor_core::PolygonObject> pickingObjects = sceneTransformedObjectsForPicking();
        quader_editor_core::SelectionMode mode = session_.selection_mode();
        int activeObjectId = session_.active_object_id();
        quader_editor_core::SelectionEditOperation operation =
                quader_editor_core::selection_edit_operation_from_modifiers(
                        request.controlPressed,
                        request.shiftPressed);
        const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
        const quader_poly::Ray ray = toRuntime(request.ray);
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
            syncSceneSelectionFromModeling();
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
        syncSceneSelectionFromModeling();
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

    ModelingCommandResult activateViewportTool(ModelingViewportTool tool) {
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
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_hover, settings);
    }

    ModelingBoxPreview beginBoxFootprint(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxRayRuntimeCommandSettings settings;
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
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
        return executeBoxPreviewCommand(quader_editor_core::EditorRuntimeCommandId::update_box_footprint, settings);
    }

    ModelingBoxPreview beginBoxHeight(const ModelingBoxToolEvent& event) {
        quader_editor_core::BoxHeightRuntimeCommandSettings settings;
        settings.screen_position = toRuntime(event.screenPosition);
        settings.ray = toRuntime(event.ray);
        settings.grid_size = event.gridSize;
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

        std::vector<int> objectIds;
        objectIds.reserve(entities.size());
        for (const EntityId entity : entities) {
            const auto object = objectRegistry_.modelingObjectForEntity(entity);
            if (object.has_value()) {
                objectIds.push_back(*object);
            }
        }
        if (!entities.empty() && objectIds.empty()) {
            return {.ok = false, .message = "Selection does not contain modeling objects."};
        }

        int activeObjectId = -1;
        if (active != InvalidEntityId) {
            const auto object = objectRegistry_.modelingObjectForEntity(active);
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
            syncSceneSelectionFromModeling();
            return {.ok = true, .changed = false};
        }

        session_.set_selected_object_ids(objectIds, activeObjectId);
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = true};
    }

    ModelingCommandResult setComponentSelection(const ModelingComponentSelectionRequest& request) {
        quader_editor_core::PolygonObject* object = findObject(session_, request.modelingObjectId);
        if (object == nullptr) {
            return {.ok = false, .message = "Selection target mesh was not found."};
        }

        const quader_editor_core::SelectionMode mode = appSelectionModeToRuntime(request.mode);
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
        session_.set_component_selection(request.modelingObjectId, std::move(selection), mode);
        syncSceneSelectionFromModeling();
        invalidateSelectionOverlayCaches();
        return {.ok = true, .changed = session_.state().selection_revision != selectionBefore};
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

    [[nodiscard]] std::uint64_t selectionOverlayToolSignature() const {
        std::uint64_t hash = 1469598103934665603ULL;
        mixToolStateHash(hash, static_cast<int>(runtime_.active_tool()));

        const quader_editor_core::CutToolState& cut = runtime_.cut_state();
        const quader_editor_core::CutHoverState& cutHover = runtime_.cut_hover();
        mixToolStateHash(hash, quader_editor_core::cut_is_active(cut));
        mixToolStateHash(hash, static_cast<int>(cut.points.size()));
        for (const quader::QVec3& point : cut.points) {
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
        for (const quader::QVec3& point : mirror.plane.points) {
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
        quader_editor_core::TransformDragPreviewInput input;
        input.camera = toRuntime(request.camera);
        input.screen_position = toRuntime(request.screenPosition);
        input.ray = toRuntime(request.ray);
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
            syncSceneObjectsFromModeling(true);
            syncSceneSelectionFromModeling();
        }
        return transformDragPreview(result.changed);
    }

    void syncSceneObjectsFromModeling(bool force = false) {
        meshSyncService_.syncSceneObjectsFromModeling(session_, scene_, assets_, sceneSyncService_, force);
    }

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

        quader_editor_core::BoxPreviewDescriptor preview = runtime_.box_preview_descriptor(true);
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

bool ModelingSessionService::runtimeAvailable() const {
    return impl_->runtimeAvailable();
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

std::optional<EntityId> ModelingSessionService::entityForModelingObject(int modelingObjectId) const {
    return impl_->entityForModelingObject(modelingObjectId);
}

std::optional<int> ModelingSessionService::modelingObjectForEntity(EntityId entity) const {
    return impl_->modelingObjectForEntity(entity);
}

std::optional<MeshAsset> ModelingSessionService::compileObjectMesh(int modelingObjectId) const {
    return impl_->compileObjectMesh(modelingObjectId);
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

ModelingCommandResult ModelingSessionService::restoreHistoryMemento(const ModelingSessionMemento& memento) {
    return impl_->restoreHistoryMemento(memento);
}

void ModelingSessionService::projectObjectSelectionFromScene() {
    impl_->syncObjectSelectionFromScene();
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

std::unique_ptr<IUndoableCommand> ModelingSessionService::createDeleteSelectionCommand() {
    return impl_->createDeleteSelectionCommand();
}

bool ModelingSessionService::hasSelectedSceneGroupRoot() const {
    return impl_->hasSelectedSceneGroupRoot();
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
