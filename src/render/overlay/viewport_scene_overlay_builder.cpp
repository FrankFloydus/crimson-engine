////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/viewport_scene_overlay_builder.hpp"

#include "assets/asset_registry.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

bool entitySelected(const SceneDocument& document, EntityId entity) {
    const SelectionComponent* selection = document.selection(entity);
    return selection != nullptr && selection->selected;
}

/**
 * Represents a Wire Edge Point value used by scene overlay extraction.
 */
struct WireEdgePoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;
};

/**
 * Stores the Wire Edge Key data contract used by scene overlay extraction.
 */
struct WireEdgeKey {
    WireEdgePoint a;
    WireEdgePoint b;

    bool operator==(const WireEdgeKey& other) const {
        return a.x == other.a.x && a.y == other.a.y && a.z == other.a.z &&
                b.x == other.b.x && b.y == other.b.y && b.z == other.b.z;
    }
};

/**
 * Represents a Wire Edge Key Hash value used by scene overlay extraction.
 */
struct WireEdgeKeyHash {
    std::size_t operator()(const WireEdgeKey& key) const {
        auto mix = [](std::size_t hash, std::int64_t value) {
            hash ^= static_cast<std::size_t>(value) + 0x9e3779b97f4a7c15ull + (hash << 6u) + (hash >> 2u);
            return hash;
        };
        std::size_t hash = 1469598103934665603ull;
        hash = mix(hash, key.a.x);
        hash = mix(hash, key.a.y);
        hash = mix(hash, key.a.z);
        hash = mix(hash, key.b.x);
        hash = mix(hash, key.b.y);
        return mix(hash, key.b.z);
    }
};

/**
 * Stores the Wire Edge Record data contract used by scene overlay extraction.
 */
struct WireEdgeRecord {
    QVec3 start = {};
    QVec3 end = {};
    QVec3 firstNormal = {};
    std::uint32_t count = 0;
    bool coplanarWithFirst = true;
};

WireEdgePoint quantizedWirePoint(QVec3 value) {
    constexpr double kScale = 1000000.0;
    return {
            static_cast<std::int64_t>(std::llround(static_cast<double>(value.x) * kScale)),
            static_cast<std::int64_t>(std::llround(static_cast<double>(value.y) * kScale)),
            static_cast<std::int64_t>(std::llround(static_cast<double>(value.z) * kScale)),
    };
}

bool wirePointLess(const WireEdgePoint& left, const WireEdgePoint& right) {
    if (left.x != right.x) {
        return left.x < right.x;
    }
    if (left.y != right.y) {
        return left.y < right.y;
    }
    return left.z < right.z;
}

WireEdgeKey wireEdgeKey(QVec3 a, QVec3 b) {
    WireEdgePoint qa = quantizedWirePoint(a);
    WireEdgePoint qb = quantizedWirePoint(b);
    if (wirePointLess(qb, qa)) {
        std::swap(qa, qb);
    }
    return {.a = qa, .b = qb};
}

void appendMeshSelectionWire(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets,
        EntityId entity,
        std::uint32_t color = kOverlaySelectionWireColor,
        ViewportOverlayLayer layer = ViewportOverlayLayer::SourceWire,
        ViewportOverlayStyleRole styleRole = ViewportOverlayStyleRole::SelectedSourceWire,
        ViewportOverlaySource source = ViewportOverlaySource::Selection) {
    QDR_PROFILE_FUNCTION();
    if (!document.isEffectivelyVisible(entity)) {
        return;
    }

    const MeshComponent* meshComponent = document.mesh(entity);
    if (meshComponent == nullptr) {
        return;
    }

    const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return;
    }

    const QMat4 worldFromLocal = document.worldTransformMatrix(entity);
    std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
    edges.reserve(mesh->indices.size());

    auto appendEdge = [&](std::uint32_t a, std::uint32_t b, QVec3 triangleNormal) {
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) {
            return;
        }
        const QVec3 start = transformPoint(worldFromLocal, mesh->vertices[a].position);
        const QVec3 end = transformPoint(worldFromLocal, mesh->vertices[b].position);
        const WireEdgeKey key = wireEdgeKey(start, end);
        WireEdgeRecord& record = edges[key];
        if (record.count == 0u) {
            record.start = start;
            record.end = end;
            record.firstNormal = triangleNormal;
        } else if (length(record.firstNormal) > 0.000001f && length(triangleNormal) > 0.000001f) {
            record.coplanarWithFirst =
                    record.coplanarWithFirst &&
                    std::abs(dot(normalize(record.firstNormal), normalize(triangleNormal))) > 0.999f;
        }
        ++record.count;
    };

    for (std::size_t index = 0; index + 2u < mesh->indices.size(); index += 3u) {
        const std::uint32_t a = mesh->indices[index];
        const std::uint32_t b = mesh->indices[index + 1u];
        const std::uint32_t c = mesh->indices[index + 2u];
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || c >= mesh->vertices.size()) {
            continue;
        }
        const QVec3 worldA = transformPoint(worldFromLocal, mesh->vertices[a].position);
        const QVec3 worldB = transformPoint(worldFromLocal, mesh->vertices[b].position);
        const QVec3 worldC = transformPoint(worldFromLocal, mesh->vertices[c].position);
        QVec3 normal = cross(worldB - worldA, worldC - worldA);
        if (length(normal) > 0.000001f) {
            normal = normalize(normal);
        }
        appendEdge(a, b, normal);
        appendEdge(b, c, normal);
        appendEdge(c, a, normal);
    }

    for (const auto& [key, edge] : edges) {
        (void)key;
        if (edge.count == 2u && edge.coplanarWithFirst) {
            continue;
        }
        frame.lines.push_back(ViewportOverlayLine{
                .layer = layer,
                .start = edge.start,
                .end = edge.end,
                .color = color,
                .widthPixels = 0.0f,
                .styleRole = styleRole,
                .source = source,
        });
    }
}

