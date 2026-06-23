////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/ViewportSceneOverlayBuilder.h"

#include "assets/AssetRegistry.h"
#include "render/ViewportOverlayPolicy.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"

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
    Vec3 start = {};
    Vec3 end = {};
    Vec3 firstNormal = {};
    std::uint32_t count = 0;
    bool coplanarWithFirst = true;
};

WireEdgePoint quantizedWirePoint(Vec3 value) {
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

WireEdgeKey wireEdgeKey(Vec3 a, Vec3 b) {
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

    const Mat4 worldFromLocal = document.worldTransformMatrix(entity);
    std::unordered_map<WireEdgeKey, WireEdgeRecord, WireEdgeKeyHash> edges;
    edges.reserve(mesh->indices.size());

    auto appendEdge = [&](std::uint32_t a, std::uint32_t b, Vec3 triangleNormal) {
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) {
            return;
        }
        const Vec3 start = transformPoint(worldFromLocal, mesh->vertices[a].position);
        const Vec3 end = transformPoint(worldFromLocal, mesh->vertices[b].position);
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
        const Vec3 worldA = transformPoint(worldFromLocal, mesh->vertices[a].position);
        const Vec3 worldB = transformPoint(worldFromLocal, mesh->vertices[b].position);
        const Vec3 worldC = transformPoint(worldFromLocal, mesh->vertices[c].position);
        Vec3 normal = cross(worldB - worldA, worldC - worldA);
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
    const std::array<Vec3, 8> corners{{
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
                .widthPixels = 1.5f,
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

void appendSceneWireframeOverlay(
        ViewportOverlayFrame& frame,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    for (const EntityId entity : document.entities()) {
        appendMeshSelectionWire(
                frame,
                document,
                assets,
                entity,
                rgba8(255, 255, 255),
                ViewportOverlayLayer::WireframeBase,
                ViewportOverlayStyleRole::WireframeBase,
                ViewportOverlaySource::SceneWireframe);
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
    appendHierarchySelectionWires(baseFrame, document, assets);
    appendGroupSelectionCages(baseFrame, document, assets);
    return baseFrame;
}

ViewportOverlayFrame ViewportSceneOverlayBuilder::buildWireframeFrame(
        ViewportOverlayFrame baseFrame,
        ModelingSelectionMode selectionMode,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    stripWireframeObjectFaceFills(baseFrame, selectionMode);
    appendSceneWireframeOverlay(baseFrame, document, assets);
    return baseFrame;
}

} // namespace quader
