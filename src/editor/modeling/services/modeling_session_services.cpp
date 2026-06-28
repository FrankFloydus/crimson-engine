////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/modeling_session_services.hpp"

#include "editor/modeling/services/modeling_instant_operation_catalog.hpp"
#include "editor/modeling/services/modeling_operation_settings.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/projection/picking_cache_projection.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/selection/box_selection_rules.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>

#include <editor/interactions/cut_tool.hpp>
#include <editor/interactions/mirror_tool.hpp>
#include <editor/interactions/selection_box.hpp>
#include <editor/interactions/transform_drag.hpp>
#include <editor/interactions/viewport_projection.hpp>
#include <mesh/polygon/document.hpp>

namespace quader {
namespace {

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

void mixOverlaySignatureHash(std::uint64_t& hash, std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
}

void mixOverlaySignatureHash(std::uint64_t& hash, bool value) {
    mixOverlaySignatureHash(hash, value ? 1ULL : 0ULL);
}

void mixOverlaySignatureHash(std::uint64_t& hash, int value) {
    mixOverlaySignatureHash(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void mixOverlaySignatureHash(std::uint64_t& hash, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(value));
    mixOverlaySignatureHash(hash, bits);
}

void mixOverlaySignatureHash(std::uint64_t& hash, quader::QVec3 value) {
    mixOverlaySignatureHash(hash, static_cast<double>(value.x));
    mixOverlaySignatureHash(hash, static_cast<double>(value.y));
    mixOverlaySignatureHash(hash, static_cast<double>(value.z));
}

quader_editor_core::ViewportCameraState toRuntimeCamera(const ModelingViewportCamera& value) {
    quader_editor_core::ViewportCameraState camera;
    camera.projection = value.orthographic
            ? quader_editor_core::CameraProjection::orthographic
            : quader_editor_core::CameraProjection::perspective;
    camera.transform = {
            .x_axis = value.xAxis,
            .y_axis = value.yAxis,
            .z_axis = value.zAxis,
            .origin = value.origin,
    };
    camera.viewport_size = {
            std::max(value.viewportSize.x, 1.0f),
            std::max(value.viewportSize.y, 1.0f),
    };
    camera.orthographic_size = std::max(value.orthographicSize, 0.0001);
    camera.fov_degrees = value.fovDegrees;
    return camera;
}

QVec3 fromRuntime(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

quader::QVec3 toRuntimeVector(QVec3 value) {
    return {value.x, value.y, value.z};
}

quader_poly::Ray toRuntimeRay(const ModelingRay& ray) {
    return {
            .origin = toRuntimeVector(ray.origin),
            .direction = toRuntimeVector(normalize(ray.direction)),
    };
}

quader_poly::PickOptions pickOptionsFromRequest(const ModelingPickSelectRequest& request) {
    return quader_editor_core::selection_pick_options_from_radii(request.vertexRadius, request.edgeRadius);
}

void applyPickedSelectionToSession(
        quader_editor_core::EditorSession& session,
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects,
        quader_editor_core::SelectionMode mode,
        int activeObjectId) {
    quader_editor_core::SessionState& state = session.state();
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

/**
 * Stores the Drawn Plane Preview Frame data contract used by the modeling overlay service.
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
 * Represents a Preview Triangle Mesh value used by the modeling overlay service.
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
 * Stores the Cut Preview Point Key data contract used by the modeling overlay service.
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
 * Stores the Cut Preview Edge Key data contract used by the modeling overlay service.
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
 * Represents a Cut Preview Edge Accumulator value used by the modeling overlay service.
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

quader_editor_core::RectangleSelectionMatch rectangleSelectionMatch(
        ModelingBoxSelectionRule rule,
        QVec2 start,
        QVec2 end) {
    switch (resolveBoxSelectionRule(rule, start, end)) {
    case ModelingBoxSelectionRule::Contain:
        return quader_editor_core::RectangleSelectionMatch::contain;
    case ModelingBoxSelectionRule::Crossing:
        return quader_editor_core::RectangleSelectionMatch::crossing;
    case ModelingBoxSelectionRule::DragDirection:
        break;
    }
    return quader_editor_core::RectangleSelectionMatch::crossing;
}

void includeProjectedObjectPoint(
        quader_editor_core::RectangleProjectedObject& projected,
        QVec2 screen) {
    projected.screen_points.push_back(screen);
    if (!projected.has_screen_bounds) {
        projected.screen_min = screen;
        projected.screen_max = screen;
        projected.has_screen_bounds = true;
        return;
    }
    projected.screen_min.x = std::min(projected.screen_min.x, screen.x);
    projected.screen_min.y = std::min(projected.screen_min.y, screen.y);
    projected.screen_max.x = std::max(projected.screen_max.x, screen.x);
    projected.screen_max.y = std::max(projected.screen_max.y, screen.y);
}

} // namespace

bool ModelingSelectionService::hasComponentSelection(const ModelingSelectionSummary& summary) {
    return summary.selectedVertexCount > 0 || summary.selectedEdgeCount > 0 || summary.selectedFaceCount > 0;
}

void ModelingSelectionService::invalidatePickingCache() const {
    pickingCache_ = {};
}

const std::vector<quader_editor_core::PolygonObject>&
ModelingSelectionService::cachedSceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const std::uint64_t sceneRevision = scene.revision();
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

    const std::vector<quader_editor_core::PolygonObject> runtimeObjects = session.objects();
    pickingCache_.objects = PickingCacheProjection::sceneTransformedObjectsForPicking(
            runtimeObjects,
            scene,
            objectRegistry);
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

std::vector<quader_editor_core::PolygonObject> ModelingSelectionService::sceneTransformedObjectsForPicking(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    return cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
}

const std::vector<quader_editor_core::PickObjectBounds>& ModelingSelectionService::cachedPickObjectBounds(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.boundsValid) {
        return pickingCache_.bounds;
    }

    pickingCache_.bounds.clear();
    pickingCache_.bounds.reserve(objects.size());
    for (const quader_editor_core::PolygonObject& object : objects) {
        pickingCache_.bounds.push_back(PickingCacheProjection::buildObjectPickBounds(object));
    }
    pickingCache_.boundsValid = true;
    return pickingCache_.bounds;
}

const std::vector<quader_editor_core::PickObjectFaceGeometry>& ModelingSelectionService::cachedPickFaceGeometry(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        quader_poly::PickOptions pickOptions) const {
    const std::vector<quader_editor_core::PolygonObject>& objects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    if (pickingCache_.faceGeometryValid) {
        bool signaturesMatch = pickingCache_.faceGeometryEntries.size() == objects.size();
        for (std::size_t index = 0; signaturesMatch && index < objects.size(); ++index) {
            signaturesMatch =
                    pickingCache_.faceGeometryEntries[index].objectId == objects[index].id &&
                    pickingCache_.faceGeometryEntries[index].geometrySignature ==
                            PickingCacheProjection::objectFaceGeometrySignature(objects[index], pickOptions);
        }
        if (signaturesMatch) {
            return pickingCache_.faceGeometry;
        }
    }

    std::vector<ModelingPickFaceGeometryCacheEntry> previousEntries = std::move(pickingCache_.faceGeometryEntries);
    std::vector<quader_editor_core::PickObjectFaceGeometry> previousGeometry = std::move(pickingCache_.faceGeometry);
    std::vector<ModelingPickFaceGeometryCacheEntry> nextEntries;
    std::vector<quader_editor_core::PickObjectFaceGeometry> nextGeometry;
    nextEntries.reserve(objects.size());
    nextGeometry.reserve(objects.size());

    for (const quader_editor_core::PolygonObject& object : objects) {
        const std::uint64_t signature = PickingCacheProjection::objectFaceGeometrySignature(object, pickOptions);
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

bool ModelingSelectionService::applyPickSelection(
        quader_editor_core::EditorSession& session,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        const ModelingPickSelectRequest& request) const {
    const std::vector<quader_editor_core::PolygonObject>& cachedObjects =
            cachedSceneTransformedObjectsForPicking(session, scene, objectRegistry);
    std::vector<quader_editor_core::PolygonObject> pickingObjects(cachedObjects.begin(), cachedObjects.end());
    quader_editor_core::SelectionMode mode = session.selection_mode();
    int activeObjectId = session.active_object_id();
    quader_editor_core::SelectionEditOperation operation =
            quader_editor_core::selection_edit_operation_from_modifiers(
                    request.controlPressed,
                    request.shiftPressed);
    const quader_poly::PickOptions pickOptions = pickOptionsFromRequest(request);
    const quader_poly::Ray ray = toRuntimeRay(request.ray);
    const std::vector<quader_editor_core::PickObjectBounds>& pickBounds =
            cachedPickObjectBounds(session, scene, objectRegistry);
    const std::vector<quader_editor_core::PickObjectFaceGeometry>& pickFaceGeometry =
            cachedPickFaceGeometry(session, scene, objectRegistry, pickOptions);
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
                    false,
                    pickBounds,
                    pickFaceGeometry);
            applyPickedSelectionToSession(session, pickingObjects, mode, activeObjectId);
        }
        return true;
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
                        pickBounds,
                        pickFaceGeometry);
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
            return false;
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
                pickOptions,
                pickBounds,
                pickFaceGeometry);
    } else if (multiClick && mode == quader_editor_core::SelectionMode::edge) {
        hit = quader_editor_core::pick_select_edge_loop(
                pickingObjects,
                mode,
                operation,
                ray,
                activeObjectId,
                pickOptions,
                pickBounds,
                pickFaceGeometry);
    } else if (multiClick && mode == quader_editor_core::SelectionMode::vertex) {
        hit = quader_editor_core::pick_select_all_vertices(
                pickingObjects,
                mode,
                operation,
                ray,
                activeObjectId,
                pickOptions,
                pickBounds,
                pickFaceGeometry);
    } else {
        hit = quader_editor_core::pick_select(
                pickingObjects,
                mode,
                operation,
                ray,
                activeObjectId,
                pickOptions,
                true,
                pickBounds,
                pickFaceGeometry);
    }
    if (hit || !request.preserveSelectionOnMiss) {
        applyPickedSelectionToSession(session, pickingObjects, mode, activeObjectId);
        return true;
    }
    return true;
}

quader_editor_core::RectangleSelectionState ModelingSelectionService::rectangleSelectionState(
        const quader_editor_core::EditorSession& session) const {
    quader_editor_core::RectangleSelectionState selection;
    selection.mode = session.selection_mode();
    selection.active_object_id = session.active_object_id();
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        if (!object.selected) {
            continue;
        }
        if (selection.mode == quader_editor_core::SelectionMode::object) {
            selection.selected_object_ids.push_back(object.id);
            continue;
        }
        if (selection.mode == quader_editor_core::SelectionMode::face) {
            for (const quader_poly::ElementId faceId : object.selection.faces) {
                const quader_poly::Face* face = quader_poly::find_face(object.document, faceId);
                std::vector<int> vertexIds;
                if (face != nullptr) {
                    vertexIds.reserve(face->vertices.size());
                    for (const quader_poly::ElementId vertexId : face->vertices) {
                        vertexIds.push_back(static_cast<int>(vertexId));
                    }
                }
                selection.selected_faces.push_back({object.id, static_cast<int>(faceId), std::move(vertexIds)});
            }
            if (object.id == selection.active_object_id &&
                    object.selection.has_active &&
                    object.selection.active_kind == quader_poly::ElementKind::face) {
                selection.active_face_index = static_cast<int>(object.selection.active_face);
            }
        } else if (selection.mode == quader_editor_core::SelectionMode::edge) {
            for (const quader_poly::Edge edge : object.selection.edges) {
                selection.selected_edges.push_back({
                        object.id,
                        static_cast<int>(edge.a),
                        static_cast<int>(edge.b),
                });
            }
            if (object.id == selection.active_object_id &&
                    object.selection.has_active &&
                    object.selection.active_kind == quader_poly::ElementKind::edge) {
                selection.active_edge_vertex_indices = {
                        static_cast<int>(object.selection.active_edge.a),
                        static_cast<int>(object.selection.active_edge.b),
                };
            }
        } else if (selection.mode == quader_editor_core::SelectionMode::vertex) {
            for (const quader_poly::ElementId vertexId : object.selection.vertices) {
                selection.selected_vertices.push_back({object.id, static_cast<int>(vertexId)});
            }
            if (object.id == selection.active_object_id &&
                    object.selection.has_active &&
                    object.selection.active_kind == quader_poly::ElementKind::vertex) {
                selection.active_vertex_indices = {static_cast<int>(object.selection.active_vertex)};
            }
        }
    }
    return selection;
}

quader_editor_core::RectangleSelectionUniverse ModelingSelectionService::rectangleSelectionUniverse(
        const quader_editor_core::EditorSession& session) const {
    quader_editor_core::RectangleSelectionUniverse universe;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        universe.object_ids.push_back(object.id);
        for (const quader_poly::Face& face : object.document.faces) {
            std::vector<int> vertexIds;
            vertexIds.reserve(face.vertices.size());
            for (const quader_poly::ElementId vertexId : face.vertices) {
                vertexIds.push_back(static_cast<int>(vertexId));
            }
            universe.faces.push_back({object.id, static_cast<int>(face.id), std::move(vertexIds)});
        }
        for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
            universe.edges.push_back({object.id, static_cast<int>(edge.a), static_cast<int>(edge.b)});
        }
        for (const quader_poly::Vertex& vertex : object.document.vertices) {
            universe.vertices.push_back({object.id, static_cast<int>(vertex.id)});
        }
    }
    return universe;
}

quader_editor_core::RectangleProjectedHitsRequest ModelingSelectionService::rectangleProjectedHitsRequest(
        const std::vector<quader_editor_core::PolygonObject>& pickingObjects,
        quader_editor_core::SelectionMode selectionMode,
        const ModelingBoxSelectRequest& request) const {
    const quader_editor_core::ViewportCameraState camera = toRuntimeCamera(request.camera);
    quader_editor_core::RectangleProjectedHitsRequest hitsRequest;
    hitsRequest.mode = selectionMode;
    hitsRequest.match = rectangleSelectionMatch(request.rule, request.start, request.end);
    hitsRequest.rect = quader_editor_core::normalized_screen_rect(request.start, request.end);

    for (const quader_editor_core::PolygonObject& object : pickingObjects) {
        std::unordered_map<int, QVec2> projectedVertices;
        projectedVertices.reserve(object.document.vertices.size());
        quader_editor_core::RectangleProjectedObject projectedObject;
        projectedObject.object_id = object.id;
        for (const quader_poly::Vertex& vertex : object.document.vertices) {
            QVec2 screen;
            if (!quader_editor_core::project_viewport_point(camera, vertex.position, screen)) {
                continue;
            }
            projectedVertices.emplace(static_cast<int>(vertex.id), screen);
            includeProjectedObjectPoint(projectedObject, screen);
        }
        if (projectedObject.has_screen_bounds) {
            projectedObject.has_screen_center = true;
            projectedObject.screen_center = {
                    (projectedObject.screen_min.x + projectedObject.screen_max.x) * 0.5f,
                    (projectedObject.screen_min.y + projectedObject.screen_max.y) * 0.5f,
            };
        }

        for (const quader_poly::Face& face : object.document.faces) {
            quader_editor_core::RectangleProjectedFace projectedFace;
            projectedFace.object_id = object.id;
            projectedFace.face_index = static_cast<int>(face.id);
            projectedFace.vertex_indices.reserve(face.vertices.size());
            projectedFace.screen_vertices.reserve(face.vertices.size());
            QVec2 center{};
            bool allProjected = !face.vertices.empty();
            for (const quader_poly::ElementId vertexId : face.vertices) {
                projectedFace.vertex_indices.push_back(static_cast<int>(vertexId));
                const auto found = projectedVertices.find(static_cast<int>(vertexId));
                if (found == projectedVertices.end()) {
                    allProjected = false;
                    continue;
                }
                center = center + found->second;
                projectedFace.screen_vertices.push_back(found->second);
            }
            if (allProjected && !projectedFace.screen_vertices.empty()) {
                projectedFace.has_screen_position = true;
                projectedFace.screen_position = center / static_cast<float>(projectedFace.screen_vertices.size());
                projectedObject.screen_polygons.push_back(projectedFace.screen_vertices);
            } else {
                projectedFace.screen_vertices.clear();
            }
            hitsRequest.faces.push_back(std::move(projectedFace));
        }

        for (const quader_poly::Edge edge : quader_poly::document_edges(object.document)) {
            quader_editor_core::RectangleProjectedEdge projectedEdge;
            projectedEdge.object_id = object.id;
            projectedEdge.a_index = static_cast<int>(edge.a);
            projectedEdge.b_index = static_cast<int>(edge.b);
            const auto a = projectedVertices.find(static_cast<int>(edge.a));
            const auto b = projectedVertices.find(static_cast<int>(edge.b));
            if (a != projectedVertices.end() && b != projectedVertices.end()) {
                projectedEdge.has_screen_start = true;
                projectedEdge.screen_start = a->second;
                projectedEdge.has_screen_end = true;
                projectedEdge.screen_end = b->second;
                projectedEdge.has_screen_position = true;
                projectedEdge.screen_position = (a->second + b->second) * 0.5f;
                projectedObject.screen_segments.push_back({a->second, b->second});
            }
            hitsRequest.edges.push_back(projectedEdge);
        }

        for (const quader_poly::Vertex& vertex : object.document.vertices) {
            quader_editor_core::RectangleProjectedVertex projectedVertex;
            projectedVertex.object_id = object.id;
            projectedVertex.vertex_index = static_cast<int>(vertex.id);
            const auto found = projectedVertices.find(static_cast<int>(vertex.id));
            if (found != projectedVertices.end()) {
                projectedVertex.has_screen_position = true;
                projectedVertex.screen_position = found->second;
            }
            hitsRequest.vertices.push_back(projectedVertex);
        }

        hitsRequest.objects.push_back(std::move(projectedObject));
    }
    return hitsRequest;
}

bool ModelingSelectionService::applyRectangleSelection(
        quader_editor_core::EditorSession& session,
        const quader_editor_core::RectangleSelectionState& selection) const {
    quader_editor_core::SessionState& state = session.state();
    bool changed = state.selection_mode != selection.mode ||
            state.active_object_id != selection.active_object_id;

    std::unordered_set<int> selectedObjects(selection.selected_object_ids.begin(), selection.selected_object_ids.end());
    std::unordered_map<int, std::vector<int>> selectedFacesByObject;
    std::unordered_map<int, std::vector<quader_poly::Edge>> selectedEdgesByObject;
    std::unordered_map<int, std::vector<int>> selectedVerticesByObject;
    if (selection.mode == quader_editor_core::SelectionMode::face) {
        for (const quader_editor_core::RectangleSelectionFace face : selection.selected_faces) {
            selectedFacesByObject[face.object_id].push_back(face.face_index);
        }
    } else if (selection.mode == quader_editor_core::SelectionMode::edge) {
        std::unordered_set<int> fullObjectIds(
                selection.selected_edge_full_object_ids.begin(),
                selection.selected_edge_full_object_ids.end());
        for (const quader_editor_core::PolygonObject& object : state.objects) {
            if (fullObjectIds.find(object.id) == fullObjectIds.end()) {
                continue;
            }
            selectedEdgesByObject[object.id] = quader_poly::document_edges(object.document);
        }
        for (const quader_editor_core::RectangleSelectionEdge edge : selection.selected_edges) {
            selectedEdgesByObject[edge.object_id].push_back(quader_poly::make_edge(
                    static_cast<quader_poly::ElementId>(edge.a_index),
                    static_cast<quader_poly::ElementId>(edge.b_index)));
        }
    } else if (selection.mode == quader_editor_core::SelectionMode::vertex) {
        std::unordered_set<int> fullObjectIds(
                selection.selected_vertex_full_object_ids.begin(),
                selection.selected_vertex_full_object_ids.end());
        for (const quader_editor_core::PolygonObject& object : state.objects) {
            if (fullObjectIds.find(object.id) == fullObjectIds.end()) {
                continue;
            }
            std::vector<int>& vertices = selectedVerticesByObject[object.id];
            vertices.reserve(object.document.vertices.size());
            for (const quader_poly::Vertex& vertex : object.document.vertices) {
                vertices.push_back(static_cast<int>(vertex.id));
            }
        }
        for (const quader_editor_core::RectangleSelectionVertex vertex : selection.selected_vertices) {
            selectedVerticesByObject[vertex.object_id].push_back(vertex.vertex_index);
        }
    }

    for (quader_editor_core::PolygonObject& object : state.objects) {
        bool nextSelected = false;
        quader_poly::Selection nextSelection;
        if (selection.mode == quader_editor_core::SelectionMode::object) {
            nextSelected = selectedObjects.find(object.id) != selectedObjects.end();
        } else {
            nextSelection.mode = quader_editor_core::component_selection_mode(selection.mode);
            if (selection.mode == quader_editor_core::SelectionMode::face) {
                const auto found = selectedFacesByObject.find(object.id);
                if (found != selectedFacesByObject.end()) {
                    for (const int faceId : found->second) {
                        nextSelection.faces.push_back(static_cast<quader_poly::ElementId>(faceId));
                    }
                }
                if (object.id == selection.active_object_id &&
                        selection.active_face_index >= 0 &&
                        std::find(
                                nextSelection.faces.begin(),
                                nextSelection.faces.end(),
                                static_cast<quader_poly::ElementId>(selection.active_face_index)) !=
                                nextSelection.faces.end()) {
                    nextSelection.has_active = true;
                    nextSelection.active_kind = quader_poly::ElementKind::face;
                    nextSelection.active_face = static_cast<quader_poly::ElementId>(selection.active_face_index);
                }
            } else if (selection.mode == quader_editor_core::SelectionMode::edge) {
                const auto found = selectedEdgesByObject.find(object.id);
                if (found != selectedEdgesByObject.end()) {
                    nextSelection.edges = found->second;
                }
                if (object.id == selection.active_object_id && !nextSelection.edges.empty()) {
                    const quader_poly::Edge active = nextSelection.edges.back();
                    nextSelection.has_active = true;
                    nextSelection.active_kind = quader_poly::ElementKind::edge;
                    nextSelection.active_edge = active;
                }
            } else if (selection.mode == quader_editor_core::SelectionMode::vertex) {
                const auto found = selectedVerticesByObject.find(object.id);
                if (found != selectedVerticesByObject.end()) {
                    for (const int vertexId : found->second) {
                        nextSelection.vertices.push_back(static_cast<quader_poly::ElementId>(vertexId));
                    }
                }
                if (object.id == selection.active_object_id && !nextSelection.vertices.empty()) {
                    const quader_poly::ElementId active = nextSelection.vertices.back();
                    nextSelection.has_active = true;
                    nextSelection.active_kind = quader_poly::ElementKind::vertex;
                    nextSelection.active_vertex = active;
                }
            }
            nextSelected = !nextSelection.empty();
        }

        changed = changed ||
                object.selected != nextSelected ||
                !selectionMatches(object.selection, nextSelection);
        object.selected = nextSelected;
        object.selection = std::move(nextSelection);
    }

    if (!changed) {
        return false;
    }
    state.selection_mode = selection.mode;
    state.active_object_id = selection.active_object_id;
    quader_editor_core::normalize_active_object_id(state.objects, state.active_object_id);
    state.dirty = true;
    ++state.selection_revision;
    return true;
}

bool ModelingSelectionService::selectionMatches(
        const quader_poly::Selection& left,
        const quader_poly::Selection& right) {
    return quader_editor_core::polygon_selection_matches(left, right);
}

SceneModelingSyncService::SceneModelingSyncService(ModelingObjectRuntimeMap& objectMap)
    : objectMap_(objectMap) {}

ModelingObjectRuntimeMap& SceneModelingSyncService::objectMap() {
    return objectMap_;
}

const ModelingObjectRuntimeMap& SceneModelingSyncService::objectMap() const {
    return objectMap_;
}

bool SceneModelingSyncService::sceneObjectsSyncValid() const {
    return sceneObjectsSyncValid_;
}

std::uint64_t SceneModelingSyncService::syncedModelingContentRevision() const {
    return syncedModelingContentRevision_;
}

void SceneModelingSyncService::invalidateSceneObjects() {
    sceneObjectsSyncValid_ = false;
}

void SceneModelingSyncService::markSceneObjectsSynced(std::uint64_t contentRevision) {
    syncedModelingContentRevision_ = contentRevision;
    sceneObjectsSyncValid_ = true;
}

bool SceneModelingSyncService::sceneObjectsSyncedForCurrentModelingContent(
        const quader_editor_core::EditorSession& session,
        const SceneDocument& scene) const {
    if (!sceneObjectsSyncValid_ || syncedModelingContentRevision_ != session.state().content_revision) {
        return false;
    }
    if (objectMap_.size() != session.objects().size()) {
        return false;
    }
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::optional<EntityId> entity = objectMap_.entityForModelingObject(object.id);
        if (!entity.has_value() || !scene.exists(*entity)) {
            return false;
        }
        const ModelingObjectComponent* modelingObject = scene.modelingObject(*entity);
        const MeshComponent* mesh = scene.mesh(*entity);
        if (modelingObject == nullptr || mesh == nullptr || mesh->mesh == InvalidAssetId) {
            return false;
        }
    }
    return true;
}

bool ModelingOverlayService::hasModalOverlay(const ModelingToolStatus& status) {
    return status.insertEdgeLoopHasPreview ||
            status.knifeHasCurrent ||
            status.cutHasLine ||
            status.mirrorHasLine ||
            status.polyHasCurrent;
}

void ModelingOverlayService::invalidate() const {
    selectionOverlayCache_.invalidate();
    overlayBaseCache_.valid = false;
}

std::uint64_t ModelingOverlayService::selectionOverlaySignature(
        const quader_editor_core::EditorRuntime& runtime) const {
    std::uint64_t hash = 1469598103934665603ULL;
    mixOverlaySignatureHash(hash, static_cast<int>(runtime.active_tool()));

    const quader_editor_core::SelectionHoverState& hover = runtime.hover();
    mixOverlaySignatureHash(hash, hover.object_id);
    mixOverlaySignatureHash(hash, static_cast<int>(hover.kind));
    mixOverlaySignatureHash(hash, static_cast<int>(hover.vertex_id));
    mixOverlaySignatureHash(hash, static_cast<int>(hover.edge.a));
    mixOverlaySignatureHash(hash, static_cast<int>(hover.edge.b));
    mixOverlaySignatureHash(hash, static_cast<int>(hover.face_id));
    mixOverlaySignatureHash(hash, hover.selected_modifier_pressed);
    mixOverlaySignatureHash(hash, hover.candidate_object_id);

    const quader_editor_core::TransformDragState& drag = runtime.transform_drag_state();
    mixOverlaySignatureHash(hash, drag.active);
    mixOverlaySignatureHash(hash, static_cast<int>(drag.tool));
    mixOverlaySignatureHash(hash, static_cast<int>(drag.operation));
    mixOverlaySignatureHash(hash, static_cast<int>(drag.axis));
    mixOverlaySignatureHash(hash, drag.pivot);
    mixOverlaySignatureHash(hash, drag.scene_transform_preview_eligible);
    mixOverlaySignatureHash(hash, static_cast<int>(drag.snap_target.kind));
    mixOverlaySignatureHash(hash, drag.snap_target.object_id);
    mixOverlaySignatureHash(hash, static_cast<int>(drag.snap_target.vertex_id));
    mixOverlaySignatureHash(hash, drag.snap_target.position);
    if (drag.active) {
        const std::vector<quader_editor_core::SelectionOverlayPoint> snapPoints =
                quader_editor_core::transform_drag_snap_overlay_points(drag);
        mixOverlaySignatureHash(hash, static_cast<int>(snapPoints.size()));
        for (const quader_editor_core::SelectionOverlayPoint& point : snapPoints) {
            mixOverlaySignatureHash(hash, static_cast<int>(point.layer));
            mixOverlaySignatureHash(hash, point.position);
            mixOverlaySignatureHash(hash, static_cast<std::uint64_t>(point.color_abgr));
            mixOverlaySignatureHash(hash, static_cast<double>(point.size_pixels));
            mixOverlaySignatureHash(hash, point.object_id);
            mixOverlaySignatureHash(hash, static_cast<int>(point.vertex_id));
        }
    }

    const quader_editor_core::CutToolState& cut = runtime.cut_state();
    const quader_editor_core::CutHoverState& cutHover = runtime.cut_hover();
    mixOverlaySignatureHash(hash, quader_editor_core::cut_is_active(cut));
    mixOverlaySignatureHash(hash, static_cast<int>(cut.points.size()));
    for (const quader::QVec3& point : cut.points) {
        mixOverlaySignatureHash(hash, point);
    }
    mixOverlaySignatureHash(hash, static_cast<int>(cut.target_object_ids.size()));
    for (const int objectId : cut.target_object_ids) {
        mixOverlaySignatureHash(hash, objectId);
    }
    mixOverlaySignatureHash(hash, cut.source_object_id);
    mixOverlaySignatureHash(hash, cut.face_normal);
    mixOverlaySignatureHash(hash, static_cast<int>(cut.mode));
    mixOverlaySignatureHash(hash, cut.dragging_point_index);
    mixOverlaySignatureHash(hash, cut.dragging_new_point);
    mixOverlaySignatureHash(hash, cut.dragging_line);
    mixOverlaySignatureHash(hash, cut.drag_line_anchor);
    mixOverlaySignatureHash(hash, cut.preview_valid);
    mixOverlaySignatureHash(hash, cut.preview_counts.target_count);
    mixOverlaySignatureHash(hash, cut.preview_counts.split_count);
    mixOverlaySignatureHash(hash, cut.preview_counts.removed_count);
    mixOverlaySignatureHash(hash, cutHover.valid);
    mixOverlaySignatureHash(hash, cutHover.object_id);
    mixOverlaySignatureHash(hash, cutHover.position);

    const quader_editor_core::MirrorToolState& mirror = runtime.mirror_state();
    const quader_editor_core::MirrorHoverState& mirrorHover = runtime.mirror_hover();
    mixOverlaySignatureHash(hash, quader_editor_core::mirror_is_active(mirror));
    mixOverlaySignatureHash(hash, static_cast<int>(mirror.plane.points.size()));
    for (const quader::QVec3& point : mirror.plane.points) {
        mixOverlaySignatureHash(hash, point);
    }
    mixOverlaySignatureHash(hash, mirror.plane.face_normal);
    mixOverlaySignatureHash(hash, mirror.plane.dragging_point_index);
    mixOverlaySignatureHash(hash, mirror.plane.dragging_new_point);
    mixOverlaySignatureHash(hash, mirror.plane.dragging_line);
    mixOverlaySignatureHash(hash, mirror.plane.drag_line_anchor);
    mixOverlaySignatureHash(hash, static_cast<int>(mirror.target_object_ids.size()));
    for (const int objectId : mirror.target_object_ids) {
        mixOverlaySignatureHash(hash, objectId);
    }
    mixOverlaySignatureHash(hash, mirror.preview_valid);
    mixOverlaySignatureHash(hash, mirror.preview_counts.target_count);
    mixOverlaySignatureHash(hash, mirror.preview_counts.mirrored_count);
    mixOverlaySignatureHash(hash, mirrorHover.valid);
    mixOverlaySignatureHash(hash, mirrorHover.object_id);
    mixOverlaySignatureHash(hash, mirrorHover.position);
    return hash;
}

void SelectionOverlayCache::invalidate() const {
    valid_ = false;
}

bool SelectionOverlayCache::matches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return valid_ &&
            contentRevision_ == contentRevision &&
            selectionRevision_ == selectionRevision &&
            sceneRevision_ == sceneRevision &&
            selectionMode_ == selectionMode &&
            overlaySignature_ == overlaySignature &&
            hoverMatches(hover_, hover);
}

const ViewportOverlayFrame& SelectionOverlayCache::frame() const {
    return frame_;
}

void SelectionOverlayCache::store(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    frame_ = std::move(frame);
    contentRevision_ = contentRevision;
    selectionRevision_ = selectionRevision;
    sceneRevision_ = sceneRevision;
    selectionMode_ = selectionMode;
    overlaySignature_ = overlaySignature;
    hover_ = hover;
    valid_ = true;
}

bool SelectionOverlayCache::hoverMatches(
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

bool ModelingOverlayService::selectionFrameMatches(
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    return selectionOverlayCache_.matches(
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            overlaySignature,
            hover);
}

const ViewportOverlayFrame& ModelingOverlayService::selectionFrame() const {
    return selectionOverlayCache_.frame();
}

void ModelingOverlayService::storeSelectionFrame(
        ViewportOverlayFrame frame,
        std::uint64_t contentRevision,
        std::uint64_t selectionRevision,
        std::uint64_t sceneRevision,
        quader_editor_core::SelectionMode selectionMode,
        std::uint64_t overlaySignature,
        const quader_editor_core::SelectionHoverState& hover) const {
    selectionOverlayCache_.store(
            std::move(frame),
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            overlaySignature,
            hover);
}

const quader_editor_core::SelectionOverlayFrame& ModelingOverlayService::selectionOverlayBaseFrame(
        const quader_editor_core::EditorSession& session) const {
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const quader_editor_core::SelectionMode selectionMode = session.selection_mode();
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
    next.objectEntries.reserve(session.objects().size());

    const quader_editor_core::SelectionHoverState emptyHover;
    for (const quader_editor_core::PolygonObject& object : session.objects()) {
        const std::uint64_t signature = PickingCacheProjection::objectFaceGeometrySignature(object);
        auto reusable = previousEntries.end();
        for (auto it = previousEntries.begin(); it != previousEntries.end(); ++it) {
            if (it->objectId == object.id &&
                    it->geometrySignature == signature &&
                    it->selectionMode == selectionMode &&
                    it->selected == object.selected &&
                    ModelingSelectionService::selectionMatches(it->selection, object.selection)) {
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

quader_editor_core::SelectionOverlayFrame ModelingOverlayService::selectionOverlayCurrentFrame(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::EditorRuntime& runtime,
        const quader_editor_core::SelectionHoverState& hover) const {
    const quader_editor_core::SelectionOverlayFrame& base = selectionOverlayBaseFrame(session);
    quader_editor_core::SelectionOverlayFrame hoverFrame =
            quader_editor_core::build_selection_hover_overlay_frame(
                    session.objects(),
                    session.selection_mode(),
                    hover,
                    {});
    const auto appendRuntimeToolOverlay = [&](quader_editor_core::SelectionOverlayFrame& frame) {
        const quader_editor_core::InsertEdgeLoopToolState& insertLoop = runtime.insert_edge_loop_state();
        frame.lines.insert(frame.lines.end(), insertLoop.preview_lines.begin(), insertLoop.preview_lines.end());

        const quader_editor_core::KnifeToolState& knife = runtime.knife_state();
        frame.points.insert(frame.points.end(), knife.preview_points.begin(), knife.preview_points.end());

        const quader_editor_core::PolyToolState& poly = runtime.poly_state();
        frame.lines.insert(frame.lines.end(), poly.preview_lines.begin(), poly.preview_lines.end());
        frame.points.insert(frame.points.end(), poly.preview_points.begin(), poly.preview_points.end());
    };
    const bool suppressBaseUnderHover =
            quader_editor_core::selection_hover_target_is_selected(session.objects(), session.selection_mode(), hover);
    if (suppressBaseUnderHover) {
        quader_editor_core::SelectionOverlayFrame frame;
        frame.hover_target_selected = hoverFrame.hover_target_selected;
        appendSelectedHoverFilteredBaseFrame(frame, base, hoverFrame, suppressBaseUnderHover);
        appendOverlayFrame(frame, hoverFrame);
        appendRuntimeToolOverlay(frame);
        return frame;
    }

    quader_editor_core::SelectionOverlayFrame frame = base;
    appendOverlayFrame(frame, hoverFrame);
    appendRuntimeToolOverlay(frame);
    return frame;
}

ViewportOverlayFrame ModelingOverlayService::selectionOverlayFrame(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::EditorRuntime& runtime,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        const std::vector<quader_editor_core::PolygonObject>& previewObjects) const {
    const quader_editor_core::SelectionHoverState& hover = runtime.hover();
    const std::uint64_t contentRevision = session.state().content_revision;
    const std::uint64_t selectionRevision = session.state().selection_revision;
    const std::uint64_t sceneRevision = scene.revision();
    const quader_editor_core::SelectionMode selectionMode = session.selection_mode();
    const std::uint64_t overlaySignature = selectionOverlaySignature(runtime);
    if (selectionFrameMatches(
                contentRevision,
                selectionRevision,
                sceneRevision,
                selectionMode,
                overlaySignature,
                hover)) {
        return selectionFrame();
    }

    const quader_editor_core::SelectionOverlayFrame currentFrame =
            selectionOverlayCurrentFrame(session, runtime, hover);
    ViewportOverlayFrame frame = SelectionOverlayProjection::projectSelectionOverlayFrame(
            currentFrame,
            objectRegistry,
            scene);
    SelectionOverlayProjection::appendProjectedOverlayLines(
            frame,
            runtime.knife_state().preview_lines,
            objectRegistry,
            scene,
            ViewportOverlayLayer::ToolPreviewLine,
            ViewportOverlayStyleRole::ToolPreviewLine,
            ViewportOverlaySource::Tool);
    if (runtime.transform_drag_active()) {
        SelectionOverlayProjection::appendWorldSpaceOverlayPoints(frame,
                quader_editor_core::transform_drag_snap_overlay_points(runtime.transform_drag_state()));
    }
    const quader_editor_core::CutToolState& cut = runtime.cut_state();
    if (quader_editor_core::cut_is_active(cut)) {
        const std::vector<quader::QVec3> cutPoints =
                quader_editor_core::cut_overlay_points(cut, runtime.cut_hover().position, runtime.cut_hover().valid);
        if (quader_editor_core::cut_can_preview(cut) && cut.preview_valid) {
            const PreviewTriangleMesh keepMesh = previewMeshFromDocuments(runtime.cut_preview_documents(
                    previewObjects,
                    quader_editor_core::CutPreviewMeshSide::keep));
            const PreviewTriangleMesh discardMesh = previewMeshFromDocuments(runtime.cut_preview_documents(
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
    const quader_editor_core::MirrorToolState& mirror = runtime.mirror_state();
    if (quader_editor_core::mirror_is_active(mirror)) {
        const std::vector<quader::QVec3> mirrorPoints =
                quader_editor_core::mirror_overlay_points(
                        mirror,
                        runtime.mirror_hover().position,
                        runtime.mirror_hover().valid);
        appendDrawnPlaneToolOverlay(
                frame,
                mirrorPoints,
                drawnPlaneSupportPoint(mirrorPoints, mirror.plane.face_normal),
                mirrorPoints.size() >= 2U);
    }
    storeSelectionFrame(
            std::move(frame),
            contentRevision,
            selectionRevision,
            sceneRevision,
            selectionMode,
            overlaySignature,
            hover);
    return selectionFrame();
}

std::vector<ModelingToolPreviewMesh> ModelingOverlayService::toolPreviewMeshes(
        const quader_editor_core::EditorRuntime& runtime,
        const SceneDocument& scene,
        const ModelingObjectRuntimeMap& objectRegistry,
        const std::vector<quader_editor_core::PolygonObject>& previewObjects) const {
    std::vector<ModelingToolPreviewMesh> previewMeshes;
    if (const std::optional<MeshAsset> boxPreview =
                    boxPreviewMeshFromDescriptor(runtime.box_preview_descriptor(true))) {
        previewMeshes.push_back(ModelingToolPreviewMesh{
                .sourceEntity = InvalidEntityId,
                .material = InvalidAssetId,
                .mesh = *boxPreview,
        });
    }

    const quader_editor_core::MirrorToolState& mirror = runtime.mirror_state();
    if (!quader_editor_core::mirror_is_active(mirror) ||
            !quader_editor_core::mirror_can_preview(mirror) ||
            !mirror.preview_valid) {
        return previewMeshes;
    }

    const std::vector<quader_poly::Document> documents =
            runtime.mirror_preview_documents(previewObjects);
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

ModelingCommandResult ModelingCommandExecutor::runtimeUnavailable() {
    return {.ok = false, .message = "Modeling runtime is not available in this build."};
}

ModelingCommandResult ModelingCommandExecutor::fromRuntime(const quader_editor_core::CommandResult& result) {
    return {
            .ok = result.ok,
            .changed = result.changed,
            .message = result.message,
    };
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::runtimeSyncPlan(
        bool mayChangeContent,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter,
        const quader_editor_core::CommandResult& result) {
    SyncPlan plan;
    plan.syncMeshes = mayChangeContent && (contentAfter != contentBefore || result.changed);
    plan.forceMeshSync = result.changed;
    plan.syncSelection = true;
    plan.invalidateOverlay = result.changed;
    return plan;
}

ModelingCommandExecutor::SyncPlan ModelingCommandExecutor::operationSyncPlan(
        quader_editor_core::EditorOperationCommandPhase phase,
        const quader_editor_core::PreviewTransactionStatus& previewBefore,
        const quader_editor_core::EditorOperationCommandResult& result,
        std::uint64_t contentBefore,
        std::uint64_t contentAfter) {
    const bool previewPhase = phase != quader_editor_core::EditorOperationCommandPhase::execute;
    const bool previewFinished = result.preview_finish.has_value() && result.preview_finish->before.active;
    const bool sceneChanged = contentAfter != contentBefore;

    SyncPlan plan;
    plan.syncMeshes = sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.forceMeshSync = ModelingMeshSyncService::shouldForceRefresh(
            previewPhase || previewBefore.active,
            previewFinished);
    plan.syncSelection = result.command.ok || sceneChanged || previewPhase || previewBefore.active || previewFinished;
    plan.invalidateOverlay = plan.syncMeshes;
    return plan;
}

std::optional<RuntimeOperationExecutor::OperationCommand> RuntimeOperationExecutor::operationForInstantCommand(
        ModelingInstantOperationId operation,
        const ModelingInteractionSettings& interactionSettings) const {
    const ModelingInstantOperationCatalogEntry* entry = findModelingInstantOperationCatalogEntry(operation);
    if (entry == nullptr || !entry->runtimeOperation.has_value()) {
        return std::nullopt;
    }

    OperationCommand command;
    command.operation = *entry->runtimeOperation;
    command.settings = settingsForOperation(*entry->runtimeOperation, interactionSettings);

    return command;
}

quader_editor_core::EditorOperationCommand RuntimeOperationExecutor::makeCommand(
        const OperationCommand& operation) {
    quader_editor_core::EditorOperationCommand command;
    command.operation_id = operation.operation;
    command.phase = operation.phase;
    command.settings = operation.settings;
    return command;
}

quader_editor_core::EditorOperationCommandResult RuntimeOperationExecutor::execute(
        quader_editor_core::EditorSession& session,
        const OperationCommand& operation) {
    return quader_editor_core::execute_command(session, makeCommand(operation));
}

quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::copyCommand() {
    return quader_editor_core::EditorRuntimeCommandId::copy_selected_meshes;
}

quader_editor_core::EditorRuntimeCommandId ModelingClipboardService::pasteCommand() {
    return quader_editor_core::EditorRuntimeCommandId::paste_copied_meshes;
}

quader_editor_core::EditorRuntimeCommand ModelingClipboardService::makeCopyCommand() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = copyCommand();
    return command;
}

quader_editor_core::EditorRuntimeCommand ModelingClipboardService::makePasteCommand() {
    quader_editor_core::EditorRuntimeCommand command;
    command.command_id = pasteCommand();
    command.settings = quader_editor_core::PasteCopiedMeshesRuntimeCommandSettings {};
    return command;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::undoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::undo;
}

quader_editor_core::EditorRuntimeCommandId ModelingHistoryBridge::redoCommand() {
    return quader_editor_core::EditorRuntimeCommandId::redo;
}

quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() {
    return repeatLastAction_;
}

const quader_editor_core::RepeatLastActionState& ModelingHistoryBridge::repeatLastAction() const {
    return repeatLastAction_;
}

quader_editor_core::CommandResult ModelingHistoryBridge::executeRepeatLastAction(
        quader_editor_core::EditorSession& session) {
    return quader_editor_core::execute_repeat_last_action(session, repeatLastAction_);
}

void ModelingHistoryBridge::recordOperation(
        const quader_editor_core::EditorOperationCommand& command,
        const quader_editor_core::CommandResult& result) {
    quader_editor_core::record_repeatable_operation(repeatLastAction_, command, result);
}

void ModelingHistoryBridge::recordTransform(
        const quader_editor_core::EditorSession& session,
        const quader_editor_core::TransformDragState& drag,
        bool duplicateSelected) {
    quader_editor_core::record_repeatable_transform(
            repeatLastAction_,
            session,
            drag,
            duplicateSelected);
}

ModelingInteractionSettingsService::ModelingInteractionSettingsService()
    : settings_(normalize(ModelingInteractionSettings{})) {}

ModelingInteractionSettings ModelingInteractionSettingsService::settings() const {
    return settings_;
}

ModelingInteractionSettings ModelingInteractionSettingsService::normalize(
        const ModelingInteractionSettings& settings) const {
    ModelingInteractionSettings normalized = settings;
    normalized.bridgeSteps = quader_editor_core::normalize_bridge_operation_settings(
            quader_editor_core::BridgeOperationSettings{.steps = normalized.bridgeSteps}).steps;
    const quader_editor_core::EdgeBevelOperationSettings edgeBevel =
            quader_editor_core::normalize_edge_bevel_operation_settings({
                    .width = normalized.edgeBevelWidth,
                    .profile = normalized.edgeBevelProfile,
                    .segments = normalized.edgeBevelSegments,
            });
    normalized.edgeBevelWidth = edgeBevel.width;
    normalized.edgeBevelProfile = edgeBevel.profile;
    normalized.edgeBevelSegments = edgeBevel.segments;
    const quader_editor_core::ThickenOperationSettings thicken =
            quader_editor_core::normalize_thicken_operation_settings({
                    .thickness = normalized.thickenThickness,
                    .from_center = normalized.thickenFromCenter,
            });
    normalized.thickenThickness = thicken.thickness;
    normalized.thickenFromCenter = thicken.from_center;
    const quader_editor_core::SliceQuadOperationSettings slice =
            quader_editor_core::normalize_slice_quad_operation_settings({
                    .x_slices = normalized.sliceQuadXSlices,
                    .y_slices = normalized.sliceQuadYSlices,
            });
    normalized.sliceQuadXSlices = slice.x_slices;
    normalized.sliceQuadYSlices = slice.y_slices;
    normalized.mergeByDistanceTolerance = quader_editor_core::normalize_merge_by_distance_operation_settings(
            quader_editor_core::MergeByDistanceOperationSettings{.tolerance = normalized.mergeByDistanceTolerance}).tolerance;
    return normalized;
}

bool ModelingInteractionSettingsService::setNormalized(ModelingInteractionSettings settings) {
    settings = normalize(settings);
    const bool changed = settings.bridgeSteps != settings_.bridgeSteps ||
            settings.edgeBevelWidth != settings_.edgeBevelWidth ||
            settings.edgeBevelProfile != settings_.edgeBevelProfile ||
            settings.edgeBevelSegments != settings_.edgeBevelSegments ||
            settings.thickenThickness != settings_.thickenThickness ||
            settings.thickenFromCenter != settings_.thickenFromCenter ||
            settings.sliceQuadXSlices != settings_.sliceQuadXSlices ||
            settings.sliceQuadYSlices != settings_.sliceQuadYSlices ||
            settings.mergeByDistanceTolerance != settings_.mergeByDistanceTolerance ||
            settings.cutMode != settings_.cutMode;
    settings_ = settings;
    return changed;
}

void ModelingInteractionSettingsService::resetForPreviewAction(ModelingActionPreviewKind previewKind) {
    const ModelingInteractionSettings defaults = normalize(ModelingInteractionSettings{});
    switch (previewKind) {
    case ModelingActionPreviewKind::EdgeBevel:
        settings_.edgeBevelWidth = defaults.edgeBevelWidth;
        settings_.edgeBevelProfile = defaults.edgeBevelProfile;
        settings_.edgeBevelSegments = defaults.edgeBevelSegments;
        return;
    case ModelingActionPreviewKind::Thicken:
        settings_.thickenThickness = defaults.thickenThickness;
        settings_.thickenFromCenter = defaults.thickenFromCenter;
        return;
    case ModelingActionPreviewKind::MergeByDistance:
        settings_.mergeByDistanceTolerance = defaults.mergeByDistanceTolerance;
        return;
    case ModelingActionPreviewKind::Bridge:
    case ModelingActionPreviewKind::SliceQuad:
    case ModelingActionPreviewKind::None:
        return;
    }
}

void ModelingInteractionSettingsService::setCutMode(ModelingCutMode mode) {
    settings_.cutMode = mode;
}

void ModelingInteractionSettingsService::cycleCutModeWithoutRuntime() {
    settings_.cutMode =
            settings_.cutMode == ModelingCutMode::DiscardLeft ? ModelingCutMode::DiscardRight :
            settings_.cutMode == ModelingCutMode::DiscardRight ? ModelingCutMode::KeepBoth :
            ModelingCutMode::DiscardLeft;
}

const std::string& ModelingToolSettingsService::activePreviewActionId() const {
    return activePreviewActionId_;
}

const std::string& ModelingToolSettingsService::activePreviewOperationId() const {
    return activePreviewOperationId_;
}

const std::string& ModelingToolSettingsService::activePreviewLabel() const {
    return activePreviewLabel_;
}

void ModelingToolSettingsService::setPreviewMetadata(std::string actionId, std::string operationId, std::string label) {
    activePreviewActionId_ = std::move(actionId);
    activePreviewOperationId_ = std::move(operationId);
    activePreviewLabel_ = std::move(label);
}

void ModelingToolSettingsService::clearPreviewMetadata() {
    activePreviewActionId_.clear();
    activePreviewOperationId_.clear();
    activePreviewLabel_.clear();
}

ModelingToolSettingsTarget ModelingToolSettingsService::operationPreviewTarget(
        ModelingToolSettingsTargetKind kind,
        std::string_view fallbackLabel,
        std::string_view fallbackOperationId) const {
    ModelingToolSettingsTarget target;
    target.kind = kind;
    target.active = true;
    target.operationPreviewActive = true;
    target.label = activePreviewLabel_.empty() ? std::string(fallbackLabel) : activePreviewLabel_;
    target.actionId = activePreviewActionId_;
    target.operationId = activePreviewOperationId_.empty() ? std::string(fallbackOperationId) : activePreviewOperationId_;
    return target;
}

ModelingToolSettingsTarget ModelingToolSettingsService::activePreviewTarget(
        const quader_editor_core::PreviewTransactionStatus& preview) const {
    ModelingToolSettingsTarget target;
    const ModelingToolSettingsTargetKind previewKind = preview.active ?
            toolSettingsTargetKindForPreview(preview.kind) :
            ModelingToolSettingsTargetKind::None;
    if (previewKind == ModelingToolSettingsTargetKind::None) {
        return target;
    }

    std::string fallbackOperationId;
    if (const std::optional<quader_editor_core::EditorOperationId> operation =
                operationForPreviewKind(preview.kind)) {
        if (const quader_editor_core::EditorOperation* schema =
                    quader_editor_core::editor_operation_for_id(*operation)) {
            fallbackOperationId = std::string(schema->id());
        }
    }
    return operationPreviewTarget(
            previewKind,
            fallbackToolSettingsLabel(previewKind),
            fallbackOperationId);
}

ModelingToolSettingsTarget ModelingToolSettingsService::activeRuntimeToolTarget(
        const quader_editor_core::EditorRuntimeStatus& runtimeStatus) const {
    ModelingToolSettingsTarget target;
    if (runtimeStatus.cut_active) {
        target.kind = ModelingToolSettingsTargetKind::Cut;
        target.active = true;
        target.label = fallbackToolSettingsLabel(ModelingToolSettingsTargetKind::Cut);
        return target;
    }
    return target;
}

ModelingActionQuery ModelingContextActionService::effectiveActionQuery(
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    if (query.suppressWhenModalToolOwnsViewport) {
        query.modalToolOwnsViewport = query.modalToolOwnsViewport || modalToolOwnsViewport;
    }
    return query;
}

ModelingActionSet ModelingContextActionService::actionSet(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionSetFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

std::vector<ModelingActionItem> ModelingContextActionService::actions(
        const ModelingSelectionSummary& summary,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    return ModelingActionCatalog::actionsFor(summary, effectiveActionQuery(query, modalToolOwnsViewport));
}

std::optional<ModelingActionItem> ModelingContextActionService::action(
        const ModelingSelectionSummary& summary,
        std::string_view actionId,
        ModelingActionQuery query,
        bool modalToolOwnsViewport) const {
    query = effectiveActionQuery(query, modalToolOwnsViewport);
    if (query.modalToolOwnsViewport) {
        return std::nullopt;
    }

    std::optional<ModelingActionItem> item = ModelingActionCatalog::actionFor(summary, actionId);
    if (!item.has_value()) {
        return std::nullopt;
    }
    if (item->visibility == ModelingActionVisibility::Hidden && !query.includeHidden) {
        return std::nullopt;
    }
    if (item->visibility == ModelingActionVisibility::Disabled && !query.includeDisabled) {
        return std::nullopt;
    }
    return item;
}

ModelingContextActionResolution ModelingContextActionService::resolveExecutableAction(
        const ModelingSelectionSummary& summary,
        std::string_view actionId,
        bool modalToolOwnsViewport) const {
    ModelingContextActionResolution resolution;
    if (modalToolOwnsViewport) {
        resolution.message = "Viewport is owned by an active modal tool.";
        return resolution;
    }

    ModelingActionQuery query;
    query.surface = ModelingActionSurface::ViewportContextMenu;
    query.modalToolOwnsViewport = false;
    query.includeDisabled = true;
    query.includeHidden = false;

    const std::optional<ModelingActionItem> action = this->action(summary, actionId, query, modalToolOwnsViewport);
    if (!action.has_value()) {
        resolution.message = "Action is not available in the current mode.";
        return resolution;
    }
    if (action->visibility != ModelingActionVisibility::Enabled) {
        resolution.action = *action;
        resolution.message = action->disabledReason.empty()
                ? "Action is disabled for the current selection."
                : action->disabledReason;
        return resolution;
    }

    resolution.ok = true;
    resolution.action = *action;
    return resolution;
}

} // namespace quader