void appendWireEdge(
        std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash>& edges,
        QVec3 start,
        QVec3 end) {
    const WireEdgeKey key = wireEdgeKey(start, end);
    WireEdgeRecord& record = edges[key];
    if (record.count == 0u) {
        record.start = start;
        record.end = end;
    }
    ++record.count;
}

void emitWireframeEdges(ViewportOverlayFrame& frame, const std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash>& edges) {
    for (const auto& [key, edge] : edges) {
        (void)key;
        frame.lines.push_back(ViewportOverlayLine{
                .layer = ViewportOverlayLayer::WireframeXray,
                .start = edge.start,
                .end = edge.end,
                .color = kOverlayWireframeXrayColor,
                .widthPixels = 0.0f,
                .styleRole = ViewportOverlayStyleRole::WireframeBase,
                .source = ViewportOverlaySource::SceneWireframe,
        });
    }
}

bool appendAuthoredPolygonWireframe(
        ViewportOverlayFrame& frame,
        const MeshAsset& mesh,
        const QMat4& worldFromLocal) {
    if (!mesh.hasAuthoredPolygonDocument || !mesh.authoredPolygonDocument) {
        return false;
    }

    const quader_poly::Document& document = *mesh.authoredPolygonDocument;
    std::unordered_map<quader_poly::ElementId, QVec3> vertices;
    vertices.reserve(document.vertices.size());
    for (const quader_poly::Vertex& vertex : document.vertices) {
        vertices.emplace(vertex.id, transformPoint(worldFromLocal, vertex.position));
    }

    std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
    for (const quader_poly::Face& face : document.faces) {
        if (face.vertices.size() < 2u) {
            continue;
        }
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const quader_poly::ElementId aId = face.vertices[index];
            const quader_poly::ElementId bId = face.vertices[(index + 1u) % face.vertices.size()];
            const auto a = vertices.find(aId);
            const auto b = vertices.find(bId);
            if (a == vertices.end() || b == vertices.end()) {
                continue;
            }
            appendWireEdge(edges, a->second, b->second);
        }
    }
    if (edges.empty()) {
        return false;
    }

    emitWireframeEdges(frame, edges);
    return true;
}

void appendTriangleMeshWireframe(
        ViewportOverlayFrame& frame,
        const MeshAsset& mesh,
        const QMat4& worldFromLocal) {
    std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
    edges.reserve(mesh.indices.size());
    auto appendTriangleEdge = [&](std::uint32_t a, std::uint32_t b) {
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
            return;
        }
        appendWireEdge(
                edges,
                transformPoint(worldFromLocal, mesh.vertices[a].position),
                transformPoint(worldFromLocal, mesh.vertices[b].position));
    };

    for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
        appendTriangleEdge(mesh.indices[index], mesh.indices[index + 1u]);
        appendTriangleEdge(mesh.indices[index + 1u], mesh.indices[index + 2u]);
        appendTriangleEdge(mesh.indices[index + 2u], mesh.indices[index]);
    }
    emitWireframeEdges(frame, edges);
}

