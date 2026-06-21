#include "quader/modeling/ModelingSessionService.h"

#include "quader/assets/AssetRegistry.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/scene/SceneEntityNaming.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
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
#include <quader_editor_core/tool/mirror_tool.h>
#include <quader_editor_core/tool/move_tool.h>
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

quader_editor_core::EditorOperationSettings defaultSettingsForOperation(
        quader_editor_core::EditorOperationId operation,
        std::string_view actionId = {}) {
    switch (operation) {
    case quader_editor_core::EditorOperationId::bridge:
        return quader_editor_core::BridgeOperationSettings{};
    case quader_editor_core::EditorOperationId::edge_bevel:
        return quader_editor_core::EdgeBevelOperationSettings{};
    case quader_editor_core::EditorOperationId::thicken:
        return quader_editor_core::ThickenOperationSettings{};
    case quader_editor_core::EditorOperationId::slice_quad:
        return quader_editor_core::SliceQuadOperationSettings{};
    case quader_editor_core::EditorOperationId::merge_by_distance:
        return quader_editor_core::MergeByDistanceOperationSettings{};
    case quader_editor_core::EditorOperationId::bevel_vertices:
        return quader_editor_core::BevelVerticesOperationSettings{};
    case quader_editor_core::EditorOperationId::duplicate_meshes:
        return quader_editor_core::DuplicateMeshesOperationSettings{};
    case quader_editor_core::EditorOperationId::recalculate_face_normals: {
        quader_editor_core::RecalculateFaceNormalsOperationSettings settings;
        settings.outside = actionId != "recalculate_inside_mesh" && actionId != "recalculate_inside_faces";
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

ViewportOverlayLayer fromSdk(quader_editor_core::SelectionOverlayLayer layer) {
    return static_cast<ViewportOverlayLayer>(static_cast<int>(layer));
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
        result.triangles.push_back(ViewportOverlayTriangle{
                .layer = fromSdk(triangle.layer),
                .a = transformObjectPoint(triangle.object_id, fromSdk(triangle.a), entityByObject, scene),
                .b = transformObjectPoint(triangle.object_id, fromSdk(triangle.b), entityByObject, scene),
                .c = transformObjectPoint(triangle.object_id, fromSdk(triangle.c), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(triangle.layer), triangle.color_abgr),
        });
    }
    for (const quader_editor_core::SelectionOverlayLine& line : frame.lines) {
        if (!objectEffectivelyVisible(line.object_id, entityByObject, scene)) {
            continue;
        }
        result.lines.push_back(ViewportOverlayLine{
                .layer = fromSdk(line.layer),
                .start = transformObjectPoint(line.object_id, fromSdk(line.start), entityByObject, scene),
                .end = transformObjectPoint(line.object_id, fromSdk(line.end), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(line.layer), line.color_abgr),
                .widthPixels = line.width_pixels,
        });
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : frame.points) {
        if (!objectEffectivelyVisible(point.object_id, entityByObject, scene)) {
            continue;
        }
        result.points.push_back(ViewportOverlayPoint{
                .layer = fromSdk(point.layer),
                .position = transformObjectPoint(point.object_id, fromSdk(point.position), entityByObject, scene),
                .color = viewportOverlayColorFor(fromSdk(point.layer), point.color_abgr),
                .sizePixels = point.size_pixels,
        });
    }
    return result;
}

MeshAsset toMeshAsset(const quader_editor_core::PolygonObject& object) {
    const quader_poly::CompiledMesh compiled = quader_poly::compile_document(object.document);

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

const quader_editor_core::PolygonObject* findObject(
        const quader_editor_core::EditorSession& session,
        int sdkObjectId) {
    const auto& objects = session.objects();
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
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayTriangle& triangle) {
    if (!hoverFrame.hover_target_selected) {
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
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayLine& line) {
    if (!hoverFrame.hover_target_selected) {
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
        const quader_editor_core::SelectionOverlayFrame& hoverFrame,
        const quader_editor_core::SelectionOverlayPoint& point) {
    if (!hoverFrame.hover_target_selected || point.layer != quader_editor_core::SelectionOverlayLayer::selected_vertex) {
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
        const quader_editor_core::SelectionOverlayFrame& hoverFrame) {
    target.triangles.reserve(target.triangles.size() + base.triangles.size());
    target.lines.reserve(target.lines.size() + base.lines.size());
    target.points.reserve(target.points.size() + base.points.size());
    for (const quader_editor_core::SelectionOverlayTriangle& triangle : base.triangles) {
        if (!hoverFrameSuppressesTriangle(hoverFrame, triangle)) {
            target.triangles.push_back(triangle);
        }
    }
    for (const quader_editor_core::SelectionOverlayLine& line : base.lines) {
        if (!hoverFrameSuppressesLine(hoverFrame, line)) {
            target.lines.push_back(line);
        }
    }
    for (const quader_editor_core::SelectionOverlayPoint& point : base.points) {
        if (!hoverFrameSuppressesPoint(hoverFrame, point)) {
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
    Impl(SceneDocument& scene, AssetRegistry& assets) : scene_(scene), assets_(assets) {}

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
        std::vector<ModelingObjectRecord> records;
        records.reserve(entityByObject_.size());
        for (const auto& [sdkObjectId, entity] : entityByObject_) {
            ModelingObjectRecord record;
            record.sdkObjectId = sdkObjectId;
            record.entity = entity;
            if (const MeshComponent* mesh = scene_.mesh(entity)) {
                record.mesh = mesh->mesh;
            }
            if (const NameComponent* name = scene_.name(entity)) {
                record.name = name->name;
            }
            records.push_back(std::move(record));
        }
        std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.entity < rhs.entity;
        });
        return records;
    }

    [[nodiscard]] std::optional<EntityId> entityForSdkObject(int sdkObjectId) const {
        const auto it = entityByObject_.find(sdkObjectId);
        if (it == entityByObject_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<int> sdkObjectForEntity(EntityId entity) const {
        const auto it = objectByEntity_.find(entity);
        if (it == objectByEntity_.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    [[nodiscard]] std::optional<MeshAsset> compileObjectMesh(int sdkObjectId) const {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolygonObject* object = findObject(session_, sdkObjectId);
        if (object == nullptr) {
            return std::nullopt;
        }
        return toMeshAsset(*object);
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
                const Vec3 world = transformObjectPoint(object.id, fromSdk(vertex->position), entityByObject_, scene_);
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
        return fromSdk(session_.selection_mode());
#else
        return ModelingSelectionMode::Object;
#endif
    }

    [[nodiscard]] ModelingSelectionSummary selectionSummary() const {
#if QUADER_HAS_MODELING_SDK
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
        if (selectionOverlayCacheValid_ &&
                selectionOverlayCacheContentRevision_ == contentRevision &&
                selectionOverlayCacheSelectionRevision_ == selectionRevision &&
                selectionOverlayCacheSceneRevision_ == sceneRevision &&
                selectionOverlayCacheSelectionMode_ == selectionMode &&
                hoverMatches(selectionOverlayCacheHover_, hover)) {
            return selectionOverlayCacheFrame_;
        }

        selectionOverlayCacheFrame_ = fromSdk(selectionOverlayCurrentFrame(hover),
                entityByObject_,
                scene_);
        selectionOverlayCacheValid_ = true;
        selectionOverlayCacheContentRevision_ = contentRevision;
        selectionOverlayCacheSelectionRevision_ = selectionRevision;
        selectionOverlayCacheSceneRevision_ = sceneRevision;
        selectionOverlayCacheSelectionMode_ = selectionMode;
        selectionOverlayCacheHover_ = hover;
        return selectionOverlayCacheFrame_;
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

    void syncObjectSelectionFromScene() {
#if QUADER_HAS_MODELING_SDK
        if (session_.selection_mode() != quader_editor_core::SelectionMode::object) {
            return;
        }

        std::vector<int> objectIds;
        objectIds.reserve(scene_.selectedEntities().size());
        for (const EntityId entity : scene_.selectedEntities()) {
            const auto it = objectByEntity_.find(entity);
            if (it != objectByEntity_.end()) {
                objectIds.push_back(it->second);
            }
        }

        int activeObjectId = -1;
        const auto primary = objectByEntity_.find(scene_.primarySelection());
        if (primary != objectByEntity_.end()) {
            activeObjectId = primary->second;
        }

        std::vector<int> currentObjectIds = session_.selected_object_ids();
        std::sort(currentObjectIds.begin(), currentObjectIds.end());
        std::vector<int> desiredObjectIds = objectIds;
        std::sort(desiredObjectIds.begin(), desiredObjectIds.end());
        if (currentObjectIds == desiredObjectIds && session_.active_object_id() == activeObjectId) {
            return;
        }

        session_.set_selected_object_ids(objectIds, activeObjectId);
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
            const auto it = entityByObject_.find(objectId);
            if (it != entityByObject_.end()) {
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
        const auto active = entityByObject_.find(activeObjectId);
        if (active != entityByObject_.end()) {
            const SelectionComponent* selection = scene_.selection(active->second);
            if (selection && selection->selected) {
                scene_.setActiveSelection(active->second);
            }
        }
#endif
    }

    ModelingCommandResult setSelectionMode(ModelingSelectionMode mode) {
#if QUADER_HAS_MODELING_SDK
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
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::copy_selected_meshes, false);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult pasteSelection() {
#if QUADER_HAS_MODELING_SDK
        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::paste_copied_meshes;
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
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::undo, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult redo() {
#if QUADER_HAS_MODELING_SDK
        return executeRuntimeNoSettings(quader_editor_core::EditorRuntimeCommandId::redo, true);
#else
        return {.ok = false, .message = "Modeling SDK is not available in this build."};
#endif
    }

    ModelingCommandResult repeatLastAction() {
#if QUADER_HAS_MODELING_SDK
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::CommandResult result =
                quader_editor_core::execute_repeat_last_action(session_, repeatLastAction_);
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
        quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result.command);
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

    [[nodiscard]] bool modalToolOwnsViewport() const {
#if QUADER_HAS_MODELING_SDK
        switch (runtime_.active_tool()) {
        case quader_editor_core::ViewportInteractionTool::box:
        case quader_editor_core::ViewportInteractionTool::insert_edge_loop:
        case quader_editor_core::ViewportInteractionTool::knife:
        case quader_editor_core::ViewportInteractionTool::cut:
        case quader_editor_core::ViewportInteractionTool::poly:
            return true;
        case quader_editor_core::ViewportInteractionTool::select:
        case quader_editor_core::ViewportInteractionTool::move:
        case quader_editor_core::ViewportInteractionTool::extend:
        case quader_editor_core::ViewportInteractionTool::extrude:
        case quader_editor_core::ViewportInteractionTool::rotate:
        case quader_editor_core::ViewportInteractionTool::scale:
        case quader_editor_core::ViewportInteractionTool::mirror:
        case quader_editor_core::ViewportInteractionTool::unknown:
            break;
        }
#endif
        return false;
    }

    [[nodiscard]] ModelingActionQuery effectiveActionQuery(ModelingActionQuery query) const {
        query.modalToolOwnsViewport = query.modalToolOwnsViewport || modalToolOwnsViewport();
        return query;
    }

    [[nodiscard]] ModelingActionSet contextActionSet(ModelingActionQuery query) const {
        return ModelingActionCatalog::actionSetFor(selectionSummary(), effectiveActionQuery(query));
    }

    [[nodiscard]] std::vector<ModelingActionItem> contextActions(ModelingActionQuery query) const {
        return ModelingActionCatalog::actionsFor(selectionSummary(), effectiveActionQuery(query));
    }

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

        return executeSdkOperation(
                operation->operation_id(),
                defaultSettingsForOperation(operation->operation_id(), found->actionId));
#else
        (void)actionId;
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
        quader_editor_core::EditorOperationCommand command;
        command.operation_id = sdkOperation;
        command.phase = quader_editor_core::EditorOperationCommandPhase::execute;
        command.settings = std::move(settings);

        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorOperationCommandResult result =
                quader_editor_core::execute_command(session_, command);
        if (session_.state().content_revision != contentBefore) {
            syncSceneObjectsFromSdk();
        }
        syncSceneSelectionFromSdk();
        quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result.command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }
#endif

    ModelingCommandResult executeInstantOperation(ModelingInstantOperationId operation) {
#if QUADER_HAS_MODELING_SDK
        if (operation == ModelingInstantOperationId::DeleteSelection) {
            return deleteSelection();
        }

        quader_editor_core::EditorOperationId sdkOperation = quader_editor_core::EditorOperationId::delete_selection;
        quader_editor_core::EditorOperationSettings settings = quader_editor_core::SelectionOperationSettings{};
        switch (operation) {
        case ModelingInstantOperationId::DuplicateMeshes:
            sdkOperation = quader_editor_core::EditorOperationId::duplicate_meshes;
            settings = quader_editor_core::DuplicateMeshesOperationSettings{};
            break;
        case ModelingInstantOperationId::CombineMeshes:
            sdkOperation = quader_editor_core::EditorOperationId::combine_meshes;
            break;
        case ModelingInstantOperationId::ExtractFaces:
            sdkOperation = quader_editor_core::EditorOperationId::extract_faces;
            break;
        case ModelingInstantOperationId::DetachFaces:
            sdkOperation = quader_editor_core::EditorOperationId::detach_faces;
            break;
        case ModelingInstantOperationId::Bridge:
            sdkOperation = quader_editor_core::EditorOperationId::bridge;
            settings = quader_editor_core::BridgeOperationSettings{};
            break;
        case ModelingInstantOperationId::SnapVerticesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::snap_vertices_to_active;
            break;
        case ModelingInstantOperationId::ConnectVertices:
            sdkOperation = quader_editor_core::EditorOperationId::connect_vertices;
            break;
        case ModelingInstantOperationId::DissolveVertices:
            sdkOperation = quader_editor_core::EditorOperationId::dissolve_vertices;
            break;
        case ModelingInstantOperationId::BevelVertices:
            sdkOperation = quader_editor_core::EditorOperationId::bevel_vertices;
            settings = quader_editor_core::BevelVerticesOperationSettings{};
            break;
        case ModelingInstantOperationId::MergeVerticesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::merge_vertices_to_active;
            break;
        case ModelingInstantOperationId::MergeVerticesToCenter:
            sdkOperation = quader_editor_core::EditorOperationId::merge_vertices_to_center;
            break;
        case ModelingInstantOperationId::SnapEdgesToActive:
            sdkOperation = quader_editor_core::EditorOperationId::snap_edges_to_active;
            break;
        case ModelingInstantOperationId::ConnectEdges:
            sdkOperation = quader_editor_core::EditorOperationId::connect_edges;
            break;
        case ModelingInstantOperationId::SplitEdges:
            sdkOperation = quader_editor_core::EditorOperationId::split_edges;
            break;
        case ModelingInstantOperationId::DissolveEdges:
            sdkOperation = quader_editor_core::EditorOperationId::dissolve_edges;
            break;
        case ModelingInstantOperationId::MergeEdges:
            sdkOperation = quader_editor_core::EditorOperationId::merge_edges;
            break;
        case ModelingInstantOperationId::CollapseEdges:
            sdkOperation = quader_editor_core::EditorOperationId::collapse_edges;
            break;
        case ModelingInstantOperationId::FillHole:
            sdkOperation = quader_editor_core::EditorOperationId::fill_hole;
            break;
        case ModelingInstantOperationId::CombineFaces:
            sdkOperation = quader_editor_core::EditorOperationId::combine_faces;
            break;
        case ModelingInstantOperationId::CollapseFaces:
            sdkOperation = quader_editor_core::EditorOperationId::collapse_faces;
            break;
        case ModelingInstantOperationId::RadialAlign:
            sdkOperation = quader_editor_core::EditorOperationId::radial_align;
            break;
        case ModelingInstantOperationId::FlipFaceNormals:
            sdkOperation = quader_editor_core::EditorOperationId::flip_face_normals;
            break;
        case ModelingInstantOperationId::RecalculateFaceNormals:
            sdkOperation = quader_editor_core::EditorOperationId::recalculate_face_normals;
            settings = quader_editor_core::RecalculateFaceNormalsOperationSettings{};
            break;
        case ModelingInstantOperationId::ShadeFacesSmooth:
            sdkOperation = quader_editor_core::EditorOperationId::shade_faces_smooth;
            break;
        case ModelingInstantOperationId::ShadeFacesFlat:
            sdkOperation = quader_editor_core::EditorOperationId::shade_faces_flat;
            break;
        case ModelingInstantOperationId::HardenEdgeNormals:
            sdkOperation = quader_editor_core::EditorOperationId::harden_edge_normals;
            break;
        case ModelingInstantOperationId::SoftenEdgeNormals:
            sdkOperation = quader_editor_core::EditorOperationId::soften_edge_normals;
            break;
        case ModelingInstantOperationId::DeleteSelection:
            break;
        }

        return executeSdkOperation(sdkOperation, std::move(settings));
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
        return {
                .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
                .changed = finish.before.changed,
        };
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
        return {
                .ok = !finish.before.active || finish.before.kind == quader_editor_core::PreviewTransactionKind::transform,
                .changed = finish.restored,
        };
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

        quader_editor_core::record_repeatable_transform(
                repeatLastAction_,
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
        activeTransformDragPreviewDriven_ = previewDriven;
        activeTransformDragDuplicateSelected_ = request.duplicateSelected;

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
        const bool previewDriven = activeTransformDragPreviewDriven_;
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
                quader_editor_core::record_repeatable_transform(
                        repeatLastAction_,
                        session_,
                        repeatDrag,
                        activeTransformDragDuplicateSelected_);
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
    ModelingCommandResult setActiveRuntimeTool(quader_editor_core::ViewportInteractionTool tool) {
        quader_editor_core::SetActiveToolRuntimeCommandSettings settings;
        settings.tool = tool;

        quader_editor_core::EditorRuntimeCommand command;
        command.command_id = quader_editor_core::EditorRuntimeCommandId::set_active_tool;
        command.settings = settings;

        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, command);
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }
#endif

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

private:
#if QUADER_HAS_MODELING_SDK
    struct PickFaceGeometryCacheEntry {
        int objectId = -1;
        std::uint64_t geometrySignature = 0;
    };

    struct PickingCache {
        bool objectsValid = false;
        bool boundsValid = false;
        bool faceGeometryValid = false;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;
        std::uint64_t sceneRevision = 0;
        std::vector<quader_editor_core::PolygonObject> objects;
        std::vector<quader_editor_core::PickObjectBounds> bounds;
        std::vector<quader_editor_core::PickObjectFaceGeometry> faceGeometry;
        std::vector<PickFaceGeometryCacheEntry> faceGeometryEntries;
    };

    struct OverlayBaseObjectCacheEntry {
        int objectId = -1;
        std::uint64_t geometrySignature = 0;
        quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
        bool selected = false;
        quader_poly::Selection selection;
        quader_editor_core::SelectionOverlayFrame frame;
    };

    struct OverlayBaseCache {
        bool valid = false;
        std::uint64_t contentRevision = 0;
        std::uint64_t selectionRevision = 0;
        quader_editor_core::SelectionMode selectionMode = quader_editor_core::SelectionMode::object;
        quader_editor_core::SelectionOverlayFrame frame;
        std::vector<OverlayBaseObjectCacheEntry> objectEntries;
    };

    void invalidateSelectionOverlayCaches() const {
        selectionOverlayCacheValid_ = false;
        overlayBaseCache_.valid = false;
    }

    static bool hoverMatches(
            const quader_editor_core::SelectionHoverState& left,
            const quader_editor_core::SelectionHoverState& right) {
        return left.object_id == right.object_id &&
                left.kind == right.kind &&
                left.vertex_id == right.vertex_id &&
                left.edge == right.edge &&
                left.face_id == right.face_id &&
                left.selected_modifier_pressed == right.selected_modifier_pressed &&
                left.candidate_object_id == right.candidate_object_id;
    }

    [[nodiscard]] bool sceneObjectsSyncedForCurrentSdkContent() const {
        if (!sceneObjectsSyncValid_ || syncedSdkContentRevision_ != session_.state().content_revision) {
            return false;
        }
        if (entityByObject_.size() != session_.objects().size()) {
            return false;
        }
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            const auto entity = entityByObject_.find(object.id);
            if (entity == entityByObject_.end() || !scene_.exists(entity->second)) {
                return false;
            }
            const ModelingObjectComponent* modelingObject = scene_.modelingObject(entity->second);
            const MeshComponent* mesh = scene_.mesh(entity->second);
            if (modelingObject == nullptr || mesh == nullptr || mesh->mesh == InvalidAssetId) {
                return false;
            }
        }
        return true;
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
        const quader_editor_core::PreviewTransactionStatus status = session_.preview_transaction_status();
        return status.active && status.kind == quader_editor_core::PreviewTransactionKind::transform;
    }

    void clearTransformDragRuntimeState() {
        runtime_.clear_transform_drag_state();
        runtime_.clear_gizmo_hover();
        activeTransformDragPreviewDriven_ = false;
        activeTransformDragDuplicateSelected_ = false;
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
        preview.previewDriven = activeTransformDragPreviewDriven_;
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
            const bool previewDriven = activeTransformDragPreviewDriven_;
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
        if (activeTransformDragPreviewDriven_) {
            syncSceneObjectsFromSdk(true);
            syncSceneSelectionFromSdk();
        }
        return transformDragPreview(result.changed);
    }
#endif

    void syncSceneObjectsFromSdk(bool force = false) {
#if QUADER_HAS_MODELING_SDK
        if (!force && sceneObjectsSyncedForCurrentSdkContent()) {
            return;
        }

        std::unordered_map<std::uint32_t, AssetId> materialBySlot;
        std::optional<AssetId> firstModelingMaterial;
        for (const auto& [objectId, entity] : entityByObject_) {
            const ModelingObjectComponent* modelingObject = scene_.modelingObject(entity);
            const MaterialComponent* material = scene_.material(entity);
            if (modelingObject == nullptr || material == nullptr || material->material == InvalidAssetId) {
                continue;
            }
            materialBySlot.try_emplace(modelingObject->materialSlot, material->material);
            if (!firstModelingMaterial) {
                firstModelingMaterial = material->material;
            }
        }

        std::unordered_map<int, bool> liveObjects;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            liveObjects[object.id] = true;
            const std::uint32_t materialSlot = static_cast<std::uint32_t>(object.material_id);
            const auto existing = entityByObject_.find(object.id);
            if (existing == entityByObject_.end()) {
                const std::string name = SceneEntityNaming::nextNameForKind(scene_, SceneEntityKind::Mesh);
                MeshAsset mesh = toMeshAsset(object);
                mesh.name = name;
                const AssetId meshId = assets_.addMesh(std::move(mesh));
                const EntityId entity = scene_.createEntity(name);
                scene_.setModelingObject(entity, ModelingObjectComponent{
                        .objectId = object.id,
                        .materialSlot = materialSlot,
                });
                scene_.setMesh(entity, MeshComponent{.mesh = meshId});
                const auto material = materialBySlot.find(materialSlot);
                if (material != materialBySlot.end()) {
                    scene_.setMaterial(entity, MaterialComponent{.material = material->second});
                } else if (firstModelingMaterial) {
                    scene_.setMaterial(entity, MaterialComponent{.material = *firstModelingMaterial});
                }
                entityByObject_[object.id] = entity;
                objectByEntity_[entity] = object.id;
                continue;
            }

            const EntityId entity = existing->second;
            MeshAsset mesh = toMeshAsset(object);
            if (const NameComponent* name = scene_.name(entity)) {
                mesh.name = name->name;
            }
            AssetId meshId = InvalidAssetId;
            if (const MeshComponent* meshComponent = scene_.mesh(entity)) {
                meshId = meshComponent->mesh;
            }
            bool meshContentsReplaced = false;
            if (meshId == InvalidAssetId) {
                meshId = assets_.addMesh(std::move(mesh));
            } else if (assets_.replaceMesh(meshId, std::move(mesh))) {
                meshContentsReplaced = true;
            } else {
                MeshAsset replacement = toMeshAsset(object);
                if (const NameComponent* name = scene_.name(entity)) {
                    replacement.name = name->name;
                }
                meshId = assets_.addMesh(std::move(replacement));
            }
            const ModelingObjectComponent nextObject{
                    .objectId = object.id,
                    .materialSlot = materialSlot,
            };
            const ModelingObjectComponent* currentObject = scene_.modelingObject(entity);
            if (currentObject == nullptr ||
                    currentObject->objectId != nextObject.objectId ||
                    currentObject->materialSlot != nextObject.materialSlot) {
                scene_.setModelingObject(entity, nextObject);
            }
            const MeshComponent* currentMesh = scene_.mesh(entity);
            if (currentMesh == nullptr || currentMesh->mesh != meshId || meshContentsReplaced) {
                scene_.setMesh(entity, MeshComponent{.mesh = meshId});
            }
        }

        std::vector<int> removedObjects;
        for (const auto& [objectId, entity] : entityByObject_) {
            if (!liveObjects.contains(objectId)) {
                removedObjects.push_back(objectId);
            }
        }
        for (const int objectId : removedObjects) {
            const EntityId entity = entityByObject_[objectId];
            if (scene_.exists(entity)) {
                scene_.destroyEntity(entity);
            }
            objectByEntity_.erase(entity);
            entityByObject_.erase(objectId);
        }
        syncedSdkContentRevision_ = session_.state().content_revision;
        sceneObjectsSyncValid_ = true;
#endif
    }

#if QUADER_HAS_MODELING_SDK
    [[nodiscard]] const std::vector<quader_editor_core::PolygonObject>& cachedSceneTransformedObjectsForPicking() const {
        const std::uint64_t contentRevision = session_.state().content_revision;
        const std::uint64_t selectionRevision = session_.state().selection_revision;
        const std::uint64_t sceneRevision = scene_.revision();
        const bool geometryChanged =
                !pickingCache_.objectsValid ||
                pickingCache_.contentRevision != contentRevision ||
                pickingCache_.sceneRevision != sceneRevision;
        if (pickingCache_.objectsValid &&
                pickingCache_.contentRevision == contentRevision &&
                pickingCache_.selectionRevision == selectionRevision &&
                pickingCache_.sceneRevision == sceneRevision) {
            return pickingCache_.objects;
        }

        pickingCache_.objects.clear();
        const std::vector<quader_editor_core::PolygonObject> sdkObjects = session_.objects();
        pickingCache_.objects.reserve(sdkObjects.size());
        for (quader_editor_core::PolygonObject object : sdkObjects) {
            const auto entity = entityByObject_.find(object.id);
            if (entity == entityByObject_.end()) {
                continue;
            }
            const VisibilityComponent* visibility = scene_.visibility(entity->second);
            if (!scene_.isEffectivelyVisible(entity->second) ||
                    (visibility != nullptr && !visibility->selectable)) {
                continue;
            }
            const Mat4 worldFromObject = scene_.worldTransformMatrix(entity->second);
            for (quader_poly::Vertex& vertex : object.document.vertices) {
                vertex.position = toSdk(transformPoint(worldFromObject, fromSdk(vertex.position)));
            }
            pickingCache_.objects.push_back(std::move(object));
        }
        pickingCache_.contentRevision = contentRevision;
        pickingCache_.selectionRevision = selectionRevision;
        pickingCache_.sceneRevision = sceneRevision;
        pickingCache_.objectsValid = true;
        if (geometryChanged) {
            pickingCache_.boundsValid = false;
            pickingCache_.faceGeometryValid = false;
        }
        return pickingCache_.objects;
    }

    [[nodiscard]] std::vector<quader_editor_core::PolygonObject> sceneTransformedObjectsForPicking() const {
        return cachedSceneTransformedObjectsForPicking();
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectBounds>& cachedPickObjectBounds() const {
        const std::vector<quader_editor_core::PolygonObject>& objects = cachedSceneTransformedObjectsForPicking();
        if (pickingCache_.boundsValid) {
            return pickingCache_.bounds;
        }

        pickingCache_.bounds.clear();
        pickingCache_.bounds.reserve(objects.size());
        for (const quader_editor_core::PolygonObject& object : objects) {
            pickingCache_.bounds.push_back(buildObjectPickBounds(object));
        }
        pickingCache_.boundsValid = true;
        return pickingCache_.bounds;
    }

    [[nodiscard]] const std::vector<quader_editor_core::PickObjectFaceGeometry>& cachedPickFaceGeometry(
            quader_poly::PickOptions pickOptions = {}) const {
        const std::vector<quader_editor_core::PolygonObject>& objects = cachedSceneTransformedObjectsForPicking();
        if (pickingCache_.faceGeometryValid) {
            bool signaturesMatch = pickingCache_.faceGeometryEntries.size() == objects.size();
            for (std::size_t index = 0; signaturesMatch && index < objects.size(); ++index) {
                signaturesMatch =
                        pickingCache_.faceGeometryEntries[index].objectId == objects[index].id &&
                        pickingCache_.faceGeometryEntries[index].geometrySignature ==
                                objectPickFaceGeometrySignature(objects[index], pickOptions);
            }
            if (signaturesMatch) {
                return pickingCache_.faceGeometry;
            }
        }

        std::vector<PickFaceGeometryCacheEntry> previousEntries = std::move(pickingCache_.faceGeometryEntries);
        std::vector<quader_editor_core::PickObjectFaceGeometry> previousGeometry = std::move(pickingCache_.faceGeometry);
        std::vector<PickFaceGeometryCacheEntry> nextEntries;
        std::vector<quader_editor_core::PickObjectFaceGeometry> nextGeometry;
        nextEntries.reserve(objects.size());
        nextGeometry.reserve(objects.size());

        for (const quader_editor_core::PolygonObject& object : objects) {
            const std::uint64_t signature = objectPickFaceGeometrySignature(object, pickOptions);
            auto reusable = previousEntries.end();
            for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
                if (it->objectId == object.id && it->geometrySignature == signature) {
                    reusable = it;
                    break;
                }
            }
            if (reusable != previousEntries.end()) {
                const std::size_t index = static_cast<std::size_t>(std::distance(previousEntries.begin(), reusable));
                nextGeometry.push_back(std::move(previousGeometry[index]));
                reusable->objectId = -1;
            } else {
                nextGeometry.push_back({
                        object.id,
                        quader_poly::build_face_pick_geometry(object.document, pickOptions),
                });
            }
            nextEntries.push_back({
                    .objectId = object.id,
                    .geometrySignature = signature,
            });
        }

        pickingCache_.faceGeometry = std::move(nextGeometry);
        pickingCache_.faceGeometryEntries = std::move(nextEntries);
        pickingCache_.faceGeometryValid = true;
        return pickingCache_.faceGeometry;
    }

    [[nodiscard]] const quader_editor_core::SelectionOverlayFrame& selectionOverlayBaseFrame() const {
        const std::uint64_t contentRevision = session_.state().content_revision;
        const std::uint64_t selectionRevision = session_.state().selection_revision;
        const quader_editor_core::SelectionMode selectionMode = session_.selection_mode();
        if (overlayBaseCache_.valid &&
                overlayBaseCache_.contentRevision == contentRevision &&
                overlayBaseCache_.selectionRevision == selectionRevision &&
                overlayBaseCache_.selectionMode == selectionMode) {
            return overlayBaseCache_.frame;
        }

        std::vector<OverlayBaseObjectCacheEntry> previousEntries = std::move(overlayBaseCache_.objectEntries);
        OverlayBaseCache next;
        next.valid = true;
        next.contentRevision = contentRevision;
        next.selectionRevision = selectionRevision;
        next.selectionMode = selectionMode;
        next.objectEntries.reserve(session_.objects().size());

        const quader_editor_core::SelectionHoverState emptyHover;
        for (const quader_editor_core::PolygonObject& object : session_.objects()) {
            const std::uint64_t signature = objectPickFaceGeometrySignature(object);
            auto reusable = previousEntries.end();
            for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
                if (it->objectId == object.id &&
                        it->geometrySignature == signature &&
                        it->selectionMode == selectionMode &&
                        it->selected == object.selected &&
                        sdkSelectionMatches(it->selection, object.selection)) {
                    reusable = it;
                    break;
                }
            }

            OverlayBaseObjectCacheEntry entry;
            if (reusable != previousEntries.end()) {
                entry = std::move(*reusable);
                reusable->objectId = -1;
            } else {
                entry.objectId = object.id;
                entry.geometrySignature = signature;
                entry.selectionMode = selectionMode;
                entry.selected = object.selected;
                entry.selection = object.selection;
                entry.frame = quader_editor_core::build_selection_overlay_frame(
                        std::span<const quader_editor_core::PolygonObject>(&object, 1U),
                        selectionMode,
                        emptyHover,
                        {});
            }
            appendOverlayFrame(next.frame, entry.frame);
            next.objectEntries.push_back(std::move(entry));
        }

        overlayBaseCache_ = std::move(next);
        return overlayBaseCache_.frame;
    }

    [[nodiscard]] quader_editor_core::SelectionOverlayFrame selectionOverlayCurrentFrame(
            const quader_editor_core::SelectionHoverState& hover) const {
        const quader_editor_core::SelectionOverlayFrame& base = selectionOverlayBaseFrame();
        quader_editor_core::SelectionOverlayFrame hoverFrame =
                quader_editor_core::build_selection_hover_overlay_frame(
                        session_.objects(),
                        session_.selection_mode(),
                        hover,
                        {});
        const auto appendTransformDragSnapPoints = [&](quader_editor_core::SelectionOverlayFrame& frame) {
            if (!runtime_.transform_drag_active()) {
                return;
            }
            std::vector<quader_editor_core::SelectionOverlayPoint> snapPoints =
                    quader_editor_core::transform_drag_snap_overlay_points(runtime_.transform_drag_state());
            frame.points.insert(frame.points.end(), snapPoints.begin(), snapPoints.end());
        };
        if (hoverFrame.hover_target_selected) {
            quader_editor_core::SelectionOverlayFrame frame;
            frame.hover_target_selected = hoverFrame.hover_target_selected;
            appendSelectedHoverFilteredBaseFrame(frame, base, hoverFrame);
            appendOverlayFrame(frame, hoverFrame);
            appendTransformDragSnapPoints(frame);
            return frame;
        }

        quader_editor_core::SelectionOverlayFrame frame = base;
        appendOverlayFrame(frame, hoverFrame);
        appendTransformDragSnapPoints(frame);
        return frame;
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
                    !sdkSelectionMatches(object.selection, nextSelection);
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
        const std::uint64_t contentBefore = session_.state().content_revision;
        const quader_editor_core::EditorRuntimeCommandResult result =
                quader_editor_core::execute_runtime_command(session_, runtime_, noSettingsRuntimeCommand(commandId));
        if (mayChangeContent &&
                (session_.state().content_revision != contentBefore || result.command.changed)) {
            syncSceneObjectsFromSdk(result.command.changed);
        }
        syncSceneSelectionFromSdk();
        return {
                .ok = result.command.ok,
                .changed = result.command.changed,
                .message = result.command.message,
        };
    }
#endif

    ModelingCreateBoxResult createSceneObjectFromLatest(const std::string& name, const std::string& message) {
#if QUADER_HAS_MODELING_SDK
        const quader_editor_core::PolygonObject& object = session_.objects().back();
        MeshAsset mesh = toMeshAsset(object);
        mesh.name = name;
        const AssetId meshId = assets_.addMesh(std::move(mesh));

        const EntityId entity = scene_.createEntity(name);
        scene_.setModelingObject(entity, ModelingObjectComponent{
                .objectId = object.id,
                .materialSlot = static_cast<std::uint32_t>(object.material_id),
        });
        scene_.setMesh(entity, MeshComponent{.mesh = meshId});
        scene_.clearSelection();
        scene_.setSelected(entity, true);

        entityByObject_[object.id] = entity;
        objectByEntity_[entity] = object.id;

        return {
                .ok = true,
                .changed = true,
                .message = message,
                .object = {
                        .sdkObjectId = object.id,
                        .entity = entity,
                        .mesh = meshId,
                        .name = name,
                },
        };
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
    std::unordered_map<int, EntityId> entityByObject_;
    std::unordered_map<EntityId, int> objectByEntity_;

#if QUADER_HAS_MODELING_SDK
    quader_editor_core::EditorSession session_;
    quader_editor_core::EditorRuntime runtime_;
    quader_editor_core::RepeatLastActionState repeatLastAction_;
    bool sceneObjectsSyncValid_ = false;
    std::uint64_t syncedSdkContentRevision_ = 0;
    mutable bool selectionOverlayCacheValid_ = false;
    mutable std::uint64_t selectionOverlayCacheContentRevision_ = 0;
    mutable std::uint64_t selectionOverlayCacheSelectionRevision_ = 0;
    mutable std::uint64_t selectionOverlayCacheSceneRevision_ = 0;
    mutable quader_editor_core::SelectionMode selectionOverlayCacheSelectionMode_ =
            quader_editor_core::SelectionMode::object;
    mutable quader_editor_core::SelectionHoverState selectionOverlayCacheHover_;
    mutable ViewportOverlayFrame selectionOverlayCacheFrame_;
    mutable PickingCache pickingCache_;
    mutable OverlayBaseCache overlayBaseCache_;
    bool activeTransformDragPreviewDriven_ = false;
    bool activeTransformDragDuplicateSelected_ = false;
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

ModelingActionSet ModelingSessionService::contextActionSet(const ModelingActionQuery& query) const {
    return impl_->contextActionSet(query);
}

std::vector<ModelingActionItem> ModelingSessionService::contextActions(const ModelingActionQuery& query) const {
    return impl_->contextActions(query);
}

ModelingCommandResult ModelingSessionService::executeContextAction(std::string_view actionId) {
    return impl_->executeContextAction(actionId);
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

ModelingCommandResult ModelingSessionService::clearSelectionHover() {
    return impl_->clearSelectionHover();
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

} // namespace quader
