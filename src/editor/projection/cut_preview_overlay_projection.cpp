////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/projection/cut_preview_overlay_projection.hpp"

#include "render/viewport_overlay_policy.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>

namespace quader {
namespace {

constexpr float kCutPreviewPlaneQuantization = 0.0001f;
constexpr float kCutPreviewFeatureEdgeCosine = 0.985f;

/**
 * Stores the Cut Preview Point Key data contract used by projection.
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
 * Stores the Cut Preview Edge Key data contract used by projection.
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
 * Represents a Cut Preview Edge Accumulator value used by projection.
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

float previewMeshBoundsMagnitude(const CutPreviewOverlayProjection::PreviewTriangleMesh& mesh) {
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
        const CutPreviewOverlayProjection::PreviewTriangleMesh& mesh,
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

} // namespace

CutPreviewOverlayProjection::PreviewTriangleMesh CutPreviewOverlayProjection::meshFromDocuments(
        const std::vector<quader_poly::Document>& documents) {
    PreviewTriangleMesh mesh;
    for (const quader_poly::Document& document : documents) {
        const quader_poly::CompiledMesh compiled = quader_poly::compile_document(document);
        const std::uint32_t vertexOffset = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.reserve(mesh.vertices.size() + compiled.vertices.size());
        mesh.indices.reserve(mesh.indices.size() + compiled.indices.size());
        for (const quader_poly::CompiledVertex& vertex : compiled.vertices) {
            mesh.vertices.push_back(vertex.position);
        }
        for (const std::uint32_t index : compiled.indices) {
            if (index < compiled.vertices.size()) {
                mesh.indices.push_back(vertexOffset + index);
            }
        }
    }
    return mesh;
}

void CutPreviewOverlayProjection::appendMeshFill(
        ViewportOverlayFrame& frame,
        const PreviewTriangleMesh& mesh,
        std::uint32_t color,
        ViewportOverlayStyleRole styleRole) {
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

void CutPreviewOverlayProjection::appendResultLines(
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

} // namespace quader