void appendHierarchySelectionWires(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    for (const EntityId selected : document.selectedEntities()) {
        if (!document.hasChildren(selected)) {
            continue;
        }
        for (const EntityId descendant : document.descendants(selected, false)) {
            if (entitySelected(document, descendant)) {
                continue;
            }
            appendMeshSelectionWire(
                    frame,
                    document,
                    assets,
                    descendant,
                    kOverlaySelectionWireColor,
                    ViewportOverlayLayer::SourceWire,
                    ViewportOverlayStyleRole::SelectedSourceWire,
                    ViewportOverlaySource::HierarchySelection);
        }
    }
}

void appendBoundsCage(ViewportOverlayFrame& frame, const SceneBounds& bounds) {
    const std::array<QVec3, 8> corners{{
            {bounds.min.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.min.y, bounds.min.z},
            {bounds.max.x, bounds.max.y, bounds.min.z},
            {bounds.min.x, bounds.max.y, bounds.min.z},
            {bounds.min.x, bounds.min.y, bounds.max.z},
            {bounds.max.x, bounds.min.y, bounds.max.z},
            {bounds.max.x, bounds.max.y, bounds.max.z},
            {bounds.min.x, bounds.max.y, bounds.max.z},
    }};
    constexpr std::array<std::array<int, 2>, 12> edges{{
            {{0, 1}},
            {{1, 2}},
            {{2, 3}},
            {{3, 0}},
            {{4, 5}},
            {{5, 6}},
            {{6, 7}},
            {{7, 4}},
            {{0, 4}},
            {{1, 5}},
            {{2, 6}},
            {{3, 7}},
    }};
    for (const auto edge : edges) {
        frame.lines.push_back(ViewportOverlayLine{
                .layer = ViewportOverlayLayer::GroupBounds,
                .start = corners[static_cast<std::size_t>(edge[0])],
                .end = corners[static_cast<std::size_t>(edge[1])],
                .color = kOverlayGroupBoundsColor,
                .widthPixels = kOverlayGroupBoundsLineWidthPixels,
                .styleRole = ViewportOverlayStyleRole::GroupBounds,
                .source = ViewportOverlaySource::GroupBounds,
        });
    }
}

void appendGroupSelectionCages(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    for (const EntityId selected : document.selectedEntities()) {
        if (document.kind(selected) != EntityKind::Empty || !document.hasChildren(selected)) {
            continue;
        }
        if (const std::optional<SceneBounds> bounds = entityHierarchyWorldBounds(document, assets, selected)) {
            appendBoundsCage(frame, *bounds);
        }
    }
}

void appendSelectedLightMarkers(ViewportOverlayFrame& frame, const SceneDocument& document) {
    constexpr float kLightMarkerRadius = 0.28f;
    constexpr float kDirectionalLightLength = 0.85f;
    const std::array<QVec3, 3> axes{{
            {kLightMarkerRadius, 0.0f, 0.0f},
            {0.0f, kLightMarkerRadius, 0.0f},
            {0.0f, 0.0f, kLightMarkerRadius},
    }};

    for (const EntityId selected : document.selectedEntities()) {
        const LightComponent* light = document.light(selected);
        if (light == nullptr || !document.isEffectivelyVisible(selected)) {
            continue;
        }
        const QMat4 world = document.worldTransformMatrix(selected);
        const QVec3 position = transformPoint(world, {});
        frame.points.push_back(ViewportOverlayPoint{
                .layer = ViewportOverlayLayer::SelectedVertex,
                .position = position,
                .color = kOverlaySelectedVertexColor,
                .sizePixels = kOverlayLightMarkerPointSizePixels,
                .styleRole = ViewportOverlayStyleRole::SelectedVertex,
                .source = ViewportOverlaySource::Selection,
                .outlineGrowthPixels = kOverlayLightMarkerOutlineGrowthPixels,
        });

        if (light->type == LightType::Directional) {
            QVec3 direction = normalize(transformVector(world, {0.0f, -1.0f, 0.0f}));
            if (length(direction) <= 0.000001f) {
                direction = {0.0f, -1.0f, 0.0f};
            }
            const QVec3 tip = position + direction * kDirectionalLightLength;
            frame.lines.push_back(ViewportOverlayLine{
                    .layer = ViewportOverlayLayer::SelectedEdge,
                    .start = position,
                    .end = tip,
                    .color = kOverlaySelectionWireColor,
                    .widthPixels = 0.0f,
                    .styleRole = ViewportOverlayStyleRole::SelectionWire,
                    .source = ViewportOverlaySource::Selection,
            });
            const QVec3 right = normalize(cross(direction, {0.0f, 1.0f, 0.0f}));
            const QVec3 tick = length(right) <= 0.000001f ? QVec3{kLightMarkerRadius, 0.0f, 0.0f} : right * kLightMarkerRadius;
            frame.lines.push_back(ViewportOverlayLine{
                    .layer = ViewportOverlayLayer::SelectedEdge,
                    .start = tip - tick,
                    .end = tip + tick,
                    .color = kOverlaySelectionWireColor,
                    .widthPixels = 0.0f,
                    .styleRole = ViewportOverlayStyleRole::SelectionWire,
                    .source = ViewportOverlaySource::Selection,
            });
            continue;
        }

        for (const QVec3 axis : axes) {
            frame.lines.push_back(ViewportOverlayLine{
                    .layer = ViewportOverlayLayer::SelectedEdge,
                    .start = position - axis,
                    .end = position + axis,
                    .color = kOverlaySelectionWireColor,
                    .widthPixels = 0.0f,
                    .styleRole = ViewportOverlayStyleRole::SelectionWire,
                    .source = ViewportOverlaySource::Selection,
            });
        }
    }
}

void appendSceneWireframeOverlay(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    for (const EntityId entity : document.entities()) {
        QDR_PROFILE_SCOPE("Viewport scene wireframe entity");
        if (!document.isEffectivelyVisible(entity)) {
            continue;
        }
        const MeshComponent* meshComponent = document.mesh(entity);
        if (meshComponent == nullptr) {
            continue;
        }
        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
            continue;
        }
        const QMat4 worldFromLocal = document.worldTransformMatrix(entity);
        if (!appendAuthoredPolygonWireframe(frame, *mesh, worldFromLocal)) {
            appendTriangleMeshWireframe(frame, *mesh, worldFromLocal);
        }
    }
}

bool wireframeUsesWireOnlyObjectSelection(ModelingSelectionMode mode) {
    return mode == ModelingSelectionMode::Object || mode == ModelingSelectionMode::Group;
}

bool wireframeSuppressesTriangleLayer(ViewportOverlayLayer layer) {
    return layer == ViewportOverlayLayer::SelectedVisibleFace ||
            layer == ViewportOverlayLayer::SelectedXrayFace ||
            layer == ViewportOverlayLayer::SelectedFaceMask ||
            layer == ViewportOverlayLayer::HoverFace;
}

bool persistentObjectSelectionTriangle(const ViewportOverlayTriangle& triangle) {
    if (triangle.source != ViewportOverlaySource::Selection || triangle.element.objectId < 0) {
        return false;
    }
    return triangle.layer == ViewportOverlayLayer::SelectedVisibleFace ||
            triangle.layer == ViewportOverlayLayer::SelectedXrayFace ||
            triangle.layer == ViewportOverlayLayer::SelectedFaceMask;
}

bool persistentObjectSelectionLine(const ViewportOverlayLine& line) {
    if (line.source != ViewportOverlaySource::Selection || line.element.objectId < 0) {
        return false;
    }
    return line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
            line.layer == ViewportOverlayLayer::SelectedEdge;
}

void stripWireframeObjectFaceFills(ViewportOverlayFrame& frame, ModelingSelectionMode mode) {
    if (!wireframeUsesWireOnlyObjectSelection(mode)) {
        return;
    }

    frame.triangles.erase(
            std::remove_if(frame.triangles.begin(), frame.triangles.end(), [](const ViewportOverlayTriangle& triangle) {
                return wireframeSuppressesTriangleLayer(triangle.layer);
            }),
            frame.triangles.end());
}

} // namespace

ViewportOverlayFrame ViewportSceneOverlayBuilder::buildSelectionFrame(
        ViewportOverlayFrame baseFrame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    appendHierarchySelectionWires(baseFrame, document, assets);
    appendGroupSelectionCages(baseFrame, document, assets);
    appendSelectedLightMarkers(baseFrame, document);
    return baseFrame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::stripPersistentObjectSelection(
        ViewportOverlayFrame frame,
        ModelingSelectionMode selectionMode) {
    if (selectionMode != ModelingSelectionMode::Object) {
        return frame;
    }

    frame.triangles.erase(
            std::remove_if(frame.triangles.begin(), frame.triangles.end(), persistentObjectSelectionTriangle),
            frame.triangles.end());
    frame.lines.erase(
            std::remove_if(frame.lines.begin(), frame.lines.end(), persistentObjectSelectionLine),
            frame.lines.end());
    return frame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::stripWireframeSelectionFills(
        ViewportOverlayFrame frame,
        ModelingSelectionMode selectionMode) {
    stripWireframeObjectFaceFills(frame, selectionMode);
    return frame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::buildWireframeFrame(
        ViewportOverlayFrame baseFrame,
        ModelingSelectionMode selectionMode,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    stripWireframeObjectFaceFills(baseFrame, selectionMode);
    appendSceneWireframeOverlay(baseFrame, document, assets);
    return baseFrame;
}

} // namespace quader
